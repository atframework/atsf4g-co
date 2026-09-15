// Copyright 2026 atframework
//
// teamsvr-room 快照压缩、状态恢复与锁持有者变化用例。
// 通过 fake journal 的真实 sequence/hash chain 与 global_now_offset_guard 驱动定时维护。

#include "teamsvr_room_test_common.h"  // NOLINT: build/include_subdir

#include <algorithm>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace {
using teamsvr_room_test::add_team_any_data_entry;
using teamsvr_room_test::add_team_any_value_entry;
using teamsvr_room_test::count_personal_actions;
using teamsvr_room_test::fake_team_room_channel;
using teamsvr_room_test::global_now_offset_guard;
using teamsvr_room_test::kTestZoneId;
using teamsvr_room_test::make_foreign_lock;
using teamsvr_room_test::make_personal_channel;
using teamsvr_room_test::make_team_key;
using teamsvr_room_test::make_user_key;
using teamsvr_room_test::next_test_team_id;
using teamsvr_room_test::room_test_cfg_values;
using teamsvr_room_test::room_test_env;
using teamsvr_room_test::self_lock_holder;
using teamsvr_room_test::setup_standard_team;
using teamsvr_room_test::standard_team_members;

// 写 N 个 member_update 事件(产生可压缩日志)
bool write_member_update_logs(room_test_env& env, const team_room::ptr_t& room,
                              const PROJECT_NAMESPACE_ID::DUserIDKey& key, int count) {
  for (int i = 0; i < count; ++i) {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), key);
    update->set_client_version("ut-compact-v" + std::to_string(i));
    int32_t ret = env.run("write_log", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
    if (0 != ret) {
      return false;
    }
  }
  return 0 == env.sync(room->get_team_key());
}

const atfw::dtmq::SSChannelUpdateReq* find_compact_update(const fake_team_room_channel& fake) {
  for (const auto& record : fake.update_requests()) {
    if (record.request.compact_sequence() > 0) {
      return &record.request;
    }
  }
  return nullptr;
}
}  // namespace

// ============ CMP-00: 枚举 send_update 调用点: 初始创建无历史日志; 后续 update 必经压缩选择或证明无可压缩日志 ============
// 生产仅两个 send_update 调用点: create_team 初始快照(无 pick, 无历史可裁)与 do_maintenance
// (每次先 pick_compact_sequence 再合并续租+压缩+快照)。本用例按 update 请求序列锁定该结构,
// 新增绕过压缩选择的调用点会打破形状枚举而红灯。
CASE_TEST(teamsvr_room_compact, update_call_sites_compact_selection_coverage) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);

  // 调用点 1: 初始 create update —— 频道无任何历史日志, 保存快照但不携带压缩边界
  CASE_EXPECT_GE(fake.update_requests().size(), 1u);
  if (fake.update_requests().empty()) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  const auto& create_update = fake.update_requests().front().request;
  CASE_EXPECT_TRUE(create_update.save());
  CASE_EXPECT_EQ(0, create_update.compact_sequence());
  CASE_EXPECT_TRUE(create_update.has_custom_data());
  size_t updates_after_create = fake.update_requests().size();

  // 调用点 2: 无新日志时的续租 update —— pick 运行但证明无可压缩日志(compact=0, 不带快照)
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_GT(fake.update_requests().size(), updates_after_create);
  updates_after_create = fake.update_requests().size();
  CASE_EXPECT_TRUE(nullptr == find_compact_update(fake));

  // 调用点 2(可压缩分支): 写入足量日志并推进时间后, 维护 update 必须携带 pick 选择的压缩边界
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 8));
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  const atfw::dtmq::SSChannelUpdateReq* compact_update = find_compact_update(fake);
  CASE_EXPECT_TRUE(nullptr != compact_update);

  // 全序列形状: create 之后的每个 update 要么是无可压缩日志的续租(compact=0 且不裁日志),
  // 要么是携带快照的压缩 update(compact>0 且 save); 携带压缩的 update 边界单调不回退
  int64_t last_compact_sequence = 0;
  for (size_t index = 1; index < fake.update_requests().size(); ++index) {
    const auto& req = fake.update_requests()[index].request;
    if (req.compact_sequence() > 0) {
      CASE_EXPECT_GT(req.compact_sequence(), last_compact_sequence);
      last_compact_sequence = req.compact_sequence();
      CASE_EXPECT_TRUE(req.save());
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-01: 无可压缩日志时维护仍发送续租 update，但不设置 compact/快照 ============
CASE_TEST(teamsvr_room_compact, maintenance_without_compactable_logs) {
  // 大保留窗口: 少量新日志既不满足数量维度也不满足时间维度
  room_test_cfg_values cfg;
  cfg.compact_log_start_seconds = 3600;
  cfg.compact_log_keep_count = 32;
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  size_t updates_before = fake.update_calls();

  // 推进到续租时间点并驱动定时器: 只有少量日志(<=keep)时无可压缩内容
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  // 维护发生(续租 update)，但没有压缩请求
  CASE_EXPECT_GT(fake.update_calls(), updates_before);
  CASE_EXPECT_TRUE(nullptr == find_compact_update(fake));

  // 房间仍持有锁
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-03/CMP-06/07/08: 数量维度压缩与快照内容 ============
CASE_TEST(teamsvr_room_compact, count_based_compaction_and_snapshot_content) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  // gc_log_count=10, keep_percent=50, keep_count=2 -> 保留 max(5, 2)=5 条
  // 写 8 个事件日志，加上已有日志，未压缩日志数超过触发线 max(10*60%, 5)=6
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 8));
  CASE_EXPECT_GT(fake.last_sequence(), 8);

  int64_t sequence_before_maintenance = fake.last_sequence();

  // 数量维度加速: 未压缩日志超过触发线后立即触发维护(无需等到续租时间)。
  // 双维度硬保证语义下时间维度也须放行: 推进到保留窗口(5s)之外，旧日志不再受时间维度保护
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  const atfw::dtmq::SSChannelUpdateReq* compact_update = find_compact_update(fake);
  CASE_EXPECT_TRUE(nullptr != compact_update);
  if (nullptr == compact_update) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 压缩请求: save=true，携带 custom/private 快照
  CASE_EXPECT_TRUE(compact_update->save());
  CASE_EXPECT_GT(compact_update->compact_sequence(), 0);
  CASE_EXPECT_LE(compact_update->compact_sequence(), sequence_before_maintenance);
  CASE_EXPECT_EQ(compact_update->compact_sequence(), compact_update->stateful_sequence());

  // custom data 覆盖成员/队长/配置与最新 ack
  atfw::team::DTeamStorage storage;
  CASE_EXPECT_TRUE(compact_update->custom_data().UnpackTo(&storage));
  CASE_EXPECT_GE(storage.member_size(), 3);
  CASE_EXPECT_EQ(members.owner.user_id(), storage.captain_user_key().user_id());
  CASE_EXPECT_GT(storage.acknowledge_action_sequence(), 0);
  // saved_action_sequence 覆盖快照所含最新状态
  CASE_EXPECT_EQ(sequence_before_maintenance, storage.saved_action_sequence());

  // private data 覆盖 team_created 与压缩边界
  atfw::team::DTeamRoomPrivateData private_data;
  CASE_EXPECT_TRUE(compact_update->private_data().UnpackTo(&private_data));
  CASE_EXPECT_TRUE(private_data.team_created());
  CASE_EXPECT_EQ(compact_update->compact_sequence(), private_data.last_compact_sequence());
  CASE_EXPECT_GT(private_data.last_compact_timepoint().seconds(), 0);

  // 压缩后 journal 裁剪语义与 mq_channel::compact_sequence 一致: 边界日志保留，边界之前的日志全部移除
  CASE_EXPECT_EQ(compact_update->compact_sequence(), fake.last_removed_sequence());
  bool boundary_retained = false;
  for (const auto& message : fake.journal()) {
    CASE_EXPECT_GE(message.sequence(), fake.last_removed_sequence());
    if (message.sequence() == fake.last_removed_sequence()) {
      boundary_retained = true;
    }
  }
  CASE_EXPECT_TRUE(boundary_retained);
  // 保留条数不超过 keep_by_count(5) + 后续新增 + 1 条边界日志
  size_t event_logs_after = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent);
  size_t total_logs_after = fake.journal().size();
  CASE_EXPECT_LE(total_logs_after, 5 + 3 + 1);  // 保留窗口 + 维护自身产生的 kResetLock/kUpdateCustomData/kNoop + 边界
  CASE_EXPECT_LE(event_logs_after, 5u + 1u);    // 保留窗口 + 边界日志(同为 event)

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-02: 每次维护都重新选择压缩点(不依赖 over-percent/start-time 加速条件) ============
CASE_TEST(teamsvr_room_compact, compaction_repicked_each_maintenance) {
  // 加速条件全部关闭: over_percent 极大、start_time 极大。只有续租时间点触发维护。
  // 双维度硬保证语义下时间维度不保留旧日志需显式给出小的保留窗口(keep_seconds=5，
  // 与巨大的 start_time 无关; 保留窗口 <= start_time 的钳制见 get_compact_log_keep_time)
  room_test_cfg_values cfg;
  cfg.compact_log_start_seconds = 3600;
  cfg.compact_log_over_percent = 100000;
  cfg.compact_log_keep_count = 2;
  cfg.compact_log_keep_percent = 50;
  cfg.compact_log_keep_seconds = 5;
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 8));

  // 第一次续租维护(6s > renew 5s): 加速条件未满足，但保留策略允许 -> 压缩照常发生
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  const atfw::dtmq::SSChannelUpdateReq* compact1 = find_compact_update(fake);
  CASE_EXPECT_TRUE(nullptr != compact1);
  if (nullptr == compact1) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  int64_t boundary1 = compact1->compact_sequence();
  CASE_EXPECT_GT(boundary1, 0);
  CASE_EXPECT_EQ(boundary1, fake.last_removed_sequence());

  // 再写日志后第二次续租维护: 压缩点重新选择并推进(不是一次性行为)
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 4));
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  int64_t boundary2 = 0;
  for (const auto& record : fake.update_requests()) {
    if (record.request.compact_sequence() > boundary2) {
      boundary2 = record.request.compact_sequence();
    }
  }
  CASE_EXPECT_GT(boundary2, boundary1);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-04: 时间维度精确语义(keep_time 默认 start/2；只裁剪窗口外日志；keep 不超 start) ============
CASE_TEST(teamsvr_room_compact, time_policy_compaction_window) {
  // 数量维度关闭(keep_count/keep_percent 均为 0 -> 数量维度不保留任何日志)，keep_time 缺省 = start/2 = 5s
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 0;
  cfg.compact_log_keep_percent = 0;
  cfg.compact_log_over_percent = 100000;
  cfg.compact_log_start_seconds = 10;
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  auto count_versions = [&fake](const char* prefix) {
    size_t ret = 0;
    fake.foreach_team_action([&ret, prefix](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
      if (action.action_case() == atfw::team::DTeamAction::kMemberUpdate &&
          action.member_update().client_version().compare(0, 3, prefix) == 0) {
        ++ret;
      }
      return true;
    });
    return ret;
  };

  // T0: batch w0-(3 条)
  for (int i = 0; i < 3; ++i) {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
    action.mutable_member_update()->set_client_version("w0-v" + std::to_string(i));
    CASE_EXPECT_EQ(0, env.run("write_w0", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  {
    global_now_offset_guard guard(std::chrono::seconds{3});
    // T0+3: batch w1-(3 条)
    for (int i = 0; i < 3; ++i) {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      action.mutable_member_update()->set_client_version("w1-v" + std::to_string(i));
      CASE_EXPECT_EQ(0, env.run("write_w1", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // T0+6: 续租维护(renew 5s 到期)。keep_deadline = T0+6-5 = T0+1:
    //   w0-(T0) 在窗口外被裁剪，w1-(T0+3) 在窗口内保留
    global_now_offset_guard::advance(std::chrono::seconds{3});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    // 开区间语义: 边界日志(窗口外最新的 w0- 日志)保留，其余 w0- 被裁剪;w1- 全在窗口内保留
    int64_t retained_w0_sequence = 0;
    fake.foreach_team_action(
        [&retained_w0_sequence](const atfw::dtmq::DChannelMessage& message, const atfw::team::DTeamAction& action) {
          if (action.action_case() == atfw::team::DTeamAction::kMemberUpdate &&
              action.member_update().client_version().compare(0, 3, "w0-") == 0) {
            retained_w0_sequence = message.sequence();
          }
          return true;
        });
    CASE_EXPECT_EQ(1u, count_versions("w0-"));
    CASE_EXPECT_EQ(fake.last_removed_sequence(), retained_w0_sequence);
    CASE_EXPECT_EQ(3u, count_versions("w1-"));

    // T0+8: batch w2-(2 条)
    global_now_offset_guard::advance(std::chrono::seconds{2});
    for (int i = 0; i < 2; ++i) {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      action.mutable_member_update()->set_client_version("w2-v" + std::to_string(i));
      CASE_EXPECT_EQ(0, env.run("write_w2", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // T0+12: 续租维护。keep_deadline = T0+12-5 = T0+7: 维护#1 在 T0+6 追加的续租/快照通知日志
    // 同样在窗口之外，时间维度的裁剪点推进到该快照通知日志(开区间语义: 边界日志本身保留)；
    // w0-/w1- 全部位于边界之前被裁剪，w2-(T0+8) 在窗口内保留
    global_now_offset_guard::advance(std::chrono::seconds{4});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_EQ(0u, count_versions("w0-"));
    CASE_EXPECT_EQ(0u, count_versions("w1-"));
    CASE_EXPECT_EQ(2u, count_versions("w2-"));
    {
      // 边界日志是维护#1 的快照通知日志(非 team action)，按开区间语义保留在 journal 首位
      const auto& journal_logs = fake.journal();
      CASE_EXPECT_FALSE(journal_logs.empty());
      if (!journal_logs.empty()) {
        CASE_EXPECT_EQ(atfw::dtmq::DChannelMessageDetail::kUpdateCustomData,
                       journal_logs.front().detail().command_case());
        CASE_EXPECT_EQ(fake.last_removed_sequence(), journal_logs.front().sequence());
      }
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-04b: keep_time 不允许大于 start_time(钳制)，否则按时间维度永远无法压缩 ============
CASE_TEST(teamsvr_room_compact, time_policy_keep_time_clamped_to_start) {
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 0;
  cfg.compact_log_keep_percent = 0;
  cfg.compact_log_over_percent = 100000;
  cfg.compact_log_start_seconds = 10;
  cfg.compact_log_keep_seconds = 100;  // > start，应被钳制为 10s
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 3));
  auto& fake = env.channel(team_id);

  // 推进 11s(> start=10s): 若钳制生效 keep=10s，日志超出窗口被压缩；若未钳制(keep=100s)则不会压缩
  {
    global_now_offset_guard guard(std::chrono::seconds{11});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(nullptr != find_compact_update(fake));
  CASE_EXPECT_GT(fake.last_removed_sequence(), 0);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-05: 数量+时间组合取更保守(更小)裁剪点: 时间维度保护新日志不被数量维度裁掉 ============
CASE_TEST(teamsvr_room_compact, combined_policy_picks_conservative_cutoff) {
  // keep_by_count=2(数量维度激进)，keep_time=5s(时间维度保护新日志)
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 2;
  cfg.compact_log_keep_percent = 0;
  cfg.compact_log_start_seconds = 10;
  cfg.compact_log_over_percent = 100000;  // 关闭数量加速，仅续租驱动
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  int64_t setup_last = fake.last_sequence();

  // 单一 guard 保持时间单调: 批次在 T0+6 写入(新日志)，维护在 T0+7 触发
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    // T0+6: 写入 6 条新日志(不驱动定时器，避免提前触发维护)
    CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 6));

    // T0+7: 第一次续租维护。数量维度要求裁到剩 2 条(激进)，时间维度保护 5s 窗口内的新日志:
    //   组合取 min -> 只裁掉 setup 期的旧日志，新日志全部保留
    global_now_offset_guard::advance(std::chrono::seconds{1});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  // 压缩边界停在 setup 最后一日志(时间维度下限)，不进入新批次
  CASE_EXPECT_EQ(setup_last, fake.last_removed_sequence());
  // 新批次(6 条 member_update)全部保留；边界日志(setup 最后一条 add_member event)按开区间语义一并保留
  CASE_EXPECT_EQ(7u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));
  CASE_EXPECT_TRUE(nullptr != find_compact_update(fake));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-05(partial): keep 边界值准确 —— 未压缩数 == keep 时不裁; == keep+1 起按第 delete 条为边界精确裁剪 ============
// 默认配置 gc=10/percent=50/keep_count=2 -> keep=max(5,2)=5(percent 分支); 裁剪边界是数量维度的第
// delete_by_count 条未压缩日志(边界日志保留、严格小于边界的日志移除), 与时间维度取较小者。
CASE_TEST(teamsvr_room_compact, keep_percent_boundary_exact) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);
  const int64_t keep = 5;

  // 阶段 1: 补足到恰好 keep 条未压缩日志, 推进到时间窗口之外 -> 数量维度放行 0 条, 不压缩
  {
    int64_t uncompacted = static_cast<int64_t>(fake.journal().size());
    if (uncompacted < keep) {
      CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, static_cast<int>(keep - uncompacted)));
    }
    CASE_EXPECT_EQ(static_cast<size_t>(keep), fake.journal().size());
  }
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(nullptr == find_compact_update(fake));
  CASE_EXPECT_EQ(0, fake.last_removed_sequence());

  // 阶段 2: 再写 3 条, 数量维度放行 delete=total-keep 条 -> 边界精确落在第 delete 条
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 3));
  // 维护前的 journal 快照(含上一轮续租日志): 动态计算边界, 不假设日志构成
  std::vector<atfw::dtmq::DChannelMessage> journal_before;
  for (const auto& message : fake.journal()) {
    journal_before.push_back(message);
  }
  const int64_t delete_by_count = static_cast<int64_t>(journal_before.size()) - keep;
  CASE_EXPECT_GT(delete_by_count, 0);
  const int64_t expected_boundary = journal_before[static_cast<size_t>(delete_by_count - 1)].sequence();
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  const atfw::dtmq::SSChannelUpdateReq* compact_update = find_compact_update(fake);
  CASE_EXPECT_TRUE(nullptr != compact_update);
  if (nullptr != compact_update) {
    CASE_EXPECT_EQ(expected_boundary, compact_update->compact_sequence());
  }
  CASE_EXPECT_EQ(expected_boundary, fake.last_removed_sequence());
  // 严格小于边界的日志全部移除; 边界及之后的日志(含边界本身)全部保留
  for (const auto& message : journal_before) {
    bool expect_retained = message.sequence() >= expected_boundary;
    bool found = false;
    for (const auto& after : fake.journal()) {
      if (after.sequence() == message.sequence()) {
        found = true;
        break;
      }
    }
    CASE_EXPECT_EQ(expect_retained, found);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-05(partial): keep_count 大于 percent 时取较大者, 边界同样精确 ============
CASE_TEST(teamsvr_room_compact, keep_count_dominates_boundary_exact) {
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 7;  // max(10*50%, 7) = 7
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);
  const int64_t keep = 7;

  // 恰好 keep 条: 不压缩
  {
    int64_t uncompacted = static_cast<int64_t>(fake.journal().size());
    if (uncompacted < keep) {
      CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, static_cast<int>(keep - uncompacted)));
    }
    CASE_EXPECT_EQ(static_cast<size_t>(keep), fake.journal().size());
  }
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(nullptr == find_compact_update(fake));
  CASE_EXPECT_EQ(0, fake.last_removed_sequence());

  // 再写 4 条: delete=total-7, 边界精确落在第 delete 条
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 4));
  std::vector<atfw::dtmq::DChannelMessage> journal_before;
  for (const auto& message : fake.journal()) {
    journal_before.push_back(message);
  }
  const int64_t delete_by_count = static_cast<int64_t>(journal_before.size()) - keep;
  CASE_EXPECT_GT(delete_by_count, 0);
  const int64_t expected_boundary = journal_before[static_cast<size_t>(delete_by_count - 1)].sequence();
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  const atfw::dtmq::SSChannelUpdateReq* compact_update = find_compact_update(fake);
  CASE_EXPECT_TRUE(nullptr != compact_update);
  if (nullptr != compact_update) {
    CASE_EXPECT_EQ(expected_boundary, compact_update->compact_sequence());
  }
  CASE_EXPECT_EQ(expected_boundary, fake.last_removed_sequence());
  for (const auto& message : journal_before) {
    bool expect_retained = message.sequence() >= expected_boundary;
    bool found = false;
    for (const auto& after : fake.journal()) {
      if (after.sequence() == message.sequence()) {
        found = true;
        break;
      }
    }
    CASE_EXPECT_EQ(expect_retained, found);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-12(partial): 数量加速 —— 三重门控放行且未压缩数超过 gc*over% 时立即触发维护 ============
// fake 模式锁租约固定 10s(续租点 5s)。start=5s 时 oldest+start 晚于当前点, 立即触发只可能来自
// over-percent 分支, 从而隔离数量加速方向。
CASE_TEST(teamsvr_room_compact, acceleration_over_percent_immediate) {
  room_test_cfg_values cfg;
  cfg.compact_log_start_seconds = 5;  // keep_time=2.5s
  cfg.compact_log_over_percent = 60;  // 数量加速阈值 = 10*60% = 6
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);

  // T0 写 5 条(未压缩 > keep 5 且 > 阈值 6); 首次续租点 T0+5, 尚无任何维护(last_maintenance=0)
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 5));

  // T0+2: 点火一次仅重排定时器(不到续租点不维护), 清掉 setup 期“立即接管”定时器对后续断言的遮蔽
  {
    global_now_offset_guard guard(std::chrono::seconds{2});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  size_t updates_before = fake.update_calls();

  // T0+3: 再写 1 条触发重调度。时间加速目标 oldest+start=T0+5 晚于当前点,
  // 立即触发(timeout<=now)只可能来自 over-percent 数量加速分支
  {
    global_now_offset_guard guard(std::chrono::seconds{3});
    CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 1));
    CASE_EXPECT_LE(room->debug_timer_timeout(), atfw::util::time::time_utility::now());
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  // 维护已在 T0+3 执行(早于续租点 T0+5): 发生压缩
  CASE_EXPECT_GT(fake.update_calls(), updates_before);
  CASE_EXPECT_TRUE(nullptr != find_compact_update(fake));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-12(partial): 时间加速 —— 最早未压缩日志超过 start_time 时维护立即触发(不等续租点);
// 对照组证明时间维度未放行时不加速; 压缩推进后增量门控阻止加速点停留过去 ============
// 机制说明: 事件回环仅在未压缩数越过 max(gc*over%, keep) 触发线时重算定时器(与 over 分支同阈值),
// 因此时间加速的独立可观测路径是恢复时的 0->有效 oldest 迁移调度。over=200 关闭数量加速分支,
// 老日志队伍恢复后立即维护只能来自时间加速方向。
CASE_TEST(teamsvr_room_compact, acceleration_start_time_direction) {
  room_test_cfg_values cfg;
  cfg.compact_log_start_seconds = 5;   // keep_time=2.5s
  cfg.compact_log_over_percent = 200;  // 数量加速阈值 = 20, 本用例各阶段均不达到
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  const int64_t team_old = next_test_team_id();
  const int64_t team_fresh = next_test_team_id();
  auto key_owner = make_user_key(1, 8801);
  // 两支队伍构造相同日志: add owner + 12 轮 member_update; old 队全部日志时间为 now-100s
  auto inject_logs = [&env, &key_owner](int64_t team_id) {
    env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* add_member = action.mutable_add_member();
      protobuf_copy_message(*add_member->mutable_user_key(), key_owner);
      add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      return action;
    }());
    for (int index = 0; index < 12; ++index) {
      env.inject_team_action(team_id, [&key_owner, index]() {
        atfw::team::DTeamAction action;
        auto* update = action.mutable_member_update();
        protobuf_copy_message(*update->mutable_user_key(), key_owner);
        update->set_client_version("v" + std::to_string(index));
        return action;
      }());
    }
  };
  {
    global_now_offset_guard guard(std::chrono::seconds{-100});
    inject_logs(team_old);
  }
  inject_logs(team_fresh);

  team_room::ptr_t room_old = env.setup_ready_room(team_old);
  team_room::ptr_t room_fresh = env.setup_ready_room(team_fresh);
  CASE_EXPECT_TRUE(!!room_old && !!room_fresh);
  if (!room_old || !room_fresh) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake_old = env.channel(team_old);
  auto& fake_fresh = env.channel(team_fresh);
  // 一次点火完成接管; old 队恢复后 oldest=now-100 -> 三重门控放行 -> 时间加速到期(now-95)。
  // reset_room_timer 把过期时刻收敛到时间轮 last_tick+1, 需要虚拟时间再前进 1s 以上才会点火维护;
  // fresh 队 oldest=now -> 时间维度未放行 -> 仅接管, 定时器停在续租点(now+5, +2s 窗口内不触发)
  env.drive_timer_ticks();
  {
    global_now_offset_guard guard(std::chrono::seconds{2});
    env.drive_timer_ticks();
  }
  CASE_EXPECT_EQ(0, env.sync(team_old));
  CASE_EXPECT_EQ(0, env.sync(team_fresh));

  CASE_EXPECT_TRUE(room_old->is_lock_holder());
  CASE_EXPECT_TRUE(room_fresh->is_lock_holder());
  // old 队: 维护已在接管后立即执行(续租点之前), 发生压缩
  CASE_EXPECT_TRUE(nullptr != find_compact_update(fake_old));
  CASE_EXPECT_GT(fake_old.last_removed_sequence(), 0);
  // 压缩推进后无新增: 增量门控关闭, 加速点不得停留过去, 下一轮回到续租点
  CASE_EXPECT_GT(room_old->debug_timer_timeout(),
                 atfw::util::time::time_utility::now() + std::chrono::seconds{3});
  // fresh 队: 时间维度未放行 -> 不加速, 无压缩, 定时器指向续租点
  CASE_EXPECT_TRUE(nullptr == find_compact_update(fake_fresh));
  CASE_EXPECT_GT(room_fresh->debug_timer_timeout(),
                 atfw::util::time::time_utility::now() + std::chrono::seconds{3});

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-09: 压缩提交成功但 update 响应丢失 -> 旧主控本地边界滞后但不回退/不覆盖，恢复等价 ============
CASE_TEST(teamsvr_room_compact, compact_committed_response_lost) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 6));

  // 压缩提交成功但响应丢失(服务端已裁剪+保存快照，客户端看到错误)
  fake.next_update_fault.present = true;
  fake.next_update_fault.commit_first = true;
  fake.next_update_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
  }
  int64_t committed_boundary = fake.last_removed_sequence();
  CASE_EXPECT_GT(committed_boundary, 0);
  // 客户端本地边界滞后(响应丢失 -> 不更新本地压缩边界)
  CASE_EXPECT_EQ(0, room->debug_last_compact_sequence());
  // 非锁冲突错误不退位
  CASE_EXPECT_TRUE(room->is_lock_holder());
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 下一次维护: 基于服务端已压缩的缓存重新选择，压缩点不回退
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 6));
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  // 任何新压缩请求的边界都必须严格大于已提交边界(不以旧边界覆盖)
  int64_t boundary2 = 0;
  for (const auto& record : fake.update_requests()) {
    if (record.request.compact_sequence() > boundary2) {
      boundary2 = record.request.compact_sequence();
    }
  }
  CASE_EXPECT_GT(boundary2, committed_boundary);
  CASE_EXPECT_EQ(boundary2, fake.last_removed_sequence());
  // 本地边界追平服务端
  CASE_EXPECT_EQ(boundary2, room->debug_last_compact_sequence());

  // 新节点从快照恢复: 压缩边界/成员状态与旧主控等价
  room_test_env::clear_rooms();
  team_room::ptr_t recovered = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!recovered);
  if (recovered) {
    CASE_EXPECT_EQ(boundary2, recovered->debug_last_compact_sequence());
    CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) != nullptr);
    CASE_EXPECT_TRUE(recovered->find_member(members.normal, false) != nullptr);
    CASE_EXPECT_GT(recovered->debug_saved_action_sequence(), 0);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-10: admission 本地过期后 update 失败 -> 重试/换主都不永久丢记录或错误批准 ============
CASE_TEST(teamsvr_room_compact, admission_expired_then_update_failure) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 建立邀请(默认 5s 过期)
  auto invitee = make_user_key(1, 8401);
  atfw::team::SSTeamRoomAddInvitationReq invite_req;
  protobuf_copy_message(*invite_req.mutable_sender_user_key(), members.normal);
  protobuf_copy_message(*invite_req.mutable_invitation()->mutable_inviter(), members.normal);
  protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee(), invitee);
  protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee_private_channel(),
                        make_personal_channel(8401));
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 8401, atfw::team::DTeamMemberAction::kInvited));

  // 推进到过期后，update 预提交失败: 维护在本地清理了过期邀请，但快照未提交
  auto& fake = env.channel(team_id);
  fake.next_update_fault.present = true;
  fake.next_update_fault.commit_first = false;
  fake.next_update_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  // 清理不发送通知；房间仍是主控(非锁冲突错误不退位)
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 8401, atfw::team::DTeamMemberAction::kInvited));
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 8401, atfw::team::DTeamMemberAction::kRejectInvitation));
  CASE_EXPECT_TRUE(room->is_lock_holder());

  // 本地清理后 approve 立即返回 not-found(不依赖快照提交)
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                 env.run("approve_after_cleanup", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                 }));

  // 故障解除后重试维护成功
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());

  // 换主恢复: 新房间从 journal 重放看到该邀请，但已过期 -> approve 返回 not-found，且不补发通知。
  // 有效期基于真实时钟，这里保持时间偏移使"过期"在新主控视角同样成立
  {
    global_now_offset_guard guard(std::chrono::seconds{13});
    room_test_env::clear_rooms();
    team_room::ptr_t recovered = env.setup_ready_room(team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                     env.run("approve_expired", [recovered, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(recovered->approve_invitation(ctx, approve_req)));
                     }));
      CASE_EXPECT_EQ(1u, count_personal_actions(env, 8401, atfw::team::DTeamMemberAction::kInvited));
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-13: 只含 kResetLock 续租日志的空闲频道按时间维度压缩(FIX-04 回归) ============
CASE_TEST(teamsvr_room_compact, reset_lock_only_channel_time_compaction) {
  // 数量维度关闭(keep_count/keep_percent 均为 0 -> 数量维度不保留任何日志)，纯时间维度裁剪
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 0;
  cfg.compact_log_keep_percent = 0;
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);

  // 队伍创建后不再写入业务日志，仅锁续租(kResetLock)产生新日志。
  // 先推进一次触发续租+维护(setup 日志按时间维度压缩，仅边界 event 保留)
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  size_t reset_logs = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kResetLock);
  CASE_EXPECT_GT(reset_logs, 0u);
  // 首次维护已按时间维度压缩: setup 的 event 日志被裁到只剩边界(开区间语义保留)
  int64_t retained_event_sequence = 0;
  size_t retained_events = 0;
  for (const auto& message : fake.journal()) {
    if (message.detail().command_case() == atfw::dtmq::DChannelMessageDetail::kEvent) {
      ++retained_events;
      retained_event_sequence = message.sequence();
    }
  }
  CASE_EXPECT_EQ(1u, retained_events);
  CASE_EXPECT_GT(fake.last_removed_sequence(), 0);
  CASE_EXPECT_EQ(fake.last_removed_sequence(), retained_event_sequence);

  // 推进 compact_log_start_time(10s): 最老未压缩日志(含 kResetLock)超过窗口 -> 时间维度触发维护并压缩
  {
    global_now_offset_guard guard(std::chrono::seconds{11});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  // 时间维度压缩完成(keep_time=5s，所有日志都早于窗口 -> 全部裁剪，边界不早于最早日志)
  CASE_EXPECT_TRUE(nullptr != find_compact_update(fake));
  CASE_EXPECT_GT(fake.last_removed_sequence(), 0);
  // 二次压缩后边界为续租日志: 所有 event 日志均已裁掉
  CASE_EXPECT_EQ(0u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));

  // 房间仍主控且持续可写
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-14: 最小保留条数是硬保证(回归): 日志全部超出时间保留窗口但不足 keep_count 时不压缩 ============
CASE_TEST(teamsvr_room_compact, keep_count_floor_protects_old_logs) {
  // 事故场景: keep_count=30，未压缩日志仅数条且全部超出时间保留窗口。
  // 旧实现把"数量维度保留全部"(裁剪点 0)误判为"数量维度不限制"而取时间维度裁剪点，
  // 导致压缩推进到最后一条日志
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 30;
  cfg.compact_log_keep_percent = 50;
  cfg.compact_log_over_percent = 100000;  // 关闭数量加速，仅续租驱动
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  size_t event_logs_before = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent);

  // 推进到保留窗口(5s)之外: 全部未压缩日志都超出时间保留窗口，但总数远小于 keep_count=30
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  // 数量维度要保留全部日志 -> 不压缩
  CASE_EXPECT_TRUE(nullptr == find_compact_update(fake));
  CASE_EXPECT_EQ(0, fake.last_removed_sequence());
  CASE_EXPECT_EQ(event_logs_before, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));

  // 数量维度也放行后(日志数超过 keep_count)压缩恢复: 再写 31 条并推进时间
  // (guard 是绝对偏移，需推进到 phase 1 之后的下一个续租点)
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 31));
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  const atfw::dtmq::SSChannelUpdateReq* compact_update = find_compact_update(fake);
  CASE_EXPECT_TRUE(nullptr != compact_update);
  // 压缩后未压缩日志仍不少于 keep_count(30) 条(最小保留条数是硬保证)
  CASE_EXPECT_GE(fake.journal().size(), 30u);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CMP-11: 快照覆盖(saved)超过裁剪边界(compact)时, 重叠日志不参与重放, 恢复幂等且不回退 ============
CASE_TEST(teamsvr_room_compact, snapshot_coverage_beyond_compact_replay_idempotent) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto key_owner = make_user_key(1, 8601);
  auto key_admin = make_user_key(1, 8602);
  auto invitee = make_user_key(1, 8603);
  int64_t compact_sequence = 0;
  int64_t saved_sequence = 0;
  {
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    // 快照覆盖范围内日志: add owner / add admin / 两轮 member_update / 一条邀请
    auto* add_owner_log = env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* add_member = action.mutable_add_member();
      protobuf_copy_message(*add_member->mutable_user_key(), key_owner);
      add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      return action;
    }());
    CASE_EXPECT_TRUE(nullptr != add_owner_log);
    compact_sequence = nullptr != add_owner_log ? add_owner_log->sequence() : 0;
    env.inject_team_action(team_id, [&key_admin]() {
      atfw::team::DTeamAction action;
      auto* add_member = action.mutable_add_member();
      protobuf_copy_message(*add_member->mutable_user_key(), key_admin);
      add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
      return action;
    }());
    env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), key_owner);
      update->set_client_version("v1-owner");
      return action;
    }());
    env.inject_team_action(team_id, [&key_admin]() {
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), key_admin);
      update->set_client_version("v2-admin");
      return action;
    }());
    auto* invite_log = env.inject_team_action(team_id, [&key_owner, &invitee]() {
      atfw::team::DTeamAction action;
      auto* invitation = action.mutable_add_invitation();
      protobuf_copy_message(*invitation->mutable_inviter(), key_owner);
      protobuf_copy_message(*invitation->mutable_invitee(), invitee);
      protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
      *invitation->mutable_expired_timepoint() =
          protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{3600});
      return action;
    }());
    CASE_EXPECT_TRUE(nullptr != invite_log);
    saved_sequence = nullptr != invite_log ? invite_log->sequence() : 0;
    // 快照之后日志: 两轮新 member_update
    env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), key_owner);
      update->set_client_version("post-owner");
      return action;
    }());
    env.inject_team_action(team_id, [&key_admin]() {
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), key_admin);
      update->set_client_version("post-admin");
      return action;
    }());

    // 快照覆盖到 saved_sequence(含邀请), 但裁剪边界停在更早的 compact_sequence:
    // 日志 (compact, saved] 既在快照里又留在 journal 中(重叠区), 重放必须从 saved+1 开始
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*storage.mutable_captain_user_key(), key_owner);
    auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    auto* owner_member = storage.add_member();
    protobuf_copy_message(*owner_member->mutable_user_key(), key_owner);
    owner_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    owner_member->set_client_version("v1-owner");
    *owner_member->mutable_joined_timepoint() = now_tp;
    *owner_member->mutable_last_heartbeat_timepoint() = now_tp;
    auto* admin_member = storage.add_member();
    protobuf_copy_message(*admin_member->mutable_user_key(), key_admin);
    admin_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    admin_member->set_client_version("v2-admin");
    *admin_member->mutable_joined_timepoint() = now_tp;
    *admin_member->mutable_last_heartbeat_timepoint() = now_tp;
    auto* pending = storage.add_pending_invitation();
    protobuf_copy_message(*pending->mutable_inviter(), key_owner);
    protobuf_copy_message(*pending->mutable_invitee(), invitee);
    protobuf_copy_message(*pending->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
    *pending->mutable_start_timepoint() = now_tp;
    *pending->mutable_expired_timepoint() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{3600});
    storage.set_saved_action_sequence(saved_sequence);
    storage.set_acknowledge_action_sequence(saved_sequence);
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    private_data.set_last_compact_sequence(compact_sequence);
    *private_data.mutable_last_compact_timepoint() = now_tp;
    fake.set_private_data(private_data);
  }

  size_t personal_before = env.personal_message_count();
  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 裁剪边界与覆盖范围不混用(CMP-08): compact 保持更早边界, saved 覆盖快照
  CASE_EXPECT_EQ(compact_sequence, room->debug_last_compact_sequence());
  CASE_EXPECT_EQ(saved_sequence, room->debug_saved_action_sequence());
  // 重叠区日志不重复应用: 成员恰好 owner+admin(不重复), 状态为快照后增量终态
  CASE_EXPECT_EQ(2u, room->debug_member_lru_keys().size());
  auto owner_member = room->find_member(key_owner, false);
  auto admin_member = room->find_member(key_admin, false);
  CASE_EXPECT_TRUE(!!owner_member && !!admin_member);
  if (owner_member) {
    CASE_EXPECT_EQ("post-owner", owner_member->member_data.client_version());
  }
  if (admin_member) {
    CASE_EXPECT_EQ("post-admin", admin_member->member_data.client_version());
  }
  // 恢复期间零个人通知
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  // 快照中的邀请恢复为恰一条: 可批准一次, 重复批准 not-found(无幽灵 admission)
  CASE_EXPECT_EQ(0,
                 env.run("approve_restored", [room, &invitee](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::SSTeamRoomApproveInvitationReq req;
                   protobuf_copy_message(*req.mutable_sender_user_key(), invitee);
                   protobuf_copy_message(*req.mutable_invitee(), invitee);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, req)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(nullptr != room->find_member(invitee, false));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                 env.run("approve_restored_twice", [room, &invitee](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::SSTeamRoomApproveInvitationReq req;
                   protobuf_copy_message(*req.mutable_sender_user_key(), invitee);
                   protobuf_copy_message(*req.mutable_invitee(), invitee);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, req)));
                 }));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}
CASE_TEST(teamsvr_room_recovery, snapshot_restore_equivalence) {
  // 准入有效期拉长: 双维度硬保证语义下压缩需推进到保留窗口(5s)之外，不能顺带让准入过期
  room_test_cfg_values cfg;
  cfg.invitation_expire_seconds = 3600;
  cfg.join_request_expire_seconds = 3600;
  room_test_env env(cfg);
  if (!env.start()) {
    return;
  }

  // 源队伍: 创建并写入成员/邀请/申请/共享数据/配置
  int64_t source_team = next_test_team_id();
  team_room::ptr_t source_room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, source_team, source_room, members));
  if (!source_room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto invitee = make_user_key(1, 8201);
  auto invite_req = [&members, &invitee]() {
    atfw::team::SSTeamRoomAddInvitationReq req;
    protobuf_copy_message(*req.mutable_sender_user_key(), members.normal);
    auto* invitation = req.mutable_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
    return req;
  }();
  CASE_EXPECT_EQ(0, env.run("add_invitation", [source_room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(source_room->add_invitation(ctx, invite_req)));
  }));

  auto applicant = make_user_key(1, 8202);
  auto join_req = [applicant]() {
    atfw::team::SSTeamRoomAddJoinRequestReq req;
    protobuf_copy_message(*req.mutable_sender_user_key(), applicant);
    auto* join_request = req.mutable_join_request();
    protobuf_copy_message(*join_request->mutable_requester(), applicant);
    protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                          make_personal_channel(applicant.user_id()));
    return req;
  }();
  CASE_EXPECT_EQ(0, env.run("add_join", [source_room, &join_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(source_room->add_join_request(ctx, join_req)));
  }));

  auto& source_fake = env.channel(source_team);
  {
    atfw::team::DTeamAction action;
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 100, "shared-1");
    action.mutable_team_update()->mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    // team_update 整体替换配置并重订默认值: 上限会被重置为 excel 默认值(3)，恢复后还要审批第 4 名成员，
    // 这里显式留出成员余量
    action.mutable_team_update()->mutable_configure()->set_max_member_count(8);
    CASE_EXPECT_EQ(0, env.run("team_update", [source_room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(source_room->send_action(ctx, action)));
    }));

    // 成员共享数据同样先于快照写入，恢复后的可见性由下方 condition 校验覆盖
    atfw::team::DTeamAction member_action;
    auto* member_update = member_action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), members.normal);
    add_team_any_data_entry(member_update->mutable_shared_member_data(), 7, "member-shared-1");
    CASE_EXPECT_EQ(0, env.run("member_update_shared_data",
                              [source_room, &member_action](rpc::context& ctx) -> rpc::result_code_type {
                                RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(source_room->send_action(ctx, member_action)));
                              }));
  }
  CASE_EXPECT_EQ(0, env.sync(source_team));
  // 共享数据事件日志的落点: 之后的压缩必须把它们裁剪进快照，使恢复只能来自 custom_data 的
  // dump/restore 回填，而不是 journal 重放
  int64_t shared_data_sequence = source_fake.last_sequence();

  // 通过真实 room maintenance 生成 custom/private 快照和 compact 边界；不能手工拼一个宣称
  // 覆盖最新 sequence 却缺失 admission 的快照，也不能把 A 队 journal 复制到 B 队频道。
  // 双维度硬保证语义下需推进到保留窗口(5s)之外，时间维度才放行裁剪
  CASE_EXPECT_TRUE(write_member_update_logs(env, source_room, members.normal, 8));
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(source_team));
  }
  CASE_EXPECT_TRUE(nullptr != find_compact_update(source_fake));
  CASE_EXPECT_GE(source_fake.last_removed_sequence(), shared_data_sequence);

  // 快照之后再追加一条增量，恢复必须同时读取权威快照和剩余 journal。
  atfw::team::DTeamAction post_snapshot_update;
  protobuf_copy_message(*post_snapshot_update.mutable_member_update()->mutable_user_key(), members.normal);
  post_snapshot_update.mutable_member_update()->set_client_version("after-snapshot");
  CASE_EXPECT_EQ(
      0,
      env.run("post_snapshot_update", [source_room, &post_snapshot_update](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(source_room->send_action(ctx, post_snapshot_update)));
      }));
  CASE_EXPECT_EQ(0, env.sync(source_team));

  // 丢弃旧 room，再从同一 team channel 的已保存快照和 compact 后增量建立新 room。
  size_t personal_before_restore = env.personal_message_count();
  room_test_env::clear_rooms();
  source_room.reset();
  team_room::ptr_t restored_room = env.setup_ready_room(source_team);
  CASE_EXPECT_TRUE(!!restored_room);
  if (!restored_room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  // fixture 复用进程级 subscriber；显式投递完整 snapshot，等价于新进程首次订阅，
  // 确保下面断言不是只从旧 subscriber 的已应用内存状态恢复。
  CASE_EXPECT_EQ(0, env.sync(source_team, true));

  // 恢复期间不发送历史个人通知(RCV-03): 邀请/申请日志重放不触发 invited 等通知
  CASE_EXPECT_EQ(personal_before_restore, env.personal_message_count());

  // 成员、快照后增量和写权限均恢复。
  CASE_EXPECT_TRUE(restored_room->find_member(members.owner, false) != nullptr);
  CASE_EXPECT_TRUE(restored_room->find_member(members.admin, false) != nullptr);
  auto restored_normal = restored_room->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!restored_normal);
  if (restored_normal) {
    CASE_EXPECT_EQ("after-snapshot", restored_normal->member_data.client_version());
  }

  // team_type 经快照回填(后续审批/配置默认值均以其为准)
  CASE_EXPECT_EQ(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL), restored_room->get_team_type());

  // configure 在快照中把 invite_role 提高到 ADMIN，NORMAL 恢复后仍不能发邀请。
  auto denied_invitee = make_user_key(1, 8203);
  atfw::team::SSTeamRoomAddInvitationReq denied_invite_req;
  protobuf_copy_message(*denied_invite_req.mutable_sender_user_key(), members.normal);
  auto* denied_invitation = denied_invite_req.mutable_invitation();
  protobuf_copy_message(*denied_invitation->mutable_inviter(), members.normal);
  protobuf_copy_message(*denied_invitation->mutable_invitee(), denied_invitee);
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
      env.run("invite_after_restore", [restored_room, &denied_invite_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored_room->add_invitation(ctx, denied_invite_req)));
      }));

  // 快照前写入的共享队伍/成员数据经 dump_public_data -> restore_snapshot 的 map<->repeated 转换链回填后
  // 仍然存在: 与 task_action 层同入口(check_action_permission)，以携带等值 condition 的 update 验证，
  // 不依赖内部 map accessor。条件值不匹配时被拒绝，证明判定确实基于恢复后的数据而非空条件放行。
  auto check_restored_permission = [&env, &restored_room, &members](const atfw::team::DTeamAction& action) {
    return env.run("check_shared_data_after_restore",
                   [restored_room, &members, &action](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(restored_room->check_action_permission(ctx, members.normal, action)));
                   });
  };
  {
    // 队伍级: 恢复后的 shared_team_data[100] == "shared-1" 才允许写入
    atfw::team::DTeamAction team_action;
    add_team_any_data_entry(team_action.mutable_team_update()->mutable_shared_team_data(), 101, "post-restore");
    add_team_any_value_entry(team_action.mutable_team_update()->add_condition()->mutable_shared_team_data(), 100,
                             "shared-1");
    CASE_EXPECT_EQ(0, check_restored_permission(team_action));

    atfw::team::DTeamAction team_mismatch;
    add_team_any_data_entry(team_mismatch.mutable_team_update()->mutable_shared_team_data(), 101, "post-restore");
    add_team_any_value_entry(team_mismatch.mutable_team_update()->add_condition()->mutable_shared_team_data(), 100,
                             "shared-other");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH, check_restored_permission(team_mismatch));
  }
  {
    // 成员级: 恢复后的 normal 成员 shared_member_data[7] == "member-shared-1"
    atfw::team::DTeamAction member_action;
    auto* update = member_action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    add_team_any_data_entry(update->mutable_shared_member_data(), 8, "post-restore-member");
    auto* group = update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.normal);
    add_team_any_value_entry(group->mutable_member_condition()->mutable_shared_member_data(), 7, "member-shared-1");
    CASE_EXPECT_EQ(0, check_restored_permission(member_action));

    atfw::team::DTeamAction member_mismatch;
    auto* mismatch_update = member_mismatch.mutable_member_update();
    protobuf_copy_message(*mismatch_update->mutable_user_key(), members.normal);
    auto* mismatch_group = mismatch_update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*mismatch_group->mutable_user_key(), members.normal);
    add_team_any_value_entry(mismatch_group->mutable_member_condition()->mutable_shared_member_data(), 7,
                             "member-shared-other");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_restored_permission(member_mismatch));
  }

  // invitation/join request 来自真实快照；通过各自后续业务流程验证，不依赖内部 map accessor。
  size_t personal_before = env.personal_message_count();
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
  size_t sends_before = source_fake.send_message_calls();
  int32_t approve_ret =
      env.run("approve_restored", [restored_room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored_room->approve_invitation(ctx, approve_req)));
      });
  CASE_EXPECT_EQ(0, approve_ret);
  if (0 == approve_ret) {
    CASE_EXPECT_GT(source_fake.send_message_calls(), sends_before);
  }
  CASE_EXPECT_EQ(0, env.sync(source_team));
  CASE_EXPECT_TRUE(restored_room->find_member(invitee, false) != nullptr);
  CASE_EXPECT_GT(env.personal_message_count(), personal_before);

  atfw::team::SSTeamRoomRejectJoinRequestReq reject_join_req;
  protobuf_copy_message(*reject_join_req.mutable_sender_user_key(), members.admin);
  protobuf_copy_message(*reject_join_req.mutable_applicant(), applicant);
  CASE_EXPECT_EQ(
      0, env.run("reject_join_after_restore",
                 [restored_room, &reject_join_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored_room->reject_join_request(ctx, reject_join_req)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(source_team));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-04: custom Any 损坏时恢复失败，room 不进入可写主控状态 ============
CASE_TEST(teamsvr_room_recovery, corrupt_custom_data_restore_fails) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto& fake = env.channel(team_id);
  fake.ensure_created();
  fake.set_corrupt_custom_data("type.googleapis.com/atframework.team.DTeamStorage", std::string("\xff\xfe\xfd\xfc"));

  // await_ready 在快照恢复失败时返回错误
  int32_t ready_ret = env.run("await_ready_corrupt", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    auto room = team_room_manager::me()->mutable_room(ctx, make_team_key(team_id));
    if (!room) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
  });
  CASE_EXPECT_NE(0, ready_ret);

  auto room = team_room_manager::me()->get_room(make_team_key(team_id));
  if (room) {
    CASE_EXPECT_FALSE(room->is_lock_holder());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-04/BND-03: 完整快照 key 指向其他区服时拒绝恢复，避免跨队伍状态串读 ============
CASE_TEST(teamsvr_room_recovery, snapshot_identity_validation_rejects_cross_zone_keys) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto target_key = make_team_key(team_id, kTestZoneId + 1);
  auto& fake = env.channel(target_key);
  fake.ensure_created();

  atfw::team::DTeamStorage wrong_storage;
  protobuf_copy_message(*wrong_storage.mutable_team_key(), make_team_key(team_id, kTestZoneId));
  auto* member = wrong_storage.add_member();
  protobuf_copy_message(*member->mutable_user_key(), make_user_key(kTestZoneId, 8499));
  member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  fake.set_custom_data(wrong_storage);

  size_t sends_before = fake.send_message_calls();
  size_t updates_before = fake.update_calls();
  CASE_EXPECT_FALSE(!!env.setup_ready_room(target_key));
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  CASE_EXPECT_EQ(updates_before, fake.update_calls());

  auto room = team_room_manager::me()->get_room(target_key);
  if (room) {
    CASE_EXPECT_FALSE(room->is_lock_holder());
    CASE_EXPECT_FALSE(!!room->find_member(member->user_key(), false));
  }

  // 只携带 zone_id 不是 legacy key，而是损坏的部分身份；不能按当前 team_id 静默补齐。
  int64_t partial_team_id = next_test_team_id();
  auto partial_key = make_team_key(partial_team_id, kTestZoneId + 1);
  auto& partial_fake = env.channel(partial_key);
  partial_fake.ensure_created();
  atfw::team::DTeamStorage partial_storage;
  partial_storage.mutable_team_key()->set_zone_id(kTestZoneId + 1);
  partial_fake.set_custom_data(partial_storage);
  CASE_EXPECT_FALSE(!!env.setup_ready_room(partial_key));
  CASE_EXPECT_EQ(0u, partial_fake.send_message_calls());
  CASE_EXPECT_EQ(0u, partial_fake.update_calls());

  // zone_id=0 是合法的全局团队身份，不能再当作“旧快照缺字段”补成分区团队。
  int64_t global_team_id = next_test_team_id();
  auto zoned_key = make_team_key(global_team_id, kTestZoneId + 1);
  auto& global_fake = env.channel(zoned_key);
  global_fake.ensure_created();
  atfw::team::DTeamStorage global_storage;
  protobuf_copy_message(*global_storage.mutable_team_key(), make_team_key(global_team_id, 0));
  global_fake.set_custom_data(global_storage);

  CASE_EXPECT_FALSE(!!env.setup_ready_room(zoned_key));
  CASE_EXPECT_EQ(0u, global_fake.send_message_calls());
  CASE_EXPECT_EQ(0u, global_fake.update_calls());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-09(partial): add_member 已提交但回包失败、approve 未提交时重试不重复添加 ============
CASE_TEST(teamsvr_room_recovery, approve_crash_checkpoint_retry) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto invitee = make_user_key(1, 8211);
  auto invite_req = [&members, &invitee]() {
    atfw::team::SSTeamRoomAddInvitationReq req;
    protobuf_copy_message(*req.mutable_sender_user_key(), members.normal);
    auto* invitation = req.mutable_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
    return req;
  }();
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 第一次 approve 的 add_member+approve 合并为一次频道写入；编排其提交成功但回包失败。
  auto& fake = env.channel(team_id);
  fake.next_send_fault.present = true;
  fake.next_send_fault.commit_first = true;
  fake.next_send_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;

  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
  int32_t first_ret = env.run("approve_fault", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  });
  CASE_EXPECT_NE(0, first_ret);

  // 合并写入已整体提交: 事件回环后成员存在，approve 一并生效、邀请已清理
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(invitee, false) != nullptr);
  // 已提交的一半在回环时产生恰好一次入队通知(apply 路径幂等: 邀请记录随回环清理)
  CASE_EXPECT_EQ(1u, count_personal_actions(env, invitee.user_id(), atfw::team::DTeamMemberAction::kJoinedTeam));
  size_t personal_total_before = env.personal_message_count();

  // 重试 approve: 邀请已清理，返回 not-found 且不再追加任何事件(不重复添加成员)
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                 env.run("approve_retry", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                 }));
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  // 崩溃点重试不产生第二次个人通知(通知与日志写入解耦，须独立断言)
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, count_personal_actions(env, invitee.user_id(), atfw::team::DTeamMemberAction::kJoinedTeam));
  CASE_EXPECT_EQ(personal_total_before, env.personal_message_count());

  // 日志中 invitee 的 add_member 只出现一次
  size_t add_count = 0;
  fake.foreach_team_action(
      [&invitee, &add_count](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kAddMember &&
            action.add_member().user_key().user_id() == invitee.user_id()) {
          ++add_count;
        }
        return true;
      });
  CASE_EXPECT_EQ(1u, add_count);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-09(partial): approve 合并写入预提交失败 -> 零提交零通知, 重试后恰一次生效 ============
CASE_TEST(teamsvr_room_recovery, approve_crash_before_commit_retry) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto invitee = make_user_key(1, 8217);
  atfw::team::SSTeamRoomAddInvitationReq invite_req;
  protobuf_copy_message(*invite_req.mutable_sender_user_key(), members.normal);
  auto* invitation = invite_req.mutable_invitation();
  protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
  protobuf_copy_message(*invitation->mutable_invitee(), invitee);
  protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee);

  // 崩溃点在合并写入提交之前: add_member+approve 均未到达 journal, 无任何成员/通知副作用
  fake.next_send_fault.present = true;
  fake.next_send_fault.commit_first = false;
  fake.next_send_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;
  int32_t first_ret = env.run("approve_pre_commit_fault", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  });
  CASE_EXPECT_NE(0, first_ret);
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(nullptr == room->find_member(invitee, false));
  CASE_EXPECT_EQ(0u, count_personal_actions(env, invitee.user_id(), atfw::team::DTeamMemberAction::kJoinedTeam));
  auto count_actions = [&fake](atfw::team::DTeamAction::ActionCase action_case) {
    size_t ret = 0;
    fake.foreach_team_action(
        [&ret, action_case](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
          if (action.action_case() == action_case) {
            ++ret;
          }
          return true;
        });
    return ret;
  };
  // 邀请本身仍在(只有 add_invitation 一条, approve 系零写入)
  CASE_EXPECT_EQ(1u, count_actions(atfw::team::DTeamAction::kAddInvitation));
  CASE_EXPECT_EQ(0u, count_actions(atfw::team::DTeamAction::kApproveInvitation));

  // 重试 approve: 恰一次合并提交(add_member+approve), 恰一次入队通知, 邀请随回环清理
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("approve_retry", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  }));
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(nullptr != room->find_member(invitee, false));
  CASE_EXPECT_EQ(1u, count_actions(atfw::team::DTeamAction::kApproveInvitation));
  CASE_EXPECT_EQ(1u, count_personal_actions(env, invitee.user_id(), atfw::team::DTeamMemberAction::kJoinedTeam));
  // 日志中 invitee 的 add_member 只出现一次
  size_t add_count = 0;
  fake.foreach_team_action(
      [&invitee, &add_count](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kAddMember &&
            action.add_member().user_key().user_id() == invitee.user_id()) {
          ++add_count;
        }
        return true;
      });
  CASE_EXPECT_EQ(1u, add_count);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-10(partial): destroy-team 提交后重建 room 不得恢复为可写未销毁队伍 ============
CASE_TEST(teamsvr_room_recovery, destroyed_team_not_recreated) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 解散队伍并回环
  atfw::team::DTeamAction destroy_action;
  protobuf_copy_message(*destroy_action.mutable_destroy_team(), make_team_key(team_id));
  CASE_EXPECT_EQ(0, env.run("destroy_team", [room, &destroy_action](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, destroy_action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 真实丢弃旧 room，再从同一 subscriber 的快照/journal 建立新 room；不能复用 manager 中
  // 已经标记 destroyed 的原对象来冒充恢复覆盖。
  room_test_env::clear_rooms();
  room.reset();
  team_room::ptr_t recovered = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!recovered);
  if (recovered) {
    // 新进程订阅会收到完整 snapshot/journal；fixture 复用 subscriber，因此显式重投。
    CASE_EXPECT_EQ(0, env.sync(team_id, true));
    atfw::team::SSTeamRoomCreateReq create_req;
    protobuf_copy_message(*create_req.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*create_req.mutable_sender_user_key(), members.owner);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED,
                   env.run("recreate_destroyed", [recovered, &create_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(recovered->create_team(ctx, create_req)));
                   }));
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-10(partial): destroy-channel 已提交但响应丢失 -> 重试幂等, 重启后旧 team id 不得复活 ============
CASE_TEST(teamsvr_room_recovery, destroy_channel_response_lost_recovery) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);

  // 所有成员退出 -> 空房间维护写 destroy_team
  auto remove_member_fn = [&env, room](const PROJECT_NAMESPACE_ID::DUserIDKey& key) {
    return env.run("remove_member", [room, &key](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), key);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
  };
  CASE_EXPECT_EQ(0, remove_member_fn(members.normal));
  CASE_EXPECT_EQ(0, remove_member_fn(members.admin));
  CASE_EXPECT_EQ(0, remove_member_fn(members.owner));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // destroy-channel 首次调用: 服务端已销毁但响应丢失(客户端视为失败, 稍后重试)
  fake.next_destroy_fault.present = true;
  fake.next_destroy_fault.commit_first = true;
  fake.next_destroy_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    for (int round = 0; round < 8 && 0 == fake.destroy_calls(); ++round) {
      global_now_offset_guard::advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
  }
  // 服务端已提交销毁(带 kDestroy 日志), 客户端继续重试且幂等(不报错、不重复副作用)
  CASE_EXPECT_GE(fake.destroy_calls(), 1u);
  CASE_EXPECT_TRUE(fake.is_destroyed());
  {
    global_now_offset_guard guard(std::chrono::seconds{20});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  size_t event_logs_after_destroy = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent);

  // 真实丢弃旧 room。已销毁频道在 fake 模式下不再向新订阅提供就绪快照(WAL-08 已用真实 mq_channel
  // 锁定“带 destroy 元数据快照恢复销毁状态 + create 拒绝”语义), 这里锁定重启侧契约:
  // 旧 team id 无法恢复出可写未销毁队伍, 任何重建尝试零写入。
  room_test_env::clear_rooms();
  room.reset();
  team_room::ptr_t recovered = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!recovered);
  auto manager_room = team_room_manager::me()->get_room(make_team_key(team_id));
  if (manager_room) {
    CASE_EXPECT_FALSE(manager_room->is_lock_holder());
    atfw::team::SSTeamRoomCreateReq create_req;
    protobuf_copy_message(*create_req.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*create_req.mutable_sender_user_key(), members.owner);
    CASE_EXPECT_NE(0,
                   env.run("recreate_after_destroy_channel",
                           [manager_room, &create_req](rpc::context& ctx) -> rpc::result_code_type {
                             RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(manager_room->create_team(ctx, create_req)));
                           }));
  }
  // 恢复尝试未产生任何新的队伍事件(重建拒绝零写入)
  CASE_EXPECT_EQ(event_logs_after_destroy, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-01: 快照恢复后的 LRU 物理顺序(按 max(joined,heartbeat) 升序，同值按 user key) ============
CASE_TEST(teamsvr_room_recovery, snapshot_restore_lru_order) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto key_a = make_user_key(1, 8501);  // 最久未活跃
  auto key_b = make_user_key(1, 8502);
  auto key_c = make_user_key(1, 8503);  // 最近活跃
  auto key_d = make_user_key(1, 8504);  // 与 B 同访问时间(平局按 user key)
  {
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*storage.mutable_captain_user_key(), key_a);
    auto now = atfw::util::time::time_utility::now();
    auto add_member_with_time = [&storage, &now](const PROJECT_NAMESPACE_ID::DUserIDKey& key, int64_t joined_ago,
                                                 int64_t heartbeat_ago, atfw::team::EnTeamPermissionRole role) {
      auto* member = storage.add_member();
      protobuf_copy_message(*member->mutable_user_key(), key);
      member->set_role(role);
      *member->mutable_joined_timepoint() = protobuf_from_system_clock(now - std::chrono::seconds{joined_ago});
      *member->mutable_last_heartbeat_timepoint() =
          protobuf_from_system_clock(now - std::chrono::seconds{heartbeat_ago});
    };
    // 乱序写入快照: 恢复应按 max(joined,heartbeat) 重建 LRU 物理顺序
    add_member_with_time(key_c, 60, 1, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);    // max = T-1
    add_member_with_time(key_a, 100, 100, atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);  // max = T-100
    add_member_with_time(key_d, 50, 50, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);   // max = T-50(平局, key 大)
    add_member_with_time(key_b, 50, 50, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);   // max = T-50(平局, key 小)
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    fake.set_private_data(private_data);
  }

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // LRU front->back: A(T-100) -> B(T-50,key 小) -> D(T-50,key 大) -> C(T-1)
  auto lru_keys = room->debug_member_lru_keys();
  CASE_EXPECT_EQ(4u, lru_keys.size());
  if (4u == lru_keys.size()) {
    CASE_EXPECT_EQ(key_a.user_id(), lru_keys[0].user_id());
    CASE_EXPECT_EQ(key_b.user_id(), lru_keys[1].user_id());
    CASE_EXPECT_EQ(key_d.user_id(), lru_keys[2].user_id());
    CASE_EXPECT_EQ(key_c.user_id(), lru_keys[3].user_id());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-05: 快照含无效/重复 member key -> 跳过无效、首个重复生效，状态一致 ============
CASE_TEST(teamsvr_room_recovery, snapshot_invalid_duplicate_member_keys) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto key_owner = make_user_key(1, 8511);
  auto key_normal = make_user_key(1, 8512);
  {
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*storage.mutable_captain_user_key(), key_owner);
    auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    auto add_member = [&storage, &now_tp](const PROJECT_NAMESPACE_ID::DUserIDKey& key,
                                          atfw::team::EnTeamPermissionRole role) {
      auto* member = storage.add_member();
      protobuf_copy_message(*member->mutable_user_key(), key);
      member->set_role(role);
      *member->mutable_joined_timepoint() = now_tp;
      *member->mutable_last_heartbeat_timepoint() = now_tp;
    };
    add_member(key_owner, atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    // 无效成员: zone_id=0 / user_id=0
    add_member(make_user_key(0, 9999), atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    add_member(make_user_key(1, 0), atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    // 重复 key: 首个(NORMAL)生效，后续(ADMIN)忽略
    add_member(key_normal, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    add_member(key_normal, atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    fake.set_private_data(private_data);
  }

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 无效成员被跳过，重复成员首个生效
  auto normal_member = room->find_member(key_normal, false);
  CASE_EXPECT_TRUE(!!normal_member);
  if (normal_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, normal_member->member_data.role());
  }
  CASE_EXPECT_TRUE(room->find_member(key_owner, false) != nullptr);
  CASE_EXPECT_EQ(2u, room->debug_member_lru_keys().size());
  // 队长一致(OWNER)
  auto captain = room->find_member(key_owner, false);
  CASE_EXPECT_TRUE(!!captain);
  if (captain) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain->member_data.role());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-06: 缺失 private data 的旧快照兼容恢复(GAP-10 legacy): 从缓存起点幂等重放 ============
CASE_TEST(teamsvr_room_recovery, snapshot_missing_private_data_legacy) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto key_owner = make_user_key(1, 8521);
  {
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    // 频道上已有部分日志(saved=2 之前一条 add_member，之后一条 member_update)
    auto* add_log = env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* add_member = action.mutable_add_member();
      protobuf_copy_message(*add_member->mutable_user_key(), key_owner);
      add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      return action;
    }());
    CASE_EXPECT_TRUE(nullptr != add_log);
    // 立即取 sequence: 继续 append 会使 journal vector 扩容导致指针失效
    const int64_t saved_sequence = nullptr != add_log ? add_log->sequence() : 0;
    env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), key_owner);
      update->set_client_version("after-saved");
      return action;
    }());

    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*storage.mutable_captain_user_key(), key_owner);
    // 快照含 add_member 时的成员(保存点 = 该日志)
    auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    auto* member = storage.add_member();
    protobuf_copy_message(*member->mutable_user_key(), key_owner);
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    *member->mutable_joined_timepoint() = now_tp;
    *member->mutable_last_heartbeat_timepoint() = now_tp;
    storage.set_saved_action_sequence(saved_sequence);
    storage.set_acknowledge_action_sequence(saved_sequence);
    fake.set_custom_data(storage);
    // 故意不设置 private data(legacy 频道)
  }

  size_t personal_before = env.personal_message_count();
  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 缺失 private data 时从缓存起点重放: 幂等(成员不重复)，快照之后的日志照常应用
  CASE_EXPECT_EQ(1u, room->debug_member_lru_keys().size());
  auto owner_member = room->find_member(key_owner, false);
  CASE_EXPECT_TRUE(!!owner_member);
  if (owner_member) {
    CASE_EXPECT_EQ("after-saved", owner_member->member_data.client_version());
  }
  // 恢复期间无个人通知副作用
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());
  // 恢复出可写队伍(team_created 由 public 数据推导)
  CASE_EXPECT_EQ(0,
                 env.run("write_after_legacy_restore", [room, &key_owner](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* update = action.mutable_member_update();
                   protobuf_copy_message(*update->mutable_user_key(), key_owner);
                   update->set_client_version("post-restore");
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-06(partial): private 快照落后/不同代时 private team data 不静默丢失, 代际按 public 推导愈合 ============
CASE_TEST(teamsvr_room_recovery, snapshot_stale_private_generation_preserved) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto key_owner = make_user_key(1, 8541);
  {
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    // 快照覆盖范围内: add owner + 一轮 member_update; 快照之后: 再一轮 member_update
    auto* add_log = env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* add_member = action.mutable_add_member();
      protobuf_copy_message(*add_member->mutable_user_key(), key_owner);
      add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      return action;
    }());
    CASE_EXPECT_TRUE(nullptr != add_log);
    auto* in_snapshot_log = env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), key_owner);
      update->set_client_version("in-snapshot");
      return action;
    }());
    CASE_EXPECT_TRUE(nullptr != in_snapshot_log);
    // 立即取 sequence: 继续 append 会使 journal vector 扩容导致指针失效
    const int64_t saved_sequence = nullptr != in_snapshot_log ? in_snapshot_log->sequence() : 0;
    env.inject_team_action(team_id, [&key_owner]() {
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), key_owner);
      update->set_client_version("post-snapshot");
      return action;
    }());

    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*storage.mutable_captain_user_key(), key_owner);
    auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    auto* member = storage.add_member();
    protobuf_copy_message(*member->mutable_user_key(), key_owner);
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    member->set_client_version("in-snapshot");
    *member->mutable_joined_timepoint() = now_tp;
    *member->mutable_last_heartbeat_timepoint() = now_tp;
    storage.set_saved_action_sequence(saved_sequence);
    storage.set_acknowledge_action_sequence(saved_sequence);
    fake.set_custom_data(storage);

    // 落后/不同代的 private 快照: team_created=false(旧代际)、裁剪边界更早、携带私有 keyed 数据
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(false);
    private_data.set_last_compact_sequence(0);
    auto* entry = private_data.add_private_team_data();
    entry->set_key(7);
    entry->mutable_value()->mutable_data()->set_type_url("type.googleapis.com/atframework.team.ut_private_data");
    entry->mutable_value()->mutable_data()->set_value("ut-private-7");
    fake.set_private_data(private_data);
  }

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // team_created 由 public 信号推导(快照有成员/队长) -> 房间恢复为可写已创建队伍
  auto owner_member = room->find_member(key_owner, false);
  CASE_EXPECT_TRUE(!!owner_member);
  if (owner_member) {
    CASE_EXPECT_EQ("post-snapshot", owner_member->member_data.client_version());
  }
  CASE_EXPECT_EQ(0,
                 env.run("write_after_stale_private", [room, &key_owner](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* update = action.mutable_member_update();
                   protobuf_copy_message(*update->mutable_user_key(), key_owner);
                   update->set_client_version("post-restore");
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 再写足量日志触发压缩快照: dump 出的 private 快照必须保留原私有数据且 team_created 愈合为 true
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, key_owner, 6));
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  const atfw::dtmq::SSChannelUpdateReq* compact_update = find_compact_update(env.channel(team_id));
  CASE_EXPECT_TRUE(nullptr != compact_update);
  if (nullptr != compact_update) {
    atfw::team::DTeamRoomPrivateData dumped_private;
    CASE_EXPECT_TRUE(compact_update->private_data().UnpackTo(&dumped_private));
    CASE_EXPECT_TRUE(dumped_private.team_created());
    bool found_private_entry = false;
    for (const auto& data : dumped_private.private_team_data()) {
      if (7 == data.key()) {
        found_private_entry = true;
        CASE_EXPECT_EQ("ut-private-7", data.value().data().value());
      }
    }
    CASE_EXPECT_TRUE(found_private_entry);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-07: 快照边界矛盾(compact>saved 或 saved>频道最新)拒绝恢复，房间保持不可写 ============
CASE_TEST(teamsvr_room_recovery, snapshot_boundary_contradiction_rejected) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // 变体1: last_compact_sequence > saved_action_sequence
  int64_t team1 = next_test_team_id();
  {
    auto& fake = env.channel(team1);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team1));
    storage.set_saved_action_sequence(1);
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    private_data.set_last_compact_sequence(10);  // 矛盾: compact 超过快照覆盖
    fake.set_private_data(private_data);
  }
  // 变体2: saved_action_sequence 超过频道已见最新日志
  int64_t team2 = next_test_team_id();
  {
    auto& fake = env.channel(team2);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team2));
    storage.set_saved_action_sequence(1000);  // 矛盾: 频道只有 create 日志
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    fake.set_private_data(private_data);
  }

  for (int64_t team_id : {team1, team2}) {
    auto& fake = env.channel(team_id);
    size_t sends_before = fake.send_message_calls();
    size_t resets_before = fake.reset_lock_calls();
    int32_t ready_ret = env.run("await_ready_contradiction", [team_id](rpc::context& ctx) -> rpc::result_code_type {
      auto room = team_room_manager::me()->mutable_room(ctx, make_team_key(team_id));
      if (!room) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
      }
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
    });
    CASE_EXPECT_NE(0, ready_ret);

    auto room = team_room_manager::me()->get_room(make_team_key(team_id));
    if (room) {
      CASE_EXPECT_FALSE(room->is_lock_holder());
      // 座位带: 直接写入也被拒绝
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE,
                     env.run("write_blocked", [room](rpc::context& ctx) -> rpc::result_code_type {
                       atfw::team::DTeamAction action;
                       protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(),
                                             make_user_key(1, 8531));
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                     }));
    }
    // 零写入: 不产生任何频道写/锁变更
    CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
    CASE_EXPECT_EQ(resets_before, fake.reset_lock_calls());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-08: 心跳/ack 更新后无新日志即切主: 新主控以恢复点为下限，不误踢在线成员 ============
CASE_TEST(teamsvr_room_recovery, failover_without_new_logs_no_premature_kick) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // T1: normal 心跳(仅本地状态，无新日志)
  CASE_EXPECT_EQ(0, env.run("heartbeat_t1", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomHeartbeatReq hb;
    protobuf_copy_message(*hb.mutable_user_key(), members.normal);
    hb.set_user_router_server_id(0x1234);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
  }));

  // T1+25s(未到 30s 过期线): 丢弃旧 room，再从未包含本地心跳的 DTMQ 快照建立新 room。
  // 这是真实 failover/restart 对象边界，不在同一个 room 上强制调用 restore 来冒充切主。
  {
    global_now_offset_guard guard(std::chrono::seconds{25});
    room_test_env::clear_rooms();
    room.reset();
    room = env.setup_ready_room(team_id);
    CASE_EXPECT_TRUE(!!room);
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    // fixture 复用进程级 client_subscriber；新节点订阅在生产中必然收到完整 snapshot，
    // 因此对新 room 显式投递一次，而不是让它只复用旧 subscriber cache。
    CASE_EXPECT_EQ(0, env.sync(team_id, true));
    // 恢复下限保护: 新主控不得以快照中的旧心跳时间误踢在线成员
    CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);

    // 恢复点 + 27s 内: 仍不踢(恢复点为下限, 30s 过期线未到)
    for (int round = 0; round < 3; ++round) {
      global_now_offset_guard::advance(std::chrono::seconds{9});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
    CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);

    // 此刻(T2+27)心跳 owner -> owner 过期线顺延到 T2+57
    CASE_EXPECT_EQ(0, env.run("heartbeat_t2", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::SSTeamRoomHeartbeatReq hb;
      protobuf_copy_message(*hb.mutable_user_key(), members.owner);
      hb.set_user_router_server_id(0x1235);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
    }));

    // T2+36: normal 到达恢复点+30s 过期线被踢出; owner 因新心跳存活
    for (int round = 0; round < 3; ++round) {
      global_now_offset_guard::advance(std::chrono::seconds{3});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
    CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());
    CASE_EXPECT_TRUE(room->find_member(members.owner, false) != nullptr);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-01b: 未过期他人锁不写入/不抢锁(FIX-07)，按锁超时重新调度接管 ============
CASE_TEST(teamsvr_room_lock, unexpired_foreign_lock_no_steal) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());

  // 第三方持锁(3s 后过期)，经日志下发 -> 本节点退位
  auto& fake = env.channel(team_id);
  fake.set_lock(make_foreign_lock("teamsvr-room:foreign-node", 3));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 写入尝试: 视图中的他人锁未过期 -> 客户端直接拒绝(不发起 CAS; DTMQ 乐观锁是持有者回显语义，
  // 视图新鲜时 CAS 必然通过会造成抢锁)
  size_t resets_before = fake.reset_lock_calls();
  size_t events_before = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent);
  int32_t write_ret = env.run("write_under_foreign_lock", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED, write_ret);
  CASE_EXPECT_EQ(resets_before, fake.reset_lock_calls());  // 未发起 CAS
  CASE_EXPECT_EQ(events_before, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 定时器按锁超时时间调度接管
  {
    auto next_event = room->get_next_timer_event(atfw::util::time::time_utility::now());
    CASE_EXPECT_EQ(static_cast<int32_t>(team_room_timer_event_type::kAcquireLock),
                   static_cast<int32_t>(next_event.type));
  }

  // 锁到期后定时器驱动接管
  {
    global_now_offset_guard guard(std::chrono::seconds{4});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());
  CASE_EXPECT_EQ(0, env.run("write_after_takeover", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-04: 接管无队长快照(有成员) -> 新主控确定性选举；备用节点不选举 ============
CASE_TEST(teamsvr_room_lock, captainless_snapshot_takeover_election) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  auto setup_captainless_channel = [&env](int64_t team_id, bool self_lock, uint64_t& m1, uint64_t& m2) {
    m1 = 8541;
    m2 = 8542;
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    // 无队长
    auto now = atfw::util::time::time_utility::now();
    auto* member1 = storage.add_member();
    protobuf_copy_message(*member1->mutable_user_key(), make_user_key(1, m1));
    member1->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member1->mutable_joined_timepoint() = protobuf_from_system_clock(now - std::chrono::seconds{100});
    *member1->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now - std::chrono::seconds{100});
    auto* member2 = storage.add_member();
    protobuf_copy_message(*member2->mutable_user_key(), make_user_key(1, m2));
    member2->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member2->mutable_joined_timepoint() = protobuf_from_system_clock(now - std::chrono::seconds{50});
    *member2->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now - std::chrono::seconds{50});
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    fake.set_private_data(private_data);
    protobuf_copy_message(fake.mutable_lock(), self_lock ? make_foreign_lock(self_lock_holder(), 3600)
                                                         : make_foreign_lock("teamsvr-room:other-node", 3600));
  };

  // 新主控(锁归本节点): 接管时确定性选举最早入队成员为队长
  int64_t team_a = next_test_team_id();
  uint64_t m1 = 0, m2 = 0;
  setup_captainless_channel(team_a, true, m1, m2);
  team_room::ptr_t room_a = env.setup_ready_room(team_a);
  CASE_EXPECT_TRUE(!!room_a);
  if (!room_a) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_TRUE(room_a->is_lock_holder());
  CASE_EXPECT_EQ(0, env.sync(team_a));

  auto& fake_a = env.channel(team_a);
  size_t election_count = 0;
  fake_a.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kElectionCaptain) {
      ++election_count;
      CASE_EXPECT_EQ(m1, action.election_captain().user_key().user_id());  // 最早入队
    }
    return true;
  });
  CASE_EXPECT_EQ(1u, election_count);
  // 选举事件由恢复流程的延迟 flush 写入，需再同步一轮回环应用后新队长才晋升 OWNER
  // (角色由 apply 侧 change_captain 维护，事件只携带 user_key)
  CASE_EXPECT_EQ(0, env.sync(team_a));
  {
    auto elected = room_a->find_member(make_user_key(1, m1), false);
    CASE_EXPECT_TRUE(!!elected);
    if (elected) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, elected->member_data.role());
    }
  }

  // 备用节点(他人持锁): 恢复同一份无队长快照时不产生选举写入
  int64_t team_b = next_test_team_id();
  setup_captainless_channel(team_b, false, m1, m2);
  team_room::ptr_t room_b = env.setup_ready_room(team_b);
  CASE_EXPECT_TRUE(!!room_b);
  if (!room_b) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_FALSE(room_b->is_lock_holder());
  auto& fake_b = env.channel(team_b);
  size_t sends_before_b = fake_b.send_message_calls();
  CASE_EXPECT_EQ(0, env.sync(team_b));
  CASE_EXPECT_EQ(sends_before_b, fake_b.send_message_calls());
  fake_b.foreach_team_action([](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    CASE_EXPECT_TRUE(action.action_case() != atfw::team::DTeamAction::kElectionCaptain);
    return true;
  });

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-05: 事件已应用+通知已入队，flush 前锁转移 -> 旧主控不产生副作用(GAP-06 fencing) ============
CASE_TEST(teamsvr_room_lock, flush_drops_notifications_after_lock_loss) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  auto invitee = make_user_key(1, 8551);
  // 同一批次: 先邀请事件(持锁应用并入队通知)，后锁转移日志(退位)
  env.inject_team_action(team_id, [&members, &invitee]() {
    atfw::team::DTeamAction action;
    auto* invitation = action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(8551));
    return action;
  }());
  fake.set_lock(make_foreign_lock("teamsvr-room:new-master", 3600));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 旧主控不得发出已入队的个人通知(至多一次+fencing)；队列清空
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 8551, atfw::team::DTeamMemberAction::kInvited));
  CASE_EXPECT_EQ(0u, room->debug_pending_notification_count());
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 邀请状态已应用(新主控重放/恢复后可见): 备用节点上可读
  atfw::team::SSTeamRoomRejectInvitationReq reject_req;
  protobuf_copy_message(*reject_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*reject_req.mutable_invitee(), invitee);
  // 备用节点上 invitee 本人拒绝: 不写频道(锁在新主控)，但准入状态可见 -> 不是 not-found
  int32_t reject_ret = env.run("reject_on_standby", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, reject_req)));
  });
  // 锁已易主: 写入被锁拒绝(FIX-07 不抢锁)
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED, reject_ret);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-06(partial): 单 room 模拟竞争者先赢 CAS；失败者不被后续事件复活 ============
CASE_TEST(teamsvr_room_lock, concurrent_cas_competitor_no_revive) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  {
    // 预置: 已创建频道 + 已过期的老锁(两个候选都看到这把老锁)
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    auto* member = storage.add_member();
    protobuf_copy_message(*member->mutable_user_key(), make_user_key(1, 8561));
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    *member->mutable_joined_timepoint() = now_tp;
    *member->mutable_last_heartbeat_timepoint() = now_tp;
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    fake.set_private_data(private_data);
    protobuf_copy_message(fake.mutable_lock(), make_foreign_lock("teamsvr-room:crashed-node", -10));
  }

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 竞态: 本节点 CAS 老锁之前，竞争者已先成功(服务端锁静默易主为未过期竞争者)
  auto& fake = env.channel(team_id);
  protobuf_copy_message(fake.mutable_lock(), make_foreign_lock("teamsvr-room:competitor", 3600));

  // 定时器驱动接管: CAS 冲突 -> 失败且不抢竞争者的锁(FIX-07)
  size_t resets_before = fake.reset_lock_calls();
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(resets_before + 1, fake.reset_lock_calls());
  CASE_EXPECT_FALSE(room->is_lock_holder());
  CASE_EXPECT_EQ("teamsvr-room:competitor", fake.lock().lock_holder());

  // 迟到的竞争者锁日志 + 业务事件: 只读应用，不复活 writer 身份
  fake.set_lock(make_foreign_lock("teamsvr-room:competitor", 3600));  // 与当前一致 -> 不追加日志
  env.inject_team_action(team_id, []() {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), make_user_key(1, 8561));
    action.mutable_member_update()->set_client_version("from-competitor");
    return action;
  }());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_FALSE(room->is_lock_holder());
  auto member = room->find_member(make_user_key(1, 8561), false);
  CASE_EXPECT_TRUE(!!member);
  if (member) {
    CASE_EXPECT_EQ("from-competitor", member->member_data.client_version());
  }

  // 竞争者租约到期后(视图仍是旧锁) -> 定时器驱动接管成功
  {
    global_now_offset_guard guard(std::chrono::seconds{3601});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-06(partial): 迟到 CAS 成功响应不得让写副作用逃脱 fencing(锁已易主时写入必被拒且退位) ============
CASE_TEST(teamsvr_room_lock, late_cas_success_response_fenced_write) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto key_member = make_user_key(1, 8571);
  {
    // 预置: 已创建频道 + 一名成员 + 已过期的老锁
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    auto* member = storage.add_member();
    protobuf_copy_message(*member->mutable_user_key(), key_member);
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    *member->mutable_joined_timepoint() = now_tp;
    *member->mutable_last_heartbeat_timepoint() = now_tp;
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    fake.set_private_data(private_data);
    protobuf_copy_message(fake.mutable_lock(), make_foreign_lock("teamsvr-room:crashed-node", -10));
  }

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 本节点对过期老锁 CAS: 服务端已提交(锁归本节点), 成功响应挂起在途
  auto& fake = env.channel(team_id);
  env.reset_lock_response_gate.armed = true;
  auto write_task = env.runtime().run_task(
      "write_with_late_cas_success", std::chrono::seconds{8},
      [room, &key_member](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::DTeamAction action;
        auto* update = action.mutable_member_update();
        protobuf_copy_message(*update->mutable_user_key(), key_member);
        update->set_client_version("from-stale-writer");
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      });
  CASE_EXPECT_FALSE(write_task.empty());
  if (write_task.empty()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_TRUE(env.wait_for([&env, &fake]() {
    return fake.reset_lock_calls() >= 1 && room_test_env::gate_parked(env.reset_lock_response_gate);
  }));

  // 挂起窗口内锁易主为竞争者(其 CAS 后到但先生效于订阅视图), 竞争者继续产生业务事件
  fake.set_lock(make_foreign_lock("teamsvr-room:competitor", 3600));
  env.inject_team_action(team_id, [&key_member]() {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), key_member);
    update->set_client_version("from-competitor");
    return action;
  }());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 放行迟到成功响应: 即使本地瞬时被加冕, 随后的写入也必须被服务端 fencing 拒绝并退位,
  // 任何已易主 lock epoch 下本节点不得有副作用逃脱(至多一个副作用生产者)
  CASE_EXPECT_TRUE(room_test_env::release_gate(env.reset_lock_response_gate));
  auto write_result = env.runtime().wait(write_task, std::chrono::seconds{15});
  CASE_EXPECT_TRUE(write_result.task_exited && !write_result.hard_timed_out);
  CASE_EXPECT_NE(0, write_result.result_code);
  CASE_EXPECT_FALSE(room->is_lock_holder());
  CASE_EXPECT_EQ("teamsvr-room:competitor", fake.lock().lock_holder());

  // 零逃脱: 写入请求即使到达服务端也被 fencing 拒绝, journal 中没有任何本节点提交的事件
  CASE_EXPECT_EQ(0u, fake.update_calls());
  bool stale_writer_committed = false;
  fake.foreach_team_action([&stale_writer_committed](const atfw::dtmq::DChannelMessage&,
                                                     const atfw::team::DTeamAction& action) {
    if (action.has_member_update() && "from-stale-writer" == action.member_update().client_version()) {
      stale_writer_committed = true;
    }
    return true;
  });
  CASE_EXPECT_FALSE(stale_writer_committed);
  auto member = room->find_member(key_member, false);
  CASE_EXPECT_TRUE(!!member);
  if (member) {
    CASE_EXPECT_EQ("from-competitor", member->member_data.client_version());
  }

  // 竞争者租约到期后接管仍可用: 迟到响应没有腐蚀锁状态
  {
    global_now_offset_guard guard(std::chrono::seconds{3601});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-07(partial): 同一 runtime 泵内续租 update 与业务 send 交错时边界不回退 ============
CASE_TEST(teamsvr_room_lock, concurrent_renew_and_send_monotonic) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);
  CASE_EXPECT_TRUE(write_member_update_logs(env, room, members.normal, 6));

  // 推进到续租时间点: 业务 send 与维护 update 并发(同一代泵内先后注入)
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    auto task_a = env.runtime().run_task(
        "concurrent_send", std::chrono::seconds{8}, [room, &members](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::DTeamAction action;
          protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
          action.mutable_member_update()->set_client_version("concurrent-v");
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
        });
    CASE_EXPECT_FALSE(task_a.empty());
    env.drive_timer_ticks();  // 驱动维护(update)与业务 send 并发完成
    if (!task_a.empty()) {
      auto wait_result = env.runtime().wait(task_a, std::chrono::seconds{15});
      CASE_EXPECT_TRUE(wait_result.task_exited && !wait_result.hard_timed_out);
      CASE_EXPECT_EQ(0, wait_result.result_code);
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  // 两个请求都成功，房间仍是主控
  CASE_EXPECT_TRUE(room->is_lock_holder());
  // 压缩边界单调不回退(所有 update 请求的 compact_sequence 非递减)
  int64_t last_compact = 0;
  for (const auto& record : fake.update_requests()) {
    CASE_EXPECT_GE(record.request.compact_sequence(), 0);
    if (record.request.compact_sequence() > 0) {
      CASE_EXPECT_GE(record.request.compact_sequence(), last_compact);
      last_compact = record.request.compact_sequence();
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-01: 空锁/过期他人锁可 CAS 接管 ============
CASE_TEST(teamsvr_room_lock, cas_acquire_semantics) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();

  // 空锁: 全新频道订阅后首个写入者直接接管(创建流程隐含覆盖，这里用 setup 验证)
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());

  // 他人持有的锁已过期: 推送锁变更后本节点退位，写入走 CAS 接管过期锁成功
  auto& fake = env.channel(team_id);
  ::atfw::dtmq::DChannelOptimisticLock expired_lock;
  expired_lock.set_lock_holder("teamsvr-room:expired-node");
  *expired_lock.mutable_timeout() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() - std::chrono::seconds{1});
  fake.set_lock(expired_lock);
  // set_lock 追加 kResetLock 日志，推送后本节点观察到锁易主并退位
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_FALSE(room->is_lock_holder());

  CASE_EXPECT_EQ(0, env.run("write_after_expired", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-02: 写入遇到真实锁冲突时立即退位，停止副作用 ============
CASE_TEST(teamsvr_room_lock, write_conflict_steps_down) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());

  // 服务端锁被第三方静默接管(未过期，不下发日志)：旧主控视角仍以为自己是持有者，
  // 写入携带 expect=自己 的锁检查器，服务端 CAS 失败并回带真实锁 -> 立即退位
  auto& fake = env.channel(team_id);
  ::atfw::dtmq::DChannelOptimisticLock other_lock;
  other_lock.set_lock_holder("teamsvr-room:other-node");
  *other_lock.mutable_timeout() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{3600});
  protobuf_copy_message(fake.mutable_lock(), other_lock);

  size_t event_logs_before = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent);
  int32_t write_ret = env.run("write_conflict", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED, write_ret);
  // RPC 发出但被服务端锁拒绝: 权威事件日志不增加
  CASE_EXPECT_EQ(event_logs_before, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 退位后不再产生个人通知副作用
  size_t personal_before = env.personal_message_count();
  env.inject_team_action(team_id, [&members]() {
    atfw::team::DTeamAction action;
    auto* invitation = action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    auto invitee = make_user_key(1, 8301);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(8301));
    return action;
  }());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-03: 响应丢失但真实 holder 已是本节点时按幂等成功处理 ============
CASE_TEST(teamsvr_room_lock, reset_lock_response_loss_idempotent) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 首次抢锁: 提交(锁已归本节点)后响应丢失
  auto& fake = env.channel(team_id);
  fake.next_reset_lock_fault.present = true;
  fake.next_reset_lock_fault.commit_first = true;
  fake.next_reset_lock_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;

  int32_t first_ret = env.run("acquire_lock_lost", [room](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomHeartbeatReq hb;  // heartbeat 会触发 acquire_lock
    auto key = make_user_key(1, 8311);
    protobuf_copy_message(*hb.mutable_user_key(), key);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
  });
  CASE_EXPECT_NE(0, first_ret);

  // 服务端锁已是本节点(holder 恢复逻辑可见)
  auto holder = std::string("teamsvr-room:") + std::string(logic_config::me()->get_local_server_name());
  CASE_EXPECT_EQ(holder, fake.lock().lock_holder());

  // 重试: 真实 holder 是自己 -> 幂等成功
  int32_t retry_ret = env.run("acquire_lock_retry", [room](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomHeartbeatReq hb;
    auto key = make_user_key(1, 8311);
    protobuf_copy_message(*hb.mutable_user_key(), key);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
  });
  // 成员不存在返回 member-not-found，但锁已幂等取得
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND, retry_ret);
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ RCV-11: 固定 seed 混合 action trace, 各压缩/重启点恢复后规范化终态与全量日志 oracle 相同 ============
namespace {
constexpr uint32_t kMixedTraceSeed = 20260915;
constexpr int32_t kMixedTraceOpCount = 24;
// 压缩/重启点: 第 8/16 条之后(op 下标 7/15)
constexpr int32_t kMixedTraceCheckpointA = 8;
constexpr int32_t kMixedTraceCheckpointB = 16;

// 单条 trace 操作: 结构固定(操作类型/操作者/目标), 数据值由固定 seed 的 rng 生成。
// 权限与存在性均由结构保证: 邀请角色固定 NORMAL、队长独享操作只由 owner 发起、
// 审批/拒绝目标都在前序操作中显式加入、被移除成员不再参与后续操作。
bool apply_mixed_trace_op(room_test_env& env, const team_room::ptr_t& room, int32_t op_index,
                          const standard_team_members& members,
                          const std::vector<PROJECT_NAMESPACE_ID::DUserIDKey>& extras, std::mt19937& rng) {
  // 每条操作固定消费一次 rng, 保证两次运行的数据值序列一致
  const std::string value = "trace-" + std::to_string(op_index) + "-" + std::to_string(rng());
  const auto& extra = [&extras](size_t idx) -> const PROJECT_NAMESPACE_ID::DUserIDKey& { return extras[idx]; };

  switch (op_index) {
    case 0: {  // team_update: 配置替换(抬高成员上限) + 共享数据 101
      atfw::team::DTeamAction action;
      auto* update = action.mutable_team_update();
      update->mutable_configure()->set_max_member_count(8);
      update->mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
      add_team_any_data_entry(update->mutable_shared_team_data(), 101, value);
      CASE_EXPECT_EQ(0, env.run("op0_team_update", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 1: {  // member_update(normal): client_version
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      action.mutable_member_update()->set_client_version(value);
      CASE_EXPECT_EQ(0, env.run("op1_member_update", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 2: {  // 邀请 u1(normal 发起, 携带 admission 数据)
      atfw::team::SSTeamRoomAddInvitationReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.normal);
      auto* invitation = req.mutable_invitation();
      protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
      protobuf_copy_message(*invitation->mutable_invitee(), extra(0));
      protobuf_copy_message(*invitation->mutable_invitee_private_channel(),
                            make_personal_channel(extra(0).user_id()));
      add_team_any_data_entry(invitation->mutable_team_admission_data(), 201, value);
      CASE_EXPECT_EQ(0, env.run("op2_invite_u1", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
      }));
      break;
    }
    case 3: {  // 申请 u2(携带 admission 数据)
      atfw::team::SSTeamRoomAddJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), extra(1));
      auto* join_request = req.mutable_join_request();
      protobuf_copy_message(*join_request->mutable_requester(), extra(1));
      protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                            make_personal_channel(extra(1).user_id()));
      add_team_any_data_entry(join_request->mutable_member_admission_data(), 202, value);
      CASE_EXPECT_EQ(0, env.run("op3_join_u2", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      }));
      break;
    }
    case 4: {  // owner 批准 u2 入队
      atfw::team::SSTeamRoomApproveJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.owner);
      protobuf_copy_message(*req.mutable_applicant(), extra(1));
      CASE_EXPECT_EQ(0, env.run("op4_approve_u2", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, req)));
      }));
      break;
    }
    case 5: {  // u2 提为 ADMIN
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_set_role()->mutable_user_key(), extra(1));
      action.mutable_member_set_role()->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
      CASE_EXPECT_EQ(0, env.run("op5_role_u2", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 6: {  // team_update: 共享数据 102
      atfw::team::DTeamAction action;
      add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 102, value);
      CASE_EXPECT_EQ(0, env.run("op6_team_update", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 7: {  // member_update(u2): 成员共享数据 7
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), extra(1));
      add_team_any_data_entry(update->mutable_shared_member_data(), 7, value);
      CASE_EXPECT_EQ(0, env.run("op7_member_data_u2", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 8: {  // 邀请 u3(admin 发起)
      atfw::team::SSTeamRoomAddInvitationReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.admin);
      auto* invitation = req.mutable_invitation();
      protobuf_copy_message(*invitation->mutable_inviter(), members.admin);
      protobuf_copy_message(*invitation->mutable_invitee(), extra(2));
      protobuf_copy_message(*invitation->mutable_invitee_private_channel(),
                            make_personal_channel(extra(2).user_id()));
      CASE_EXPECT_EQ(0, env.run("op8_invite_u3", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
      }));
      break;
    }
    case 9: {  // owner 撤回 u1 邀请
      atfw::team::SSTeamRoomRejectInvitationReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.owner);
      protobuf_copy_message(*req.mutable_invitee(), extra(0));
      CASE_EXPECT_EQ(0, env.run("op9_reject_invite_u1", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, req)));
      }));
      break;
    }
    case 10: {  // 申请 u4
      atfw::team::SSTeamRoomAddJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), extra(3));
      auto* join_request = req.mutable_join_request();
      protobuf_copy_message(*join_request->mutable_requester(), extra(3));
      protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                            make_personal_channel(extra(3).user_id()));
      CASE_EXPECT_EQ(0, env.run("op10_join_u4", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      }));
      break;
    }
    case 11: {  // owner 拒绝 u4
      atfw::team::SSTeamRoomRejectJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.owner);
      protobuf_copy_message(*req.mutable_applicant(), extra(3));
      CASE_EXPECT_EQ(0, env.run("op11_reject_u4", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_join_request(ctx, req)));
      }));
      break;
    }
    case 12: {  // 申请 u5
      atfw::team::SSTeamRoomAddJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), extra(4));
      auto* join_request = req.mutable_join_request();
      protobuf_copy_message(*join_request->mutable_requester(), extra(4));
      protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                            make_personal_channel(extra(4).user_id()));
      CASE_EXPECT_EQ(0, env.run("op12_join_u5", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      }));
      break;
    }
    case 13: {  // owner 批准 u5 入队
      atfw::team::SSTeamRoomApproveJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.owner);
      protobuf_copy_message(*req.mutable_applicant(), extra(4));
      CASE_EXPECT_EQ(0, env.run("op13_approve_u5", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, req)));
      }));
      break;
    }
    case 14: {  // owner 移除 u5
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), extra(4));
      action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
      CASE_EXPECT_EQ(0, env.run("op14_remove_u5", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 15: {  // member_update(admin): client_version
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.admin);
      action.mutable_member_update()->set_client_version(value);
      CASE_EXPECT_EQ(0, env.run("op15_member_update_admin", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 16: {  // team_update: 共享数据 103 + 删除 101(空 value 表示删除)
      atfw::team::DTeamAction action;
      auto* shared = action.mutable_team_update()->mutable_shared_team_data();
      add_team_any_data_entry(shared, 103, value);
      auto* deletion = shared->Add();
      deletion->set_key(101);
      CASE_EXPECT_EQ(0, env.run("op16_team_update_delete", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 17: {  // normal 提为 ADMIN
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_set_role()->mutable_user_key(), members.normal);
      action.mutable_member_set_role()->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
      CASE_EXPECT_EQ(0, env.run("op17_role_normal", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 18: {  // 邀请 u6(admin 发起, 携带 admission 数据)
      atfw::team::SSTeamRoomAddInvitationReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.admin);
      auto* invitation = req.mutable_invitation();
      protobuf_copy_message(*invitation->mutable_inviter(), members.admin);
      protobuf_copy_message(*invitation->mutable_invitee(), extra(5));
      protobuf_copy_message(*invitation->mutable_invitee_private_channel(),
                            make_personal_channel(extra(5).user_id()));
      add_team_any_data_entry(invitation->mutable_team_admission_data(), 203, value);
      CASE_EXPECT_EQ(0, env.run("op18_invite_u6", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
      }));
      break;
    }
    case 19: {  // u1 改为走申请
      atfw::team::SSTeamRoomAddJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), extra(0));
      auto* join_request = req.mutable_join_request();
      protobuf_copy_message(*join_request->mutable_requester(), extra(0));
      protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                            make_personal_channel(extra(0).user_id()));
      CASE_EXPECT_EQ(0, env.run("op19_join_u1", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      }));
      break;
    }
    case 20: {  // owner 批准 u1 入队
      atfw::team::SSTeamRoomApproveJoinRequestReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.owner);
      protobuf_copy_message(*req.mutable_applicant(), extra(0));
      CASE_EXPECT_EQ(0, env.run("op20_approve_u1", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, req)));
      }));
      break;
    }
    case 21: {  // member_update(u1): 成员共享数据 9
      atfw::team::DTeamAction action;
      auto* update = action.mutable_member_update();
      protobuf_copy_message(*update->mutable_user_key(), extra(0));
      add_team_any_data_entry(update->mutable_shared_member_data(), 9, value);
      CASE_EXPECT_EQ(0, env.run("op21_member_data_u1", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    case 22: {  // team_update: 共享数据 104
      atfw::team::DTeamAction action;
      add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 104, value);
      CASE_EXPECT_EQ(0, env.run("op22_team_update", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
    default: {  // case 23: member_update(u2): client_version 覆盖
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), extra(1));
      action.mutable_member_update()->set_client_version(value);
      CASE_EXPECT_EQ(0, env.run("op23_member_update_u2", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      break;
    }
  }
  return true;
}

bool apply_mixed_trace_block(room_test_env& env, const std::function<team_room::ptr_t()>& current_room,
                             int32_t begin, int32_t end, const standard_team_members& members,
                             const std::vector<PROJECT_NAMESPACE_ID::DUserIDKey>& extras, std::mt19937& rng) {
  for (int32_t op_index = begin; op_index < end; ++op_index) {
    if (!apply_mixed_trace_op(env, current_room(), op_index, members, extras, rng)) {
      CASE_EXPECT_TRUE(false);
      return false;
    }
    int32_t sync_ret = env.sync(current_room()->get_team_key());
    CASE_EXPECT_EQ(0, sync_ret);
    if (0 != sync_ret) {
      return false;
    }
  }
  return true;
}

bool user_key_order_less(const PROJECT_NAMESPACE_ID::DUserIDKey& left, const PROJECT_NAMESPACE_ID::DUserIDKey& right) {
  if (left.zone_id() != right.zone_id()) {
    return left.zone_id() < right.zone_id();
  }
  return left.user_id() < right.user_id();
}

// 驱动时间轮直到出现本轮新增的 compact update(恢复后的房间先点火接管定时器,
// 维护到期被 reset_room_timer 收敛到 last_tick+1; 续租间隔 5s, 每轮推进 4s 保证下一轮维护点火;
// 总推进不超过 18s, 低于成员离线过期 30s 不会误踢)
bool drive_until_fresh_compact(room_test_env& env, fake_team_room_channel& fake, size_t updates_before) {
  global_now_offset_guard guard(std::chrono::seconds{6});
  for (int32_t round = 0; round < 4; ++round) {
    env.drive_timer_ticks();
    for (size_t idx = updates_before; idx < fake.update_requests().size(); ++idx) {
      if (fake.update_requests()[idx].request.compact_sequence() > 0) {
        return true;
      }
    }
    guard.advance(std::chrono::seconds{4});
  }
  return false;
}

// 驱动一次真实维护产生 compact update, 取其快照内容规范化并序列化:
// 剔除随运行环境/压缩进度变化的游标、压缩点与时间戳, repeated 字段按稳定 key 排序,
// 使"全量日志直连"与"压缩+快照恢复"两种路径的业务终态可直接比较
std::string capture_compact_snapshot_state(room_test_env& env, fake_team_room_channel& fake, int64_t team_id) {
  const size_t updates_before = fake.update_requests().size();
  CASE_EXPECT_TRUE(drive_until_fresh_compact(env, fake, updates_before));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 取本轮维护的 compact update(快照为 dump_public_data/dump_private_data 的全量状态)
  const atfw::dtmq::SSChannelUpdateReq* compact_update = nullptr;
  for (size_t idx = updates_before; idx < fake.update_requests().size(); ++idx) {
    if (fake.update_requests()[idx].request.compact_sequence() > 0) {
      compact_update = &fake.update_requests()[idx].request;
    }
  }
  CASE_EXPECT_TRUE(nullptr != compact_update);
  if (nullptr == compact_update) {
    return std::string();
  }

  atfw::team::DTeamStorage storage;
  atfw::team::DTeamRoomPrivateData private_data;
  CASE_EXPECT_TRUE(compact_update->custom_data().UnpackTo(&storage));
  CASE_EXPECT_TRUE(compact_update->private_data().UnpackTo(&private_data));

  // 消费游标/保存边界/压缩点与 team_key(两遍运行取不同 team id)不属于业务终态
  storage.clear_team_key();
  storage.clear_acknowledge_action_sequence();
  storage.clear_acknowledge_action_hash_code();
  storage.clear_saved_action_sequence();
  private_data.clear_last_compact_sequence();
  private_data.clear_last_compact_timepoint();

  const auto sort_any_by_key = [](google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* list) {
    std::sort(list->begin(), list->end(),
              [](const atfw::team::DTeamAnyDataWithKey& left, const atfw::team::DTeamAnyDataWithKey& right) {
                return left.key() < right.key();
              });
  };

  auto* member_list = storage.mutable_member();
  std::sort(member_list->begin(), member_list->end(),
            [](const atfw::team::DTeamMember& left, const atfw::team::DTeamMember& right) {
              return user_key_order_less(left.user_key(), right.user_key());
            });
  for (auto& member : *member_list) {
    member.clear_joined_timepoint();
    member.clear_last_heartbeat_timepoint();
    member.clear_acknowledge_action_sequence();
    member.clear_acknowledge_action_hash_code();
    member.clear_user_router_server_id();
    sort_any_by_key(member.mutable_shared_member_data());
  }

  auto* invitation_list = storage.mutable_pending_invitation();
  std::sort(invitation_list->begin(), invitation_list->end(),
            [](const atfw::team::DTeamInvitation& left, const atfw::team::DTeamInvitation& right) {
              return user_key_order_less(left.invitee(), right.invitee());
            });
  for (auto& invitation : *invitation_list) {
    invitation.clear_team_key();
    invitation.clear_start_timepoint();
    invitation.clear_expired_timepoint();
    sort_any_by_key(invitation.mutable_team_admission_data());
  }

  auto* join_request_list = storage.mutable_pending_join_request();
  std::sort(join_request_list->begin(), join_request_list->end(),
            [](const atfw::team::DTeamJoinRequest& left, const atfw::team::DTeamJoinRequest& right) {
              return user_key_order_less(left.requester(), right.requester());
            });
  for (auto& join_request : *join_request_list) {
    join_request.clear_team_key();
    join_request.clear_expired_timepoint();
    join_request.clear_user_router_server_id();
    sort_any_by_key(join_request.mutable_member_admission_data());
  }

  sort_any_by_key(storage.mutable_shared_team_data());
  sort_any_by_key(private_data.mutable_private_team_data());

  // DebugString 比较: 等价性与序列化一致, 失败时输出可读差异
  return storage.DebugString() + "|" + private_data.DebugString();
}

std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> make_mixed_trace_extras() {
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> extras;
  for (uint64_t index = 0; index < 6; ++index) {
    extras.push_back(make_user_key(kTestZoneId, 9101 + index));
  }
  return extras;
}
}  // namespace

CASE_TEST(teamsvr_room_recovery, seeded_mixed_trace_restore_oracle) {
  // 准入有效期拉长: 压缩需推进时钟, 不能顺带让准入过期; 两次运行配置一致
  room_test_cfg_values cfg;
  cfg.invitation_expire_seconds = 3600;
  cfg.join_request_expire_seconds = 3600;

  // team id 依赖 discovery 就绪且不同 env 的哈希环可能不同, 每遍各自在 env.start 之后取号;
  // 规范化时剔除 team_key, 两遍业务状态可直接比较

  // 第一遍: 全量日志直连(不驱动维护、不压缩), 产出规范化终态 oracle
  std::string oracle_state;
  {
    room_test_env env(cfg);
    if (!env.start()) {
      return;
    }
    const int64_t team_id = next_test_team_id();
    CASE_EXPECT_NE(0, team_id);
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto extras = make_mixed_trace_extras();
    std::mt19937 rng(kMixedTraceSeed);
    const auto current_room = [&room]() { return room; };
    CASE_EXPECT_TRUE(apply_mixed_trace_block(env, current_room, 0, kMixedTraceOpCount, members, extras, rng));
    oracle_state = capture_compact_snapshot_state(env, env.channel(team_id), team_id);
    CASE_EXPECT_FALSE(oracle_state.empty());
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
  }

  // 第二遍: 同一 team 同一 trace; 两个检查点经真实维护压缩 + 丢弃房间从快照和剩余日志恢复,
  // 恢复后继续后续操作, 终态必须与全量 oracle 相同
  std::string restored_state;
  {
    room_test_env env(cfg);
    if (!env.start()) {
      return;
    }
    const int64_t team_id = next_test_team_id();
    CASE_EXPECT_NE(0, team_id);
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    auto extras = make_mixed_trace_extras();
    std::mt19937 rng(kMixedTraceSeed);
    const auto current_room = [&room]() { return room; };

    const auto compact_and_restore = [&]() -> bool {
      // 只认本轮新增的 compact update, 避免把上一检查点的陈旧快照当作本次压缩证据
      const size_t updates_before = fake.update_requests().size();
      CASE_EXPECT_TRUE(drive_until_fresh_compact(env, fake, updates_before));
      CASE_EXPECT_EQ(0, env.sync(team_id));
      CASE_EXPECT_GT(fake.last_removed_sequence(), 0);
      room_test_env::clear_rooms();
      room.reset();
      room = env.setup_ready_room(team_id);
      CASE_EXPECT_TRUE(!!room);
      if (!room) {
        return false;
      }
      CASE_EXPECT_EQ(0, env.sync(team_id, true));
      return true;
    };

    CASE_EXPECT_TRUE(apply_mixed_trace_block(env, current_room, 0, kMixedTraceCheckpointA, members, extras, rng));
    CASE_EXPECT_TRUE(compact_and_restore());
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    CASE_EXPECT_TRUE(apply_mixed_trace_block(env, current_room, kMixedTraceCheckpointA, kMixedTraceCheckpointB,
                                             members, extras, rng));
    CASE_EXPECT_TRUE(compact_and_restore());
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    CASE_EXPECT_TRUE(apply_mixed_trace_block(env, current_room, kMixedTraceCheckpointB, kMixedTraceOpCount, members,
                                             extras, rng));
    restored_state = capture_compact_snapshot_state(env, fake, team_id);
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
  }

  CASE_EXPECT_EQ(oracle_state, restored_state);
}

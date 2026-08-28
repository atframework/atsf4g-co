// Copyright 2026 atframework
//
// teamsvr-room 日志压缩、快照恢复与锁转移用例(TEAM_ROOM_TEST_PLAN.md §4.5 CMP/RCV/LCK)。
// 通过 fake journal 的真实 sequence/hash chain 与 global_now_offset_guard 驱动定时维护。

#include "teamsvr_room_test_common.h"

namespace {
using namespace teamsvr_room_test;

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

  env.clear_rooms();
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
    env.clear_rooms();
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

  // 压缩后 journal 裁剪: 保留条数不超过 keep_by_count(5) + 后续新增
  size_t event_logs_after = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent);
  size_t total_logs_after = fake.journal().size();
  CASE_EXPECT_LE(total_logs_after, 5 + 3);  // 保留窗口 + 维护自身产生的 kResetLock/kUpdateCustomData/kNoop
  CASE_EXPECT_LE(event_logs_after, 5u);
  (void)event_logs_after;

  env.clear_rooms();
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
    env.clear_rooms();
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

  env.clear_rooms();
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
    guard.advance(std::chrono::seconds{3});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_EQ(0u, count_versions("w0-"));
    CASE_EXPECT_EQ(3u, count_versions("w1-"));

    // T0+8: batch w2-(2 条)
    guard.advance(std::chrono::seconds{2});
    for (int i = 0; i < 2; ++i) {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      action.mutable_member_update()->set_client_version("w2-v" + std::to_string(i));
      CASE_EXPECT_EQ(0, env.run("write_w2", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // T0+12: 续租维护。keep_deadline = T0+12-5 = T0+7: w1-(T0+3) 被裁剪，w2-(T0+8) 保留
    guard.advance(std::chrono::seconds{4});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_EQ(0u, count_versions("w0-"));
    CASE_EXPECT_EQ(0u, count_versions("w1-"));
    CASE_EXPECT_EQ(2u, count_versions("w2-"));
  }

  env.clear_rooms();
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

  env.clear_rooms();
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
    guard.advance(std::chrono::seconds{1});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  // 压缩边界停在 setup 最后一日志(时间维度下限)，不进入新批次
  CASE_EXPECT_EQ(setup_last, fake.last_removed_sequence());
  // 新批次(6 条 member_update)全部保留
  CASE_EXPECT_EQ(6u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));
  CASE_EXPECT_TRUE(nullptr != find_compact_update(fake));

  env.clear_rooms();
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
  env.clear_rooms();
  team_room::ptr_t recovered = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!recovered);
  if (recovered) {
    CASE_EXPECT_EQ(boundary2, recovered->debug_last_compact_sequence());
    CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) != nullptr);
    CASE_EXPECT_TRUE(recovered->find_member(members.normal, false) != nullptr);
    CASE_EXPECT_GT(recovered->debug_saved_action_sequence(), 0);
  }

  env.clear_rooms();
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
    env.clear_rooms();
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

  env.clear_rooms();
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

  // 队伍创建后无任何 event 日志，但锁续租(kResetLock)会产生日志。
  // 先推进一次触发续租，产生纯 kResetLog 频道内容
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  size_t reset_logs = fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kResetLock);
  CASE_EXPECT_GT(reset_logs, 0u);
  CASE_EXPECT_EQ(0u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));

  // 推进 compact_log_start_time(10s): 最老未压缩日志(含 kResetLock)超过窗口 -> 时间维度触发维护并压缩
  {
    global_now_offset_guard guard(std::chrono::seconds{11});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }

  // 时间维度压缩完成(keep_time=5s，所有日志都早于窗口 -> 全部裁剪，边界不早于最早日志)
  CASE_EXPECT_TRUE(nullptr != find_compact_update(fake));
  CASE_EXPECT_GT(fake.last_removed_sequence(), 0);

  // 房间仍主控且持续可写
  CASE_EXPECT_TRUE(room->is_lock_holder());

  env.clear_rooms();
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

  env.clear_rooms();
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
  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  // 第一次 approve 的首个写入是 add_member；编排其提交成功但回包失败，因此函数在写 approve 前退出。
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

  // add_member 已提交: 事件回环后成员存在，但 approve 尚未写入、邀请尚未清理。
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(invitee, false) != nullptr);

  // 重试 approve: 不再写第二个 add_member，只写 approve 事件
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("approve_retry", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  }));
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

  // 最终只有一个成员，邀请已清理(重试 approve 返回 not-found)
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                 env.run("approve_after_cleanup", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                 }));

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

  env.clear_rooms();
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
  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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
    const int64_t saved_sequence = add_log ? add_log->sequence() : 0;
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

  env.clear_rooms();
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

  env.clear_rooms();
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
    env.clear_rooms();
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
      guard.advance(std::chrono::seconds{9});
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
      guard.advance(std::chrono::seconds{3});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
    CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());
    CASE_EXPECT_TRUE(room->find_member(members.owner, false) != nullptr);
  }

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
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

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

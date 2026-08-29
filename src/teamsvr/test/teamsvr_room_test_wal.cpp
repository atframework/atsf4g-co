// Copyright 2026 atframework
//
// WAL-layer contract cases for the teamsvr-room service (see src/teamsvr/TEAM_ROOM_TEST_PLAN.md §4.6).
//
// The fixture (teamsvr_room_test_common.h, wal_journal_mode) compiles the real mq_channel sources and
// drives the real wal_publisher/wal_object: journal commits use allocate_log/emplace_back_log with the
// real sequence allocator and hash chain, and delivery flows through the real vtable
// (publisher_send_snapshot/publisher_send_logs) -> rpc::dtmq::channel_event_sync -> the real
// client_subscriber::global_receive_channel_event, exactly like the production proxy -> room seam.
// Distribution/forwarding stays covered by component-dtmq-proxysvr-unit-test.

#include "teamsvr_room_test_common.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>
#include <cstdint>
#include <vector>

namespace {
using teamsvr_room_test::room_test_env;
}  // namespace

// ============ WAL-01(前置 compile/start smoke): 真实 mq_channel/wal_publisher 层可启动并交付首份快照 ============
// 阶段 A 第 3 步的前置验证: WAL 模式夹具可在 room 测试进程内启动，订阅心跳 auto-create 出
// 真实可写频道(kCreate 日志)。按生产契约，未订阅过的 client 首次以 checkpoint 0 上来触发
// 快照下发(新 memory_only 频道 last_removed_sequence 初始化为 1，见
// mq_channel::get_last_message_sequence 注释)，快照携带 kCreate 日志。
// 注意: 真实频道首条日志的 sequence 由 mq_channel::alloc_message_sequence 以微秒时间戳播种，
// 断言一律使用相对比较，不假设从 1 开始
CASE_TEST(teamsvr_room_wal, channel_smoke_and_ready) {
  room_test_env env;
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);

  // 订阅心跳触发 auto-create(kCreate) -> 回包 -> ready
  team_room::ptr_t room = env.setup_ready_room(team_key);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_MSG_INFO() << "wal smoke: batches=" << env.wal_event_batches().size() << '\n';
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_TRUE(room->is_subscriber_ready());

  CASE_EXPECT_EQ(0, env.wal_converge());

  auto channel = env.wal_find_channel(team_key);
  CASE_EXPECT_TRUE(!!channel);
  if (channel) {
    CASE_EXPECT_TRUE(channel->is_writable());
    CASE_EXPECT_TRUE(channel->is_available());
    CASE_EXPECT_GT(channel->get_last_message_sequence(), 0);
    CASE_EXPECT_NE(0u, channel->get_last_hash_code());

    const auto& logs = channel->get_shared_wal_object()->get_all_logs();
    CASE_EXPECT_EQ(1u, logs.size());
    if (!logs.empty()) {
      // 首条(唯一)日志是 kCreate，sequence 为正且与频道最新序号一致，哈希链覆盖最新日志
      CASE_EXPECT_EQ(atfw::dtmq::DChannelMessageDetail::kCreate, logs.back()->detail().command_case());
      CASE_EXPECT_GT(logs.back()->sequence(), 0);
      CASE_EXPECT_EQ(channel->get_last_message_sequence(), logs.back()->sequence());
      CASE_EXPECT_EQ(channel->get_last_hash_code(), logs.back()->hash_code());
    }
  }

  // 首次订阅(checkpoint=0)按生产契约走快照下发: 新建 memory_only 频道的
  // last_removed_sequence 初始化为 1(mq_channel::get_last_message_sequence 注释: 未订阅过的
  // client 第一条消息带 0 上来应触发快照)，因此首投递是含全部日志的 DChannelSnapshot。
  // subscribe 后 handler 内的 tick/broadcast(初始 bound 为空)可能再补一拍增量批
  bool saw_snapshot_batch = false;
  bool saw_create_log = false;
  for (const auto& record : env.wal_event_batches()) {
    if (record.batch.has_channel_snapshot()) {
      saw_snapshot_batch = true;
      for (const auto& message : record.batch.channel_snapshot().messages()) {
        if (message.detail().command_case() == atfw::dtmq::DChannelMessageDetail::kCreate) {
          saw_create_log = true;
        }
      }
    }
    for (const auto& message : record.batch.channel_message()) {
      if (message.detail().command_case() == atfw::dtmq::DChannelMessageDetail::kCreate) {
        saw_create_log = true;
      }
    }
  }
  CASE_EXPECT_TRUE(saw_snapshot_batch);
  CASE_EXPECT_TRUE(saw_create_log);

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-01: publisher 混合 DTeamAction 提交，真实 sequence/hash 链经 SSChannelEventSync 后 Room 终态与 journal 一致 ============
CASE_TEST(teamsvr_room_wal, mixed_actions_replay_matches_journal) {
  room_test_env env;
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);

  auto owner_key = teamsvr_room_test::make_user_key(1, 8301);
  auto admin_key = teamsvr_room_test::make_user_key(1, 8302);
  auto normal_key = teamsvr_room_test::make_user_key(1, 8303);
  auto outsider_key = teamsvr_room_test::make_user_key(1, 8304);
  auto owner_channel = teamsvr_room_test::make_personal_channel(8301);
  auto admin_channel = teamsvr_room_test::make_personal_channel(8302);
  auto normal_channel = teamsvr_room_test::make_personal_channel(8303);
  auto outsider_channel = teamsvr_room_test::make_personal_channel(8304);

  // 混合 action trace: 成员、keyed shared data、成员数据更新与移除，全部经真实 publisher 提交
  std::vector<atfw::team::DTeamAction> trace;
  {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), owner_key);
    protobuf_copy_message(*add_member->mutable_user_channel(), owner_channel);
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    trace.push_back(action);
  }
  {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), admin_key);
    protobuf_copy_message(*add_member->mutable_user_channel(), admin_channel);
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    trace.push_back(action);
  }
  {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), normal_key);
    protobuf_copy_message(*add_member->mutable_user_channel(), normal_channel);
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    trace.push_back(action);
  }
  {
    atfw::team::DTeamAction action;
    teamsvr_room_test::add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 100,
                                               "wal-team-1");
    trace.push_back(action);
  }
  {
    atfw::team::DTeamAction action;
    auto* member_update = action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), admin_key);
    member_update->set_client_version("wal-v1");
    teamsvr_room_test::add_team_any_data_entry(member_update->mutable_shared_member_data(), 7, "wal-member-1");
    trace.push_back(action);
  }
  {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), normal_key);
    trace.push_back(action);
  }

  room_test_env::wal_channel_ptr_t channel;
  CASE_EXPECT_EQ(0, env.run("prepare_journal", [&](rpc::context& ctx) -> rpc::result_code_type {
                   auto ensure_ret = RPC_AWAIT_CODE_RESULT(env.wal_ensure_channel(ctx, team_key));
                   if (0 != ensure_ret) {
                     RPC_RETURN_CODE(ensure_ret);
                   }
                   channel = env.wal_find_channel(team_key);
                   if (!channel) {
                     RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
                   }

                   // 预置本节点持有的乐观锁(kResetLock 日志)，镜像 create_team 之后的锁状态
                   atfw::dtmq::DChannelOptimisticLock lock;
                   lock.set_lock_holder(teamsvr_room_test::self_lock_holder());
                   *lock.mutable_timeout() = protobuf_from_system_clock(
                       atfw::util::time::time_utility::now() + std::chrono::seconds{env.cfg().lock_lease_seconds});
                   channel->set_lock(ctx, lock);

                   for (const auto& action : trace) {
                     auto commit_ret = RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action));
                     if (0 != commit_ret) {
                       RPC_RETURN_CODE(commit_ret);
                     }
                   }
                   RPC_RETURN_CODE(0);
                 }));
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // journal 侧不变量: kCreate + kResetLock + N 条 event，sequence 严格递增，哈希链覆盖最新日志
  {
    const auto& logs = channel->get_shared_wal_object()->get_all_logs();
    CASE_EXPECT_EQ(trace.size() + 2, logs.size());
    int64_t prev_sequence = 0;
    for (const auto& log : logs) {
      CASE_EXPECT_GT(log->sequence(), prev_sequence);
      prev_sequence = log->sequence();
    }
    CASE_EXPECT_EQ(prev_sequence, channel->get_last_message_sequence());
    if (!logs.empty()) {
      CASE_EXPECT_EQ(channel->get_last_hash_code(), logs.back()->hash_code());
    }
  }

  // room 以 checkpoint=0 首次订阅: 真实 publisher 按生产契约以快照回投全部日志
  // (未订阅过的 client 首次带 0 上来触发快照，见 mq_channel::get_last_message_sequence 注释)
  team_room::ptr_t room = env.setup_ready_room(team_key);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.wal_converge());

  // 终态与 journal 重放结果一致: 成员/角色/成员数据
  auto owner_member = room->find_member(owner_key, false);
  CASE_EXPECT_TRUE(!!owner_member);
  if (owner_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, owner_member->member_data.role());
  }
  auto admin_member = room->find_member(admin_key, false);
  CASE_EXPECT_TRUE(!!admin_member);
  if (admin_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, admin_member->member_data.role());
    CASE_EXPECT_EQ("wal-v1", admin_member->member_data.client_version());
  }
  CASE_EXPECT_TRUE(nullptr == room->find_member(normal_key, false));

  // 下发的日志集合与 journal 一致(快照/增量两种形式都计入;允许重复投递，不允许缺失或越界)
  int64_t last_journal_sequence = channel->get_last_message_sequence();
  {
    std::vector<int64_t> delivered;
    for (const auto& record : env.wal_event_batches()) {
      for (const auto& message : record.batch.channel_message()) {
        delivered.push_back(message.sequence());
      }
      for (const auto& message : record.batch.channel_snapshot().messages()) {
        delivered.push_back(message.sequence());
      }
    }
    CASE_EXPECT_FALSE(delivered.empty());
    for (int64_t sequence : delivered) {
      CASE_EXPECT_GE(sequence, 1);
      CASE_EXPECT_LE(sequence, last_journal_sequence);
    }
    // 每条 journal 日志都至少投递一次
    for (const auto& log : channel->get_shared_wal_object()->get_all_logs()) {
      bool found = false;
      for (int64_t sequence : delivered) {
        if (sequence == log->sequence()) {
          found = true;
          break;
        }
      }
      if (!found) {
        CASE_EXPECT_TRUE(found);
        break;
      }
    }
  }

  // keyed shared data 经真实事件路径可被 condition 观察(与恢复用例同 oracle，不依赖内部 map)
  auto check_condition = [&env, room, &owner_key](const atfw::team::DTeamAction& action) {
    return env.run("wal_check_condition", [room, &owner_key, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, owner_key, action)));
    });
  };
  {
    // 队伍级: shared_team_data[100] == "wal-team-1"
    atfw::team::DTeamAction match;
    teamsvr_room_test::add_team_any_data_entry(match.mutable_team_update()->mutable_shared_team_data(), 101,
                                               "post-wal");
    teamsvr_room_test::add_team_any_value_entry(match.mutable_team_update()->add_condition()->mutable_shared_team_data(),
                                                100, "wal-team-1");
    CASE_EXPECT_EQ(0, check_condition(match));

    atfw::team::DTeamAction mismatch;
    teamsvr_room_test::add_team_any_data_entry(mismatch.mutable_team_update()->mutable_shared_team_data(), 101,
                                               "post-wal");
    teamsvr_room_test::add_team_any_value_entry(
        mismatch.mutable_team_update()->add_condition()->mutable_shared_team_data(), 100, "wal-other");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH, check_condition(mismatch));
  }
  {
    // 成员级: admin 的 shared_member_data[7] == "wal-member-1"
    atfw::team::DTeamAction match;
    auto* update = match.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), admin_key);
    teamsvr_room_test::add_team_any_data_entry(update->mutable_shared_member_data(), 8, "post-wal-member");
    auto* group = update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), admin_key);
    teamsvr_room_test::add_team_any_value_entry(group->mutable_member_condition()->mutable_shared_member_data(), 7,
                                                "wal-member-1");
    CASE_EXPECT_EQ(0, check_condition(match));

    atfw::team::DTeamAction mismatch;
    auto* mismatch_update = mismatch.mutable_member_update();
    protobuf_copy_message(*mismatch_update->mutable_user_key(), admin_key);
    auto* mismatch_group = mismatch_update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*mismatch_group->mutable_user_key(), admin_key);
    teamsvr_room_test::add_team_any_value_entry(mismatch_group->mutable_member_condition()->mutable_shared_member_data(),
                                                7, "wal-member-other");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH, check_condition(mismatch));
  }

  // 追加一条新日志后，以追加前的 (sequence, hash) 为 checkpoint 重订阅: 只补新日志，不重发快照
  env.wal_clear_event_batches();
  int64_t checkpoint_sequence = channel->get_last_message_sequence();
  uint64_t checkpoint_hash = channel->get_last_hash_code();
  {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), outsider_key);
    protobuf_copy_message(*add_member->mutable_user_channel(), outsider_channel);
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    CASE_EXPECT_EQ(0, env.run("commit_more", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action)));
                   }));
  }
  int64_t new_sequence = channel->get_last_message_sequence();
  CASE_EXPECT_GT(new_sequence, checkpoint_sequence);
  CASE_EXPECT_EQ(0, env.run("resubscribe", [&](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(
                       RPC_AWAIT_CODE_RESULT(env.wal_resubscribe(ctx, channel, checkpoint_sequence, checkpoint_hash)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());

  bool saw_new_log = false;
  for (const auto& record : env.wal_event_batches()) {
    // checkpoint 命中且 hash 匹配时绝不允许退回快照
    CASE_EXPECT_FALSE(record.batch.has_channel_snapshot());
    for (const auto& message : record.batch.channel_message()) {
      CASE_EXPECT_EQ(new_sequence, message.sequence());
      if (message.sequence() == new_sequence) {
        saw_new_log = true;
      }
    }
  }
  CASE_EXPECT_TRUE(saw_new_log);
  CASE_EXPECT_TRUE(nullptr != room->find_member(outsider_key, false));

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-02: subscriber checkpoint 正常只补后续日志，hash 不匹配强制 snapshot ============
CASE_TEST(teamsvr_room_wal, checkpoint_incremental_and_hash_mismatch_snapshot) {
  room_test_env env;
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);

  auto u1 = teamsvr_room_test::make_user_key(1, 8401);
  auto u2 = teamsvr_room_test::make_user_key(1, 8402);
  auto u3 = teamsvr_room_test::make_user_key(1, 8403);

  room_test_env::wal_channel_ptr_t channel;
  CASE_EXPECT_EQ(0, env.run("prepare_journal", [&](rpc::context& ctx) -> rpc::result_code_type {
                   auto ensure_ret = RPC_AWAIT_CODE_RESULT(env.wal_ensure_channel(ctx, team_key));
                   if (0 != ensure_ret) {
                     RPC_RETURN_CODE(ensure_ret);
                   }
                   channel = env.wal_find_channel(team_key);
                   if (!channel) {
                     RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
                   }
                   for (auto user_key : {u1, u2}) {
                     atfw::team::DTeamAction action;
                     auto* add_member = action.mutable_add_member();
                     protobuf_copy_message(*add_member->mutable_user_key(), user_key);
                     add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                     auto commit_ret = RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action));
                     if (0 != commit_ret) {
                       RPC_RETURN_CODE(commit_ret);
                     }
                   }
                   RPC_RETURN_CODE(0);
                 }));
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  // 此时 journal: kCreate + add u1 + add u2 共 3 条日志(sequence 为时间播种，只做相对断言)
  {
    const auto& logs = channel->get_shared_wal_object()->get_all_logs();
    CASE_EXPECT_EQ(3u, logs.size());
    CASE_EXPECT_GT(channel->get_last_message_sequence(), 0);
    if (!logs.empty()) {
      CASE_EXPECT_EQ(channel->get_last_message_sequence(), logs.back()->sequence());
    }
  }

  // room 首订阅(checkpoint=0): 按生产契约走快照下发(新 memory_only 频道 last_removed 初始为 1，
  // 未订阅过的 client 以 checkpoint 0 上来触发快照)，快照携带全部 3 条日志
  team_room::ptr_t room = env.setup_ready_room(team_key);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.wal_converge());
  CASE_EXPECT_TRUE(nullptr != room->find_member(u1, false));
  CASE_EXPECT_TRUE(nullptr != room->find_member(u2, false));
  {
    bool saw_snapshot = false;
    for (const auto& record : env.wal_event_batches()) {
      if (!record.batch.has_channel_snapshot()) {
        continue;
      }
      saw_snapshot = true;
      const auto& snapshot = record.batch.channel_snapshot();
      CASE_EXPECT_EQ(3, snapshot.messages_size());
      CASE_EXPECT_EQ(channel->get_last_message_sequence(), snapshot.channel_metadata().last_sequence());
    }
    CASE_EXPECT_TRUE(saw_snapshot);
  }

  // ---- Part A: checkpoint 正常(命中 seq3 且 hash 匹配) -> 只补后续日志 ----
  env.wal_clear_event_batches();
  int64_t checkpoint_sequence = channel->get_last_message_sequence();
  uint64_t checkpoint_hash = channel->get_last_hash_code();
  {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), u3);
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    CASE_EXPECT_EQ(0, env.run("commit_u3", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action)));
                   }));
  }
  int64_t sequence_u3 = channel->get_last_message_sequence();
  CASE_EXPECT_GT(sequence_u3, checkpoint_sequence);

  CASE_EXPECT_EQ(0, env.run("resubscribe_normal", [&](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(
                       RPC_AWAIT_CODE_RESULT(env.wal_resubscribe(ctx, channel, checkpoint_sequence, checkpoint_hash)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());

  {
    bool saw_incremental = false;
    for (const auto& record : env.wal_event_batches()) {
      CASE_EXPECT_FALSE(record.batch.has_channel_snapshot());
      for (const auto& message : record.batch.channel_message()) {
        // checkpoint 命中时只下发 checkpoint 之后的日志，绝不重发 [1..checkpoint]
        CASE_EXPECT_GT(message.sequence(), checkpoint_sequence);
        if (message.sequence() == sequence_u3) {
          saw_incremental = true;
        }
      }
    }
    CASE_EXPECT_TRUE(saw_incremental);
  }
  CASE_EXPECT_TRUE(nullptr != room->find_member(u3, false));

  // ---- Part B: checkpoint hash 不匹配 -> 强制 snapshot，错误分支不继续增量 ----
  env.wal_clear_event_batches();
  uint64_t actual_hash = channel->get_last_hash_code();
  uint64_t wrong_hash = actual_hash ^ 0xA5A5A5A5A5A5A5A5ULL;
  CASE_EXPECT_NE(actual_hash, wrong_hash);
  CASE_EXPECT_NE(0u, wrong_hash);

  CASE_EXPECT_EQ(0, env.run("resubscribe_hash_mismatch", [&](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_resubscribe(ctx, channel, sequence_u3, wrong_hash)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());

  {
    // hash 不匹配的订阅请求本身必须走快照分支(不因错误检查点继续增量);期间并发的合法心跳
    // 交付(如房间回执确认触发的增量)属于正常行为，只约束其内容不得回退到 checkpoint 之前
    bool saw_snapshot = false;
    for (const auto& record : env.wal_event_batches()) {
      for (const auto& message : record.batch.channel_message()) {
        CASE_EXPECT_GT(message.sequence(), checkpoint_sequence);
      }
      if (!record.batch.has_channel_snapshot()) {
        continue;
      }
      saw_snapshot = true;
      const auto& snapshot = record.batch.channel_snapshot();
      CASE_EXPECT_EQ(sequence_u3, snapshot.channel_metadata().last_sequence());
      // 快照覆盖全部缓存日志(kCreate + 3 条事件)，与频道侧 journal 一致
      CASE_EXPECT_EQ(channel->get_shared_wal_object()->get_all_logs().size(),
                     static_cast<size_t>(snapshot.messages_size()));
    }
    CASE_EXPECT_TRUE(saw_snapshot);
  }

  // 快照重放后成员状态保留，不出现半恢复或丢成员
  CASE_EXPECT_TRUE(nullptr != room->find_member(u1, false));
  CASE_EXPECT_TRUE(nullptr != room->find_member(u2, false));
  CASE_EXPECT_TRUE(nullptr != room->find_member(u3, false));

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-03: 落后于 last_removed 的订阅者收快照，跟得上的订阅者仍只收增量 ============
// 双订阅者以独立 key 驱动真实 publisher 的订阅决策: 落后订阅者(checkpoint < last_removed_key)
// 必须收到快照且不携带增量; 跟得上的订阅者(checkpoint 命中现有日志且 hash 匹配)只收后续增量。
// 两类批次覆盖的日志集合与真实 journal 剩余日志一致(等价性)，房间真实订阅者持续只收增量
CASE_TEST(teamsvr_room_wal, lagging_subscriber_snapshot_caught_up_incremental) {
  room_test_env env;
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);
  const std::string kLaggingKey = "server:wal-lagging";
  const std::string kCaughtUpKey = "server:wal-caught-up";

  room_test_env::wal_channel_ptr_t channel;
  CASE_EXPECT_EQ(0, env.run("prepare_journal", [&](rpc::context& ctx) -> rpc::result_code_type {
                   auto ensure_ret = RPC_AWAIT_CODE_RESULT(env.wal_ensure_channel(ctx, team_key));
                   if (0 != ensure_ret) {
                     RPC_RETURN_CODE(ensure_ret);
                   }
                   channel = env.wal_find_channel(team_key);
                   if (!channel) {
                     RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
                   }
                   for (uint64_t user_id = 8501; user_id <= 8504; ++user_id) {
                     atfw::team::DTeamAction action;
                     auto* add_member = action.mutable_add_member();
                     protobuf_copy_message(*add_member->mutable_user_key(), teamsvr_room_test::make_user_key(1, user_id));
                     add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                     // 第二条日志跳号，压缩边界落在非连续 sequence 上
                     int64_t gap = (8502 == user_id) ? 1000 : 0;
                     auto commit_ret = RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action, gap));
                     if (0 != commit_ret) {
                       RPC_RETURN_CODE(commit_ret);
                     }
                   }
                   RPC_RETURN_CODE(0);
                 }));
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // room 以真实订阅入住(初次按契约收到快照)，作为“跟得上”的真实状态载体
  team_room::ptr_t room = env.setup_ready_room(team_key);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  for (uint64_t user_id = 8501; user_id <= 8504; ++user_id) {
    CASE_EXPECT_TRUE(nullptr != room->find_member(teamsvr_room_test::make_user_key(1, user_id), false));
  }

  // 压缩边界取第 3 条日志(u3，跳号之后)的 sequence: 剩余 [boundary..end]
  const auto& logs = channel->get_shared_wal_object()->get_all_logs();
  CASE_EXPECT_GE(logs.size(), 5u);
  if (logs.size() < 5) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  int64_t boundary = logs[2]->sequence();
  CASE_EXPECT_EQ(0, env.run("compact", [&](rpc::context& ctx) -> rpc::result_code_type {
                   channel->compact_sequence(boundary);
                   channel->tick(ctx);
                   RPC_RETURN_CODE(0);
                 }));
  const auto* last_removed = channel->get_shared_wal_object()->get_last_removed_key();
  CASE_EXPECT_TRUE(nullptr != last_removed);
  if (nullptr != last_removed) {
    CASE_EXPECT_EQ(boundary, *last_removed);
  }

  // ---- 落后订阅者: checkpoint=0 < last_removed -> 快照，且不允许增量分支 ----
  env.wal_clear_event_batches();
  CASE_EXPECT_EQ(0, env.run("subscribe_lagging", [&](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(
                       RPC_AWAIT_CODE_RESULT(env.wal_resubscribe_as(ctx, channel, kLaggingKey, 0, 0, false)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());
  {
    bool saw_snapshot = false;
    for (const auto& record : env.wal_event_batches()) {
      bool for_lagging = false;
      for (const auto& key : record.batch.subscriber_keys()) {
        if (key == kLaggingKey) {
          for_lagging = true;
          break;
        }
      }
      if (!for_lagging) {
        continue;
      }
      CASE_EXPECT_EQ(0, record.batch.channel_message_size());
      if (!record.batch.has_channel_snapshot()) {
        continue;
      }
      saw_snapshot = true;
      // 快照覆盖压缩后全部剩余日志(>= boundary)，与真实 journal 剩余集合一致
      const auto& snapshot = record.batch.channel_snapshot();
      CASE_EXPECT_EQ(channel->get_shared_wal_object()->get_all_logs().size(),
                     static_cast<size_t>(snapshot.messages_size()));
      for (const auto& message : snapshot.messages()) {
        CASE_EXPECT_GE(message.sequence(), boundary);
      }
      CASE_EXPECT_EQ(channel->get_last_message_sequence(), snapshot.channel_metadata().last_sequence());
      CASE_EXPECT_EQ(boundary, snapshot.channel_runtime().last_removed_sequence());
    }
    CASE_EXPECT_TRUE(saw_snapshot);
  }

  // ---- 追加新日志后，跟得上的订阅者只收增量 ----
  env.wal_clear_event_batches();
  CASE_EXPECT_EQ(0, env.run("commit_u5", [&](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* add_member = action.mutable_add_member();
                   protobuf_copy_message(*add_member->mutable_user_key(), teamsvr_room_test::make_user_key(1, 8505));
                   add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action)));
                 }));
  int64_t new_sequence = channel->get_last_message_sequence();
  {
    // checkpoint=boundary(命中保留日志且 hash 匹配) -> 只补 boundary 之后的日志
    uint64_t boundary_hash = 0;
    for (const auto& log : channel->get_shared_wal_object()->get_all_logs()) {
      if (log->sequence() == boundary) {
        boundary_hash = log->hash_code();
        break;
      }
    }
    CASE_EXPECT_NE(0u, boundary_hash);
    CASE_EXPECT_EQ(0, env.run("subscribe_caught_up", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                         env.wal_resubscribe_as(ctx, channel, kCaughtUpKey, boundary, boundary_hash, false)));
                   }));
  }
  CASE_EXPECT_EQ(0, env.wal_converge());
  {
    bool saw_incremental = false;
    std::vector<int64_t> delivered;
    for (const auto& record : env.wal_event_batches()) {
      bool for_caught_up = false;
      for (const auto& key : record.batch.subscriber_keys()) {
        if (key == kCaughtUpKey) {
          for_caught_up = true;
          break;
        }
      }
      if (!for_caught_up) {
        continue;
      }
      // checkpoint 命中且 hash 匹配时不得退回快照
      CASE_EXPECT_FALSE(record.batch.has_channel_snapshot());
      for (const auto& message : record.batch.channel_message()) {
        CASE_EXPECT_GT(message.sequence(), boundary);
        delivered.push_back(message.sequence());
        if (message.sequence() == new_sequence) {
          saw_incremental = true;
        }
      }
    }
    CASE_EXPECT_TRUE(saw_incremental);
    // 增量链覆盖 (boundary, last] 的全部剩余日志，与快照视图(多含 boundary 本身)等价
    for (const auto& log : channel->get_shared_wal_object()->get_all_logs()) {
      if (log->sequence() <= boundary) {
        continue;
      }
      bool found = false;
      for (int64_t sequence : delivered) {
        if (sequence == log->sequence()) {
          found = true;
          break;
        }
      }
      if (!found) {
        CASE_EXPECT_TRUE(found);
        break;
      }
    }
  }

  // 房间真实订阅者持续应用增量(u5 可见)
  CASE_EXPECT_TRUE(nullptr != room->find_member(teamsvr_room_test::make_user_key(1, 8505), false));

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-04: compact_sequence 边界保留/删除语义与 Room 重放起点一致(含非连续 sequence) ============
// 全链路走真实路径: 建队/加成员/带跳号的事件 -> 房间真实维护触发压缩 update -> 真实 mq_channel
// 裁剪 -> 丢弃旧 room 后以 checkpoint=0 重订阅(真实快照 + 剩余日志)恢复。锁定真实裁剪契约:
// sequence < compact_sequence 的日志被物理移除、== compact_sequence 的日志保留(fake journal 已对齐同一开区间语义)；恢复房间的压缩边界/成员/共享数据与权威频道一致
CASE_TEST(teamsvr_room_wal, compact_boundary_and_replay_start) {
  // 双维度硬保证语义下，订阅者缓存在维护时可能尚未积累到最小保留条数(WAL 事件异步送达);
  // 数量维度关闭(keep_count/keep_percent 均为 0)使本用例聚焦时间维度裁剪契约
  teamsvr_room_test::room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 0;
  cfg.compact_log_keep_percent = 0;
  room_test_env env(cfg);
  env.wal_journal_mode = true;  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);

  team_room::ptr_t room;
  teamsvr_room_test::standard_team_members members;
  {
    members.owner = teamsvr_room_test::make_user_key(1, 8601);
    members.admin = teamsvr_room_test::make_user_key(1, 8602);
    members.normal = teamsvr_room_test::make_user_key(1, 8603);
    members.outsider = teamsvr_room_test::make_user_key(1, 8604);
    members.owner_channel = teamsvr_room_test::make_personal_channel(8601);
    members.admin_channel = teamsvr_room_test::make_personal_channel(8602);
    members.normal_channel = teamsvr_room_test::make_personal_channel(8603);
    int32_t create_ret = env.setup_created_team(team_id, members.owner, members.owner_channel, &room);
    CASE_EXPECT_EQ(0, create_ret);
    if (0 != create_ret || !room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // owner 添加 admin/normal 成员(真实 send_action -> WAL send_message handler -> 真实 journal)
    int32_t members_ret = env.run("setup_members", [&members, &room](rpc::context& ctx) -> rpc::result_code_type {
      {
        atfw::team::DTeamAction action;
        auto* add_member = action.mutable_add_member();
        protobuf_copy_message(*add_member->mutable_user_key(), members.admin);
        protobuf_copy_message(*add_member->mutable_user_channel(), members.admin_channel);
        add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
        int32_t ret = RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action));
        if (0 != ret) {
          RPC_RETURN_CODE(ret);
        }
      }
      {
        atfw::team::DTeamAction action;
        auto* add_member = action.mutable_add_member();
        protobuf_copy_message(*add_member->mutable_user_key(), members.normal);
        protobuf_copy_message(*add_member->mutable_user_channel(), members.normal_channel);
        add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }
    });
    CASE_EXPECT_EQ(0, members_ret);
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_TRUE(nullptr != room->find_member(members.admin, false));
    CASE_EXPECT_TRUE(nullptr != room->find_member(members.normal, false));
    if (nullptr == room->find_member(members.normal, false)) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
  }
  auto channel = env.wal_find_channel(team_key);
  CASE_EXPECT_TRUE(!!channel);
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 追加带 sequence 缺口的成员更新(共享数据 + 版本，供恢复后断言)
  CASE_EXPECT_EQ(0, env.run("commit_gapped_logs", [&](rpc::context& ctx) -> rpc::result_code_type {
                   for (int i = 0; i < 4; ++i) {
                     atfw::team::DTeamAction action;
                     auto* update = action.mutable_member_update();
                     protobuf_copy_message(*update->mutable_user_key(), members.normal);
                     update->set_client_version("wal-compact-v" + std::to_string(i));
                     teamsvr_room_test::add_team_any_data_entry(update->mutable_shared_member_data(), 30 + i,
                                                               "wal-gap-" + std::to_string(i));
                     int64_t gap = (1 == i) ? 1000 : 0;
                     auto ret = RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action, gap));
                     if (0 != ret) {
                       RPC_RETURN_CODE(ret);
                     }
                   }
                   RPC_RETURN_CODE(0);
                 }));
  int64_t sequence_before_maintenance = channel->get_last_message_sequence();
  CASE_EXPECT_GT(sequence_before_maintenance - 1000, 0);  // 跳号确实生效

  // 驱动真实维护触发压缩 update(save=true + custom/private 快照 + compact_sequence)。
  // WAL 模式下事件经真实 vtable 异步送达，数量加速的调度时机不可依赖；按 CMP 时间类用例的
  // 方式推进虚拟时间越过续租点(租约10s/续租间隔5s)，续租维护必然执行且每次维护都会重新
  // 选择压缩点(CMP-01/02 契约)
  {
    teamsvr_room_test::global_now_offset_guard advance_guard{std::chrono::seconds{6}};
    CASE_EXPECT_GT(env.drive_timer_ticks(), 0);
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  int64_t compact_boundary = room->debug_last_compact_sequence();
  CASE_EXPECT_GT(compact_boundary, 0);
  CASE_EXPECT_LE(compact_boundary, sequence_before_maintenance);

  // 真实 mq_channel 裁剪契约: < 边界物理移除；== 边界保留；last_removed_key 与房间边界一致
  const auto* last_removed = channel->get_shared_wal_object()->get_last_removed_key();
  CASE_EXPECT_TRUE(nullptr != last_removed);
  if (nullptr != last_removed) {
    CASE_EXPECT_EQ(compact_boundary, *last_removed);
  }
  {
    bool boundary_log_retained = false;
    for (const auto& log : channel->get_shared_wal_object()->get_all_logs()) {
      CASE_EXPECT_GE(log->sequence(), compact_boundary);
      if (log->sequence() == compact_boundary) {
        boundary_log_retained = true;
      }
    }
    CASE_EXPECT_TRUE(boundary_log_retained);
  }

  // 丢弃旧 room，强制以 checkpoint=0 重新订阅: 0 < last_removed -> 真实快照(含 custom/private 与剩余日志)
  env.clear_rooms();
  room.reset();
  team_room::ptr_t restored = env.setup_ready_room(team_key);
  CASE_EXPECT_TRUE(!!restored);
  if (!restored) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  env.wal_clear_event_batches();
  CASE_EXPECT_EQ(0, env.run("force_snapshot_restore", [&](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_resubscribe(ctx, channel, 0, 0)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());

  // 快照批次: 剩余日志全部 >= 边界，携带 custom/private 数据与压缩边界
  {
    bool saw_snapshot = false;
    for (const auto& record : env.wal_event_batches()) {
      if (!record.batch.has_channel_snapshot()) {
        continue;
      }
      saw_snapshot = true;
      const auto& snapshot = record.batch.channel_snapshot();
      for (const auto& message : snapshot.messages()) {
        CASE_EXPECT_GE(message.sequence(), compact_boundary);
      }
      CASE_EXPECT_EQ(compact_boundary, snapshot.channel_runtime().last_removed_sequence());

      atfw::team::DTeamStorage storage;
      CASE_EXPECT_TRUE(snapshot.channel_metadata().has_custom_data());
      CASE_EXPECT_TRUE(snapshot.channel_metadata().custom_data().UnpackTo(&storage));
      CASE_EXPECT_GE(storage.member_size(), 3);
      CASE_EXPECT_EQ(members.owner.user_id(), storage.captain_user_key().user_id());
      CASE_EXPECT_GT(storage.saved_action_sequence(), 0);
      CASE_EXPECT_LE(storage.saved_action_sequence(), channel->get_last_message_sequence());

      atfw::team::DTeamRoomPrivateData private_data;
      CASE_EXPECT_TRUE(snapshot.channel_runtime().has_private_data());
      CASE_EXPECT_TRUE(snapshot.channel_runtime().private_data().UnpackTo(&private_data));
      CASE_EXPECT_TRUE(private_data.team_created());
      CASE_EXPECT_EQ(compact_boundary, private_data.last_compact_sequence());
    }
    CASE_EXPECT_TRUE(saw_snapshot);
  }

  // 恢复房间状态与权威频道一致: 成员/角色/版本/压缩边界/保存序号
  CASE_EXPECT_TRUE(nullptr != restored->find_member(members.owner, false));
  CASE_EXPECT_TRUE(nullptr != restored->find_member(members.admin, false));
  auto restored_normal = restored->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!restored_normal);
  if (restored_normal) {
    CASE_EXPECT_EQ("wal-compact-v3", restored_normal->member_data.client_version());
  }
  CASE_EXPECT_EQ(compact_boundary, restored->debug_last_compact_sequence());
  CASE_EXPECT_GT(restored->debug_saved_action_sequence(), 0);

  // 恢复后的共享数据仍可被 condition 观察(等值放行、非等值拒绝)
  {
    atfw::team::DTeamAction match;
    auto* update = match.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    teamsvr_room_test::add_team_any_data_entry(update->mutable_shared_member_data(), 99, "post-compact");
    auto* group = update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.normal);
    teamsvr_room_test::add_team_any_value_entry(group->mutable_member_condition()->mutable_shared_member_data(), 33,
                                                "wal-gap-3");
    CASE_EXPECT_EQ(0, env.run("check_condition_match", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(restored->check_action_permission(ctx, members.owner, match)));
                   }));

    atfw::team::DTeamAction mismatch;
    auto* mismatch_update = mismatch.mutable_member_update();
    protobuf_copy_message(*mismatch_update->mutable_user_key(), members.normal);
    auto* mismatch_group = mismatch_update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*mismatch_group->mutable_user_key(), members.normal);
    teamsvr_room_test::add_team_any_value_entry(mismatch_group->mutable_member_condition()->mutable_shared_member_data(),
                                                33, "wal-gap-other");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   env.run("check_condition_mismatch", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(restored->check_action_permission(ctx, members.owner, mismatch)));
                   }));
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-05: dump_snapshot -> 新 mq_channel::load_snapshot -> Room restore 往返 ============
// 全链路走真实层: 真实房间路径构造富状态(成员/keyed shared data/维护压缩的 custom+private 快照/锁)，
// 从源频道 dump_snapshot 取得 transfer 快照，创建同 key 的空白可写频道并 load_snapshot，
// 逐字段验证 custom/private/lock/messages/compact 边界不丢失；再以新频道为唯一权威重建房间，
// 恢复结果与转移前等价，且恢复后的房间可继续写入(锁经 dump/load 存活，holder 回显 CAS 成功)、
// 写入只落新权威频道
CASE_TEST(teamsvr_room_wal, snapshot_dump_load_round_trip) {
  // 与 WAL-04 相同的配置: 数量维度关闭(keep_count/keep_percent=0)，聚焦时间维度裁剪，
  // 保证维护压缩产生确定的 compact 边界
  teamsvr_room_test::room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 0;
  cfg.compact_log_keep_percent = 0;
  room_test_env env(cfg);
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);

  team_room::ptr_t room;
  teamsvr_room_test::standard_team_members members;
  {
    members.owner = teamsvr_room_test::make_user_key(1, 8701);
    members.admin = teamsvr_room_test::make_user_key(1, 8702);
    members.normal = teamsvr_room_test::make_user_key(1, 8703);
    members.outsider = teamsvr_room_test::make_user_key(1, 8704);
    members.owner_channel = teamsvr_room_test::make_personal_channel(8701);
    members.admin_channel = teamsvr_room_test::make_personal_channel(8702);
    members.normal_channel = teamsvr_room_test::make_personal_channel(8703);
    int32_t create_ret = env.setup_created_team(team_id, members.owner, members.owner_channel, &room);
    CASE_EXPECT_EQ(0, create_ret);
    if (0 != create_ret || !room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));

    int32_t members_ret = env.run("setup_members", [&members, &room](rpc::context& ctx) -> rpc::result_code_type {
      {
        atfw::team::DTeamAction action;
        auto* add_member = action.mutable_add_member();
        protobuf_copy_message(*add_member->mutable_user_key(), members.admin);
        protobuf_copy_message(*add_member->mutable_user_channel(), members.admin_channel);
        add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
        int32_t ret = RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action));
        if (0 != ret) {
          RPC_RETURN_CODE(ret);
        }
      }
      {
        atfw::team::DTeamAction action;
        auto* add_member = action.mutable_add_member();
        protobuf_copy_message(*add_member->mutable_user_key(), members.normal);
        protobuf_copy_message(*add_member->mutable_user_channel(), members.normal_channel);
        add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }
    });
    CASE_EXPECT_EQ(0, members_ret);
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_TRUE(nullptr != room->find_member(members.admin, false));
    if (nullptr == room->find_member(members.normal, false)) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
  }
  auto channel = env.wal_find_channel(team_key);
  CASE_EXPECT_TRUE(!!channel);
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 带 sequence 缺口的成员更新(共享数据 + 版本，供转移后断言)
  CASE_EXPECT_EQ(0, env.run("commit_gapped_logs", [&](rpc::context& ctx) -> rpc::result_code_type {
                   for (int i = 0; i < 4; ++i) {
                     atfw::team::DTeamAction action;
                     auto* update = action.mutable_member_update();
                     protobuf_copy_message(*update->mutable_user_key(), members.normal);
                     update->set_client_version("wal-rt-v" + std::to_string(i));
                     teamsvr_room_test::add_team_any_data_entry(update->mutable_shared_member_data(), 40 + i,
                                                               "wal-rt-gap-" + std::to_string(i));
                     int64_t gap = (1 == i) ? 1000 : 0;
                     auto ret = RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action, gap));
                     if (0 != ret) {
                       RPC_RETURN_CODE(ret);
                     }
                   }
                   RPC_RETURN_CODE(0);
                 }));

  // 真实维护触发压缩 update(save=true + custom/private 快照 + compact_sequence)，
  // 与 WAL-04 相同: 推进虚拟时间越过续租点，续租维护必然执行并重选压缩点
  {
    teamsvr_room_test::global_now_offset_guard advance_guard{std::chrono::seconds{6}};
    CASE_EXPECT_GT(env.drive_timer_ticks(), 0);
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  int64_t compact_boundary = room->debug_last_compact_sequence();
  CASE_EXPECT_GT(compact_boundary, 0);

  // ---- dump_snapshot: transfer 快照携带 custom/private/lock/messages/compact 边界与订阅者 ----
  atfw::dtmq::channel_snapshot snapshot;
  CASE_EXPECT_EQ(0, env.run("dump_snapshot", [&](rpc::context& ctx) -> rpc::result_code_type {
                   channel->dump_snapshot(ctx, snapshot);
                   RPC_RETURN_CODE(0);
                 }));
  {
    // writable 源频道的 transfer 快照 replicate_index=0
    CASE_EXPECT_EQ(0u, snapshot.replicate_index());
    const auto& snapshot_data = snapshot.channel_data();
    CASE_EXPECT_GT(snapshot_data.messages_size(), 0);

    // 剩余日志与压缩边界一致: 全部 >= 边界，边界日志保留(开区间裁剪契约)
    bool boundary_log_found = false;
    for (const auto& message : snapshot_data.messages()) {
      CASE_EXPECT_GE(message.sequence(), compact_boundary);
      if (message.sequence() == compact_boundary) {
        boundary_log_found = true;
      }
    }
    CASE_EXPECT_TRUE(boundary_log_found);
    CASE_EXPECT_EQ(compact_boundary, snapshot_data.channel_runtime().last_removed_sequence());

    // custom data: DTeamStorage 快照(成员/队长/保存序号)
    CASE_EXPECT_TRUE(snapshot_data.channel_metadata().has_custom_data());
    atfw::team::DTeamStorage storage;
    CASE_EXPECT_TRUE(snapshot_data.channel_metadata().custom_data().UnpackTo(&storage));
    CASE_EXPECT_GE(storage.member_size(), 3);
    CASE_EXPECT_EQ(members.owner.user_id(), storage.captain_user_key().user_id());
    CASE_EXPECT_GT(storage.saved_action_sequence(), 0);

    // private data: 主控私有数据(建队标记/压缩边界)
    CASE_EXPECT_TRUE(snapshot_data.channel_runtime().has_private_data());
    atfw::team::DTeamRoomPrivateData private_data;
    CASE_EXPECT_TRUE(snapshot_data.channel_runtime().private_data().UnpackTo(&private_data));
    CASE_EXPECT_TRUE(private_data.team_created());
    CASE_EXPECT_EQ(compact_boundary, private_data.last_compact_sequence());

    // 锁与配置: 本节点持有的锁与 memory_only 配置随快照迁移
    CASE_EXPECT_TRUE(snapshot_data.has_lock());
    CASE_EXPECT_EQ(teamsvr_room_test::self_lock_holder(), snapshot_data.lock().lock_holder());
    CASE_EXPECT_TRUE(snapshot_data.channel_metadata().channel_configure().memory_only());

    // 存活订阅者随快照迁移(transfer 后新节点继承订阅关系)
    CASE_EXPECT_GT(snapshot.subscriber_size(), 0);
  }

  // ---- 空白可写频道 load_snapshot: 逐字段与源频道等价 ----
  room_test_env::wal_channel_ptr_t replacement;
  CASE_EXPECT_EQ(0, env.run("load_snapshot", [&](rpc::context& ctx) -> rpc::result_code_type {
                   auto make_ret = RPC_AWAIT_CODE_RESULT(env.wal_make_replacement_channel(ctx, team_key, replacement));
                   if (0 != make_ret) {
                     RPC_RETURN_CODE(make_ret);
                   }
                   if (!replacement->load_snapshot(ctx, std::move(snapshot))) {
                     RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
                   }
                   RPC_RETURN_CODE(0);
                 }));
  CASE_EXPECT_TRUE(!!replacement);
  if (!replacement) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  {
    CASE_EXPECT_TRUE(replacement->is_writable());
    CASE_EXPECT_TRUE(replacement->is_available());

    // sequence/hash 链与日志集合不丢失(逐条比较 sequence/hash/command_case)
    CASE_EXPECT_EQ(channel->get_last_message_sequence(), replacement->get_last_message_sequence());
    CASE_EXPECT_EQ(channel->get_last_hash_code(), replacement->get_last_hash_code());
    const auto& source_logs = channel->get_shared_wal_object()->get_all_logs();
    const auto& target_logs = replacement->get_shared_wal_object()->get_all_logs();
    CASE_EXPECT_EQ(source_logs.size(), target_logs.size());
    auto source_iter = source_logs.begin();
    auto target_iter = target_logs.begin();
    for (; source_iter != source_logs.end() && target_iter != target_logs.end(); ++source_iter, ++target_iter) {
      CASE_EXPECT_EQ((*source_iter)->sequence(), (*target_iter)->sequence());
      CASE_EXPECT_EQ((*source_iter)->hash_code(), (*target_iter)->hash_code());
      CASE_EXPECT_EQ((*source_iter)->detail().command_case(), (*target_iter)->detail().command_case());
    }

    // custom/private data 内容与覆盖序号不丢失
    CASE_EXPECT_TRUE(atfw::atapp::protobuf_equal(channel->get_custom_data(), replacement->get_custom_data()));
    CASE_EXPECT_EQ(channel->get_custom_data_sequence(), replacement->get_custom_data_sequence());
    CASE_EXPECT_TRUE(atfw::atapp::protobuf_equal(channel->get_private_data(), replacement->get_private_data()));
    CASE_EXPECT_EQ(channel->get_private_data_sequence(), replacement->get_private_data_sequence());

    // 锁与压缩边界不丢失
    CASE_EXPECT_TRUE(atfw::atapp::protobuf_equal(channel->get_lock(), replacement->get_lock()));
    CASE_EXPECT_EQ(teamsvr_room_test::self_lock_holder(), replacement->get_lock().lock_holder());
    const auto* source_removed = channel->get_shared_wal_object()->get_last_removed_key();
    const auto* target_removed = replacement->get_shared_wal_object()->get_last_removed_key();
    CASE_EXPECT_TRUE(nullptr != source_removed);
    CASE_EXPECT_TRUE(nullptr != target_removed);
    if (nullptr != source_removed && nullptr != target_removed) {
      CASE_EXPECT_EQ(*source_removed, *target_removed);
      CASE_EXPECT_EQ(compact_boundary, *target_removed);
    }
    CASE_EXPECT_EQ(channel->get_compact_stateful_sequence(), replacement->get_compact_stateful_sequence());
  }

  // ---- 激活新频道为唯一权威，丢弃旧房间后从新频道恢复 ----
  CASE_EXPECT_EQ(0, env.run("swap_authority", [&](rpc::context& ctx) -> rpc::result_code_type {
                   env.wal_swap_channel(ctx, replacement);
                   RPC_RETURN_CODE(0);
                 }));
  CASE_EXPECT_TRUE(replacement == env.wal_find_channel(team_key));

  int64_t source_last_sequence_after_swap = channel->get_last_message_sequence();
  size_t source_log_count_after_swap = channel->get_shared_wal_object()->get_all_logs().size();

  env.clear_rooms();
  room.reset();
  team_room::ptr_t restored = env.setup_ready_room(team_key);
  CASE_EXPECT_TRUE(!!restored);
  if (!restored) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 恢复结果与转移前等价: 成员/角色/版本/压缩边界/保存序号
  CASE_EXPECT_TRUE(nullptr != restored->find_member(members.owner, false));
  auto restored_admin = restored->find_member(members.admin, false);
  CASE_EXPECT_TRUE(!!restored_admin);
  if (restored_admin) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, restored_admin->member_data.role());
  }
  auto restored_normal = restored->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!restored_normal);
  if (restored_normal) {
    CASE_EXPECT_EQ("wal-rt-v3", restored_normal->member_data.client_version());
  }
  CASE_EXPECT_EQ(compact_boundary, restored->debug_last_compact_sequence());
  CASE_EXPECT_GT(restored->debug_saved_action_sequence(), 0);

  // 恢复后的共享数据仍可被 condition 观察
  {
    atfw::team::DTeamAction match;
    auto* update = match.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    teamsvr_room_test::add_team_any_data_entry(update->mutable_shared_member_data(), 99, "post-rt");
    auto* group = update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.normal);
    teamsvr_room_test::add_team_any_value_entry(group->mutable_member_condition()->mutable_shared_member_data(), 43,
                                                "wal-rt-gap-3");
    CASE_EXPECT_EQ(0, env.run("check_condition_match", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored->check_action_permission(ctx, members.owner, match)));
                   }));
  }

  // ---- 恢复后的房间继续写入: 锁经 dump/load 存活(holder 回显 CAS 成功)，只落新权威 ----
  int64_t target_sequence_before_write = replacement->get_last_message_sequence();
  size_t target_log_count_before_write = replacement->get_shared_wal_object()->get_all_logs().size();
  CASE_EXPECT_EQ(0, env.run("post_restore_write", [&](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* update = action.mutable_member_update();
                   protobuf_copy_message(*update->mutable_user_key(), members.normal);
                   update->set_client_version("wal-rt-post");
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 新权威频道收到且仅收到一条新日志，旧频道对象不再接受任何写入
  CASE_EXPECT_GT(replacement->get_last_message_sequence(), target_sequence_before_write);
  CASE_EXPECT_EQ(target_log_count_before_write + 1, replacement->get_shared_wal_object()->get_all_logs().size());
  CASE_EXPECT_EQ(source_last_sequence_after_swap, channel->get_last_message_sequence());
  CASE_EXPECT_EQ(source_log_count_after_swap, channel->get_shared_wal_object()->get_all_logs().size());

  // 写入事件从新权威回环，恢复房间应用到本地状态
  auto updated_normal = restored->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!updated_normal);
  if (updated_normal) {
    CASE_EXPECT_EQ("wal-rt-post", updated_normal->member_data.client_version());
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-06: transfer 快照携带待广播日志；转移后旧 publisher 不再接受有效 Room 写入 ============
// 场景(镜像 writable 迁移的接缝行为，分布归属仍由 component-dtmq-proxysvr 覆盖):
//   1. 源频道 journal 上存在一条未广播日志(直接提交不 tick): 转移快照必须携带它；
//   2. 新频道 load 后成为唯一权威: 旧房间的后续写入只落新频道，旧频道对象零变化；
//   3. 房间以转移前 checkpoint 对新权威增量重订阅(生产=订阅心跳到新可写节点)，
//      待广播日志被补齐投递，不回退快照
CASE_TEST(teamsvr_room_wal, transfer_pending_logs_and_old_publisher_fence) {
  room_test_env env;
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);

  auto owner_key = teamsvr_room_test::make_user_key(1, 8801);
  auto admin_key = teamsvr_room_test::make_user_key(1, 8802);
  auto outsider_key = teamsvr_room_test::make_user_key(1, 8803);
  auto owner_channel = teamsvr_room_test::make_personal_channel(8801);
  auto admin_channel = teamsvr_room_test::make_personal_channel(8802);

  team_room::ptr_t room;
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner_key, owner_channel, &room));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(0, env.run("setup_members", [&admin_key, &admin_channel, &room](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* add_member = action.mutable_add_member();
                   protobuf_copy_message(*add_member->mutable_user_key(), admin_key);
                   protobuf_copy_message(*add_member->mutable_user_channel(), admin_channel);
                   add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(nullptr != room->find_member(admin_key, false));

  auto channel = env.wal_find_channel(team_key);
  CASE_EXPECT_TRUE(!!channel);
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 房间当前 checkpoint(全部已广播日志均已应用)。转移后以它对新权威做增量重订阅
  int64_t room_ack_sequence = channel->get_last_message_sequence();
  uint64_t room_ack_hash = channel->get_last_hash_code();
  CASE_EXPECT_GT(room_ack_sequence, 0);

  // ---- 步骤 1: 提交一条不广播的日志(待广播)，房间此时看不到它 ----
  int64_t pending_sequence = 0;
  CASE_EXPECT_EQ(0, env.run("commit_pending", [&](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* add_member = action.mutable_add_member();
                   protobuf_copy_message(*add_member->mutable_user_key(), outsider_key);
                   add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                   // broadcast=false: 只入 journal，不 tick 不投递
                   auto ret = RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action, 0, false));
                   if (0 != ret) {
                     RPC_RETURN_CODE(ret);
                   }
                   pending_sequence = channel->get_last_message_sequence();
                   RPC_RETURN_CODE(0);
                 }));
  CASE_EXPECT_GT(pending_sequence, room_ack_sequence);
  CASE_EXPECT_TRUE(nullptr == room->find_member(outsider_key, false));

  // ---- dump_snapshot: 待广播日志必须包含在转移快照中 ----
  atfw::dtmq::channel_snapshot snapshot;
  CASE_EXPECT_EQ(0, env.run("dump_snapshot", [&](rpc::context& ctx) -> rpc::result_code_type {
                   channel->dump_snapshot(ctx, snapshot);
                   RPC_RETURN_CODE(0);
                 }));
  {
    CASE_EXPECT_EQ(0u, snapshot.replicate_index());
    bool pending_in_snapshot = false;
    for (const auto& message : snapshot.channel_data().messages()) {
      if (message.sequence() == pending_sequence) {
        pending_in_snapshot = true;
      }
    }
    CASE_EXPECT_TRUE(pending_in_snapshot);
    CASE_EXPECT_TRUE(snapshot.channel_data().channel_metadata().has_custom_data());
    CASE_EXPECT_TRUE(snapshot.channel_data().channel_runtime().has_private_data());
    CASE_EXPECT_TRUE(snapshot.channel_data().has_lock());
    CASE_EXPECT_GT(snapshot.subscriber_size(), 0);
  }

  // ---- 新频道 load 并激活为唯一权威(旧频道对象保活用于隔离断言) ----
  room_test_env::wal_channel_ptr_t replacement;
  CASE_EXPECT_EQ(0, env.run("load_and_swap", [&](rpc::context& ctx) -> rpc::result_code_type {
                   auto make_ret = RPC_AWAIT_CODE_RESULT(env.wal_make_replacement_channel(ctx, team_key, replacement));
                   if (0 != make_ret) {
                     RPC_RETURN_CODE(make_ret);
                   }
                   if (!replacement->load_snapshot(ctx, std::move(snapshot))) {
                     RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
                   }
                   env.wal_swap_channel(ctx, replacement);
                   RPC_RETURN_CODE(0);
                 }));
  CASE_EXPECT_TRUE(!!replacement);
  CASE_EXPECT_TRUE(replacement == env.wal_find_channel(team_key));
  if (!replacement) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 新权威 journal 与源频道完全一致(含待广播日志)
  CASE_EXPECT_EQ(channel->get_last_message_sequence(), replacement->get_last_message_sequence());
  CASE_EXPECT_EQ(channel->get_last_hash_code(), replacement->get_last_hash_code());
  CASE_EXPECT_EQ(channel->get_shared_wal_object()->get_all_logs().size(),
                 replacement->get_shared_wal_object()->get_all_logs().size());

  int64_t source_sequence_after_swap = channel->get_last_message_sequence();
  size_t source_log_count_after_swap = channel->get_shared_wal_object()->get_all_logs().size();
  int64_t target_sequence_before_write = replacement->get_last_message_sequence();

  // ---- 步骤 2: 旧房间继续写(不经重建，镜像生产中房间对迁移无感知): 只落新权威 ----
  CASE_EXPECT_EQ(0, env.run("post_transfer_write", [&](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* update = action.mutable_member_update();
                   protobuf_copy_message(*update->mutable_user_key(), admin_key);
                   update->set_client_version("wal-tx-post");
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  int64_t post_write_sequence = replacement->get_last_message_sequence();
  CASE_EXPECT_GT(post_write_sequence, target_sequence_before_write);

  // 旧 publisher 不再接受有效写入: journal 与最新序号零变化
  CASE_EXPECT_EQ(source_sequence_after_swap, channel->get_last_message_sequence());
  CASE_EXPECT_EQ(source_log_count_after_swap, channel->get_shared_wal_object()->get_all_logs().size());

  // ---- 步骤 3: 房间以转移前 checkpoint 增量重订阅新权威: 补齐待广播日志，不回退快照 ----
  // 注: 转移后新 publisher 的广播边界在待广播日志上，对 checkpoint 更早的订阅者，写回事件
  // 单独到达会因中间缺失日志导致客户端哈希链校验拒绝(由 catch-up 补齐，DTMQ 客户端自愈
  // 契约)；生产中该补齐由订阅心跳到新可写节点完成，这里以 room_ack checkpoint 显式驱动。
  // 补齐批次从 checkpoint 起连续覆盖 [pending, post_write]，两条日志一并应用
  env.wal_clear_event_batches();
  CASE_EXPECT_EQ(0, env.run("resubscribe_new_authority", [&](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(
                       RPC_AWAIT_CODE_RESULT(env.wal_resubscribe(ctx, replacement, room_ack_sequence, room_ack_hash)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());
  {
    bool saw_pending = false;
    bool saw_post_write = false;
    for (const auto& record : env.wal_event_batches()) {
      // checkpoint 命中且 hash 匹配: 只允许增量，不允许退回快照
      CASE_EXPECT_FALSE(record.batch.has_channel_snapshot());
      for (const auto& message : record.batch.channel_message()) {
        CASE_EXPECT_GT(message.sequence(), room_ack_sequence);
        CASE_EXPECT_LE(message.sequence(), post_write_sequence);
        if (message.sequence() == pending_sequence) {
          saw_pending = true;
        }
        if (message.sequence() == post_write_sequence) {
          saw_post_write = true;
        }
      }
    }
    CASE_EXPECT_TRUE(saw_pending);
    CASE_EXPECT_TRUE(saw_post_write);
  }

  // 待广播日志与转移后的写入事件一并补齐应用: 新成员可见、成员数据更新生效
  CASE_EXPECT_TRUE(nullptr != room->find_member(outsider_key, false));
  auto updated_admin = room->find_member(admin_key, false);
  CASE_EXPECT_TRUE(!!updated_admin);
  if (updated_admin) {
    CASE_EXPECT_EQ("wal-tx-post", updated_admin->member_data.client_version());
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-08: destroy/create 新代际与旧 checkpoint 交错；旧 team id 防重建 ============
// 全链路真实层: destroy_team 回环冻结房间 -> kDestroyChannel 定时真实销毁频道(kDestroy 日志) ->
// 已销毁频道上的新房间从带 destroy 元数据的快照恢复销毁状态(重新 create 拒绝且零写入) ->
// 镜像生产 make_writable_channel 的 ensure_recreate_after_destroyed 重建新代际(旧日志全部移除、
// custom/private 保留) -> 新代际房间恢复旧成员且 create 仍被 team_created 拒绝(防重建跨代际) ->
// 旧 checkpoint 订阅者经快照兜底落到新代际，不会收到任何旧代际日志
CASE_TEST(teamsvr_room_wal, destroy_recreate_epoch_and_old_checkpoint) {
  // 数量维度关闭(keep_count/keep_percent=0): 销毁前的维护按时间窗口压缩并保存含全部成员的快照
  teamsvr_room_test::room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 0;
  cfg.compact_log_keep_percent = 0;
  room_test_env env(cfg);
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);
  auto owner_key = teamsvr_room_test::make_user_key(1, 9101);
  auto admin_key = teamsvr_room_test::make_user_key(1, 9102);
  auto owner_channel = teamsvr_room_test::make_personal_channel(9101);
  auto admin_channel = teamsvr_room_test::make_personal_channel(9102);

  team_room::ptr_t room;
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner_key, owner_channel, &room));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(0, env.run("setup_members", [&admin_key, &admin_channel, &room](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* add_member = action.mutable_add_member();
                   protobuf_copy_message(*add_member->mutable_user_key(), admin_key);
                   protobuf_copy_message(*add_member->mutable_user_channel(), admin_channel);
                   add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto channel = env.wal_find_channel(team_key);
  CASE_EXPECT_TRUE(!!channel);
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 销毁前驱动一轮维护: 保存含全部成员的 custom/private 快照(重建代际后旧状态仍完整可恢复)
  {
    teamsvr_room_test::global_now_offset_guard advance_guard{std::chrono::seconds{6}};
    CASE_EXPECT_GT(env.drive_timer_ticks(), 0);
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // ---- Part A: destroy_team 回环冻结房间(后续写入返回 destroyed) ----
  CASE_EXPECT_EQ(0, env.run("destroy_team", [room, &team_key](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   protobuf_copy_message(*action.mutable_destroy_team(), team_key);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED,
                 env.run("write_after_destroy", [&admin_key, &room](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), admin_key);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));

  // ---- Part B: kDestroyChannel 定时真实销毁频道 ----
  // destroy_team 事件应用后不重排定时器(仍指向已调度的维护点)，kDestroyChannel 在下一次
  // 调度时才被选中；分轮推进+驱动直至频道真实销毁(与 LIFE-03 的分轮驱动同理)
  {
    teamsvr_room_test::global_now_offset_guard destroy_guard{std::chrono::seconds{2}};
    for (int round = 0; round < 8 && !channel->is_destroyed(); ++round) {
      destroy_guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
  }
  CASE_EXPECT_TRUE(channel->is_destroyed());
  CASE_EXPECT_FALSE(channel->is_available());
  {
    const auto& logs = channel->get_shared_wal_object()->get_all_logs();
    CASE_EXPECT_FALSE(logs.empty());
    if (!logs.empty()) {
      CASE_EXPECT_EQ(atfw::dtmq::DChannelMessageDetail::kDestroy, logs.back()->detail().command_case());
    }
  }

  // 旧代际 checkpoint: destroy_team 事件日志(最后一条 kEvent)
  int64_t old_epoch_sequence = 0;
  uint64_t old_epoch_hash = 0;
  {
    const auto& logs = channel->get_shared_wal_object()->get_all_logs();
    for (auto iter = logs.rbegin(); iter != logs.rend(); ++iter) {
      if ((*iter)->detail().command_case() == atfw::dtmq::DChannelMessageDetail::kEvent) {
        old_epoch_sequence = (*iter)->sequence();
        old_epoch_hash = (*iter)->hash_code();
        break;
      }
    }
    CASE_EXPECT_GT(old_epoch_sequence, 0);
  }
  int64_t destroy_sequence = channel->get_shared_wal_object()->get_all_logs().back()->sequence();

  // ---- Part C: 已销毁频道上的新房间恢复销毁状态(防重建) ----
  env.clear_rooms();
  room.reset();
  {
    auto restored = env.setup_ready_room(team_key);
    CASE_EXPECT_TRUE(!!restored);
    if (restored) {
      CASE_EXPECT_EQ(0, env.sync(team_id));
      atfw::team::SSTeamRoomCreateReq create_req;
      protobuf_copy_message(*create_req.mutable_team_key(), team_key);
      protobuf_copy_message(*create_req.mutable_sender_user_key(), owner_key);
      protobuf_copy_message(*create_req.mutable_sender_user_channel(), owner_channel);
      size_t logs_before = channel->get_shared_wal_object()->get_all_logs().size();
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED,
                     env.run("create_after_destroy", [restored, &create_req](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored->create_team(ctx, create_req)));
                     }));
      CASE_EXPECT_EQ(logs_before, channel->get_shared_wal_object()->get_all_logs().size());
    }
  }

  // ---- Part D: 重建新代际(镜像生产 make_writable_channel 的 ensure_recreate_after_destroyed) ----
  {
    size_t old_log_count = channel->get_shared_wal_object()->get_all_logs().size();
    CASE_EXPECT_GT(old_log_count, 0u);
    CASE_EXPECT_EQ(0, env.run("recreate_epoch", [&channel, old_log_count](rpc::context& ctx) -> rpc::result_code_type {
                     channel->ensure_recreate_after_destroyed(ctx);
                     // 生产中 set_created 与 remove_before 之间真实时间总是推进(偶发同微秒跳过由
                     // 后续维护重试)；固定虚拟时钟下推进 1ms 做等价模拟(与 WAL update handler 一致)
                     atfw::util::time::time_utility::set_global_now_offset(
                         atfw::util::time::time_utility::get_global_now_offset() + std::chrono::milliseconds{1});
                     channel->get_shared_wal_object()->remove_before(atfw::util::time::time_utility::now(),
                                                                     old_log_count);
                     RPC_RETURN_CODE(0);
                   }));
    CASE_EXPECT_TRUE(channel->is_available());
    CASE_EXPECT_FALSE(channel->is_destroyed());
    const auto& logs = channel->get_shared_wal_object()->get_all_logs();
    CASE_EXPECT_EQ(1u, logs.size());
    if (!logs.empty()) {
      // 新代际首条日志为 kCreate，序号延续分配器(高于旧代际全部日志，含 destroy 序号)
      CASE_EXPECT_EQ(atfw::dtmq::DChannelMessageDetail::kCreate, logs.front()->detail().command_case());
      CASE_EXPECT_GT(logs.front()->sequence(), destroy_sequence);
    }
    // custom/private 保留(旧代际状态仍可恢复)
    atfw::team::DTeamStorage storage;
    CASE_EXPECT_TRUE(channel->get_custom_data().UnpackTo(&storage));
    CASE_EXPECT_GE(storage.member_size(), 2);
    atfw::team::DTeamRoomPrivateData private_data;
    CASE_EXPECT_TRUE(channel->get_private_data().UnpackTo(&private_data));
    CASE_EXPECT_TRUE(private_data.team_created());
  }

  // ---- Part E: 新代际上的新房间恢复旧成员，create 仍被拒(防重建跨代际) ----
  {
    env.clear_rooms();
    auto restored = env.setup_ready_room(team_key);
    CASE_EXPECT_TRUE(!!restored);
    if (restored) {
      CASE_EXPECT_EQ(0, env.sync(team_id));
      // 进程级订阅者仍缓存旧代际视图(含 destroy_team 事件)。以 checkpoint 0 强制重订阅，
      // 让真实快照(新代际: 仅 kCreate + 旧 custom/private)覆盖客户端缓存后重新恢复
      CASE_EXPECT_EQ(0, env.run("refresh_client_view", [&](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_resubscribe(ctx, channel, 0, 0)));
                     }));
      CASE_EXPECT_EQ(0, env.sync(team_id));
      CASE_EXPECT_TRUE(nullptr != restored->find_member(owner_key, false));
      CASE_EXPECT_TRUE(nullptr != restored->find_member(admin_key, false));

      atfw::team::SSTeamRoomCreateReq create_req;
      protobuf_copy_message(*create_req.mutable_team_key(), team_key);
      protobuf_copy_message(*create_req.mutable_sender_user_key(), owner_key);
      protobuf_copy_message(*create_req.mutable_sender_user_channel(), owner_channel);
      size_t logs_before = channel->get_shared_wal_object()->get_all_logs().size();
      // team_created 随 custom/private 跨代际保留: 旧 team id 不得被重新创建
      int32_t create_ret = env.run("create_after_recreate", [restored, &create_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored->create_team(ctx, create_req)));
      });
      CASE_EXPECT_TRUE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION == create_ret ||
                       PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED == create_ret);
      CASE_EXPECT_EQ(logs_before, channel->get_shared_wal_object()->get_all_logs().size());
    }
  }

  // ---- Part F: 旧 checkpoint 订阅者经快照兜底落到新代际，不收到任何旧代际日志 ----
  {
    env.wal_clear_event_batches();
    const std::string kOldEpochKey = "server:wal-old-epoch";
    CASE_EXPECT_EQ(0, env.run("resubscribe_old_epoch", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(env.wal_resubscribe_as(ctx, channel, kOldEpochKey, old_epoch_sequence,
                                                                     old_epoch_hash, false)));
                   }));
    CASE_EXPECT_EQ(0, env.wal_converge());
    bool saw_snapshot = false;
    int64_t new_create_sequence = channel->get_shared_wal_object()->get_all_logs().front()->sequence();
    for (const auto& record : env.wal_event_batches()) {
      bool for_old_epoch = false;
      for (const auto& key : record.batch.subscriber_keys()) {
        if (key == kOldEpochKey) {
          for_old_epoch = true;
          break;
        }
      }
      if (!for_old_epoch) {
        continue;
      }
      // 旧 checkpoint 落后于重建时 remove_before 推进的 last_removed(整个旧代际被移除):
      // 快照兜底到当前代际。快照仅含新代际日志(旧代际日志含 destroy_team 事件与 kDestroy 已移除)
      CASE_EXPECT_TRUE(record.batch.has_channel_snapshot());
      if (!record.batch.has_channel_snapshot()) {
        continue;
      }
      saw_snapshot = true;
      const auto& snapshot = record.batch.channel_snapshot();
      for (const auto& message : snapshot.messages()) {
        CASE_EXPECT_GT(message.sequence(), destroy_sequence);
      }
      CASE_EXPECT_EQ(new_create_sequence, snapshot.channel_metadata().last_sequence());
    }
    CASE_EXPECT_TRUE(saw_snapshot);
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ WAL-07: 多订阅者不同 checkpoint/心跳/超时；GC 与快照兜底不越过未保存状态 ============
// 三类订阅者并存: 跟得上的(增量)、心跳过期的(投递被跳过并被订阅者管理器 GC)、房间真实订阅者。
// 契约: 心跳过期订阅者不进入任何投递批次(而非收到跳日志的错误增量)；GC 后可重新订阅恢复；
// 跟得上的订阅者与房间真实订阅者持续只收连续增量，终态完整
CASE_TEST(teamsvr_room_wal, multi_subscriber_checkpoint_gc_and_fallback) {
  room_test_env env;
  env.wal_journal_mode = true;
  if (!env.start()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  int64_t team_id = teamsvr_room_test::next_test_team_id();
  auto team_key = teamsvr_room_test::make_team_key(team_id);
  const std::string kCaughtUpKey = "server:wal-sub-caught-up";
  const std::string kStaleKey = "server:wal-sub-stale";

  room_test_env::wal_channel_ptr_t channel;
  CASE_EXPECT_EQ(0, env.run("prepare_journal", [&](rpc::context& ctx) -> rpc::result_code_type {
                   auto ensure_ret = RPC_AWAIT_CODE_RESULT(env.wal_ensure_channel(ctx, team_key));
                   if (0 != ensure_ret) {
                     RPC_RETURN_CODE(ensure_ret);
                   }
                   channel = env.wal_find_channel(team_key);
                   if (!channel) {
                     RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
                   }
                   for (uint64_t user_id = 9201; user_id <= 9204; ++user_id) {
                     atfw::team::DTeamAction action;
                     auto* add_member = action.mutable_add_member();
                     protobuf_copy_message(*add_member->mutable_user_key(), teamsvr_room_test::make_user_key(1, user_id));
                     add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                     // 第二条日志跳号，压缩边界落在非连续 sequence 上
                     int64_t gap = (9202 == user_id) ? 1000 : 0;
                     auto ret = RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action, gap));
                     if (0 != ret) {
                       RPC_RETURN_CODE(ret);
                     }
                   }
                   RPC_RETURN_CODE(0);
                 }));
  if (!channel) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 房间以真实订阅入住(作为状态等价性载体)
  team_room::ptr_t room = env.setup_ready_room(team_key);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  for (uint64_t user_id = 9201; user_id <= 9204; ++user_id) {
    CASE_EXPECT_TRUE(nullptr != room->find_member(teamsvr_room_test::make_user_key(1, user_id), false));
  }

  // 压缩边界取第 3 条日志(跳号之后)，剩余 [boundary..end]
  const auto& logs = channel->get_shared_wal_object()->get_all_logs();
  CASE_EXPECT_GE(logs.size(), 5u);
  if (logs.size() < 5) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  int64_t boundary = logs[2]->sequence();
  uint64_t boundary_hash = 0;
  for (const auto& log : logs) {
    if (log->sequence() == boundary) {
      boundary_hash = log->hash_code();
      break;
    }
  }
  CASE_EXPECT_NE(0u, boundary_hash);
  CASE_EXPECT_EQ(0, env.run("compact", [&](rpc::context& ctx) -> rpc::result_code_type {
                   channel->compact_sequence(boundary);
                   channel->tick(ctx);
                   RPC_RETURN_CODE(0);
                 }));

  // 两个外部订阅者以 boundary checkpoint 入住(均在心跳有效期内)
  for (const auto& key : {kCaughtUpKey, kStaleKey}) {
    CASE_EXPECT_EQ(0, env.run("subscribe", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(env.wal_resubscribe_as(ctx, channel, key, boundary, boundary_hash, false)));
                   }));
  }
  CASE_EXPECT_EQ(0, env.wal_converge());

  // 新日志: 心跳有效期内两个外部订阅者都收到连续增量
  env.wal_clear_event_batches();
  CASE_EXPECT_EQ(0, env.run("commit_u5", [&](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* add_member = action.mutable_add_member();
                   protobuf_copy_message(*add_member->mutable_user_key(), teamsvr_room_test::make_user_key(1, 9205));
                   add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());
  {
    int64_t new_sequence = channel->get_last_message_sequence();
    for (const auto& key : {kCaughtUpKey, kStaleKey}) {
      bool saw_incremental = false;
      for (const auto& record : env.wal_event_batches()) {
        bool for_key = false;
        for (const auto& subscriber_key : record.batch.subscriber_keys()) {
          if (subscriber_key == key) {
            for_key = true;
            break;
          }
        }
        if (!for_key) {
          continue;
        }
        CASE_EXPECT_FALSE(record.batch.has_channel_snapshot());
        for (const auto& message : record.batch.channel_message()) {
          CASE_EXPECT_GT(message.sequence(), boundary);
          if (message.sequence() == new_sequence) {
            saw_incremental = true;
          }
        }
      }
      CASE_EXPECT_TRUE(saw_incremental);
    }
  }

  // 心跳超时: 推进超过 subscriber_timeout(10s)。跟得上的订阅者与房间真实订阅者刷新心跳，
  // 过期订阅者不再刷新 -> 投递批次跳过它(不进入任何批次，而非收到错误增量)
  {
    teamsvr_room_test::global_now_offset_guard advance_guard{std::chrono::seconds{11}};
    CASE_EXPECT_EQ(0, env.run("refresh_heartbeats", [&](rpc::context& ctx) -> rpc::result_code_type {
                     // 房间真实订阅者(shared key)与跟得上的订阅者刷新心跳
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(env.wal_resubscribe(ctx, channel, channel->get_last_message_sequence(),
                                                                   channel->get_last_hash_code())));
                   }));
    CASE_EXPECT_EQ(0, env.run("refresh_caught_up", [&](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                         env.wal_resubscribe_as(ctx, channel, kCaughtUpKey, channel->get_last_message_sequence(),
                                                channel->get_last_hash_code(), false)));
                   }));
    // 驱动 publisher tick: 过期订阅者被订阅者管理器按超时移除
    CASE_EXPECT_EQ(0, env.run("tick_gc", [&](rpc::context& ctx) -> rpc::result_code_type {
                     channel->tick(ctx);
                     RPC_RETURN_CODE(0);
                   }));
  }

  env.wal_clear_event_batches();
  CASE_EXPECT_EQ(0, env.run("commit_u6", [&](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   auto* add_member = action.mutable_add_member();
                   protobuf_copy_message(*add_member->mutable_user_key(), teamsvr_room_test::make_user_key(1, 9206));
                   add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_commit_team_action(ctx, channel, action)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());
  {
    int64_t new_sequence = channel->get_last_message_sequence();
    bool caught_up_saw = false;
    for (const auto& record : env.wal_event_batches()) {
      for (const auto& subscriber_key : record.batch.subscriber_keys()) {
        // 过期订阅者被跳过(GC)，不进入任何投递批次
        CASE_EXPECT_NE(kStaleKey, subscriber_key);
        if (subscriber_key == kCaughtUpKey) {
          caught_up_saw = true;
          CASE_EXPECT_FALSE(record.batch.has_channel_snapshot());
          for (const auto& message : record.batch.channel_message()) {
            CASE_EXPECT_GT(message.sequence(), boundary);
          }
        }
      }
    }
    CASE_EXPECT_TRUE(caught_up_saw);
    CASE_EXPECT_GT(new_sequence, boundary);
  }

  // GC 后的过期订阅者可重新订阅恢复: 其 checkpoint(=boundary)未落后于 last_removed，
  // 按增量连续补齐 checkpoint 之后的全部剩余日志(u5/u6)，状态不缺口、无需快照兜底
  env.wal_clear_event_batches();
  CASE_EXPECT_EQ(0, env.run("stale_recovery", [&](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.wal_resubscribe_as(ctx, channel, kStaleKey, boundary,
                                                                                boundary_hash, false)));
                 }));
  CASE_EXPECT_EQ(0, env.wal_converge());
  {
    std::vector<int64_t> delivered;
    for (const auto& record : env.wal_event_batches()) {
      bool for_stale = false;
      for (const auto& subscriber_key : record.batch.subscriber_keys()) {
        if (subscriber_key == kStaleKey) {
          for_stale = true;
          break;
        }
      }
      if (!for_stale) {
        continue;
      }
      CASE_EXPECT_FALSE(record.batch.has_channel_snapshot());
      for (const auto& message : record.batch.channel_message()) {
        CASE_EXPECT_GT(message.sequence(), boundary);
        delivered.push_back(message.sequence());
      }
    }
    CASE_EXPECT_FALSE(delivered.empty());
    // 增量覆盖 (boundary, last] 的全部剩余日志
    for (const auto& log : channel->get_shared_wal_object()->get_all_logs()) {
      if (log->sequence() <= boundary) {
        continue;
      }
      bool found = false;
      for (int64_t sequence : delivered) {
        if (sequence == log->sequence()) {
          found = true;
          break;
        }
      }
      if (!found) {
        CASE_EXPECT_TRUE(found);
        break;
      }
    }
  }

  // 房间真实订阅者终态完整(持续增量应用)
  for (uint64_t user_id = 9201; user_id <= 9206; ++user_id) {
    CASE_EXPECT_TRUE(nullptr != room->find_member(teamsvr_room_test::make_user_key(1, user_id), false));
  }

  CASE_EXPECT_EQ(0, env.stop());
}

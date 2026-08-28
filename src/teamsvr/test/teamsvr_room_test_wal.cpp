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
// sequence < compact_sequence 的日志被物理移除、== compact_sequence 的日志保留(与 fake journal
// 的闭区间裁剪不同)；恢复房间的压缩边界/成员/共享数据与权威频道一致
CASE_TEST(teamsvr_room_wal, compact_boundary_and_replay_start) {
  room_test_env env;
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

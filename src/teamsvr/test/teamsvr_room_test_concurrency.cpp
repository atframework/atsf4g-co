// Copyright 2026 atframework
//
// teamsvr-room 并发/故障窗口用例(TEAM_ROOM_TEST_PLAN.md §4.7 CON/FLT 与 §4.5 LCK-07 乱序回包)。
// 响应挂起用 response_gate_t(custom_wait/custom_resume 确定性放行)，一次性坏包用
// inject_*_response_fault_once(mock_ss preempt 语义)，均不依赖真实 sleep 或固定 pump 次数。

#include "teamsvr_room_test_common.h"  // NOLINT: build/include_subdir

#include <string>
#include <vector>

namespace {
using teamsvr_room_test::fake_team_room_channel;
using teamsvr_room_test::global_now_offset_guard;
using teamsvr_room_test::make_foreign_lock;
using teamsvr_room_test::make_personal_channel;
using teamsvr_room_test::make_team_key;
using teamsvr_room_test::make_user_key;
using teamsvr_room_test::next_test_team_id;
using teamsvr_room_test::response_fault_kind;
using teamsvr_room_test::room_test_cfg_values;
using teamsvr_room_test::room_test_env;
using teamsvr_room_test::self_lock_holder;
using teamsvr_room_test::setup_standard_team;
using teamsvr_room_test::standard_team_members;

// 写 N 个 member_update 事件(与 recovery 用例同形的本地 helper，避免跨文件耦合)
bool write_member_updates(room_test_env& env, const team_room::ptr_t& room,
                          const PROJECT_NAMESPACE_ID::DUserIDKey& key, gsl::string_view prefix, int count) {
  for (int i = 0; i < count; ++i) {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), key);
    update->set_client_version(std::string(prefix) + std::to_string(i));
    int32_t ret = env.run("write_log", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
    if (0 != ret) {
      return false;
    }
  }
  return 0 == env.sync(room->get_team_key());
}

// 只驱动订阅者心跳与房间时间轮(启动定时任务后不等待其退出)，供挂起门场景手动交替
int32_t drive_ticks_no_wait(room_test_env& env) {
  return env.run("tick_no_wait", [](rpc::context& ctx) -> rpc::result_code_type {
    rpc::dtmq::client_subscriber::global_tick(ctx);
    team_room_manager::me()->tick(ctx);
    RPC_RETURN_CODE(0);
  });
}

bool wait_maintenance_exit(room_test_env& env) {
  return env.wait_for([]() { return !team_room_manager::me()->debug_has_running_maintenance_task(); });
}

// 构造 add_invitation 请求(本文件用例所需的最小字段)
atfw::team::SSTeamRoomAddInvitationReq make_invitation_req(const PROJECT_NAMESPACE_ID::DUserIDKey& inviter,
                                                           const PROJECT_NAMESPACE_ID::DUserIDKey& invitee,
                                                           int64_t source_type =
                                                               atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND) {
  atfw::team::SSTeamRoomAddInvitationReq req;
  protobuf_copy_message(*req.mutable_sender_user_key(), inviter);
  auto* invitation = req.mutable_invitation();
  protobuf_copy_message(*invitation->mutable_inviter(), inviter);
  protobuf_copy_message(*invitation->mutable_invitee(), invitee);
  protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
  invitation->set_team_source_type(static_cast<atfw::team::EnTeamSourceType>(source_type));
  return req;
}

atfw::team::SSTeamRoomApproveInvitationReq make_approve_invitation_req(
    const PROJECT_NAMESPACE_ID::DUserIDKey& invitee) {
  atfw::team::SSTeamRoomApproveInvitationReq req;
  protobuf_copy_message(*req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*req.mutable_invitee(), invitee);
  req.set_client_version("ut-con-approve");
  req.set_user_router_server_id(0x64000000 + invitee.user_id());
  return req;
}

atfw::team::SSTeamRoomRejectInvitationReq make_reject_invitation_req(const PROJECT_NAMESPACE_ID::DUserIDKey& sender,
                                                                     const PROJECT_NAMESPACE_ID::DUserIDKey& invitee) {
  atfw::team::SSTeamRoomRejectInvitationReq req;
  protobuf_copy_message(*req.mutable_sender_user_key(), sender);
  protobuf_copy_message(*req.mutable_invitee(), invitee);
  return req;
}

int32_t send_heartbeat(room_test_env& env, const team_room::ptr_t& room, const PROJECT_NAMESPACE_ID::DUserIDKey& key,
                       uint64_t router_id) {
  return env.run("heartbeat", [room, &key, router_id](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomHeartbeatReq hb;
    protobuf_copy_message(*hb.mutable_user_key(), key);
    hb.set_user_router_server_id(router_id);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
  });
}

// journal 条目 (action_case, sequence)，供按成员收集与顺序比对
struct journal_action_entry {
  atfw::team::DTeamAction::ActionCase action_case;
  int64_t sequence;
};
std::vector<journal_action_entry> collect_journal_actions(const fake_team_room_channel& fake, uint64_t user_id) {
  std::vector<journal_action_entry> ret;
  fake.foreach_team_action(
      [&ret, user_id](const atfw::dtmq::DChannelMessage& message, const atfw::team::DTeamAction& action) {
    uint64_t action_user = 0;
    switch (action.action_case()) {
      case atfw::team::DTeamAction::kAddMember:
        action_user = action.add_member().user_key().user_id();
        break;
      case atfw::team::DTeamAction::kRemoveMember:
        action_user = action.remove_member().user_key().user_id();
        break;
      case atfw::team::DTeamAction::kMemberUpdate:
        action_user = action.member_update().user_key().user_id();
        break;
      case atfw::team::DTeamAction::kAddInvitation:
        action_user = action.add_invitation().invitee().user_id();
        break;
      case atfw::team::DTeamAction::kApproveInvitation:
        action_user = action.approve_invitation().invitee().user_id();
        break;
      case atfw::team::DTeamAction::kRejectInvitation:
        action_user = action.reject_invitation().invitee().user_id();
        break;
      case atfw::team::DTeamAction::kElectionCaptain:
        action_user = action.election_captain().user_key().user_id();
        break;
      default:
        break;
    }
    if (0 == user_id || action_user == user_id) {
      ret.push_back({action.action_case(), message.sequence()});
    }
    return true;
  });
  return ret;
}

size_t count_personal_actions(const room_test_env& env, const atfw::dtmq::DChannelIdKey& channel,
                              atfw::team::DTeamMemberAction::ActionCase action_case) {
  size_t count = 0;
  for (const auto& record : env.personal_messages()) {
    if (record.channel.channel_id() == channel.channel_id() && record.action.action_case() == action_case) {
      ++count;
    }
  }
  return count;
}

// 清空房间后从 journal 重建并返回(模拟重启恢复)
team_room::ptr_t recover_room(room_test_env& env, int64_t team_id) {
  room_test_env::clear_rooms();
  return env.setup_ready_room(team_id);
}
}  // namespace

// ============ CON-01: 并发 create 与挂起 update 窗口并发 -> 前置占位; 重试后占位恢复成功 ============
CASE_TEST(teamsvr_room_concurrency, concurrent_create_placeholder_blocks_duplicate) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // ---- 场景 A: 首个 create 的 update 已提交但响应挂起，第二个 create 被前置占位拒绝、零写入 ----
  int64_t team_id = next_test_team_id();
  auto owner_key = make_user_key(1, 9101);
  auto owner_channel = make_personal_channel(9101);

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);
  CASE_EXPECT_EQ(0u, fake.update_calls());

  env.update_response_gate.armed = true;
  auto task_a = env.runtime().run_task(
      "create_a", std::chrono::seconds{8},
      [room, team_id, &owner_key, &owner_channel](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomCreateReq req;
        protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
        protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
        protobuf_copy_message(*req.mutable_sender_user_channel(), owner_channel);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
      });
  CASE_EXPECT_FALSE(task_a.empty());
  if (task_a.empty()) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 首个 create 的原始 update 已在途，等待其响应挂起(create 流程仍在进行)
  CASE_EXPECT_TRUE(env.wait_for(
      [&env, &fake]() { return fake.update_calls() >= 1 && room_test_env::gate_parked(env.update_response_gate); }));

  // 第二个 create 与挂起 update 窗口并发: 前置占位 -> no permission、零写入
  size_t personal_before = env.personal_message_count();
  auto other_key = make_user_key(1, 9102);
  int32_t ret_b = env.run("create_b", [room, team_id, &other_key](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomCreateReq req;
    protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req.mutable_sender_user_key(), other_key);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
  });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, ret_b);
  CASE_EXPECT_EQ(1u, fake.update_calls());
  CASE_EXPECT_EQ(0u, fake.send_message_calls());
  CASE_EXPECT_EQ(0u, fake.reset_lock_calls());
  CASE_EXPECT_EQ(0u, fake.destroy_calls());
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  // 释放挂起响应: 首个 create 恢复正常
  CASE_EXPECT_TRUE(room_test_env::release_gate(env.update_response_gate));
  auto result_a = env.runtime().wait(task_a, std::chrono::seconds{15});
  CASE_EXPECT_TRUE(result_a.task_exited && !result_a.hard_timed_out);
  CASE_EXPECT_EQ(0, result_a.result_code);

  // 仅一次原始 update; 角色为 OWNER 并持久化; 无额外事件(占位已恢复)
  CASE_EXPECT_EQ(1u, fake.update_calls());
  CASE_EXPECT_TRUE(room->is_lock_holder());
  CASE_EXPECT_EQ(0u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));
  CASE_EXPECT_EQ(1u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kUpdateCustomData));
  auto owner = room->find_member(owner_key, false);
  CASE_EXPECT_TRUE(!!owner);
  if (owner) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, owner->member_data.role());
  }
  // 前置未被第二个 create 污染: 恢复后仍只有一条 no permission
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 env.run("create_after_success",
                         [room, team_id, &other_key](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::SSTeamRoomCreateReq req;
                   protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
                   protobuf_copy_message(*req.mutable_sender_user_key(), other_key);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
                 }));
  CASE_EXPECT_EQ(1u, fake.update_calls());

  // ---- 场景 B: 首个 create 未持锁且响应挂起 -> 前置回滚; 重试恢复成功 ----
  int64_t team_b = next_test_team_id();
  auto& fake_b = env.channel(team_b);
  fake_b.ensure_created();
  protobuf_copy_message(fake_b.mutable_lock(), make_foreign_lock("teamsvr-room:other-node", 3600));
  auto owner_b = make_user_key(1, 9103);

  team_room::ptr_t room_b = env.setup_ready_room(team_b);
  CASE_EXPECT_TRUE(!!room_b);
  if (!room_b) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_FALSE(room_b->is_lock_holder());

  // 模拟外部抢锁竞争: create 的抢锁 RPC 前置冲突(FIX-07), 前置回滚
  int32_t conflict_ret = env.run("create_lock_conflict",
                                 [room_b, team_b, &owner_b](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomCreateReq req;
    protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_b));
    protobuf_copy_message(*req.mutable_sender_user_key(), owner_b);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room_b->create_team(ctx, req)));
  });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED, conflict_ret);
  CASE_EXPECT_EQ(0u, fake_b.update_calls());
  CASE_EXPECT_EQ(0u, fake_b.reset_lock_calls());

  // 冲突后重试 create: CAS 按持有者恢复成功(前置已回滚, 不再占用锁)
  {
    global_now_offset_guard guard(std::chrono::seconds{3601});
    int32_t retry_ret = env.run("create_retry_after_expiry",
                                [room_b, team_b, &owner_b](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::SSTeamRoomCreateReq req;
      protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_b));
      protobuf_copy_message(*req.mutable_sender_user_key(), owner_b);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room_b->create_team(ctx, req)));
    });
    CASE_EXPECT_EQ(0, retry_ret);
  }
  CASE_EXPECT_EQ(1u, fake_b.update_calls());
  CASE_EXPECT_TRUE(room_b->is_lock_holder());
  CASE_EXPECT_TRUE(room_b->find_member(owner_b, false) != nullptr);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CON-05: compact update 挂起窗口写入新 action -> 重启后不丢失, 回滚不覆盖新日志 ============
CASE_TEST(teamsvr_room_concurrency, compact_update_suspended_keeps_new_logs) {
  // 场景参数(keep 0/0)，快照保留窗口 = start/2 = 5s: T0 快照在 T0+6 触发清理
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

  CASE_EXPECT_TRUE(write_member_updates(env, room, members.normal, "suspend-w0-v", 3));
  const int64_t pre_maintenance_last = fake.last_sequence();

  env.update_response_gate.armed = true;
  {
    // 推进时间 6s(心跳 10s/保留 5s 窗口): T0 快照已超出保留期
    global_now_offset_guard guard(std::chrono::seconds{6});
    CASE_EXPECT_EQ(0, drive_ticks_no_wait(env));
    // 等待清理的 compact update 已提交但响应挂起
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake]() {
      return !fake.update_requests().empty() && fake.update_requests().back().request.compact_sequence() > 0 &&
             room_test_env::gate_parked(env.update_response_gate);
    }));

    // 检查挂起中的清理请求内容(快照边界)
    const auto& parked_request = fake.update_requests().back().request;
    const int64_t compact_boundary = parked_request.compact_sequence();
    CASE_EXPECT_GT(compact_boundary, 0);
    CASE_EXPECT_LE(compact_boundary, pre_maintenance_last);
    atfw::team::DTeamStorage parked_snapshot;
    CASE_EXPECT_TRUE(parked_request.custom_data().UnpackTo(&parked_snapshot));
    CASE_EXPECT_EQ(pre_maintenance_last, parked_snapshot.saved_action_sequence());

    // 挂起窗口写入新 action(late_write)，应成功
    int32_t late_ret = env.run("late_write", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      action.mutable_member_update()->set_client_version("late-write");
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
    CASE_EXPECT_EQ(0, late_ret);
    // 事件回放(sync): 新事件已入 journal
    CASE_EXPECT_EQ(0, env.sync(team_id));
    auto normal = room->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!normal);
    if (normal) {
      CASE_EXPECT_EQ("late-write", normal->member_data.client_version());
    }

    // 释放挂起的 update 响应
    CASE_EXPECT_TRUE(room_test_env::release_gate(env.update_response_gate));
    CASE_EXPECT_TRUE(wait_maintenance_exit(env));

    // 收集新日志 sequence: 继续递增且不覆盖已 saved 的快照
    int64_t late_sequence = 0;
    fake.foreach_team_action(
        [&late_sequence](const atfw::dtmq::DChannelMessage& message, const atfw::team::DTeamAction& action) {
      if (action.has_member_update() && action.member_update().client_version() == "late-write") {
        late_sequence = message.sequence();
        return false;
      }
      return true;
    });
    CASE_EXPECT_GT(late_sequence, 0);
    CASE_EXPECT_GT(late_sequence, compact_boundary);
    CASE_EXPECT_GT(late_sequence, pre_maintenance_last);
    CASE_EXPECT_EQ(compact_boundary, fake.last_removed_sequence());
    CASE_EXPECT_FALSE(fake.journal().empty());
    if (!fake.journal().empty()) {
      CASE_EXPECT_EQ(fake.last_removed_sequence(), fake.journal().front().sequence());
    }

    // 校验 compact 边界: 挂起的清理未丢失, 新日志未被覆盖
    CASE_EXPECT_EQ(compact_boundary, room->debug_last_compact_sequence());
    CASE_EXPECT_EQ(pre_maintenance_last, room->debug_saved_action_sequence());
    CASE_EXPECT_GE(room->debug_acknowledge_action_sequence(), late_sequence);
    CASE_EXPECT_TRUE(room->is_lock_holder());
  }

  // 恢复后内容: 事件 + 快照回滚不覆盖挂起窗口的新日志
  room_test_env::clear_rooms();
  team_room::ptr_t recovered = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!recovered);
  if (recovered) {
    auto normal = recovered->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!normal);
    if (normal) {
      CASE_EXPECT_EQ("late-write", normal->member_data.client_version());
    }
    CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) != nullptr);
    CASE_EXPECT_TRUE(recovered->find_member(members.admin, false) != nullptr);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LCK-07/FLT-04(partial): 续租 update 响应乱序(迟到的旧响应 send 返回) -> 恢复且不降级 ============
CASE_TEST(teamsvr_room_concurrency, renew_response_out_of_order_no_regress) {
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
  CASE_EXPECT_TRUE(room->is_lock_holder());

  atfw::dtmq::DChannelOptimisticLock initial_lock;
  protobuf_copy_message(initial_lock, fake.lock());
  const size_t updates_before = fake.update_requests().size();

  env.update_response_gate.armed = true;
  {
    // 推进时间 6s(心跳 10s, 续租间隔 5s)
    global_now_offset_guard guard(std::chrono::seconds{6});
    CASE_EXPECT_EQ(0, drive_ticks_no_wait(env));
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, updates_before]() {
      return fake.update_requests().size() > updates_before && room_test_env::gate_parked(env.update_response_gate);
    }));

    // 挂起的是续租 update(心跳/续租轮触发)，检查其请求
    const auto& renew_request = fake.update_requests().back().request;
    CASE_EXPECT_FALSE(renew_request.save());
    CASE_EXPECT_FALSE(renew_request.has_custom_data());
    CASE_EXPECT_EQ(0, renew_request.compact_sequence());
    CASE_EXPECT_TRUE(renew_request.has_compare_and_maybe_reset_lock());
    atfw::dtmq::DChannelOptimisticLock renewed_lock;
    protobuf_copy_message(renewed_lock, renew_request.compare_and_maybe_reset_lock().reset_value());
    CASE_EXPECT_EQ(self_lock_holder(), renewed_lock.lock_holder());
    CASE_EXPECT_GT(renewed_lock.timeout().seconds(), initial_lock.timeout().seconds());
    // 在途锁已续期(持有锁不变)，仅刷新本地状态
    CASE_EXPECT_EQ(renewed_lock.timeout().seconds(), fake.lock().timeout().seconds());

    // 挂起的续租响应仍持锁期间 send: 应正常返回
    int32_t send_ret = env.run("send_during_held_renew", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      action.mutable_member_update()->set_client_version("during-held-renew");
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
    CASE_EXPECT_EQ(0, send_ret);
    CASE_EXPECT_TRUE(room->is_lock_holder());

    // 释放挂起的续租响应: 不降级
    CASE_EXPECT_TRUE(room_test_env::release_gate(env.update_response_gate));
    CASE_EXPECT_TRUE(wait_maintenance_exit(env));
    CASE_EXPECT_EQ(0, env.sync(team_id));

    CASE_EXPECT_TRUE(room->is_lock_holder());
    // 在途锁仍是同一次持有(乱序响应不撤销续租)
    CASE_EXPECT_EQ(renewed_lock.timeout().seconds(), fake.lock().timeout().seconds());
    // 乱序响应后成员数据仍正常
    auto normal = room->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!normal);
    if (normal) {
      CASE_EXPECT_EQ("during-held-renew", normal->member_data.client_version());
    }

    // 推进下一轮心跳窗口
    global_now_offset_guard::advance(std::chrono::seconds{6});
    const size_t renews_before = fake.update_requests().size();
    env.drive_timer_ticks();
    CASE_EXPECT_GT(fake.update_requests().size(), renews_before);
    CASE_EXPECT_TRUE(room->is_lock_holder());
  }

  // 收集各续租 update 请求的重置超时供比对
  std::chrono::system_clock::time_point last_reset_timeout;
  for (const auto& record : fake.update_requests()) {
    if (!record.request.has_compare_and_maybe_reset_lock() ||
        !record.request.compare_and_maybe_reset_lock().has_reset_value()) {
      continue;
    }
    auto reset_timeout =
        protobuf_to_system_clock(record.request.compare_and_maybe_reset_lock().reset_value().timeout());
    if (last_reset_timeout != std::chrono::system_clock::time_point{}) {
      CASE_EXPECT_GE(reset_timeout, last_reset_timeout);
    }
    last_reset_timeout = reset_timeout;
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ FLT-02: reset_lock 响应坏包(错 type_url)与恢复并发 -> 不误报主控成功 ============
CASE_TEST(teamsvr_room_fault, malformed_reset_lock_response_no_false_master) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto& fake = env.channel(team_id);
  fake.ensure_created();

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 构造竞争方持有的锁(模拟锁被抢占)
  protobuf_copy_message(fake.mutable_lock(), make_foreign_lock("teamsvr-room:competitor", 3600));

  // 注入一次性坏包: reset_lock 响应 type_url 错误
  auto fault_rule = env.inject_reset_lock_response_fault_once(response_fault_kind::kMalformedTypeUrl);
  CASE_EXPECT_TRUE(!!fault_rule);
  if (!fault_rule) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 推进清理轮: 锁仍处于被抢占状态(清理仅刷新本地视图)
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(1u, fake.reset_lock_calls());
  CASE_EXPECT_FALSE(room->is_lock_holder());
  CASE_EXPECT_EQ("teamsvr-room:competitor", fake.lock().lock_holder());
  CASE_EXPECT_EQ(0u, fake.send_message_calls());
  CASE_EXPECT_EQ(0u, fake.update_calls());
  // 未误判主控: 没有追加 kResetLock 日志
  CASE_EXPECT_EQ(0u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kResetLock));

  // 最终锁由竞争者持有(一次性故障已消费)
  {
    global_now_offset_guard guard(std::chrono::seconds{3601});
    env.drive_timer_ticks();
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());
  CASE_EXPECT_EQ(self_lock_holder(), fake.lock().lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ FLT-02: create update 响应坏包(回包解析失败) -> 不降级; 同族恢复 ============
CASE_TEST(teamsvr_room_fault, malformed_update_body_fails_visibly) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner_key = make_user_key(1, 9301);
  auto owner_channel = make_personal_channel(9301);

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);

  auto fault_rule = env.inject_update_response_fault_once(response_fault_kind::kMalformedBody);
  CASE_EXPECT_TRUE(!!fault_rule);
  if (!fault_rule) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 坏响应体必须表现为失败(EN_SYS_PACK)，不能被当成成功
  int32_t first_ret = env.run("create_malformed_body",
                              [room, team_id, &owner_key, &owner_channel](
                                  rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomCreateReq req;
    protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
    protobuf_copy_message(*req.mutable_sender_user_channel(), owner_channel);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
  });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK, first_ret);
  // 服务端已提交初始快照(与"已提交但响应失败"同族)
  CASE_EXPECT_EQ(1u, fake.update_calls());
  CASE_EXPECT_FALSE(fake.custom_data().type_url().empty());
  // 失败可见且占位回滚: 房间未进入主控/已建状态
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 事件回放后订阅者视图看到已提交的本地节点与初始快照; 重试 create 的 CAS 按持有者返回成功
  CASE_EXPECT_EQ(0, env.sync(team_id));
  int32_t retry_ret = env.run("create_retry",
                              [room, team_id, &owner_key, &owner_channel](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomCreateReq req;
    protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
    protobuf_copy_message(*req.mutable_sender_user_channel(), owner_channel);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
  });
  CASE_EXPECT_EQ(0, retry_ret);
  CASE_EXPECT_TRUE(room->is_lock_holder());
  auto owner = room->find_member(owner_key, false);
  CASE_EXPECT_TRUE(!!owner);
  if (owner) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, owner->member_data.role());
  }
  // 共两次 update(首次快照 + 重试)，恢复成功
  CASE_EXPECT_EQ(2u, fake.update_calls());
  CASE_EXPECT_EQ(0u, fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CON-02: 同一 invitee 的 approve/reject/refresh/expiry 两两并发 -> 恰一处生效,
// 此后 admission 收敛 ============
CASE_TEST(teamsvr_room_concurrency, admission_pairwise_races_converge) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // ---- 场景 A: approve 响应挂起, reject 在途进入 -> 两者按序生效, 此后 admission ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    auto invitee = members.outsider;

    auto invite_req = make_invitation_req(members.owner, invitee);
    CASE_EXPECT_EQ(0, env.run("invite_a", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
    }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    const size_t sends_after_invite = fake.send_message_calls();

    // approve 响应(快照已写)挂起
    env.send_message_response_gate.armed = true;
    auto approve_req = make_approve_invitation_req(invitee);
    auto approve_task = env.runtime().run_task(
        "approve_a", std::chrono::seconds{8}, [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
        });
    CASE_EXPECT_FALSE(approve_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_after_invite]() {
      return fake.send_message_calls() > sends_after_invite &&
             room_test_env::gate_parked(env.send_message_response_gate);
    }));

    // approve 在途窗口 reject 进入: 原有 pending 仍在(事件未回放), reject 恢复处理
    auto reject_req = make_reject_invitation_req(invitee, invitee);
    CASE_EXPECT_EQ(0, env.run("reject_a", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, reject_req)));
    }));

    CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
    auto approve_result = env.runtime().wait(approve_task, std::chrono::seconds{15});
    CASE_EXPECT_TRUE(approve_result.task_exited && !approve_result.hard_timed_out);
    CASE_EXPECT_EQ(0, approve_result.result_code);
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // 收集 invitee 相关 journal: 顺序 add_member/approve 先于 reject
    auto journal = collect_journal_actions(fake, invitee.user_id());
    std::vector<atfw::team::DTeamAction::ActionCase> kinds;
    kinds.reserve(journal.size());
    for (const auto& entry : journal) {
      kinds.push_back(entry.action_case);
    }
    CASE_EXPECT_TRUE((std::vector<atfw::team::DTeamAction::ActionCase>{
                         atfw::team::DTeamAction::kAddInvitation, atfw::team::DTeamAction::kAddMember,
                         atfw::team::DTeamAction::kApproveInvitation,
                         atfw::team::DTeamAction::kRejectInvitation}) == kinds);

    // 结论: 恰一处生效; approve 已消费 admission, reject 事件仍可操作 -> 此后 admission
    CASE_EXPECT_TRUE(room->find_member(invitee, false) != nullptr);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                   env.run("approve_again_a", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                   }));
    // 个人通道恰一条 joined 通知(无 reject 通知)
    CASE_EXPECT_EQ(1u, count_personal_actions(env, make_personal_channel(invitee.user_id()),
                                              atfw::team::DTeamMemberAction::kJoinedTeam));
    CASE_EXPECT_EQ(0u, count_personal_actions(env, make_personal_channel(invitee.user_id()),
                                              atfw::team::DTeamMemberAction::kRejectInvitation));

    // 恢复后内容: 从 journal 重建看到同一成员
    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_TRUE(recovered->find_member(invitee, false) != nullptr);
      CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) != nullptr);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                     env.run("approve_recovered_a",
                             [recovered, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(recovered->approve_invitation(ctx, approve_req)));
                     }));
    }
  }

  // ---- 场景 B: approve 挂起窗口 refresh(重新邀请)进入 -> 旧事件确立后, 重复刷新被拒绝 ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    auto invitee = members.outsider;

    auto invite_req = make_invitation_req(members.owner, invitee);
    CASE_EXPECT_EQ(0, env.run("invite_b", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
    }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    const size_t sends_after_invite = fake.send_message_calls();

    env.send_message_response_gate.armed = true;
    auto approve_req = make_approve_invitation_req(invitee);
    auto approve_task = env.runtime().run_task(
        "approve_b", std::chrono::seconds{8}, [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
        });
    CASE_EXPECT_FALSE(approve_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_after_invite]() {
      return fake.send_message_calls() > sends_after_invite &&
             room_test_env::gate_parked(env.send_message_response_gate);
    }));

    // approve 在途窗口 refresh: 原有 pending 仍在, 重复刷新即又一次 add_invitation(来源 MATCH)
    auto refresh_req = make_invitation_req(members.owner, invitee, atfw::team::EN_TEAM_SOURCE_TYPE_MATCH);
    CASE_EXPECT_EQ(0, env.run("refresh_b", [room, &refresh_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, refresh_req)));
    }));

    CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
    auto approve_result = env.runtime().wait(approve_task, std::chrono::seconds{15});
    CASE_EXPECT_TRUE(approve_result.task_exited && !approve_result.hard_timed_out);
    CASE_EXPECT_EQ(0, approve_result.result_code);
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // 收集 journal: 校验刷新被拒、无重复 admission
    auto journal = collect_journal_actions(fake, invitee.user_id());
    std::vector<atfw::team::DTeamAction::ActionCase> kinds;
    kinds.reserve(journal.size());
    for (const auto& entry : journal) {
      kinds.push_back(entry.action_case);
    }
    CASE_EXPECT_TRUE((std::vector<atfw::team::DTeamAction::ActionCase>{
                         atfw::team::DTeamAction::kAddInvitation, atfw::team::DTeamAction::kAddMember,
                         atfw::team::DTeamAction::kApproveInvitation,
                         atfw::team::DTeamAction::kAddInvitation}) == kinds);
    CASE_EXPECT_TRUE(room->find_member(invitee, false) != nullptr);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                   env.run("approve_again_b", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                   }));

    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_TRUE(recovered->find_member(invitee, false) != nullptr);
    }
  }

  // ---- 场景 C1: approve 在途窗口 admission 到期被清理 -> 已消费入口仍生效, 此后 admission ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    auto invitee = members.outsider;

    auto invite_req = make_invitation_req(members.owner, invitee);
    CASE_EXPECT_EQ(0, env.run("invite_c", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
    }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    const size_t sends_after_invite = fake.send_message_calls();

    env.send_message_response_gate.armed = true;
    auto approve_req = make_approve_invitation_req(invitee);
    auto approve_task = env.runtime().run_task(
        "approve_c", std::chrono::seconds{8}, [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
        });
    CASE_EXPECT_FALSE(approve_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_after_invite]() {
      return fake.send_message_calls() > sends_after_invite &&
             room_test_env::gate_parked(env.send_message_response_gate);
    }));

    {
      // 推进时间 6s(邀请 5s 到期): 触发过期 admission 清理
      global_now_offset_guard guard(std::chrono::seconds{6});
      env.drive_timer_ticks();
      // 清理后 approve 响应仍挂起; 释放已提交的 add_member/approve
      CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
      auto approve_result = env.runtime().wait(approve_task, std::chrono::seconds{15});
      CASE_EXPECT_TRUE(approve_result.task_exited && !approve_result.hard_timed_out);
      CASE_EXPECT_EQ(0, approve_result.result_code);
      CASE_EXPECT_EQ(0, env.sync(team_id));

      // 结论: approve 已消费 admission, 过期清理不回收已入团成员
      CASE_EXPECT_TRUE(room->find_member(invitee, false) != nullptr);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                     env.run("approve_again_c", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                     }));
    }

    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_TRUE(recovered->find_member(invitee, false) != nullptr);
    }
  }

  // ---- 场景 C2: admission 先到期被清理, approve 回包后 -> not-found 零写入 ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    auto invitee = members.outsider;

    auto invite_req = make_invitation_req(members.owner, invitee);
    CASE_EXPECT_EQ(0, env.run("invite_c2", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
    }));
    CASE_EXPECT_EQ(0, env.sync(team_id));

    {
      global_now_offset_guard guard(std::chrono::seconds{6});
      env.drive_timer_ticks();
      const size_t sends_before = fake.send_message_calls();
      auto approve_req = make_approve_invitation_req(invitee);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                     env.run("approve_expired_c2", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                     }));
      CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
      CASE_EXPECT_TRUE(room->find_member(invitee, false) == nullptr);
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CON-03: remove-member 与 heartbeat/member-update 并发; 在途 re-add 被拒, 回放后 re-add 成功 ============
CASE_TEST(teamsvr_room_concurrency, remove_member_races_heartbeat_update_and_readd) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // ---- 场景 A: remove 响应挂起, heartbeat/member_update 在途进入 ----
  // 已调度的删除可被后续 touch 消费; member_update 应被拒(确认 remove)
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    const size_t sends_before = fake.send_message_calls();

    env.send_message_response_gate.armed = true;
    auto remove_task = env.runtime().run_task(
        "remove_normal", std::chrono::seconds{8}, [room, &members](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::DTeamAction action;
          protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), members.normal);
          action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
        });
    CASE_EXPECT_FALSE(remove_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_before]() {
      return fake.send_message_calls() > sends_before && room_test_env::gate_parked(env.send_message_response_gate);
    }));
    // remove 在途: 已调度一次删除重试标记
    CASE_EXPECT_EQ(1u, room->debug_retry_remove_count());
    CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);

    // heartbeat 在途进入: 成功且消费删除重试标记
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.normal, 0x1234));
    CASE_EXPECT_EQ(1u, fake.send_message_calls() - sends_before);

    // member_update 在途进入: 成员仍在, 不重复调度
    CASE_EXPECT_EQ(0, env.run("update_normal", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      action.mutable_member_update()->set_client_version("in-flight-update");
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));

    CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
    auto remove_result = env.runtime().wait(remove_task, std::chrono::seconds{15});
    CASE_EXPECT_TRUE(remove_result.task_exited && !remove_result.hard_timed_out);
    CASE_EXPECT_EQ(0, remove_result.result_code);
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // 结论: 成员被删除; 迟到的 member_update 被拒(find_member 为空), 删除恢复
    CASE_EXPECT_TRUE(room->find_member(members.normal, false) == nullptr);
    CASE_EXPECT_EQ(0u, room->debug_retry_remove_count());
    auto journal = collect_journal_actions(fake, members.normal.user_id());
    std::vector<atfw::team::DTeamAction::ActionCase> kinds;
    for (const auto& entry : journal) {
      if (entry.action_case != atfw::team::DTeamAction::kAddMember) {
        kinds.push_back(entry.action_case);
      }
    }
    CASE_EXPECT_TRUE((std::vector<atfw::team::DTeamAction::ActionCase>{atfw::team::DTeamAction::kRemoveMember,
                                                                       atfw::team::DTeamAction::kMemberUpdate}) ==
                                                                   kinds);

    // 删除后心跳: member-not-found; 此后心跳恢复
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND,
                   send_heartbeat(env, room, members.normal, 0x1234));
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_TRUE(room->find_member(members.normal, false) == nullptr);

    // 恢复后内容
    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_TRUE(recovered->find_member(members.normal, false) == nullptr);
      CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) != nullptr);
    }
  }

  // ---- 场景 B: remove 在途 re-add 被拒(action 层: 成员仍在), 回放后 re-add 成功且状态已清空 ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    const size_t sends_before = fake.send_message_calls();

    env.send_message_response_gate.armed = true;
    auto remove_task = env.runtime().run_task(
        "remove_admin", std::chrono::seconds{8}, [room, &members](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::DTeamAction action;
          protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), members.admin);
          action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
        });
    CASE_EXPECT_FALSE(remove_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_before]() {
      return fake.send_message_calls() > sends_before && room_test_env::gate_parked(env.send_message_response_gate);
    }));

    // remove 在途窗口 re-add 同一成员: action 层检查时成员仍在 -> already-in-team 零写入
    // (与 task_action_send_message 一致: 走无 check_action_permission 的 send_action)
    const size_t sends_in_flight = fake.send_message_calls();
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM,
                   env.run("readd_in_flight", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
                     atfw::team::DTeamAction action;
                     auto* add_member = action.mutable_add_member();
                     protobuf_copy_message(*add_member->mutable_user_key(), members.admin);
                     protobuf_copy_message(*add_member->mutable_user_channel(), members.admin_channel);
                     add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                     int32_t ret = RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, members.owner, action));
                     if (0 != ret) {
                       RPC_RETURN_CODE(ret);
                     }
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                   }));
    CASE_EXPECT_EQ(sends_in_flight, fake.send_message_calls());

    CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
    auto remove_result = env.runtime().wait(remove_task, std::chrono::seconds{15});
    CASE_EXPECT_TRUE(remove_result.task_exited && !remove_result.hard_timed_out);
    CASE_EXPECT_EQ(0, remove_result.result_code);
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_TRUE(room->find_member(members.admin, false) == nullptr);
    CASE_EXPECT_EQ(0u, room->debug_retry_remove_count());

    // 回放后 re-add: 按序(remove 先于 add)重入成功, 状态已清空
    CASE_EXPECT_EQ(0, env.run("readd_after", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      auto* add_member = action.mutable_add_member();
      protobuf_copy_message(*add_member->mutable_user_key(), members.admin);
      protobuf_copy_message(*add_member->mutable_user_channel(), members.admin_channel);
      add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
      add_member->set_client_version("readd-v2");
      int32_t ret = RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, members.owner, action));
      if (0 != ret) {
        RPC_RETURN_CODE(ret);
      }
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    auto readded = room->find_member(members.admin, false);
    CASE_EXPECT_TRUE(!!readded);
    if (readded) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, readded->member_data.role());
      CASE_EXPECT_EQ("readd-v2", readded->member_data.client_version());
    }
    CASE_EXPECT_EQ(0u, room->debug_retry_remove_count());

    // 恢复后内容: 按序 remove->add 回放后保持新数据
    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      auto member = recovered->find_member(members.admin, false);
      CASE_EXPECT_TRUE(!!member);
      if (member) {
        CASE_EXPECT_EQ("readd-v2", member->member_data.client_version());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, member->member_data.role());
      }
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CON-04: 队长转让与退出/离线踢出并发 -> 恰一次选举日志且恢复等价 ============
CASE_TEST(teamsvr_room_concurrency, captain_transfer_races_exit_and_offline_kick) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // ---- 场景 A: 转让(election)响应挂起, 此后队长退出 -> 新队长生效, 转让被拒, 恢复等价 ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    const size_t sends_before = fake.send_message_calls();

    env.send_message_response_gate.armed = true;
    auto transfer_task = env.runtime().run_task(
        "transfer_a", std::chrono::seconds{8}, [room, &members](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::DTeamAction action;
          protobuf_copy_message(*action.mutable_election_captain()->mutable_user_key(), members.admin);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
        });
    CASE_EXPECT_FALSE(transfer_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_before]() {
      return fake.send_message_calls() > sends_before && room_test_env::gate_parked(env.send_message_response_gate);
    }));

    // 转让在途窗口队长退出(事件未回放, 原角色仍是 owner): remove 恢复处理
    CASE_EXPECT_EQ(0, env.run("exit_a", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), members.owner);
      action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));

    CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
    auto transfer_result = env.runtime().wait(transfer_task, std::chrono::seconds{15});
    CASE_EXPECT_TRUE(transfer_result.task_exited && !transfer_result.hard_timed_out);
    CASE_EXPECT_EQ(0, transfer_result.result_code);
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // 结论: admin 成为唯一队长(继承 OWNER), owner 已删除; election 先于 remove, 不会再选举
    CASE_EXPECT_TRUE(room->find_member(members.owner, false) == nullptr);
    auto admin = room->find_member(members.admin, false);
    CASE_EXPECT_TRUE(!!admin);
    if (admin) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, admin->member_data.role());
    }
    int election_logs = 0;
    fake.foreach_team_action(
        [&election_logs](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
          if (action.action_case() == atfw::team::DTeamAction::kElectionCaptain) {
            ++election_logs;
          }
          return true;
        });
    CASE_EXPECT_EQ(1, election_logs);

    // 恢复后内容
    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) == nullptr);
      auto rec_admin = recovered->find_member(members.admin, false);
      CASE_EXPECT_TRUE(!!rec_admin);
      if (rec_admin) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, rec_admin->member_data.role());
      }
    }
  }

  // ---- 场景 B: 队长退出先挂起(响应挂起), 转让后提交 -> 顺序 remove->election, 不会再选新队长 ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    const size_t sends_before = fake.send_message_calls();

    env.send_message_response_gate.armed = true;
    auto exit_task = env.runtime().run_task(
        "exit_b", std::chrono::seconds{8}, [room, &members](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::DTeamAction action;
          protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), members.owner);
          action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
        });
    CASE_EXPECT_FALSE(exit_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_before]() {
      return fake.send_message_calls() > sends_before && room_test_env::gate_parked(env.send_message_response_gate);
    }));

    // 退出在途窗口触发转让(事件未回放, 原角色仍是 owner): election 恢复处理
    CASE_EXPECT_EQ(0, env.run("transfer_b", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_election_captain()->mutable_user_key(), members.normal);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));

    CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
    auto exit_result = env.runtime().wait(exit_task, std::chrono::seconds{15});
    CASE_EXPECT_TRUE(exit_result.task_exited && !exit_result.hard_timed_out);
    CASE_EXPECT_EQ(0, exit_result.result_code);
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // 结论: 顺序 remove(owner)->election(normal); 转让事件在 flush 前已写, 不会再选举
    CASE_EXPECT_TRUE(room->find_member(members.owner, false) == nullptr);
    auto normal = room->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!normal);
    if (normal) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, normal->member_data.role());
    }
    auto admin = room->find_member(members.admin, false);
    CASE_EXPECT_TRUE(!!admin);
    if (admin) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, admin->member_data.role());
    }
    int election_logs = 0;
    fake.foreach_team_action(
        [&election_logs, &members](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
          if (action.action_case() == atfw::team::DTeamAction::kElectionCaptain) {
            ++election_logs;
            CASE_EXPECT_EQ(members.normal.user_id(), action.election_captain().user_key().user_id());
          }
          return true;
        });
    CASE_EXPECT_EQ(1, election_logs);

    // 回放(sync)后不再触发选举
    CASE_EXPECT_EQ(0, env.sync(team_id));
    election_logs = 0;
    fake.foreach_team_action(
        [&election_logs](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
          if (action.action_case() == atfw::team::DTeamAction::kElectionCaptain) {
            ++election_logs;
          }
          return true;
        });
    CASE_EXPECT_EQ(1, election_logs);

    // 恢复后内容
    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) == nullptr);
      auto rec_normal = recovered->find_member(members.normal, false);
      CASE_EXPECT_TRUE(!!rec_normal);
      if (rec_normal) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, rec_normal->member_data.role());
      }
    }
  }

  // ---- 场景 C: 转让在途窗口队长被离线踢出 -> 恰一次选举日志 ----
  {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    auto& fake = env.channel(team_id);
    const size_t sends_before = fake.send_message_calls();

    global_now_offset_guard guard(std::chrono::seconds{31});
    // admin/normal 心跳保持在线; owner(旧队长)静默超过离线期(30s)
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.admin, 0x1234));
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.normal, 0x1234));

    env.send_message_response_gate.armed = true;
    auto transfer_task = env.runtime().run_task(
        "transfer_c", std::chrono::seconds{8}, [room, &members](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::DTeamAction action;
          protobuf_copy_message(*action.mutable_election_captain()->mutable_user_key(), members.admin);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
        });
    CASE_EXPECT_FALSE(transfer_task.empty());
    CASE_EXPECT_TRUE(env.wait_for([&env, &fake, sends_before]() {
      return fake.send_message_calls() > sends_before && room_test_env::gate_parked(env.send_message_response_gate);
    }));

    // 转让在途窗口分轮推进时间轮: 旧队长离线到期被踢(每轮 2s, 让 now+1 到期的踢人定时器在下一轮触发并回环)
    for (int round = 0; round < 4; ++round) {
      global_now_offset_guard::advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }

    CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
    auto transfer_result = env.runtime().wait(transfer_task, std::chrono::seconds{15});
    CASE_EXPECT_TRUE(transfer_result.task_exited && !transfer_result.hard_timed_out);
    CASE_EXPECT_EQ(0, transfer_result.result_code);
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // 结论: owner 被离线踢出, admin 成为唯一队长
    CASE_EXPECT_TRUE(room->find_member(members.owner, false) == nullptr);
    auto admin = room->find_member(members.admin, false);
    CASE_EXPECT_TRUE(!!admin);
    if (admin) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, admin->member_data.role());
    }
    bool kick_seen = false;
    int election_logs = 0;
    fake.foreach_team_action(
        [&kick_seen, &election_logs, &members](const atfw::dtmq::DChannelMessage&,
                                               const atfw::team::DTeamAction& action) {
          if (action.action_case() == atfw::team::DTeamAction::kRemoveMember &&
              action.remove_member().user_key().user_id() == members.owner.user_id()) {
            kick_seen = true;
            CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED,
                           action.remove_member().remove_member_reason());
          }
          if (action.action_case() == atfw::team::DTeamAction::kElectionCaptain) {
            ++election_logs;
          }
          return true;
        });
    CASE_EXPECT_TRUE(kick_seen);
    CASE_EXPECT_EQ(1, election_logs);

    // 恢复后内容
    auto recovered = recover_room(env, team_id);
    CASE_EXPECT_TRUE(!!recovered);
    if (recovered) {
      CASE_EXPECT_TRUE(recovered->find_member(members.owner, false) == nullptr);
      auto rec_admin = recovered->find_member(members.admin, false);
      CASE_EXPECT_TRUE(!!rec_admin);
      if (rec_admin) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, rec_admin->member_data.role());
      }
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

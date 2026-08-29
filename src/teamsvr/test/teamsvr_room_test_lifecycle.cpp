// Copyright 2026 atframework
//
// teamsvr-room 心跳、过期与房间生命周期用例(TEAM_ROOM_TEST_PLAN.md §4.8 LIFE-01~14)。
// 通过 global_now_offset_guard + drive_timer_ticks 精确触发定时事件，不依赖真实 sleep。

#include "teamsvr_room_test_common.h"

namespace {
using namespace teamsvr_room_test;

int32_t send_heartbeat(room_test_env& env, const team_room::ptr_t& room, const PROJECT_NAMESPACE_ID::DUserIDKey& key,
                    uint64_t router_id, int64_t sequence = 0, uint64_t hash_code = 0) {
  return env.run("heartbeat", [room, &key, router_id, sequence, hash_code](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomHeartbeatReq hb;
    protobuf_copy_message(*hb.mutable_user_key(), key);
    hb.set_user_router_server_id(router_id);
    hb.set_sequence(sequence);
    hb.set_hash_code(hash_code);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
  });
}
}  // namespace

// ============ LIFE-01/02: 心跳更新 router/时间/ack；router 0 不刷新；不存在成员 not-found ============
CASE_TEST(teamsvr_room_lifecycle, heartbeat_semantics) {
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
  size_t sends_before = fake.send_message_calls();

  // 正常心跳: 更新 router 与更大的 ack
  CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.normal, 0x1111, 100, 0xAAAA));
  auto normal_member = room->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!normal_member);
  if (normal_member) {
    CASE_EXPECT_EQ(0x1111u, normal_member->user_router_server_id);
    CASE_EXPECT_EQ(100, normal_member->member_data.acknowledge_action_sequence());
    CASE_EXPECT_EQ(0xAAAAu, normal_member->member_data.acknowledge_action_hash_code());
    auto heartbeat_time = normal_member->last_heartbeat_timepoint;
    CASE_EXPECT_GT(heartbeat_time, std::chrono::system_clock::from_time_t(0));

    // 较小 sequence 不覆盖已有确认值
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.normal, 0x2222, 50, 0xBBBB));
    CASE_EXPECT_EQ(100, normal_member->member_data.acknowledge_action_sequence());
    CASE_EXPECT_EQ(0xAAAAu, normal_member->member_data.acknowledge_action_hash_code());
    CASE_EXPECT_EQ(0x2222u, normal_member->user_router_server_id);

    // router id 为 0(反订阅)不刷新在线时间
    auto before_no_refresh = normal_member->last_heartbeat_timepoint;
    {
      global_now_offset_guard guard(std::chrono::seconds{5});
      CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.normal, 0));
      CASE_EXPECT_EQ(before_no_refresh, normal_member->last_heartbeat_timepoint);
      CASE_EXPECT_EQ(0u, normal_member->user_router_server_id);
    }
  }

  // 心跳不写频道日志
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());

  // 不存在成员: member-not-found
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND,
                 env.run("heartbeat_missing", [room](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::SSTeamRoomHeartbeatReq hb;
                   auto missing = make_user_key(1, 99999);
                   protobuf_copy_message(*hb.mutable_user_key(), missing);
                   hb.set_user_router_server_id(0x1);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
                 }));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-03/04: 离线到期发送 remove action，事件回环后清除成员与重试状态 ============
CASE_TEST(teamsvr_room_lifecycle, offline_member_kicked) {
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
  // owner 保持心跳，normal 不再心跳: 推进 member_offline_expire(30s)后 normal 到期
  {
    global_now_offset_guard guard(std::chrono::seconds{31});
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.owner, 0x1));
    // 分轮推进+驱动: 每轮推进 2s，让"过期期限"被推到 now+1 的定时器在下一轮触发
    for (int round = 0; round < 4; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
  }

  // 离线成员被踢出: 写 remove action 并回环
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());
  size_t remove_logs = 0;
  fake.foreach_team_action([&members, &remove_logs](const atfw::dtmq::DChannelMessage&,
                                                    const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kRemoveMember &&
        action.remove_member().user_key().user_id() == members.normal.user_id()) {
      ++remove_logs;
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED, action.remove_member().remove_member_reason());
    }
    return true;
  });
  // 恰好一条移除日志(重试状态若残留会在后续轮次补发重复日志)
  CASE_EXPECT_EQ(1u, remove_logs);
  // 重试队列已清理
  CASE_EXPECT_EQ(0u, room->debug_retry_remove_count());

  // 被移除者收到一次移除通知
  size_t removed_notify = 0;
  for (const auto& record : env.personal_messages()) {
    if (record.action.action_case() == atfw::team::DTeamMemberAction::kRemoveMember &&
        record.channel.channel_id() == make_personal_channel(members.normal.user_id()).channel_id()) {
      ++removed_notify;
    }
  }
  CASE_EXPECT_EQ(1u, removed_notify);

  // 成员仍在的 owner 未被误踢
  CASE_EXPECT_TRUE(room->find_member(members.owner, false) != nullptr);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-05: 邀请/申请到期由维护清理，不发送个人通知 ============
CASE_TEST(teamsvr_room_lifecycle, admission_expiry_cleanup_no_notify) {
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

  // 建立邀请与申请(默认 5s 过期)
  auto invitee = make_user_key(1, 8401);
  auto applicant = make_user_key(1, 8402);

  atfw::team::SSTeamRoomAddInvitationReq invite_req;
  protobuf_copy_message(*invite_req.mutable_sender_user_key(), members.normal);
  auto* invitation = invite_req.mutable_invitation();
  protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
  protobuf_copy_message(*invitation->mutable_invitee(), invitee);
  protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));

  atfw::team::SSTeamRoomAddJoinRequestReq join_req;
  protobuf_copy_message(*join_req.mutable_sender_user_key(), applicant);
  auto* join_request = join_req.mutable_join_request();
  protobuf_copy_message(*join_request->mutable_requester(), applicant);
  protobuf_copy_message(*join_request->mutable_requester_private_channel(), make_personal_channel(applicant.user_id()));
  CASE_EXPECT_EQ(0, env.run("add_join", [room, &join_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, join_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 邀请产生 invited 通知(1)，申请产生 apply_join_request 受理回执(1)
  size_t personal_before_cleanup = env.personal_message_count();
  CASE_EXPECT_EQ(2u, personal_before_cleanup);

  // 推进过期时间 + 分轮驱动维护；断言在偏移时间内完成(过期判定依赖推进后的时钟)。
  // 推进量(31s+)已超过 member_offline_expire(30s): 全员心跳保持在线，避免维护调度恢复后
  // 离线成员被踢产生额外的 remove_member 通知(本用例只关心准入清理的副作用)
  {
    global_now_offset_guard guard(std::chrono::seconds{31});
    for (const auto& key : {members.owner, members.admin, members.normal}) {
      CASE_EXPECT_EQ(0, env.run("heartbeat_keepalive", [room, &key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomHeartbeatReq hb;
        protobuf_copy_message(*hb.mutable_user_key(), key);
        hb.set_user_router_server_id(0x1234);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
      }));
    }
    for (int round = 0; round < 4; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }

    // 清理不发送任何新的个人通知
    CASE_EXPECT_EQ(personal_before_cleanup, env.personal_message_count());

    // 清理后 approve 返回 not-found
    atfw::team::SSTeamRoomApproveInvitationReq approve_req;
    protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
    protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                   env.run("approve_expired", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                   }));

    atfw::team::SSTeamRoomApproveJoinRequestReq approve_join;
    protobuf_copy_message(*approve_join.mutable_sender_user_key(), members.normal);
    protobuf_copy_message(*approve_join.mutable_applicant(), applicant);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND,
                   env.run("approve_join_expired", [room, &approve_join](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_join)));
                   }));
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-06: 成员全部离开后销毁空队伍与频道 ============
CASE_TEST(teamsvr_room_lifecycle, empty_room_destroy_flow) {
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

  // 所有成员退出
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

  // 推进 empty_room_destroy_delay(5s): 写 destroy_team 并销毁频道
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    for (int round = 0; round < 8; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
  }

  // destroy_team 事件已写入，频道随后销毁
  bool found_destroy = false;
  fake.foreach_team_action(
      [&found_destroy, team_id](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kDestroyTeam) {
          found_destroy = true;
          CASE_EXPECT_EQ(team_id, action.destroy_team().team_id());
        }
        return true;
      });
  CASE_EXPECT_TRUE(found_destroy);
  CASE_EXPECT_GE(fake.destroy_calls(), 1u);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-13: 快照恢复后的离线截止下限(FIX-02 回归) ============
CASE_TEST(teamsvr_room_lifecycle, restore_offline_deadline_floor_precise) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t target_team = next_test_team_id();
  auto owner = make_user_key(1, 8501);
  auto normal_user = make_user_key(1, 8502);
  {
    auto& target_fake = env.channel(target_team);
    target_fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(target_team));
    protobuf_copy_message(*storage.mutable_captain_user_key(), owner);
    auto stale_time = protobuf_from_system_clock(atfw::util::time::time_utility::now() - std::chrono::hours{24});
    for (const auto& member_key : {owner, normal_user}) {
      auto* member = storage.add_member();
      protobuf_copy_message(*member->mutable_user_key(), member_key);
      member->set_role(member_key.user_id() == owner.user_id() ? atfw::team::EN_TEAM_MEMBER_ROLE_OWNER
                                                               : atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
      *member->mutable_joined_timepoint() = stale_time;
      *member->mutable_last_heartbeat_timepoint() = stale_time;
    }
    target_fake.set_custom_data(storage);

    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    target_fake.set_private_data(private_data);
  }

  team_room::ptr_t restored = env.setup_ready_room(target_team);
  CASE_EXPECT_TRUE(!!restored);
  if (!restored) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 单一 guard 保持时间单调: 恢复点 + 20s 成员仍在(restore 兜底)，+33s 后被踢
  {
    global_now_offset_guard guard(std::chrono::seconds{20});
    for (int round = 0; round < 4; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(target_team));
    }
    CASE_EXPECT_TRUE(restored->find_member(normal_user, false) != nullptr);

    for (int round = 0; round < 4; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(target_team));
    }
    CASE_EXPECT_EQ(nullptr, restored->find_member(normal_user, false).get());
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-14: 对未创建队伍发心跳返回 member-not-found，幽灵空房间自洁 ============
CASE_TEST(teamsvr_room_lifecycle, heartbeat_on_uncreated_team) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();

  // 订阅 auto-create 频道并抢锁后返回 member-not-found
  int32_t hb_ret = env.run("heartbeat_uncreated", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    auto room = team_room_manager::me()->mutable_room(ctx, make_team_key(team_id));
    if (!room) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
    int32_t ready_ret = RPC_AWAIT_CODE_RESULT(room->await_ready(ctx));
    if (0 != ready_ret) {
      RPC_RETURN_CODE(ready_ret);
    }
    atfw::team::SSTeamRoomHeartbeatReq hb;
    auto key = make_user_key(1, 8601);
    protobuf_copy_message(*hb.mutable_user_key(), key);
    hb.set_user_router_server_id(0x1);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
  });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND, hb_ret);

  // 产生幽灵空房间: manager 持有该 room
  auto room = team_room_manager::me()->get_room(make_team_key(team_id));
  CASE_EXPECT_TRUE(!!room);

  // 推进 empty_room_destroy_delay 后: 写 destroy-team 并回收
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    for (int round = 0; round < 8; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
  }

  auto& fake = env.channel(team_id);
  bool found_destroy = false;
  fake.foreach_team_action([&found_destroy](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kDestroyTeam) {
      found_destroy = true;
    }
    return true;
  });
  CASE_EXPECT_TRUE(found_destroy);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-04: 移除在途不重复提交；重试间隔/次数准确；成功回环后不再重试 ============
CASE_TEST(teamsvr_room_lifecycle, remove_retry_semantics) {
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
  auto send_remove = [&env, room](const PROJECT_NAMESPACE_ID::DUserIDKey& key) {
    return env.run("send_remove", [room, &key](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), key);
      action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
  };
  auto count_remove_logs = [&fake]() {
    size_t count = 0;
    fake.foreach_team_action([&count](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
      if (action.action_case() == atfw::team::DTeamAction::kRemoveMember) {
        ++count;
      }
      return true;
    });
    return count;
  };

  // 单一 guard 保持时间单调: 时间轮的 last_tick 只前进不后退，分段 guard 回退会让后续定时器
  // 被钳制到 last_tick+1 而不可达(见 reset_timer_wheel_for_test 注释)
  global_now_offset_guard guard(std::chrono::seconds{0});

  // 1) 移除在途(重试队列占位)时不重复提交
  CASE_EXPECT_EQ(0, send_remove(members.normal));
  CASE_EXPECT_EQ(1u, count_remove_logs());
  CASE_EXPECT_EQ(0, send_remove(members.normal));  // 在途去重: 直接成功
  CASE_EXPECT_EQ(1u, count_remove_logs());
  CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);  // 等回环才真正移除

  // 2) 回环后: 成员移除、重试队列清理；越过重试点后不再补发
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());
  CASE_EXPECT_EQ(0u, room->debug_retry_remove_count());
  guard.advance(std::chrono::seconds{3});
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, count_remove_logs());  // 无重试补发
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 7003, atfw::team::DTeamMemberAction::kRemoveMember));

  // 3) 重试间隔准确: 发送失败后 interval 内不重发，到点重发
  // 续租事件可能与重试事件竞争最早到期(取决于建队后的真实耗时): 先越过续租点
  // 驱动一次续租使其重新排期到 now+lease/2，再注入失败——最早事件判定不依赖真实耗时
  guard.advance(std::chrono::seconds{3});
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(0, env.sync(team_id));

  fake.next_send_fault.present = true;
  fake.next_send_fault.commit_first = false;
  fake.next_send_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;
  const auto inject_begin = atfw::util::time::time_utility::now();
  CASE_EXPECT_NE(0, send_remove(members.admin));  // 首次发送失败 -> 进入重试队列，成员仍在
  const auto inject_end = atfw::util::time::time_utility::now();
  CASE_EXPECT_TRUE(room->find_member(members.admin, false) != nullptr);
  CASE_EXPECT_EQ(1u, count_remove_logs());
  CASE_EXPECT_EQ(1u, room->debug_retry_remove_count());

  // 时间轮最早事件为重试事件，且截止点恰为注入时刻+retry_interval(1s): 到点前不重发
  auto retry_event = room->get_next_timer_event(atfw::util::time::time_utility::now());
  CASE_EXPECT_EQ(static_cast<int32_t>(team_room_timer_event_type::kKickOfflineMember),
                 static_cast<int32_t>(retry_event.type));
  CASE_EXPECT_GE(retry_event.timeout, inject_begin + std::chrono::seconds{1});
  CASE_EXPECT_LE(retry_event.timeout, inject_end + std::chrono::seconds{1});

  // 到点重发成功，不用“多等一秒”掩盖边界错误。
  guard.advance(std::chrono::seconds{1});
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(2u, count_remove_logs());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  // 重发回环后: 成员移除，队列清理，只通知一次
  CASE_EXPECT_EQ(nullptr, room->find_member(members.admin, false).get());
  CASE_EXPECT_EQ(0u, room->debug_retry_remove_count());
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 7002, atfw::team::DTeamMemberAction::kRemoveMember));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-07: 空房间销毁倒计时期间新成员加入取消销毁；非空队伍不触发 destroy ============
CASE_TEST(teamsvr_room_lifecycle, empty_room_destroy_cancelled_by_join) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner = make_user_key(1, 8611);
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(8611)));
  auto room = team_room_manager::me()->get_room(make_team_key(team_id));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);

  // 在队伍仍有 owner 时建立一条真实邀请。空房间后由 invitee 走 approve 流程重新加入，
  // 不直接向 fake journal 塞一个绕过权限和 admission 的 add_member。
  auto joiner = make_user_key(1, 8612);
  atfw::team::SSTeamRoomAddInvitationReq invitation_req;
  protobuf_copy_message(*invitation_req.mutable_sender_user_key(), owner);
  auto* invitation = invitation_req.mutable_invitation();
  protobuf_copy_message(*invitation->mutable_inviter(), owner);
  protobuf_copy_message(*invitation->mutable_invitee(), joiner);
  protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(joiner.user_id()));
  CASE_EXPECT_EQ(0, env.run("invite_joiner", [room, &invitation_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invitation_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 唯一成员退出 -> 房间变空，销毁倒计时开始
  CASE_EXPECT_EQ(0, env.run("remove_owner", [room, &owner](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), owner);
    action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_LOGOUT);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 倒计时 5s，推进 3s 时 invitee 批准邀请并走 add_member + approve 日志流程 -> 取消销毁
  {
    global_now_offset_guard guard(std::chrono::seconds{3});
    atfw::team::SSTeamRoomApproveInvitationReq approve_req;
    protobuf_copy_message(*approve_req.mutable_sender_user_key(), joiner);
    protobuf_copy_message(*approve_req.mutable_invitee(), joiner);
    CASE_EXPECT_EQ(0, env.run("approve_join", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
    }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_TRUE(room->find_member(joiner, false) != nullptr);

    // 越过原销毁点(5s)再多推进: 非空队伍不得触发 empty-room destroy
    for (int round = 0; round < 4; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
    CASE_EXPECT_TRUE(room->find_member(joiner, false) != nullptr);
  }
  bool found_destroy = false;
  fake.foreach_team_action([&found_destroy](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kDestroyTeam) {
      found_destroy = true;
    }
    return true;
  });
  CASE_EXPECT_FALSE(found_destroy);
  CASE_EXPECT_EQ(0u, fake.destroy_calls());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-08: 房间始终只有一个下一事件定时器；更早事件可提前，较晚事件不得延后到期事件 ============
CASE_TEST(teamsvr_room_lifecycle, single_timer_earliest_event_wins) {
  // 合法配置(成员离线过期协议下限 30s)下续租点(renew=lease/2=5s)先于成员到期点
  room_test_cfg_values cfg;
  cfg.compact_log_over_percent = 100000;  // 关闭压缩加速: 搭建日志数已超默认数量触发线，
  cfg.compact_log_start_seconds = 3600;   // 否则最近事件会被压缩维护(立即)抢占
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

  // 创建快照恢复阶段(持锁前)曾把定时器武装到"立即"(kAcquireLock 空锁分支)，且 reset_room_timer
  // 只提前不延后; 先驱动一次让该立即项触发并重排到真实最近事件，再读取武装时间
  env.drive_timer_ticks();
  // 最近事件 = 续租维护(renew=lease/2=5s，成员到期在 30s 后)。期望值全部从 SUT 状态推导，
  // 不使用“当前 wall clock 距离”的容差断言，避免 CPU 调度跨秒影响结果。
  auto ev = room->get_next_timer_event(atfw::util::time::time_utility::now());
  CASE_EXPECT_EQ(static_cast<int32_t>(team_room_timer_event_type::kMaintenance), static_cast<int32_t>(ev.type));
  CASE_EXPECT_TRUE(ev.timeout == room->debug_timer_timeout());  // 定时器武装在最近事件上

  // 心跳全员(T0+2, 成员到期点推到 T0+32): 更晚的事件不得延后已武装的到期定时器
  const auto armed_before_heartbeat = room->debug_timer_timeout();
  {
    global_now_offset_guard guard(std::chrono::seconds{2});
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.owner, 0x1234));
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.admin, 0x1234));
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.normal, 0x1234));
    CASE_EXPECT_TRUE(armed_before_heartbeat == room->debug_timer_timeout());  // 不延后

    // 续租点依次到期触发维护(每次续租一个 update)，心跳有效的成员不得被误踢。
    // 每轮推进 6s，与两侧事件边界各留秒级余量，真实耗时偏移不可能越过边界
    size_t updates_before = env.channel(team_id).update_calls();
    for (int round = 0; round < 5; ++round) {
      guard.advance(std::chrono::seconds{round == 0 ? 4 : 6});  // T0+6,+12,+18,+24,+30
      env.drive_timer_ticks();
    }
    CASE_EXPECT_GE(env.channel(team_id).update_calls(), updates_before + 5);
    CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);

    // 续租已推进到 T0+35 之后，成员到期点(T0+32)成为最近事件: 成员事件被正确选中且未提前触发
    guard.advance(std::chrono::seconds{1});  // T0+31
    ev = room->get_next_timer_event(atfw::util::time::time_utility::now());
    CASE_EXPECT_EQ(static_cast<int32_t>(team_room_timer_event_type::kKickOfflineMember), static_cast<int32_t>(ev.type));
    CASE_EXPECT_TRUE(room->find_member(members.owner, false) != nullptr);
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-09: 同一 tick 只处理到期房间；pending flush 只覆盖已登记房间 ============
CASE_TEST(teamsvr_room_lifecycle, tick_only_due_rooms) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // 两个房间: A 保持心跳，B 成员静默到期
  int64_t team_a = next_test_team_id();
  int64_t team_b = next_test_team_id();
  team_room::ptr_t room_a;
  team_room::ptr_t room_b;
  standard_team_members members_a;
  standard_team_members members_b;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_a, room_a, members_a, 8700));
  CASE_EXPECT_TRUE(setup_standard_team(env, team_b, room_b, members_b, 8800));
  if (!room_a || !room_b) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto count_remove_logs = [](fake_team_room_channel& fake) {
    size_t count = 0;
    fake.foreach_team_action([&count](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
      if (action.action_case() == atfw::team::DTeamAction::kRemoveMember) {
        ++count;
      }
      return true;
    });
    return count;
  };

  {
    global_now_offset_guard guard(std::chrono::seconds{31});
    // T0+31: A 全员心跳(在线)，B 仅 owner/admin 心跳(normal 静默到期)
    for (const auto& key : {members_a.owner, members_a.admin, members_a.normal}) {
      CASE_EXPECT_EQ(0, env.run("heartbeat_a", [room_a, &key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomHeartbeatReq hb;
        protobuf_copy_message(*hb.mutable_user_key(), key);
        hb.set_user_router_server_id(0x1234);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room_a->heartbeat(ctx, hb)));
      }));
    }
    for (const auto& key : {members_b.owner, members_b.admin}) {
      CASE_EXPECT_EQ(0, env.run("heartbeat_b", [room_b, &key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomHeartbeatReq hb;
        protobuf_copy_message(*hb.mutable_user_key(), key);
        hb.set_user_router_server_id(0x1234);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room_b->heartbeat(ctx, hb)));
      }));
    }
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_a));
    CASE_EXPECT_EQ(0, env.sync(team_b));
    // B 的定时器链: 首个到期项(续租/压缩维护)处理后重排到 kick 点，继续推进直到 kick 生效
    for (int round = 0; round < 3; ++round) {
      guard.advance(std::chrono::seconds{1});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_a));
      CASE_EXPECT_EQ(0, env.sync(team_b));
    }
  }

  // B 的 normal 被踢(到期)，owner/admin 在线保留；A 无任何 remove 日志(未到期不处理)
  CASE_EXPECT_EQ(nullptr, room_b->find_member(members_b.normal, false).get());
  CASE_EXPECT_TRUE(room_b->find_member(members_b.owner, false) != nullptr);
  CASE_EXPECT_TRUE(room_a->find_member(members_a.normal, false) != nullptr);
  CASE_EXPECT_EQ(1u, count_remove_logs(env.channel(team_b)));
  CASE_EXPECT_EQ(0u, count_remove_logs(env.channel(team_a)));
  // 仅 B 的被踢成员收到个人通知；两队使用不同玩家/个人频道，能够判定副作用来源。
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 8803, atfw::team::DTeamMemberAction::kRemoveMember));
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 8703, atfw::team::DTeamMemberAction::kRemoveMember));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-10(partial): no-wait 个人频道消息在远端处理前丢弃/接收后处理失败的至多一次契约 ============
CASE_TEST(teamsvr_room_lifecycle, personal_channel_send_contract) {
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

  auto send_remove = [&env, room](const PROJECT_NAMESPACE_ID::DUserIDKey& key) {
    return env.run("send_remove", [room, &key](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), key);
      action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
  };

  // 1) 远端处理前丢弃: 通知丢失(at-most-once, GAP-06 决策)，成员移除仍是权威事实，不自动重试
  env.queue_personal_send_result(false, PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  CASE_EXPECT_EQ(0, send_remove(members.normal));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 7003, atfw::team::DTeamMemberAction::kRemoveMember));
  {
    // 多泵几代: 不会有迟到的补发
    global_now_offset_guard guard(std::chrono::seconds{3});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_EQ(0u, count_personal_actions(env, 7003, atfw::team::DTeamMemberAction::kRemoveMember));
  }

  // 2) 远端已接收后处理失败: no-wait 调用方不等待业务响应，也不会因远端错误重试并产生重复通知
  env.queue_personal_send_result(true, PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  CASE_EXPECT_EQ(0, send_remove(members.admin));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.admin, false).get());
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 7002, atfw::team::DTeamMemberAction::kRemoveMember));
  {
    global_now_offset_guard guard(std::chrono::seconds{3});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_EQ(1u, count_personal_actions(env, 7002, atfw::team::DTeamMemberAction::kRemoveMember));
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-11: 失锁后冻结维护/重试/通知；迟到定时器不写；接管后恢复 ============
CASE_TEST(teamsvr_room_lifecycle, step_down_freezes_maintenance_until_takeover) {
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
  // 第三方持锁(20s) -> 退位
  fake.set_lock(make_foreign_lock("teamsvr-room:other-master", 20));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 退位期间: 多轮驱动不产生任何频道写(维护冻结)，下一事件只有 acquire
  size_t sends_before = fake.send_message_calls();
  size_t updates_before = fake.update_calls();
  {
    global_now_offset_guard guard(std::chrono::seconds{8});
    for (int round = 0; round < 3; ++round) {
      guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
    auto next_event = room->get_next_timer_event(atfw::util::time::time_utility::now());
    CASE_EXPECT_EQ(static_cast<int32_t>(team_room_timer_event_type::kAcquireLock),
                   static_cast<int32_t>(next_event.type));
  }
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  CASE_EXPECT_EQ(updates_before, fake.update_calls());
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 他人锁到期后(锁创建于 T1，20s 租约 -> T1+20 过期): 定时器驱动接管并恢复维护
  {
    global_now_offset_guard guard(std::chrono::seconds{22});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(room->is_lock_holder());
  {
    // 接管后维护恢复(续租 update)。guard 偏移是绝对的: 接管发生在 base+22，
    // 续租点 base+27，这里必须给到 base+29 而不是再 +7(时间不可回退，时间轮 last_tick 只前进)
    global_now_offset_guard guard(std::chrono::seconds{29});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_GT(fake.update_calls(), updates_before);
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ LIFE-12: subscribe 心跳快速失败后恢复: 房间不泄漏且只恢复一次 ============
CASE_TEST(teamsvr_room_lifecycle, subscribe_failure_then_recover_once) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  {
    // 预置已创建频道与成员
    auto& fake = env.channel(team_id);
    fake.ensure_created();
    atfw::team::DTeamStorage storage;
    protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
    auto owner = make_user_key(1, 8621);
    protobuf_copy_message(*storage.mutable_captain_user_key(), owner);
    auto* member = storage.add_member();
    protobuf_copy_message(*member->mutable_user_key(), owner);
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    *member->mutable_joined_timepoint() = now_tp;
    *member->mutable_last_heartbeat_timepoint() = now_tp;
    fake.set_custom_data(storage);
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    fake.set_private_data(private_data);
  }

  // 首次 subscribe 心跳快速失败 -> 房间不就绪(重试间隔 60s 内不会自动成功)
  env.fail_subscribe_times(1, PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  size_t rooms_before = team_room_manager::me()->get_room_count();
  int32_t ready_ret1 = env.run("await_ready_fail", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    auto room = team_room_manager::me()->mutable_room(ctx, make_team_key(team_id));
    if (!room) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
  });
  CASE_EXPECT_NE(0, ready_ret1);
  // 房间不泄漏: manager 只持有一个该队伍的房间
  CASE_EXPECT_EQ(rooms_before + 1, team_room_manager::me()->get_room_count());

  // 故障清除后推进订阅重试间隔(60s): 重新就绪并恢复快照
  {
    global_now_offset_guard guard(std::chrono::seconds{61});
    env.drive_timer_ticks();
  }
  int32_t ready_ret2 = env.run("await_ready_ok", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    auto room = team_room_manager::me()->get_room(make_team_key(team_id));
    if (!room) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
  });
  CASE_EXPECT_EQ(0, ready_ret2);
  auto room = team_room_manager::me()->get_room(make_team_key(team_id));
  CASE_EXPECT_TRUE(!!room);
  if (room) {
    CASE_EXPECT_TRUE(room->is_subscriber_ready());
    CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8621), false) != nullptr);
    // 再次 await_ready: 幂等(不重复恢复, 无新通知副作用)
    size_t personal_before = env.personal_message_count();
    CASE_EXPECT_EQ(0, env.run("await_ready_again", [room](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
    }));
    CASE_EXPECT_EQ(personal_before, env.personal_message_count());
  }
  CASE_EXPECT_EQ(rooms_before + 1, team_room_manager::me()->get_room_count());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ GAP-07: 心跳标脏运行时数据，维护随续租 update 持久化 custom/private 快照 ============
// 决策(2026-08-29): 长期无新日志的新心跳不必即时持久化，但要记录 team_room 标脏；
// 维护发现脏标记后随续租 update 保存快照(负载迁移转出前数据已持久化)，保存成功后清除标记。
// 本用例配置数量/时间维度均不可压缩(compact_sequence 恒为 0)，隔离出"仅因标脏而保存"的路径
CASE_TEST(teamsvr_room_lifecycle, heartbeat_runtime_dirty_saved_on_maintenance) {
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 1000;
  cfg.compact_log_keep_percent = 100;
  cfg.compact_log_start_seconds = 3600;
  cfg.compact_log_keep_seconds = 1800;
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
  auto last_update_has_custom = [&fake]() -> bool {
    for (auto iter = fake.update_requests().rbegin(); iter != fake.update_requests().rend(); ++iter) {
      return iter->request.has_custom_data();
    }
    return false;
  };

  // 时间驱动使用单一 guard 顺序推进(guard 析构会回退虚拟时间，独立作用域会使后续定时器永不到期)
  global_now_offset_guard time_guard(std::chrono::seconds{6});
  // 无心跳变化时维护只续租不保存快照(CMP-01 契约)
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_FALSE(last_update_has_custom());

  // 心跳更新运行时数据(router/ack) -> 标脏
  int64_t heartbeat_sequence = fake.last_sequence() + 100;
  CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.normal, 0x2233, heartbeat_sequence, 0xCCCC));

  // 下一次维护随续租保存 custom/private 快照: 快照携带刷新后的 router/ack
  time_guard.advance(std::chrono::seconds{6});
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    bool has_custom = false;
    atfw::team::DTeamStorage storage;
    for (auto iter = fake.update_requests().rbegin(); iter != fake.update_requests().rend(); ++iter) {
      has_custom = iter->request.has_custom_data();
      if (has_custom) {
        CASE_EXPECT_TRUE(iter->request.custom_data().UnpackTo(&storage));
      }
      break;
    }
    CASE_EXPECT_TRUE(has_custom);
    if (has_custom) {
      const atfw::team::DTeamMember* normal_snapshot = nullptr;
      for (const auto& member : storage.member()) {
        if (member.user_key().user_id() == members.normal.user_id()) {
          normal_snapshot = &member;
        }
      }
      CASE_EXPECT_TRUE(nullptr != normal_snapshot);
      if (nullptr != normal_snapshot) {
        CASE_EXPECT_EQ(0x2233u, normal_snapshot->user_router_server_id());
        CASE_EXPECT_EQ(heartbeat_sequence, normal_snapshot->acknowledge_action_sequence());
      }
    }
  }

  // 保存成功后脏标记清除: 无新心跳的下一次维护回到仅续租(不携带快照)
  time_guard.advance(std::chrono::seconds{6});
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_FALSE(last_update_has_custom());

  // 运行时脏快照覆盖到了 saved_action_sequence；恢复时不得再回放更早日志，
  // 否则旧 add_member/member_update 会把快照中的 heartbeat router/ack 回退。
  env.clear_rooms();
  room.reset();
  auto restored = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!restored);
  if (restored) {
    CASE_EXPECT_EQ(0, env.sync(team_id));
    auto restored_member = restored->find_member(members.normal, false);
    CASE_EXPECT_TRUE(nullptr != restored_member);
    if (restored_member) {
      CASE_EXPECT_EQ(0x2233u, restored_member->member_data.user_router_server_id());
      CASE_EXPECT_EQ(heartbeat_sequence, restored_member->member_data.acknowledge_action_sequence());
      CASE_EXPECT_EQ(0xCCCCu, restored_member->member_data.acknowledge_action_hash_code());
    }
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ GAP-03: 删除重试耗尽后本地删除并尽力而为补写 remove 日志 ============
// 决策(2026-08-29): 重试上限后本地删除并留下日志(接受分叉，优先确保无泄露)。
// 验证: 重试期间成员保留(等待事件回环)；耗尽后成员本地移除、重试队列清空，
// 并以 no-wait 尽力而为补写一条 remove_member 日志(可被各节点幂等应用)
CASE_TEST(teamsvr_room_lifecycle, remove_retry_exhaustion_local_remove_with_log) {
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

  // 时间驱动使用单一 guard 顺序推进(guard 析构会回退虚拟时间，独立作用域会使后续定时器永不到期)
  // 其他成员刷新心跳，避免与 normal 同时到期(normal 的离线截止时间为最早)
  global_now_offset_guard time_guard(std::chrono::seconds{20});
  CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.owner, 0x1001));
  CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.admin, 0x1002));

  auto queue_remove_fault = [&fake]() {
    fake.next_send_fault.present = true;
    fake.next_send_fault.commit_first = false;
    fake.next_send_fault.error_code = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  };

  // 离线到期踢出: remove 写入预提交失败(故障)，成员进入重试队列并保留
  size_t remove_logs_before = 0;
  fake.foreach_team_action([&remove_logs_before](const atfw::dtmq::DChannelMessage&,
                                                 const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kRemoveMember) {
      ++remove_logs_before;
    }
    return true;
  });
  // 分轮推进+驱动(与 LIFE-03 相同): 一次性大步推进会积压多轮续租维护且过期定时器会被
  // 时间轮钳制到未来，必须逐轮推进让 kick 定时器真正触发;故障在 kick 前挂上，
  // 以重试队列为循环退出条件
  queue_remove_fault();
  for (int round = 0; round < 24 && 0 == room->debug_retry_remove_count(); ++round) {
    time_guard.advance(std::chrono::seconds{2});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_TRUE(nullptr != room->find_member(members.normal, false));
  CASE_EXPECT_EQ(1u, room->debug_retry_remove_count());

  // 两次重试均失败(达到 member_channel_notification_retry_times 上限): 每次排队故障后
  // 分轮推进驱动，直到该故障被重试的 remove 发送消费(事件次序不确定，以故障消费为准)
  for (int retry_round = 0; retry_round < 2; ++retry_round) {
    // 每个重试轮次先刷新其他成员心跳：虚拟时钟=真实时钟+偏移，循环内的真实耗时同样消耗
    // 离线期限预算；不刷新则高负载下 owner/admin 可能先于 normal 到期，误消费故障脚本
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.owner, 0x1001));
    CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.admin, 0x1002));
    queue_remove_fault();
    for (int round = 0; round < 8 && fake.next_send_fault.present; ++round) {
      time_guard.advance(std::chrono::seconds{2});
      env.drive_timer_ticks();
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
    CASE_EXPECT_FALSE(fake.next_send_fault.present);
    CASE_EXPECT_TRUE(nullptr != room->find_member(members.normal, false));
    CASE_EXPECT_EQ(1u, room->debug_retry_remove_count());
  }

  // 重试耗尽: 本地删除(无泄露) + 尽力而为补写一条 remove 日志(无故障时成功)。
  // 无故障分轮驱动直至重试队列清空(强制移除完成)
  // 耗尽循环前再刷新一次其他成员心跳(同上，防止真实耗时耗尽他们的离线期限预算)
  CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.owner, 0x1001));
  CASE_EXPECT_EQ(0, send_heartbeat(env, room, members.admin, 0x1002));
  for (int round = 0; round < 8 && 0 != room->debug_retry_remove_count(); ++round) {
    time_guard.advance(std::chrono::seconds{2});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(nullptr == room->find_member(members.normal, false));
  CASE_EXPECT_EQ(0u, room->debug_retry_remove_count());
  CASE_EXPECT_TRUE(nullptr != room->find_member(members.owner, false));
  CASE_EXPECT_TRUE(nullptr != room->find_member(members.admin, false));

  // 补写的 remove_member 日志已入 journal(供各节点幂等应用)
  size_t remove_logs_after = 0;
  fake.foreach_team_action([&remove_logs_after, &members](const atfw::dtmq::DChannelMessage&,
                                                          const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kRemoveMember &&
        action.remove_member().user_key().user_id() == members.normal.user_id()) {
      ++remove_logs_after;
    }
    return true;
  });
  CASE_EXPECT_EQ(remove_logs_before + 1, remove_logs_after);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

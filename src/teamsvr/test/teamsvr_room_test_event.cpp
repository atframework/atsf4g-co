// Copyright 2026 atframework
//
// teamsvr-room 事件应用、成员与队长用例(TEAM_ROOM_TEST_PLAN.md §4.4 EVT-01~11)。
// 通过 fake journal 直接注入 DTeamAction/非 event 日志验证 apply 语义的幂等性与确定性。

#include "teamsvr_room_test_common.h"  // NOLINT: build/include_subdir

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframework/testing/ss_action.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <logic/action/task_action_send_message.h>

#include <rpc/team/teamroomservice.atfw.gen.h>

#include <string>

namespace {
using teamsvr_room_test::add_team_any_data_entry;
using teamsvr_room_test::add_team_any_value_entry;
using teamsvr_room_test::count_personal_actions;
using teamsvr_room_test::global_now_offset_guard;
using teamsvr_room_test::kDtmqProxyNodeId;
using teamsvr_room_test::make_personal_channel;
using teamsvr_room_test::make_team_key;
using teamsvr_room_test::make_user_key;
using teamsvr_room_test::next_test_team_id;
using teamsvr_room_test::room_test_env;
using teamsvr_room_test::setup_standard_team;
using teamsvr_room_test::standard_team_members;

atfw::team::DTeamAction make_injected_add_member(const PROJECT_NAMESPACE_ID::DUserIDKey& key, uint64_t user_id,
                                                 int64_t joined_after_seconds, int64_t heartbeat_after_seconds) {
  atfw::team::DTeamAction action;
  auto* add_member = action.mutable_add_member();
  protobuf_copy_message(*add_member->mutable_user_key(), key);
  protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(user_id));
  add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  auto now = atfw::util::time::time_utility::now();
  if (joined_after_seconds != 0) {
    *add_member->mutable_joined_timepoint() =
        protobuf_from_system_clock(now + std::chrono::seconds{joined_after_seconds});
  }
  if (heartbeat_after_seconds != 0) {
    *add_member->mutable_last_heartbeat_timepoint() =
        protobuf_from_system_clock(now + std::chrono::seconds{heartbeat_after_seconds});
  }
  return action;
}
}  // namespace

// ============ EVT-01: sequence 有缺口但递增时正常应用；ack 只前进不回退 ============
CASE_TEST(teamsvr_room_event, sequence_gap_applied_and_ack_monotonic) {
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

  // 注入带 sequence 缺口的日志(gap=1 -> 跳号)
  auto extra = make_user_key(1, 9001);
  env.inject_team_action(team_id, make_injected_add_member(extra, 9001, 0, 0), 1);
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto extra_member = room->find_member(extra, false);
  CASE_EXPECT_TRUE(extra_member != nullptr);
  if (!extra_member) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  const auto joined_before = protobuf_to_system_clock(extra_member->member_data.joined_timepoint());
  const size_t member_count_before = room->debug_member_lru_keys().size();
  const int64_t ack_before = room->debug_acknowledge_action_sequence();

  // 迟到的重复 add_member(以新序号追加)不回退状态: 携带不同的显式时间戳——
  // 若被重复应用，joined_timepoint 会被覆盖成新值；幂等应用则保持首次落库的事实
  env.inject_team_action(team_id, make_injected_add_member(extra, 9001, 100, 100));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto extra_member_after = room->find_member(extra, false);
  CASE_EXPECT_TRUE(extra_member_after != nullptr);
  if (extra_member_after) {
    CASE_EXPECT_TRUE(joined_before == protobuf_to_system_clock(extra_member_after->member_data.joined_timepoint()));
  }
  CASE_EXPECT_EQ(member_count_before, room->debug_member_lru_keys().size());
  // ack 只前进不回退: 重复日志被正常消费(ack 推进到最新日志序号)，已有成员状态不重绕
  CASE_EXPECT_GT(room->debug_acknowledge_action_sequence(), ack_before);
  CASE_EXPECT_EQ(env.channel(team_id).last_sequence(), room->debug_acknowledge_action_sequence());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-02: add_member 缺少入队/心跳时间时使用事件时间；首成员成为 OWNER ============
CASE_TEST(teamsvr_room_event, add_member_default_timepoints) {
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

  // 注入无时间字段的 add_member: 兜底为事件创建时间(即注入时刻)
  auto extra = make_user_key(1, 9011);
  auto before = atfw::util::time::time_utility::now();
  // 不携带入队/心跳时间(0 表示不设置该字段)
  atfw::team::DTeamAction no_time_action;
  {
    auto* add_member = no_time_action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), extra);
    protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(9011));
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  }
  env.inject_team_action(team_id, no_time_action);
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto after = atfw::util::time::time_utility::now();

  auto member = room->find_member(extra, false);
  CASE_EXPECT_TRUE(!!member);
  if (member) {
    auto joined = protobuf_to_system_clock(member->member_data.joined_timepoint());
    CASE_EXPECT_TRUE(joined >= before && joined <= after);
    auto heartbeat = member->last_heartbeat_timepoint;
    CASE_EXPECT_TRUE(heartbeat >= before && heartbeat <= after);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-03: 重复 add_member 不回退更早入队时间与更新的心跳/路由 ============
CASE_TEST(teamsvr_room_event, duplicate_add_member_keep_earliest_state) {
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

  auto extra = make_user_key(1, 9021);
  // 第一次: 早入队时间 + 新心跳 + 路由
  env.inject_team_action(team_id, make_injected_add_member(extra, 9021, -3600, 0));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto member = room->find_member(extra, false);
  CASE_EXPECT_TRUE(!!member);
  if (member) {
    // 心跳刷新为当前
    CASE_EXPECT_EQ(0, env.run("heartbeat", [room, &extra](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::SSTeamRoomHeartbeatReq hb;
      protobuf_copy_message(*hb.mutable_user_key(), extra);
      hb.set_user_router_server_id(0x9999);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
    }));
    auto heartbeat_after_first = member->last_heartbeat_timepoint;
    auto router_after_first = member->user_router_server_id;
    auto joined_after_first = protobuf_to_system_clock(member->member_data.joined_timepoint());

    // 迟到的重复 add_member(显式携带更晚入队时间、更旧心跳、空路由)
    env.inject_team_action(team_id, make_injected_add_member(extra, 9021, 3600, -3600));
    CASE_EXPECT_EQ(0, env.sync(team_id));

    // 不推迟更早的入队时间: 入队时间保持首次的更早值不变(若被更晚值覆盖则此断言失败)
    CASE_EXPECT_EQ(joined_after_first, protobuf_to_system_clock(member->member_data.joined_timepoint()));
    // 不回退更新的心跳和路由状态
    CASE_EXPECT_TRUE(member->last_heartbeat_timepoint >= heartbeat_after_first);
    CASE_EXPECT_EQ(router_after_first, member->user_router_server_id);
    CASE_EXPECT_EQ(0x9999u, member->member_data.user_router_server_id());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-06: 队长移除后按 joined time/user key 确定性选举 ============
CASE_TEST(teamsvr_room_event, captain_election_deterministic) {
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

  // admin 比 normal 早入队: 移除队长(owner)后应由 admin 接任
  auto& fake = env.channel(team_id);
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("remove_captain", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), members.owner);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  // remove 事件 + election 事件(no_wait)
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // remove 回环时 sync 内的 converge 已触发 flush: 发现队长空缺并写入 election 日志(恰好一次)
  size_t election_count = 0;
  fake.foreach_team_action(
      [&election_count, &members](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kElectionCaptain) {
          ++election_count;
          CASE_EXPECT_EQ(members.admin.user_id(), action.election_captain().user_key().user_id());
        }
        return true;
      });
  CASE_EXPECT_EQ(1u, election_count);

  // election 日志回环
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 事件回环后 admin 成为队长
  auto admin_member = room->find_member(members.admin, false);
  CASE_EXPECT_TRUE(!!admin_member);
  if (admin_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, admin_member->member_data.role());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-07: remove 事件幂等；被移除者只收到一次有效移除通知 ============
CASE_TEST(teamsvr_room_event, remove_event_idempotent) {
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

  // owner 移除 normal
  CASE_EXPECT_EQ(0, env.run("remove_normal", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), members.normal);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  size_t personal_after_remove = 0;
  CASE_EXPECT_EQ(0, env.sync(team_id));
  personal_after_remove = env.personal_message_count();
  CASE_EXPECT_EQ(1u, personal_after_remove);
  if (1 == personal_after_remove) {
    const auto& record = env.personal_messages()[0];
    CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kRemoveMember, record.action.action_case());
    CASE_EXPECT_EQ(make_personal_channel(7003).channel_id(), record.channel.channel_id());
  }
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());

  // 重放同一 remove 事件: 幂等，无新副作用
  env.inject_team_action(team_id, [&members]() {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), members.normal);
    return action;
  }());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(personal_after_remove, env.personal_message_count());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-08: 损坏的 DTeamAction Any 使 room 退位，不应用部分状态 ============
CASE_TEST(teamsvr_room_event, corrupted_event_steps_down) {
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

  // 注入坏 Any(类型 URL 指向 DTeamAction 但 body 非法)
  env.inject_raw_log(team_id, [](atfw::dtmq::DChannelMessageDetail& detail) {
    auto* event = detail.mutable_event();
    event->set_type_url("type.googleapis.com/atframework.team.DTeamAction");
    event->set_value(std::string("\xff\xfe\xfd\xfc\x01\x02"));
  });
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 坏事件不产生部分状态: 成员/通知均无变化
  CASE_EXPECT_EQ(0u, env.personal_message_count());
  CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);

  // room 退位(不再以旧锁身份写入；后续写入会走新的 CAS 抢锁流程，仍保持单写者安全)
  CASE_EXPECT_FALSE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-09: destroy 事件标记销毁，后续写入返回 destroyed ============
CASE_TEST(teamsvr_room_event, destroy_event_marks_destroyed) {
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

  atfw::team::DTeamAction destroy_action;
  protobuf_copy_message(*destroy_action.mutable_destroy_team(), make_team_key(team_id));
  env.inject_team_action(team_id, destroy_action);
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED,
                 env.run("write_after_destroy", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::DTeamAction action;
                   protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                 }));
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-04: member_update 字段合并；管理员更新他人不刷新 LRU/不写 router；本人可更新 router ============
CASE_TEST(teamsvr_room_event, member_update_merge_and_router_trust) {
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
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto normal_member = room->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!normal_member);
  if (!normal_member) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto initial_heartbeat = normal_member->last_heartbeat_timepoint;

  // 本人 member_update(经 send_message RPC): 可更新 router/channel，合并 client_version 与 shared_member_data
  {
    atframework::testing::ss_action_invoke_options invoke_options{rpc::team::packer::get_full_name_of_send_message()};
    invoke_options.source.node_id = kDtmqProxyNodeId;
    invoke_options.source.node_name = "unit-test-dtmq-proxy";
    int32_t ret =
        env.run("self_update", [team_id, &members, &invoke_options](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::SSTeamRoomSendMessageReq request;
          protobuf_copy_message(*request.mutable_team_key(), make_team_key(team_id));
          protobuf_copy_message(*request.mutable_sender_user_key(), members.normal);
          auto* update = request.mutable_action()->mutable_member_update();
          protobuf_copy_message(*update->mutable_user_key(), members.normal);
          update->set_user_router_server_id(0x6666);
          protobuf_copy_message(*update->mutable_user_channel(), make_personal_channel(7003));
          update->set_client_version("v-self");
          add_team_any_data_entry(update->mutable_shared_member_data(), 7, "self-data");
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
              atframework::testing::invoke_ss_action<task_action_send_message>(ctx, request, invoke_options)));
        });
    CASE_EXPECT_EQ(0, ret);
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  CASE_EXPECT_EQ(0x6666u, normal_member->member_data.user_router_server_id());
  CASE_EXPECT_EQ("v-self", normal_member->member_data.client_version());
  CASE_EXPECT_EQ(1, normal_member->shared_member_data.count(7));

  // 管理员(经 send_message RPC)更新他人: client_version 合并生效，但携带的 router 被 action 层清零(GAP-05)
  {
    atframework::testing::ss_action_invoke_options invoke_options{rpc::team::packer::get_full_name_of_send_message()};
    invoke_options.source.node_id = kDtmqProxyNodeId;
    invoke_options.source.node_name = "unit-test-dtmq-proxy";
    int32_t ret =
        env.run("admin_update", [team_id, &members, &invoke_options](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::SSTeamRoomSendMessageReq request;
          protobuf_copy_message(*request.mutable_team_key(), make_team_key(team_id));
          protobuf_copy_message(*request.mutable_sender_user_key(), members.admin);
          auto* update = request.mutable_action()->mutable_member_update();
          protobuf_copy_message(*update->mutable_user_key(), members.normal);
          update->set_user_router_server_id(0x8888);  // 伪造的他人 router，应被忽略
          update->set_client_version("v-admin");
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
              atframework::testing::invoke_ss_action<task_action_send_message>(ctx, request, invoke_options)));
        });
    CASE_EXPECT_EQ(0, ret);
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  CASE_EXPECT_EQ("v-admin", normal_member->member_data.client_version());
  // router 保持本人上报的值
  CASE_EXPECT_EQ(0x6666u, normal_member->member_data.user_router_server_id());
  // member_update 不刷新心跳/LRU(apply 不 touch 访问位置)
  CASE_EXPECT_EQ(initial_heartbeat, normal_member->last_heartbeat_timepoint);

  // 心跳(携带 router)刷新可观察的在线状态；不把 LRU 容器物理顺序当成业务契约。
  {
    global_now_offset_guard guard(std::chrono::seconds{1});
    CASE_EXPECT_EQ(0, env.run("heartbeat_touch", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::SSTeamRoomHeartbeatReq hb;
      protobuf_copy_message(*hb.mutable_user_key(), members.normal);
      hb.set_user_router_server_id(0x7777);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, hb)));
    }));
  }
  CASE_EXPECT_EQ(0x7777u, normal_member->member_data.user_router_server_id());
  CASE_EXPECT_GT(normal_member->last_heartbeat_timepoint, initial_heartbeat);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-05: team_update 合并语义(configure 整体替换，shared map 合并，未携带字段保留) ============
CASE_TEST(teamsvr_room_event, team_update_merge_semantics) {
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

  // 第一次: configure(invite_role=ADMIN) + shared[1]
  {
    atfw::team::DTeamAction action;
    action.mutable_team_update()->mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 1, "one");
    CASE_EXPECT_EQ(0, env.run("team_update1", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // invite_role=ADMIN 生效: normal 成员邀请被拒
  {
    atfw::team::SSTeamRoomAddInvitationReq invite_req;
    protobuf_copy_message(*invite_req.mutable_sender_user_key(), members.normal);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_inviter(), members.normal);
    auto invitee = make_user_key(1, 9101);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee(), invitee);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee_private_channel(),
                          make_personal_channel(9101));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                   env.run("invite_denied", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
                   }));
  }

  // 第二次: 只携带 shared[2]，不携带 configure -> configure 保留
  {
    atfw::team::DTeamAction action;
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 2, "two");
    CASE_EXPECT_EQ(0, env.run("team_update2", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    atfw::team::SSTeamRoomAddInvitationReq invite_req;
    protobuf_copy_message(*invite_req.mutable_sender_user_key(), members.normal);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_inviter(), members.normal);
    auto invitee = make_user_key(1, 9102);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee(), invitee);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee_private_channel(),
                          make_personal_channel(9102));
    // configure 保留 -> 仍拒绝
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                   env.run("invite_still_denied", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
                   }));
  }

  // 第三次: 携带新 configure(只设 disable_join_request) -> 整体替换，invite_role 回到默认(NORMAL 可邀请)
  {
    atfw::team::DTeamAction action;
    action.mutable_team_update()->mutable_configure()->set_disable_join_request(true);
    CASE_EXPECT_EQ(0, env.run("team_update3", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    atfw::team::SSTeamRoomAddInvitationReq invite_req;
    protobuf_copy_message(*invite_req.mutable_sender_user_key(), members.normal);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_inviter(), members.normal);
    auto invitee = make_user_key(1, 9103);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee(), invitee);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee_private_channel(),
                          make_personal_channel(9103));
    CASE_EXPECT_EQ(0, env.run("invite_allowed", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
    }));
    // disable_join_request 生效: join request 被拒
    auto applicant = make_user_key(1, 9104);
    atfw::team::SSTeamRoomAddJoinRequestReq join_req;
    protobuf_copy_message(*join_req.mutable_sender_user_key(), applicant);
    protobuf_copy_message(*join_req.mutable_join_request()->mutable_requester(), applicant);
    protobuf_copy_message(*join_req.mutable_join_request()->mutable_requester_private_channel(),
                          make_personal_channel(9104));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                   env.run("join_denied", [room, &join_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, join_req)));
                   }));
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-06: member_update/team_update 与现有数据相同时去重(不产生频道事件)，空更新保留写入 ============
CASE_TEST(teamsvr_room_event, update_dedup_no_change) {
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
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->is_lock_holder());

  auto& fake = env.channel(team_id);
  auto count_events = [&fake]() { return fake.count_logs_by_command(atfw::dtmq::DChannelMessageDetail::kEvent); };

  // 基线数据: 成员 client_version + shared[7]，队伍 configure(invite_role=ADMIN) + shared[1]
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    update->set_client_version("v-dedup");
    add_team_any_data_entry(update->mutable_shared_member_data(), 7, "member-7");
    CASE_EXPECT_EQ(0, env.run("member_update_init", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  {
    atfw::team::DTeamAction action;
    action.mutable_team_update()->mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 1, "team-1");
    CASE_EXPECT_EQ(0, env.run("team_update_init", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  size_t events_before = count_events();

  // 完全相同的 member_update: 跳过，不追加事件
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    update->set_client_version("v-dedup");
    add_team_any_data_entry(update->mutable_shared_member_data(), 7, "member-7");
    CASE_EXPECT_EQ(0, env.run("member_update_dup", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(events_before, count_events());

  // 仅携带与现值一致的子集字段(未携带的字段不参与判定): 跳过
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    update->set_client_version("v-dedup");
    CASE_EXPECT_EQ(0, env.run("member_update_dup_subset", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(events_before, count_events());

  // 完全相同的 team_update(configure 只写门槛字段，写入前修订后与现值相等): 跳过
  {
    atfw::team::DTeamAction action;
    action.mutable_team_update()->mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 1, "team-1");
    CASE_EXPECT_EQ(0, env.run("team_update_dup", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(events_before, count_events());

  // 共享成员数据变化: 正常下发
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    add_team_any_data_entry(update->mutable_shared_member_data(), 7, "member-7-changed");
    CASE_EXPECT_EQ(0, env.run("member_update_changed", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(events_before + 1, count_events());

  // 不携带任何可落地字段的空 member_update: 不去重(保留锁探测等既有写入行为)
  {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
    CASE_EXPECT_EQ(0, env.run("member_update_empty", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(events_before + 2, count_events());

  // 目标成员不存在: 不去重，保留写入
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), make_user_key(1, 9898));
    update->set_client_version("ghost");
    CASE_EXPECT_EQ(0, env.run("member_update_ghost", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(events_before + 3, count_events());

  // 共享队伍数据变化: 正常下发
  {
    atfw::team::DTeamAction action;
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 1, "team-1-changed");
    CASE_EXPECT_EQ(0, env.run("team_update_changed", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(events_before + 4, count_events());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-10a: 重复批次/compact 点之前日志被订阅层忽略，不产生重复应用 ============
CASE_TEST(teamsvr_room_event, event_sync_duplicate_batch_ignored) {
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

  // 注入一条邀请日志并下发 -> 产生一次 invited 通知
  auto invitee = make_user_key(1, 9201);
  env.inject_team_action(team_id, [&members, &invitee]() {
    atfw::team::DTeamAction action;
    auto* invitation = action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(9201));
    return action;
  }());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 9201, atfw::team::DTeamMemberAction::kInvited));

  // 复制该日志并重放同一批次(携带合法 metadata 外壳) -> 订阅层 kIgnore，无重复应用
  const auto* invitation_log = fake.find_journal_message(fake.last_sequence());
  CASE_EXPECT_TRUE(nullptr != invitation_log);
  if (nullptr == invitation_log) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  atfw::dtmq::DChannelMessage copied_log = *invitation_log;

  atfw::dtmq::SSChannelEventSync event_sync;
  fake.fill_event_sync_metadata(event_sync);
  *event_sync.add_channel_message() = copied_log;
  CASE_EXPECT_EQ(0, env.sync_custom_event_sync(event_sync));

  // 无重复通知、状态不回退
  CASE_EXPECT_EQ(1u, count_personal_actions(env, 9201, atfw::team::DTeamMemberAction::kInvited));
  CASE_EXPECT_TRUE(room->is_lock_holder());
  CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);

  // compact 点之前的日志重放同样被忽略: 压缩后重放已裁剪日志
  for (int i = 0; i < 8; ++i) {
    atfw::team::DTeamAction update_action;
    protobuf_copy_message(*update_action.mutable_member_update()->mutable_user_key(), members.normal);
    update_action.mutable_member_update()->set_client_version("ut-dup-v" + std::to_string(i));
    CASE_EXPECT_EQ(0,
                   env.run("write_log_for_compact", [room, &update_action](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, update_action)));
                   }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  // 双维度硬保证语义下需推进到保留窗口(5s)之外，时间维度才放行裁剪
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  CASE_EXPECT_GT(fake.last_removed_sequence(), 0);
  // 重放 compact 之前的日志副本(creation 日志已被裁剪)
  atfw::dtmq::SSChannelEventSync stale_sync;
  fake.fill_event_sync_metadata(stale_sync);
  *stale_sync.add_channel_message() = copied_log;
  size_t personal_before_stale = env.personal_message_count();
  CASE_EXPECT_EQ(0, env.sync_custom_event_sync(stale_sync));
  CASE_EXPECT_EQ(personal_before_stale, env.personal_message_count());
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-10b: 批中 hash 不匹配 -> 前缀保留/后缀丢弃，快照自愈 ============
CASE_TEST(teamsvr_room_event, event_sync_hash_mismatch_prefix_then_snapshot) {
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

  // journal 中注入 3 条有序日志: A(add_member) -> B(add_member) -> C(add_member)
  auto user_a = make_user_key(1, 9211);
  auto user_b = make_user_key(1, 9212);
  auto user_c = make_user_key(1, 9213);
  env.inject_team_action(team_id, make_injected_add_member(user_a, 9211, 0, 0));
  env.inject_team_action(team_id, make_injected_add_member(user_b, 9212, 0, 0));
  env.inject_team_action(team_id, make_injected_add_member(user_c, 9213, 0, 0));

  int64_t last = fake.last_sequence();
  const atfw::dtmq::DChannelMessage* msg_a = fake.find_journal_message(last - 2);
  const atfw::dtmq::DChannelMessage* msg_b = fake.find_journal_message(last - 1);
  const atfw::dtmq::DChannelMessage* msg_c = fake.find_journal_message(last);
  CASE_EXPECT_TRUE(nullptr != msg_a && nullptr != msg_b && nullptr != msg_c);
  if (nullptr == msg_a || nullptr == msg_b || nullptr == msg_c) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 自定义批次: A 正常, B 的 hash_code 被破坏, C 正常 -> B 处 hash 链断裂
  atfw::dtmq::SSChannelEventSync event_sync;
  fake.fill_event_sync_metadata(event_sync);
  *event_sync.add_channel_message() = *msg_a;
  auto* corrupt_b = event_sync.add_channel_message();
  *corrupt_b = *msg_b;
  corrupt_b->set_hash_code(msg_b->hash_code() ^ 0x5A5AULL);
  *event_sync.add_channel_message() = *msg_c;

  CASE_EXPECT_EQ(0, env.sync_custom_event_sync(event_sync));

  // 前缀 A 已应用，B/C 未应用；room 保持主控(订阅层校验失败，房间不背锅)
  CASE_EXPECT_TRUE(room->find_member(user_a, false) != nullptr);
  CASE_EXPECT_EQ(nullptr, room->find_member(user_b, false).get());
  CASE_EXPECT_EQ(nullptr, room->find_member(user_c, false).get());
  CASE_EXPECT_TRUE(room->is_lock_holder());

  // 快照自愈: 强制快照重推后 B/C 均恢复
  CASE_EXPECT_EQ(0, env.sync(team_id, true));
  CASE_EXPECT_TRUE(room->find_member(user_b, false) != nullptr);
  CASE_EXPECT_TRUE(room->find_member(user_c, false) != nullptr);
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-10c: 批中坏 Any -> 前缀应用、room 退位(flush 丢弃待发通知)、后续事件只读应用；
//            含损坏日志的快照恢复失败并保持不可写，直到服务端修复/GC 掉损坏日志 ============
CASE_TEST(teamsvr_room_event, event_sync_corrupt_any_mid_batch) {
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

  // 批次: 邀请 V1(持锁时应用并入队通知) -> 坏 Any(退位) -> 邀请 V2(退位后只读应用)
  auto invitee1 = make_user_key(1, 9221);
  auto invitee2 = make_user_key(1, 9222);
  env.inject_team_action(team_id, [&members, &invitee1]() {
    atfw::team::DTeamAction action;
    auto* invitation = action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee1);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(9221));
    return action;
  }());
  const int64_t corrupt_sequence = fake.last_sequence() + 1;
  env.inject_raw_log(team_id, [](atfw::dtmq::DChannelMessageDetail& detail) {
    detail.mutable_event()->set_type_url("type.googleapis.com/atframework.team.DTeamAction");
    detail.mutable_event()->set_value(std::string("\xff\xfe\xfd\xfc"));
  });
  env.inject_team_action(team_id, [&members, &invitee2]() {
    atfw::team::DTeamAction action;
    auto* invitation = action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee2);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(9222));
    return action;
  }());

  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 批中退位后 flush 的锁 fencing 丢弃整个待发队列: V1/V2 均无 invited 通知(GAP-06 至多一次语义)
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 9221, atfw::team::DTeamMemberAction::kInvited));
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 9222, atfw::team::DTeamMemberAction::kInvited));
  CASE_EXPECT_FALSE(room->is_lock_holder());
  // V1(前缀)/V2(退位后只读)都已应用: 由 owner(满足 reject_invitation_role)拒绝验证 admission 状态
  for (const auto& invitee : {invitee1, invitee2}) {
    CASE_EXPECT_EQ(0, env.run("reject_prefix", [room, &members, &invitee](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::SSTeamRoomRejectInvitationReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.owner);
      protobuf_copy_message(*req.mutable_invitee(), invitee);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, req)));
    }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 含损坏日志的快照重推: 回放到损坏日志失败 -> 恢复失败, 房间保持不可写(座位带: 不基于半恢复状态写入)
  CASE_EXPECT_EQ(0, env.sync(team_id, true));
  CASE_EXPECT_FALSE(room->is_lock_holder());
  {
    int32_t write_ret =
        env.run("write_after_bad_snapshot", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::DTeamAction action;
          protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
        });
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE, write_ret);
  }

  // 服务端修复/GC 掉损坏日志后重推快照: 恢复成功并重新成为主控(锁仍在服务端归本节点)
  CASE_EXPECT_TRUE(fake.erase_log(corrupt_sequence));
  CASE_EXPECT_EQ(0, env.sync(team_id, true));
  CASE_EXPECT_TRUE(room->is_lock_holder());
  // 恢复不重放历史通知
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 9221, atfw::team::DTeamMemberAction::kInvited));
  CASE_EXPECT_EQ(0u, count_personal_actions(env, 9222, atfw::team::DTeamMemberAction::kInvited));
  // 恢复后房间可正常写入
  CASE_EXPECT_EQ(0, env.run("write_after_repair", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-10d/FLT-08: 批次来源节点切换不影响按 channel 分发 ============
CASE_TEST(teamsvr_room_event, event_sync_source_node_switch) {
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
  auto extra = make_user_key(1, 9231);
  env.inject_team_action(team_id, make_injected_add_member(extra, 9231, 0, 0));
  const auto* log = fake.find_journal_message(fake.last_sequence());
  CASE_EXPECT_TRUE(nullptr != log);
  if (nullptr == log) {
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  atfw::dtmq::SSChannelEventSync event_sync;
  fake.fill_event_sync_metadata(event_sync);
  *event_sync.add_channel_message() = *log;
  // 来源节点切换(模拟 dtmq 主备切换/迁移后的新节点下发)
  CASE_EXPECT_EQ(0, env.sync_custom_event_sync(event_sync, kDtmqProxyNodeId + 1));
  CASE_EXPECT_TRUE(room->find_member(extra, false) != nullptr);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ EVT-11: 非 DTeamAction 的 event 与非 event 日志推进 ack/oldest-log；destroy 置销毁 ============
CASE_TEST(teamsvr_room_event, non_team_action_logs_advance_ack) {
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

  // 非 DTeamAction 的 event(未知类型 URL)
  env.inject_raw_log(team_id, [](atfw::dtmq::DChannelMessageDetail& detail) {
    detail.mutable_event()->set_type_url("type.googleapis.com/com.example.UnknownEvent");
    detail.mutable_event()->set_value(std::string("payload"));
  });
  // 文本日志
  env.inject_raw_log(team_id, [](atfw::dtmq::DChannelMessageDetail& detail) { detail.set_text("ut-text-log"); });
  // noop 日志
  env.inject_raw_log(team_id, [](atfw::dtmq::DChannelMessageDetail& detail) { detail.set_noop(true); });

  int64_t last_sequence_before = fake.last_sequence();
  int64_t ack_before = room->debug_acknowledge_action_sequence();
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 这些日志不产生队伍副作用
  CASE_EXPECT_EQ(0u, env.personal_message_count());
  // room 侧消费进度: 未知 event/文本/noop 日志同样推进 ack 游标(不止 fixture journal)
  CASE_EXPECT_EQ(fake.last_sequence(), room->debug_acknowledge_action_sequence());
  CASE_EXPECT_GT(room->debug_acknowledge_action_sequence(), ack_before);
  CASE_EXPECT_TRUE(room->find_member(members.normal, false) != nullptr);

  // room 仍是主控(未损坏)
  CASE_EXPECT_TRUE(room->is_lock_holder());

  // 销毁 detail(非 event)置销毁标记
  env.inject_raw_log(team_id, [](atfw::dtmq::DChannelMessageDetail& detail) {
    detail.mutable_destroy()->mutable_removed_timepoint()->set_seconds(0);
  });
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_GT(fake.last_sequence(), last_sequence_before);
  // destroy 日志也被消费，ack 游标推进到最新日志序号
  CASE_EXPECT_EQ(fake.last_sequence(), room->debug_acknowledge_action_sequence());

  // 销毁后写入被拒(频道销毁使订阅失效，返回 DTMQ unavailable 或 destroyed)
  {
    size_t sends_before_destroy = fake.send_message_calls();
    int32_t ret = env.run("write_after_channel_destroy", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
    CASE_EXPECT_LT(ret, 0);
    CASE_EXPECT_EQ(sends_before_destroy, fake.send_message_calls());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ DATA-04: 同一请求内重复 key 拒绝且零写入；跨请求同 key 后写覆盖(GAP-11) ============
// 决策(2026-08-29): 单个请求里 keyed repeated 字段的 key 重复则失败拒绝(invalid-param 且零写入)，
// 多个请求之间 key 重复则后面的覆盖前面的。校验覆盖成员/队伍共享数据、admission 数据与更新条件
CASE_TEST(teamsvr_room_event, keyed_data_duplicate_key_rules) {
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

  // member_update 的 shared_member_data 单请求内重复 key -> invalid-param 且零写入
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    add_team_any_data_entry(update->mutable_shared_member_data(), 100, "dup-a");
    add_team_any_data_entry(update->mutable_shared_member_data(), 100, "dup-b");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                   env.run("dup_member_data", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                   }));
    CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  }

  // team_update 的 shared_team_data 单请求内重复 key -> invalid-param 且零写入
  {
    atfw::team::DTeamAction action;
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 200, "dup-a");
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 200, "dup-b");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                   env.run("dup_team_data", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                   }));
    CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  }

  // 更新条件里的 keyed 数据重复 key 同样拒绝(条件在权限检查前即被校验拦截)
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    auto* checker = update->add_condition();
    add_team_any_value_entry(checker->mutable_shared_team_data(), 300, "cond-a");
    add_team_any_value_entry(checker->mutable_shared_team_data(), 300, "cond-b");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                   env.run("dup_condition", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                   }));
    CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  }

  // 专用 admission RPC 携带重复 key -> invalid-param 且零写入、不发送受理回执
  {
    auto applicant = make_user_key(1, 8901);
    size_t personal_before = env.personal_message_count();
    atfw::team::SSTeamRoomAddJoinRequestReq req;
    protobuf_copy_message(*req.mutable_sender_user_key(), applicant);
    protobuf_copy_message(*req.mutable_join_request()->mutable_requester(), applicant);
    add_team_any_data_entry(req.mutable_join_request()->mutable_member_admission_data(), 9, "dup-a");
    add_team_any_data_entry(req.mutable_join_request()->mutable_member_admission_data(), 9, "dup-b");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                   env.run("dup_join_admission", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
                   }));
    CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
    CASE_EXPECT_EQ(personal_before, env.personal_message_count());

    auto invitee = make_user_key(1, 8902);
    atfw::team::SSTeamRoomAddInvitationReq invite_req;
    protobuf_copy_message(*invite_req.mutable_sender_user_key(), members.normal);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_inviter(), members.normal);
    protobuf_copy_message(*invite_req.mutable_invitation()->mutable_invitee(), invitee);
    add_team_any_data_entry(invite_req.mutable_invitation()->mutable_team_admission_data(), 10, "dup-a");
    add_team_any_data_entry(invite_req.mutable_invitation()->mutable_team_admission_data(), 10, "dup-b");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                   env.run("dup_invitation_admission", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
                   }));
    CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
    CASE_EXPECT_EQ(personal_before, env.personal_message_count());
  }

  // 跨请求同 key: 后写覆盖先写(两次合法写入各产生一条日志，最终状态以第二次为准)
  {
    atfw::team::DTeamAction first;
    auto* update_first = first.mutable_member_update();
    protobuf_copy_message(*update_first->mutable_user_key(), members.normal);
    add_team_any_data_entry(update_first->mutable_shared_member_data(), 400, "cross-first");
    CASE_EXPECT_EQ(0, env.run("cross_first", [room, &first](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, first)));
                   }));

    atfw::team::DTeamAction second;
    auto* update_second = second.mutable_member_update();
    protobuf_copy_message(*update_second->mutable_user_key(), members.normal);
    add_team_any_data_entry(update_second->mutable_shared_member_data(), 400, "cross-second");
    CASE_EXPECT_EQ(0, env.run("cross_second", [room, &second](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, second)));
                   }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_EQ(sends_before + 2, fake.send_message_calls());

    // 最终值以第二次为准: 等值条件 cross-second 通过、cross-first 拒绝
    atfw::team::DTeamAction check_second;
    auto* check_update = check_second.mutable_member_update();
    protobuf_copy_message(*check_update->mutable_user_key(), members.normal);
    auto* group = check_update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.normal);
    add_team_any_value_entry(group->mutable_member_condition()->mutable_shared_member_data(), 400, "cross-second");
    CASE_EXPECT_EQ(0, env.run("cond_match_second",
                             [room, &members, &check_second](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, members.owner, check_second)));
                   }));

    atfw::team::DTeamAction check_first;
    auto* check_first_update = check_first.mutable_member_update();
    protobuf_copy_message(*check_first_update->mutable_user_key(), members.normal);
    auto* group_first = check_first_update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group_first->mutable_user_key(), members.normal);
    add_team_any_value_entry(group_first->mutable_member_condition()->mutable_shared_member_data(), 400, "cross-first");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   env.run("cond_match_first",
                           [room, &members, &check_first](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(
                         RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, members.owner, check_first)));
                   }));
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ GAP-09(2026-08-29 澄清): member_update/team_update 共享数据的删除标记语义 ============
// 决策: "有 key 但 value 为空或 type_url 为空则删除该 key"仅适用于 kMemberUpdate/kTeamUpdate 的
// keyed 共享数据(其余入口为全量覆盖)。验证: 删除标记命中已存在 key 时移除该 key；未出现的 key 保留；
// 对不存在 key 的删除标记不产生状态变化(去重判定不写日志)；删除后条件观察确认数据确已移除
CASE_TEST(teamsvr_room_event, update_shared_data_delete_marker) {
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

  auto send_update = [&env, room](atfw::team::DTeamAction& action) {
    return env.run("send_update", [room, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    });
  };
  auto check_condition = [&env, room, &members](const atfw::team::DTeamAction& action) {
    return env.run("check_condition", [room, &members, &action](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, members.owner, action)));
    });
  };
  auto make_member_condition = [&members](int64_t key, const std::string& value) {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    auto* group = update->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.normal);
    add_team_any_value_entry(group->mutable_member_condition()->mutable_shared_member_data(), key, value);
    return action;
  };
  auto make_team_condition = [](int64_t key, const std::string& value) {
    atfw::team::DTeamAction action;
    auto* checker = action.mutable_team_update()->add_condition();
    add_team_any_value_entry(checker->mutable_shared_team_data(), key, value);
    return action;
  };

  // 预置成员共享数据 {101: m-101, 102: m-102} 与队伍共享数据 {201: t-201, 202: t-202}
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    add_team_any_data_entry(update->mutable_shared_member_data(), 101, "m-101");
    add_team_any_data_entry(update->mutable_shared_member_data(), 102, "m-102");
    CASE_EXPECT_EQ(0, send_update(action));
  }
  {
    atfw::team::DTeamAction action;
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 201, "t-201");
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 202, "t-202");
    CASE_EXPECT_EQ(0, send_update(action));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 删除标记移除已存在的 key(member 101 / team 201)，未提及的 key 保留(102/202)
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    add_team_any_data_entry(update->mutable_shared_member_data(), 101, "");  // payload 为空 -> 删除标记
    CASE_EXPECT_EQ(0, send_update(action));
  }
  {
    atfw::team::DTeamAction action;
    // 显式构造空 type_url 的删除标记(覆盖"type_url 为空"分支)
    auto* entry = add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 201, "t-201");
    entry->mutable_value()->mutable_data()->clear_value();
    entry->mutable_value()->mutable_data()->clear_type_url();
    CASE_EXPECT_EQ(0, send_update(action));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                 check_condition(make_member_condition(101, "m-101")));
  CASE_EXPECT_EQ(0, check_condition(make_member_condition(102, "m-102")));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                 check_condition(make_team_condition(201, "t-201")));
  CASE_EXPECT_EQ(0, check_condition(make_team_condition(202, "t-202")));

  // 对不存在 key 的删除标记不产生状态变化: 与空更新约定一致保留写入(锁探测)，
  // 但已存在的 key 不受影响(条件观察 102/202 仍通过，999 仍不存在)
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_member_update();
    protobuf_copy_message(*update->mutable_user_key(), members.normal);
    add_team_any_data_entry(update->mutable_shared_member_data(), 999, "");
    CASE_EXPECT_EQ(0, send_update(action));

    atfw::team::DTeamAction team_action;
    add_team_any_data_entry(team_action.mutable_team_update()->mutable_shared_team_data(), 999, "");
    CASE_EXPECT_EQ(0, send_update(team_action));
    CASE_EXPECT_EQ(0, env.sync(team_id));

    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_condition(make_member_condition(999, "")));
    CASE_EXPECT_EQ(0, check_condition(make_member_condition(102, "m-102")));
    CASE_EXPECT_EQ(0, check_condition(make_team_condition(202, "t-202")));
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

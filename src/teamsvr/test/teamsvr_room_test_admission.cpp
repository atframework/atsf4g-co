// Copyright 2026 atframework
//
// teamsvr-room 邀请、加入请求与个人通知用例(TEAM_ROOM_TEST_PLAN.md §4.3 ADM-01~16)。
// 断言维度: 频道日志形状(add_member/approve/reject 顺序)、个人频道 DTeamMemberAction 的目标与类型、
// PUBLIC 数据过滤(ADM-03)、通知频道以 room 本地记录为准(ADM-04)、重复请求幂等(ADM-02/10/16)。

#include "teamsvr_room_test_common.h"

namespace {
using namespace teamsvr_room_test;

// 构造 add_invitation 请求
atfw::team::SSTeamRoomAddInvitationReq make_add_invitation_req(const PROJECT_NAMESPACE_ID::DUserIDKey& sender,
                                                               const PROJECT_NAMESPACE_ID::DUserIDKey& inviter,
                                                               const PROJECT_NAMESPACE_ID::DUserIDKey& invitee,
                                                               int64_t expire_after_seconds = 0) {
  atfw::team::SSTeamRoomAddInvitationReq req;
  protobuf_copy_message(*req.mutable_sender_user_key(), sender);
  auto* invitation = req.mutable_invitation();
  protobuf_copy_message(*invitation->mutable_inviter(), inviter);
  protobuf_copy_message(*invitation->mutable_invitee(), invitee);
  protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
  if (expire_after_seconds > 0) {
    *invitation->mutable_expired_timepoint() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{expire_after_seconds});
  }
  return req;
}

atfw::team::SSTeamRoomAddJoinRequestReq make_add_join_request_req(const PROJECT_NAMESPACE_ID::DUserIDKey& applicant,
                                                                  int64_t expire_after_seconds = 0) {
  atfw::team::SSTeamRoomAddJoinRequestReq req;
  protobuf_copy_message(*req.mutable_sender_user_key(), applicant);
  auto* join_request = req.mutable_join_request();
  protobuf_copy_message(*join_request->mutable_requester(), applicant);
  protobuf_copy_message(*join_request->mutable_requester_private_channel(), make_personal_channel(applicant.user_id()));
  join_request->set_client_version("ut-join-v1");
  join_request->set_user_router_server_id(0x87654321);
  if (expire_after_seconds > 0) {
    *join_request->mutable_expired_timepoint() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{expire_after_seconds});
  }
  return req;
}
}  // namespace

// ============ ADM-01: 新邀请规范化 team id/过期时间，写 add_invitation，回环后通知 invitee ============
CASE_TEST(teamsvr_room_admission, add_invitation_normalization_and_notify) {
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

  auto invitee = make_user_key(1, 8001);
  auto req = make_add_invitation_req(members.normal, members.normal, invitee);

  auto& fake = env.channel(team_id);
  size_t sends_before = fake.send_message_calls();

  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
  }));

  // 恰好写一个 add_invitation
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());
  bool found_add_invitation = false;
  fake.foreach_team_action([&found_add_invitation, team_id, invitee](const atfw::dtmq::DChannelMessage& /*message*/,
                                                                     const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddInvitation) {
      found_add_invitation = true;
      // team id 被规范化为当前 room
      CASE_EXPECT_EQ(team_id, action.add_invitation().team_key().team_id());
      CASE_EXPECT_EQ(invitee.user_id(), action.add_invitation().invitee().user_id());
      // 缺省过期时间由 room 补齐(默认 invitation_expire)
      CASE_EXPECT_GT(action.add_invitation().expired_timepoint().seconds(), 0);
    }
    return true;
  });
  CASE_EXPECT_TRUE(found_add_invitation);

  // 事件回环后只向 invitee 发送 invited 个人通知
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, env.personal_message_count());
  if (1 == env.personal_message_count()) {
    const auto& record = env.personal_messages()[0];
    CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kInvited, record.action.action_case());
    CASE_EXPECT_EQ(make_personal_channel(8001).channel_id(), record.channel.channel_id());
    CASE_EXPECT_EQ(invitee.user_id(), record.action.invited().invitee().user_id());
  }

  // 再次同步无新事件时不产生第二条通知
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, env.personal_message_count());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-02: 完全重复邀请不追加日志 ============
CASE_TEST(teamsvr_room_admission, duplicate_invitation_no_extra_log) {
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

  auto invitee = make_user_key(1, 8011);
  auto req = make_add_invitation_req(members.normal, members.normal, invitee);
  auto& fake = env.channel(team_id);

  CASE_EXPECT_EQ(0, env.run("add_invitation_1", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  size_t sends_after_first = fake.send_message_calls();
  size_t personal_after_first = env.personal_message_count();

  // 完全相同的重复邀请: 数据未变化，不追加日志、不重复通知
  CASE_EXPECT_EQ(0, env.run("add_invitation_2", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
  }));
  CASE_EXPECT_EQ(sends_after_first, fake.send_message_calls());

  // 更晚的过期时间变化时更新且不缩短有效期
  auto req_later = make_add_invitation_req(members.normal, members.normal, invitee, 3600);
  CASE_EXPECT_EQ(0, env.run("add_invitation_3", [room, &req_later](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req_later)));
  }));
  CASE_EXPECT_EQ(sends_after_first + 1, fake.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  // 刷新事件回环会补发一次 invited 通知(现有契约: 事件应用后补发)
  CASE_EXPECT_EQ(personal_after_first + 1, env.personal_message_count());

  // 更早的过期时间不缩短有效期: 数据无变化时不追加日志
  auto req_earlier = make_add_invitation_req(members.normal, members.normal, invitee, 1);
  CASE_EXPECT_EQ(0, env.run("add_invitation_4", [room, &req_earlier](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req_earlier)));
  }));
  CASE_EXPECT_EQ(sends_after_first + 1, fake.send_message_calls());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-03: invited 只包含 PUBLIC 权限数据 ============
CASE_TEST(teamsvr_room_admission, invited_only_public_data) {
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

  // 写入 PUBLIC 与 MEMBER 两种权限的队伍/成员数据
  CASE_EXPECT_EQ(0, env.run("setup_data", [room, &members](rpc::context& ctx) -> rpc::result_code_type {
    {
      atfw::team::DTeamAction action;
      auto* team_update = action.mutable_team_update();
      add_team_any_data_entry(team_update->mutable_shared_team_data(), 1, "public-team-data")
          ->mutable_value()
          ->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC);
      add_team_any_data_entry(team_update->mutable_shared_team_data(), 2, "member-team-data")
          ->mutable_value()
          ->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
      int32_t ret = RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action));
      if (0 != ret) {
        RPC_RETURN_CODE(ret);
      }
    }
    {
      atfw::team::DTeamAction action;
      auto* member_update = action.mutable_member_update();
      add_team_any_data_entry(member_update->mutable_shared_member_data(), 3, "public-member-data")
          ->mutable_value()
          ->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC);
      add_team_any_data_entry(member_update->mutable_shared_member_data(), 4, "secret-member-data")
          ->mutable_value()
          ->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
      protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), members.normal);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto invitee = make_user_key(1, 8021);
  auto req = make_add_invitation_req(members.normal, members.normal, invitee);
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  CASE_EXPECT_EQ(1u, env.personal_message_count());
  if (1 == env.personal_message_count()) {
    const auto& invited = env.personal_messages()[0].action.invited();
    // PUBLIC 的队伍数据保留
    CASE_EXPECT_EQ(1, invited.team_admission_data_size());
    if (1 == invited.team_admission_data_size()) {
      CASE_EXPECT_EQ(1, invited.team_admission_data(0).key());
      CASE_EXPECT_EQ("public-team-data", invited.team_admission_data(0).value().data().value());
    }
    // 每个成员仅 PUBLIC 数据下发；secret 数据不泄露
    bool found_member_entry = false;
    for (const auto& member_data : invited.member_admission_data()) {
      if (member_data.user_key().user_id() == members.normal.user_id()) {
        found_member_entry = true;
        CASE_EXPECT_EQ(1, member_data.member_admission_data_size());
        if (1 == member_data.member_admission_data_size()) {
          CASE_EXPECT_EQ(3, member_data.member_admission_data(0).key());
          CASE_EXPECT_EQ("public-member-data", member_data.member_admission_data(0).value().data().value());
        }
      }
      for (const auto& kv : member_data.member_admission_data()) {
        CASE_EXPECT_NE("secret-member-data", kv.value().data().value());
      }
    }
    CASE_EXPECT_TRUE(found_member_entry);
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-04/05: 接受邀请写 add_member + approve；成员数据来自 invitee 本人请求 ============
CASE_TEST(teamsvr_room_admission, approve_invitation_writes_and_notifies) {
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

  auto invitee = make_user_key(1, 8031);
  auto invite_req = make_add_invitation_req(members.normal, members.normal, invitee);
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  size_t personal_after_invite = env.personal_message_count();

  // invitee 本人接受: 携带自己的版本/路由/共享数据(ADM-05: 不能由 inviter 伪造)
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
  approve_req.set_client_version("invitee-version-1.0");
  approve_req.set_user_router_server_id(0x11223344);
  add_team_any_data_entry(approve_req.mutable_shared_member_data(), 7, "invitee-own-data");

  auto& fake = env.channel(team_id);
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("approve_invitation", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  }));

  // 依次写 add_member 和 approve_invitation 两个事件
  CASE_EXPECT_EQ(sends_before + 2, fake.send_message_calls());
  int add_member_index = -1;
  int approve_index = -1;
  int visit_index = 0;
  fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddMember &&
        action.add_member().user_key().user_id() == invitee.user_id()) {
      add_member_index = visit_index;
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, action.add_member().role());
      CASE_EXPECT_EQ("invitee-version-1.0", action.add_member().client_version());
      CASE_EXPECT_EQ(0x11223344u, action.add_member().user_router_server_id());
      const auto& shared_data = action.add_member().shared_member_data();
      auto data_it = std::find_if(shared_data.begin(), shared_data.end(),
                                  [](const atfw::team::DTeamAnyDataWithKey& entry) { return entry.key() == 7; });
      CASE_EXPECT_TRUE(data_it != shared_data.end());
      if (data_it != shared_data.end()) {
        CASE_EXPECT_EQ("invitee-own-data", data_it->value().data().value());
      }
    } else if (action.action_case() == atfw::team::DTeamAction::kApproveInvitation) {
      approve_index = visit_index;
    }
    ++visit_index;
    return true;
  });
  CASE_EXPECT_TRUE(add_member_index >= 0 && approve_index >= 0);
  CASE_EXPECT_LT(add_member_index, approve_index);

  // 事件回环后成员入队并收到 joined_team；通知频道以 room 本地记录为准
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto new_member = room->find_member(invitee, false);
  CASE_EXPECT_TRUE(!!new_member);
  if (new_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, new_member->member_data.role());
    CASE_EXPECT_EQ("invitee-version-1.0", new_member->member_data.client_version());
  }
  CASE_EXPECT_EQ(personal_after_invite + 1, env.personal_message_count());
  if (personal_after_invite + 1 == env.personal_message_count()) {
    const auto& record = env.personal_messages().back();
    CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kJoinedTeam, record.action.action_case());
    CASE_EXPECT_EQ(make_personal_channel(8031).channel_id(), record.channel.channel_id());
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-07: 拒绝/撤回邀请写 reject_invitation，invitee 收个人回执 ============
CASE_TEST(teamsvr_room_admission, reject_invitation_notify) {
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

  auto invitee = make_user_key(1, 8041);
  auto invite_req = make_add_invitation_req(members.normal, members.normal, invitee);
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  size_t personal_after_invite = env.personal_message_count();

  // 被邀请人本人拒绝
  atfw::team::SSTeamRoomRejectInvitationReq reject_req;
  protobuf_copy_message(*reject_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*reject_req.mutable_invitee(), invitee);
  auto& fake = env.channel(team_id);
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("reject_invitation", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, reject_req)));
  }));
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(personal_after_invite + 1, env.personal_message_count());
  if (personal_after_invite + 1 == env.personal_message_count()) {
    CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kRejectInvitation,
                   env.personal_messages().back().action.action_case());
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-09/10: 加入请求规范化与重复请求 ============
CASE_TEST(teamsvr_room_admission, join_request_normalization_and_duplicate) {
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

  auto applicant = make_user_key(1, 8051);
  auto req = make_add_join_request_req(applicant);
  auto& fake = env.channel(team_id);
  size_t sends_before = fake.send_message_calls();

  CASE_EXPECT_EQ(0, env.run("add_join_1", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
  }));
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

  // 日志内容: team id 规范化、默认过期时间、申请人频道/版本/路由/admission data 保留
  bool found = false;
  fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddJoinRequest) {
      found = true;
      const auto& join_request = action.add_join_request();
      CASE_EXPECT_EQ(team_id, join_request.team_key().team_id());
      CASE_EXPECT_EQ(applicant.user_id(), join_request.requester().user_id());
      CASE_EXPECT_GT(join_request.expired_timepoint().seconds(), 0);
      CASE_EXPECT_EQ("ut-join-v1", join_request.client_version());
      CASE_EXPECT_EQ(0x87654321u, join_request.user_router_server_id());
      CASE_EXPECT_EQ(make_personal_channel(8051).channel_id(), join_request.requester_private_channel().channel_id());
    }
    return true;
  });
  CASE_EXPECT_TRUE(found);

  // 申请人收到一次 apply_join_request 受理回执(携带房间规范化后的过期时间，对端以此维护本地待处理列表);
  // 队伍成员不额外收个人通知(通过队伍频道日志感知)
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, env.personal_message_count());
  if (1 == env.personal_message_count()) {
    const auto& record = env.personal_messages()[0];
    CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kApplyJoinRequest, record.action.action_case());
    CASE_EXPECT_EQ(make_personal_channel(8051).channel_id(), record.channel.channel_id());
    CASE_EXPECT_EQ(applicant.user_id(), record.action.apply_join_request().requester().user_id());
    CASE_EXPECT_EQ(team_id, record.action.apply_join_request().team_key().team_id());
    CASE_EXPECT_GT(record.action.apply_join_request().expired_timepoint().seconds(), 0);
  }

  // 完全重复的加入请求不追加日志，也不重复发送受理回执
  size_t sends_after_first = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("add_join_2", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
  }));
  CASE_EXPECT_EQ(sends_after_first, fake.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, env.personal_message_count());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-11/12: 批准/拒绝加入请求 ============
CASE_TEST(teamsvr_room_admission, approve_and_reject_join_request) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  {
    // 批准: 依次写 add_member + approve_join_request；成员数据来自原申请；申请人收 joined_team
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (room) {
      auto applicant = make_user_key(1, 8061);
      auto req = make_add_join_request_req(applicant);
      CASE_EXPECT_EQ(0, env.run("add_join", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      }));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      atfw::team::SSTeamRoomApproveJoinRequestReq approve_req;
      protobuf_copy_message(*approve_req.mutable_sender_user_key(), members.normal);
      protobuf_copy_message(*approve_req.mutable_applicant(), applicant);

      auto& fake = env.channel(team_id);
      size_t sends_before = fake.send_message_calls();
      CASE_EXPECT_EQ(0, env.run("approve_join", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
      }));
      CASE_EXPECT_EQ(sends_before + 2, fake.send_message_calls());

      // add_member 数据来自原申请(client_version/router)
      bool found_add = false;
      fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kAddMember &&
            action.add_member().user_key().user_id() == applicant.user_id()) {
          found_add = true;
          CASE_EXPECT_EQ("ut-join-v1", action.add_member().client_version());
          CASE_EXPECT_EQ(0x87654321u, action.add_member().user_router_server_id());
        }
        return true;
      });
      CASE_EXPECT_TRUE(found_add);

      CASE_EXPECT_EQ(0, env.sync(team_id));
      CASE_EXPECT_TRUE(room->find_member(applicant, false) != nullptr);
      // 按身份断言: 申请人恰好收到一次 joined_team(避免与其他异步到达的记录耦合)
      size_t joined_notify_count = 0;
      for (const auto& record : env.personal_messages()) {
        if (record.action.action_case() == atfw::team::DTeamMemberAction::kJoinedTeam &&
            record.channel.channel_id() == make_personal_channel(8061).channel_id()) {
          ++joined_notify_count;
        }
      }
      CASE_EXPECT_EQ(1u, joined_notify_count);
      env.clear_rooms();
    }
  }

  {
    // 拒绝: 写 reject_join_request；申请人收拒绝通知；队伍成员不额外收个人通知
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (room) {
      auto applicant = make_user_key(1, 8062);
      auto req = make_add_join_request_req(applicant);
      CASE_EXPECT_EQ(0, env.run("add_join", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      }));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      atfw::team::SSTeamRoomRejectJoinRequestReq reject_req;
      protobuf_copy_message(*reject_req.mutable_sender_user_key(), members.normal);
      protobuf_copy_message(*reject_req.mutable_applicant(), applicant);

      auto& fake = env.channel(team_id);
      size_t sends_before = fake.send_message_calls();
      CASE_EXPECT_EQ(0, env.run("reject_join", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_join_request(ctx, reject_req)));
      }));
      CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

      CASE_EXPECT_EQ(0, env.sync(team_id));
      CASE_EXPECT_EQ(nullptr, room->find_member(applicant, false).get());
      size_t reject_notify_count = 0;
      for (const auto& record : env.personal_messages()) {
        if (record.action.action_case() == atfw::team::DTeamMemberAction::kRejectJoinRequest &&
            record.channel.channel_id() == make_personal_channel(8062).channel_id()) {
          ++reject_notify_count;
        }
      }
      CASE_EXPECT_EQ(1u, reject_notify_count);
      env.clear_rooms();
    }
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-08/13: 过期邀请/申请 approve 返回 not-found；reject 幂等成功 ============
CASE_TEST(teamsvr_room_admission, expired_admission_semantics) {
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

  // 已过期的邀请(过期时间在过去)
  auto invitee = make_user_key(1, 8071);
  auto invite_req = make_add_invitation_req(members.normal, members.normal, invitee);
  *invite_req.mutable_invitation()->mutable_expired_timepoint() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() - std::chrono::seconds{1});
  CASE_EXPECT_EQ(0, env.run("add_expired_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);
  size_t sends_before = fake.send_message_calls();

  // 过期邀请 approve 返回 not-found 且零写入
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                 env.run("approve_expired", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                 }));
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());

  // 过期邀请 reject 幂等成功(视为已处理)
  atfw::team::SSTeamRoomRejectInvitationReq reject_req;
  protobuf_copy_message(*reject_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*reject_req.mutable_invitee(), invitee);
  CASE_EXPECT_EQ(0, env.run("reject_expired", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, reject_req)));
  }));

  // 已过期的加入申请
  auto applicant = make_user_key(1, 8072);
  auto join_req = make_add_join_request_req(applicant);
  *join_req.mutable_join_request()->mutable_expired_timepoint() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() - std::chrono::seconds{1});
  CASE_EXPECT_EQ(0, env.run("add_expired_join", [room, &join_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, join_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  sends_before = fake.send_message_calls();
  atfw::team::SSTeamRoomApproveJoinRequestReq approve_join;
  protobuf_copy_message(*approve_join.mutable_sender_user_key(), members.normal);
  protobuf_copy_message(*approve_join.mutable_applicant(), applicant);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND,
                 env.run("approve_expired_join", [room, &approve_join](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_join)));
                 }));
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());

  atfw::team::SSTeamRoomRejectJoinRequestReq reject_join;
  protobuf_copy_message(*reject_join.mutable_sender_user_key(), members.normal);
  protobuf_copy_message(*reject_join.mutable_applicant(), applicant);
  CASE_EXPECT_EQ(0, env.run("reject_expired_join", [room, &reject_join](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_join_request(ctx, reject_join)));
  }));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-14: 失锁(备用)节点应用历史日志不发送个人通知 ============
CASE_TEST(teamsvr_room_admission, standby_node_no_personal_side_effects) {
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

  // 模拟锁被其他节点接管: 服务端改锁(镜像 set_lock 追加 kResetLock 日志)并推送，本节点退位为备用
  auto& fake = env.channel(team_id);
  ::atfw::dtmq::DChannelOptimisticLock new_lock;
  new_lock.set_lock_holder("teamsvr-room:another-node");
  *new_lock.mutable_timeout() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{3600});
  fake.set_lock(new_lock);
  CASE_EXPECT_EQ(0, env.run("push_lock_takeover", [&env, team_id](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.push_channel_events(ctx, make_team_key(team_id), false)));
  }));
  CASE_EXPECT_FALSE(room->is_lock_holder());

  size_t personal_before = env.personal_message_count();

  // 备用节点应用新的历史 admission 日志: 不发送任何个人通知
  auto invitee = make_user_key(1, 8081);
  env.inject_team_action(team_id, [&members, &invitee, team_id]() {
    atfw::team::DTeamAction action;
    auto* invitation = action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_inviter(), members.normal);
    protobuf_copy_message(*invitation->mutable_invitee(), invitee);
    protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(invitee.user_id()));
    protobuf_copy_message(*invitation->mutable_team_key(), make_team_key(team_id));
    return action;
  }());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-16: 目标已是成员时再次 approve 不重复添加、仍清理 admission ============
CASE_TEST(teamsvr_room_admission, approve_when_already_member) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  {
    // 邀请路径: invitee 已是成员(直接添加)后 approve
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (room) {
      auto invitee = make_user_key(1, 8091);
      // 先建立有效邀请(此时还不是成员)
      auto invite_req = make_add_invitation_req(members.normal, members.normal, invitee);
      CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
      }));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      // 双击/断线重连重试场景: approve 到达前 invitee 已被直接添加为成员
      CASE_EXPECT_EQ(0, env.run("add_member", [room, &invitee](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::DTeamAction action;
        auto* add_member = action.mutable_add_member();
        protobuf_copy_message(*add_member->mutable_user_key(), invitee);
        protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(invitee.user_id()));
        add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      CASE_EXPECT_EQ(0, env.sync(team_id));
      size_t personal_before = env.personal_message_count();

      // approve: 不再写第二个 add_member，仍写 approve 事件清理邀请
      atfw::team::SSTeamRoomApproveInvitationReq approve_req;
      protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
      protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
      auto& fake = env.channel(team_id);
      size_t sends_before = fake.send_message_calls();
      CASE_EXPECT_EQ(0, env.run("approve_as_member", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
      }));
      CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());  // 只有 approve 事件
      CASE_EXPECT_EQ(0, env.sync(team_id));
      // 已是成员: 补一次 joined_team 通知(重试幂等场景的当前契约)
      CASE_EXPECT_EQ(personal_before + 1, env.personal_message_count());

      env.clear_rooms();
    }
  }

  {
    // 加入请求路径: 申请人已被直接添加后 approve
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (room) {
      auto applicant = make_user_key(1, 8092);
      auto join_req = make_add_join_request_req(applicant);
      CASE_EXPECT_EQ(0, env.run("add_join", [room, &join_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, join_req)));
      }));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      CASE_EXPECT_EQ(0, env.run("add_member", [room, &applicant](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::DTeamAction action;
        auto* add_member = action.mutable_add_member();
        protobuf_copy_message(*add_member->mutable_user_key(), applicant);
        protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(applicant.user_id()));
        add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
      }));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      atfw::team::SSTeamRoomApproveJoinRequestReq approve_req;
      protobuf_copy_message(*approve_req.mutable_sender_user_key(), members.normal);
      protobuf_copy_message(*approve_req.mutable_applicant(), applicant);
      auto& fake = env.channel(team_id);
      size_t sends_before = fake.send_message_calls();
      CASE_EXPECT_EQ(
          0, env.run("approve_join_as_member", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
          }));
      CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());  // 只有 approve 事件

      env.clear_rooms();
    }
  }

  CASE_EXPECT_EQ(0, env.stop());
}

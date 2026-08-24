// Copyright 2026 atframework
//
// teamsvr-room 权限与"拒绝时零写入"用例(TEAM_ROOM_TEST_PLAN.md §4.2 PERM-01~14)。
// 所有失败断言均带统一零写入门禁: team 房间频道 send/update/reset_lock/destroy 调用数与
// 个人频道通知数保持不变。

#include "teamsvr_room_test_common.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframework/testing/ss_action.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <logic/action/task_action_send_message.h>
#include <rpc/team/teamroomservice.atfw.gen.h>

namespace {
using namespace teamsvr_room_test;

// 经由真实 task_action_send_message(路由+权限+专用流程转换)驱动写路径，返回 action 结果码。
// 注意: 业务失败码经 set_response_code 下发，task 结果固定为 EN_SUCCESS(0)，因此业务码断言
// 使用 room 专用方法/check_action_permission 路径；此处用于全链路零写入门禁与成功路径验证。
int32_t run_send_message_action(room_test_env& env, int64_t team_id, const PROJECT_NAMESPACE_ID::DUserIDKey& sender,
                                const atfw::team::DTeamAction& action) {
  return env.run("send_message_action", [team_id, sender, action](rpc::context& ctx) -> rpc::result_code_type {
    atframework::testing::ss_action_invoke_options invoke_options{rpc::team::packer::get_full_name_of_send_message()};
    invoke_options.source.node_id = kDtmqProxyNodeId;
    atfw::team::SSTeamRoomSendMessageReq request;
    protobuf_copy_message(*request.mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*request.mutable_sender_user_key(), sender);
    protobuf_copy_message(*request.mutable_action(), action);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
        atframework::testing::invoke_ss_action<task_action_send_message>(ctx, request, invoke_options)));
  });
}

// 直接调用 check_action_permission 断言精确错误码(协程内 await 同步结果)
int32_t check_permission(room_test_env& env, const team_room::ptr_t& room,
                         const PROJECT_NAMESPACE_ID::DUserIDKey& sender, const atfw::team::DTeamAction& action) {
  return env.run("check_permission",
                 [room, sender, action](ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->check_action_permission(sender, action)));
                 });
}

// 零写入门禁快照
struct write_counters {
  size_t send = 0;
  size_t update = 0;
  size_t reset_lock = 0;
  size_t destroy = 0;
  size_t personal = 0;
};

write_counters snapshot_counters(room_test_env& env, fake_team_room_channel& fake) {
  return write_counters{fake.send_message_calls(), fake.update_calls(), fake.reset_lock_calls(), fake.destroy_calls(),
                        env.personal_message_count()};
}

void expect_no_write(fake_team_room_channel& fake, room_test_env& env, const write_counters& before) {
  CASE_EXPECT_EQ(before.send, fake.send_message_calls());
  CASE_EXPECT_EQ(before.update, fake.update_calls());
  CASE_EXPECT_EQ(before.reset_lock, fake.reset_lock_calls());
  CASE_EXPECT_EQ(before.destroy, fake.destroy_calls());
  CASE_EXPECT_EQ(before.personal, env.personal_message_count());
}

atfw::team::DTeamAction make_remove_action(const PROJECT_NAMESPACE_ID::DUserIDKey& key) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), key);
  action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
  return action;
}

atfw::team::DTeamAction make_add_member_action(const PROJECT_NAMESPACE_ID::DUserIDKey& key,
                                               const atfw::dtmq::DChannelIdKey& channel,
                                               atfw::team::EnTeamPermissionRole role) {
  atfw::team::DTeamAction action;
  auto* add_member = action.mutable_add_member();
  protobuf_copy_message(*add_member->mutable_user_key(), key);
  protobuf_copy_message(*add_member->mutable_user_channel(), channel);
  add_member->set_role(role);
  return action;
}

atfw::team::DTeamAction make_member_update_action(const PROJECT_NAMESPACE_ID::DUserIDKey& key) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_member_update()->mutable_user_key(), key);
  action.mutable_member_update()->set_client_version("ut-perm-version");
  return action;
}

atfw::team::DTeamAction make_team_update_action() {
  atfw::team::DTeamAction action;
  auto* data = (*action.mutable_team_update()->mutable_shared_team_data())[42].mutable_data();
  data->set_value(std::string("ut-team-data"));
  return action;
}

atfw::team::DTeamAction make_election_action(const PROJECT_NAMESPACE_ID::DUserIDKey& key) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_election_captain()->mutable_user_key(), key);
  return action;
}

atfw::team::DTeamAction make_destroy_action(int64_t team_id) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_destroy_team(), make_team_key(team_id));
  return action;
}

atfw::team::DTeamAction make_invitation_action(const PROJECT_NAMESPACE_ID::DUserIDKey& inviter,
                                               const PROJECT_NAMESPACE_ID::DUserIDKey& target) {
  atfw::team::DTeamAction action;
  auto* invitation = action.mutable_add_invitation();
  protobuf_copy_message(*invitation->mutable_inviter(), inviter);
  protobuf_copy_message(*invitation->mutable_invitee(), target);
  protobuf_copy_message(*invitation->mutable_invitee_private_channel(), make_personal_channel(target.user_id()));
  return action;
}
}  // namespace

// ============ PERM-01: remove_member 默认门槛 ============
CASE_TEST(teamsvr_room_permission, remove_member_default_roles) {
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

  // NORMAL 删除自己成功
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_remove_action(members.normal)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());
  // 重新加回 normal 供后续断言
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner,
                                            make_add_member_action(members.normal, members.normal_channel,
                                                                   atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 游客删除自己: not in team
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
                 check_permission(env, room, members.outsider, make_remove_action(members.outsider)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.outsider, make_remove_action(members.outsider)));
  expect_no_write(fake, env, before);

  // NORMAL 删除他人: 默认需要 ADMIN
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.normal, make_remove_action(members.admin)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_remove_action(members.admin)));
  expect_no_write(fake, env, before);

  // ADMIN 删除他人成功
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, make_remove_action(members.normal)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-02: add_member 门槛与约束 ============
CASE_TEST(teamsvr_room_permission, add_member_constraints) {
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
  auto target_key = make_user_key(1, 7101);
  auto target_channel = make_personal_channel(7101);

  // NORMAL 直接添加成员失败(默认 ADMIN)
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
      check_permission(env, room, members.normal,
                       make_add_member_action(target_key, target_channel, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, run_send_message_action(
                        env, team_id, members.normal,
                        make_add_member_action(target_key, target_channel, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  expect_no_write(fake, env, before);

  // 游客添加失败: not in team
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
      check_permission(env, room, members.outsider,
                       make_add_member_action(target_key, target_channel, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, run_send_message_action(
                        env, team_id, members.outsider,
                        make_add_member_action(target_key, target_channel, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  expect_no_write(fake, env, before);

  // ADMIN 添加成功
  CASE_EXPECT_EQ(0, run_send_message_action(
                        env, team_id, members.admin,
                        make_add_member_action(target_key, target_channel, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(target_key, false) != nullptr);

  // 重复成员失败
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM,
      check_permission(env, room, members.admin,
                       make_add_member_action(target_key, target_channel, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, run_send_message_action(
                        env, team_id, members.admin,
                        make_add_member_action(target_key, target_channel, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  expect_no_write(fake, env, before);

  // 非法 key 失败
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                 check_permission(env, room, members.admin,
                                  make_add_member_action(make_user_key(0, 0), target_channel,
                                                         atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin,
                                            make_add_member_action(make_user_key(0, 0), target_channel,
                                                                   atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  expect_no_write(fake, env, before);

  // 不能授予高于操作者的角色: ADMIN 添加 OWNER 角色成员失败
  before = snapshot_counters(env, fake);
  auto high_key = make_user_key(1, 7102);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.admin,
                                  make_add_member_action(high_key, make_personal_channel(7102),
                                                         atfw::team::EN_TEAM_MEMBER_ROLE_OWNER)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin,
                                            make_add_member_action(high_key, make_personal_channel(7102),
                                                                   atfw::team::EN_TEAM_MEMBER_ROLE_OWNER)));
  expect_no_write(fake, env, before);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-03: member_update 门槛 ============
CASE_TEST(teamsvr_room_permission, member_update_roles) {
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

  // 任意成员更新自己成功
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_member_update_action(members.normal)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto normal_member = room->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!normal_member);
  if (normal_member) {
    CASE_EXPECT_EQ("ut-perm-version", normal_member->member_data.client_version());
  }

  // NORMAL 更新他人失败
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.normal, make_member_update_action(members.admin)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_member_update_action(members.admin)));
  expect_no_write(fake, env, before);

  // ADMIN 更新他人成功
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, make_member_update_action(members.normal)));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 游客更新自己: not in team
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
                 check_permission(env, room, members.outsider, make_member_update_action(members.outsider)));
  CASE_EXPECT_EQ(0,
                 run_send_message_action(env, team_id, members.outsider, make_member_update_action(members.outsider)));
  expect_no_write(fake, env, before);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-04: team_update 默认与自定义门槛 ============
CASE_TEST(teamsvr_room_permission, team_update_roles) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  {
    // 默认配置: NORMAL 可更新，游客不可
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (room) {
      CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_team_update_action()));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      auto& fake = env.channel(team_id);
      auto before = snapshot_counters(env, fake);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
                     check_permission(env, room, members.outsider, make_team_update_action()));
      CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.outsider, make_team_update_action()));
      expect_no_write(fake, env, before);
      env.clear_rooms();
    }
  }

  {
    // 自定义 ADMIN 门槛: NORMAL 失败、ADMIN 成功
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (room) {
      atfw::team::DTeamConfigure configure;
      configure.set_update_team_data_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
      atfw::team::DTeamAction config_action;
      protobuf_copy_message(*config_action.mutable_team_update()->mutable_configure(), configure);
      CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, config_action));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      auto& fake = env.channel(team_id);
      auto before = snapshot_counters(env, fake);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                     check_permission(env, room, members.normal, make_team_update_action()));
      CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_team_update_action()));
      expect_no_write(fake, env, before);

      CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, make_team_update_action()));
      CASE_EXPECT_EQ(0, env.sync(team_id));
      env.clear_rooms();
    }
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-05: election_captain 门槛 ============
CASE_TEST(teamsvr_room_permission, election_captain_roles) {
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

  // 非队长 ADMIN 转让失败(无条件修改队长固定要求 OWNER)
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.admin, make_election_action(members.normal)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, make_election_action(members.normal)));
  expect_no_write(fake, env, before);

  // 目标非成员失败
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND,
                 check_permission(env, room, members.owner, make_election_action(members.outsider)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, make_election_action(members.outsider)));
  expect_no_write(fake, env, before);

  // 当前队长(OWNER)主动转让成功，目标成为 OWNER
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, make_election_action(members.admin)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto admin_member = room->find_member(members.admin, false);
  CASE_EXPECT_TRUE(!!admin_member);
  if (admin_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, admin_member->member_data.role());
  }

  // 新队长继续转让给 normal
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, make_election_action(members.normal)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto normal_member = room->find_member(members.normal, false);
  CASE_EXPECT_TRUE(!!normal_member);
  if (normal_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, normal_member->member_data.role());
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-06: destroy_team 固定 OWNER 门槛 ============
CASE_TEST(teamsvr_room_permission, destroy_team_owner_only) {
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

  // 配置字段不能降低固定门槛
  atfw::team::DTeamConfigure configure;
  configure.set_manage_member_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  atfw::team::DTeamAction config_action;
  protobuf_copy_message(*config_action.mutable_team_update()->mutable_configure(), configure);
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, config_action));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // ADMIN 解散失败
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.admin, make_destroy_action(team_id)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, make_destroy_action(team_id)));
  expect_no_write(fake, env, before);

  // 游客解散失败
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
                 check_permission(env, room, members.outsider, make_destroy_action(team_id)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.outsider, make_destroy_action(team_id)));
  expect_no_write(fake, env, before);

  // OWNER 解散成功(写 destroy_team 事件)
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, make_destroy_action(team_id)));
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-07: add_invitation 门槛 ============
CASE_TEST(teamsvr_room_permission, add_invitation_roles) {
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
  auto invitee = make_user_key(1, 7201);

  // 伪造 inviter(sender != inviter)失败
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.normal, make_invitation_action(members.owner, invitee)));
  CASE_EXPECT_EQ(0,
                 run_send_message_action(env, team_id, members.normal, make_invitation_action(members.owner, invitee)));
  expect_no_write(fake, env, before);

  // 游客发起失败(非成员)
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
                 check_permission(env, room, members.outsider, make_invitation_action(members.outsider, invitee)));
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.outsider, make_invitation_action(members.outsider, invitee)));
  expect_no_write(fake, env, before);

  // 默认任意成员(NORMAL)可以邀请
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.normal, make_invitation_action(members.normal, invitee)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  // 事件回环后向 invitee 发送 invited 个人通知
  CASE_EXPECT_EQ(1u, env.personal_message_count());
  if (1 == env.personal_message_count()) {
    CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kInvited, env.personal_messages()[0].action.action_case());
  }

  // 自定义 ADMIN 门槛后 NORMAL 失败
  atfw::team::DTeamConfigure configure;
  configure.set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
  atfw::team::DTeamAction config_action;
  protobuf_copy_message(*config_action.mutable_team_update()->mutable_configure(), configure);
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, config_action));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto invitee2 = make_user_key(1, 7202);
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.normal, make_invitation_action(members.normal, invitee2)));
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.normal, make_invitation_action(members.normal, invitee2)));
  expect_no_write(fake, env, before);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-08: approve_invitation 仅被邀请人本人 ============
CASE_TEST(teamsvr_room_permission, approve_invitation_self_only) {
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

  auto invitee = make_user_key(1, 7301);

  // 建立邀请
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &members, &invitee](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomAddInvitationReq req;
    protobuf_copy_message(*req.mutable_sender_user_key(), members.normal);
    protobuf_copy_message(*req.mutable_invitation(),
                          *make_invitation_action(members.normal, invitee).mutable_add_invitation());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);

  // 任何第三方(含 OWNER)不能代为同意
  auto before = snapshot_counters(env, fake);
  auto personal_before = env.personal_message_count();
  {
    atfw::team::DTeamAction action;
    auto* approve = action.mutable_approve_invitation();
    protobuf_copy_message(*approve->mutable_inviter(), members.normal);
    protobuf_copy_message(*approve->mutable_invitee(), invitee);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, check_permission(env, room, members.owner, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, action));
  }
  expect_no_write(fake, env, before);
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  // 被邀请人游客本人成功(专用流程: add_member + approve_invitation)
  {
    atfw::team::DTeamAction action;
    auto* approve = action.mutable_approve_invitation();
    protobuf_copy_message(*approve->mutable_inviter(), members.normal);
    protobuf_copy_message(*approve->mutable_invitee(), invitee);
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, invitee, action));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(invitee, false) != nullptr);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-09: reject_invitation 门槛 ============
CASE_TEST(teamsvr_room_permission, reject_invitation_roles) {
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

  auto invitee = make_user_key(1, 7401);
  auto invitee2 = make_user_key(1, 7402);

  auto add_invitation_fn = [room, members](const PROJECT_NAMESPACE_ID::DUserIDKey& target) {
    return [room, members, target](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::SSTeamRoomAddInvitationReq req;
      protobuf_copy_message(*req.mutable_sender_user_key(), members.normal);
      protobuf_copy_message(*req.mutable_invitation(),
                            *make_invitation_action(members.normal, target).mutable_add_invitation());
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
    };
  };
  CASE_EXPECT_EQ(0, env.run("add_invitation_1", add_invitation_fn(invitee)));
  CASE_EXPECT_EQ(0, env.run("add_invitation_2", add_invitation_fn(invitee2)));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);

  // NORMAL 拒绝他人邀请失败(默认 ADMIN)
  auto before = snapshot_counters(env, fake);
  auto personal_before = env.personal_message_count();
  {
    atfw::team::DTeamAction action;
    auto* reject = action.mutable_reject_invitation();
    protobuf_copy_message(*reject->mutable_inviter(), members.normal);
    protobuf_copy_message(*reject->mutable_invitee(), invitee);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
  }
  expect_no_write(fake, env, before);
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  // 被邀请人游客本人成功
  {
    atfw::team::DTeamAction action;
    auto* reject = action.mutable_reject_invitation();
    protobuf_copy_message(*reject->mutable_inviter(), members.normal);
    protobuf_copy_message(*reject->mutable_invitee(), invitee);
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, invitee, action));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // ADMIN 可拒绝(撤回)他人邀请
  {
    atfw::team::DTeamAction action;
    auto* reject = action.mutable_reject_invitation();
    protobuf_copy_message(*reject->mutable_inviter(), members.normal);
    protobuf_copy_message(*reject->mutable_invitee(), invitee2);
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, action));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-10: add_join_request 门槛(FIX-03 回归: 未创建队伍) ============
CASE_TEST(teamsvr_room_permission, add_join_request_gates) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  {
    // 未创建队伍: room-not-found 且零写入(FIX-03)
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room = env.setup_ready_room(team_id);
    CASE_EXPECT_TRUE(!!room);
    if (room) {
      auto& fake = env.channel(team_id);
      auto before = snapshot_counters(env, fake);
      auto applicant = make_user_key(1, 7501);
      int32_t ret = env.run("join_uncreated", [room, applicant](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomAddJoinRequestReq req;
        protobuf_copy_message(*req.mutable_sender_user_key(), applicant);
        auto* join_request = req.mutable_join_request();
        protobuf_copy_message(*join_request->mutable_requester(), applicant);
        protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                              make_personal_channel(applicant.user_id()));
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      });
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ROOM_NOT_FOUND, ret);
      expect_no_write(fake, env, before);
      env.clear_rooms();
    }
  }

  {
    // 已创建队伍: 非成员本人成功;伪造 requester 失败;成员 already-in-team;私人队伍禁止
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (room) {
      auto& fake = env.channel(team_id);
      auto applicant = make_user_key(1, 7502);

      auto add_join_fn = [room, applicant](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomAddJoinRequestReq req;
        protobuf_copy_message(*req.mutable_sender_user_key(), applicant);
        auto* join_request = req.mutable_join_request();
        protobuf_copy_message(*join_request->mutable_requester(), applicant);
        protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                              make_personal_channel(applicant.user_id()));
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      };

      // 非成员本人默认成功
      CASE_EXPECT_EQ(0, env.run("join_self", add_join_fn));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      // 伪造 requester 失败
      auto before = snapshot_counters(env, fake);
      auto forged_target = make_user_key(1, 7503);
      CASE_EXPECT_EQ(
          PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
          env.run("join_forged", [room, applicant, forged_target](rpc::context& ctx) -> rpc::result_code_type {
            atfw::team::SSTeamRoomAddJoinRequestReq req;
            protobuf_copy_message(*req.mutable_sender_user_key(), applicant);
            auto* join_request = req.mutable_join_request();
            protobuf_copy_message(*join_request->mutable_requester(), forged_target);
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
          }));
      expect_no_write(fake, env, before);

      // 现有成员 already-in-team
      before = snapshot_counters(env, fake);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM,
                     env.run("join_member", [room, members](rpc::context& ctx) -> rpc::result_code_type {
                       atfw::team::SSTeamRoomAddJoinRequestReq req;
                       protobuf_copy_message(*req.mutable_sender_user_key(), members.normal);
                       auto* join_request = req.mutable_join_request();
                       protobuf_copy_message(*join_request->mutable_requester(), members.normal);
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
                     }));
      expect_no_write(fake, env, before);

      // 私人队伍(disable_join_request)禁止外部申请
      atfw::team::DTeamConfigure configure;
      configure.set_disable_join_request(true);
      atfw::team::DTeamAction config_action;
      protobuf_copy_message(*config_action.mutable_team_update()->mutable_configure(), configure);
      CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, config_action));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      before = snapshot_counters(env, fake);
      auto applicant2 = make_user_key(1, 7504);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                     env.run("join_private", [room, applicant2](rpc::context& ctx) -> rpc::result_code_type {
                       atfw::team::SSTeamRoomAddJoinRequestReq req;
                       protobuf_copy_message(*req.mutable_sender_user_key(), applicant2);
                       auto* join_request = req.mutable_join_request();
                       protobuf_copy_message(*join_request->mutable_requester(), applicant2);
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
                     }));
      expect_no_write(fake, env, before);

      env.clear_rooms();
    }
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-11: approve/reject join 门槛 ============
CASE_TEST(teamsvr_room_permission, approve_reject_join_roles) {
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

  auto applicant = make_user_key(1, 7601);

  auto add_join_fn = [room, applicant](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomAddJoinRequestReq req;
    protobuf_copy_message(*req.mutable_sender_user_key(), applicant);
    auto* join_request = req.mutable_join_request();
    protobuf_copy_message(*join_request->mutable_requester(), applicant);
    protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                          make_personal_channel(applicant.user_id()));
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
  };
  CASE_EXPECT_EQ(0, env.run("add_join", add_join_fn));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);

  // 游客(非成员)拒绝失败
  auto before = snapshot_counters(env, fake);
  auto personal_before = env.personal_message_count();
  {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_reject_join_request()->mutable_requester(), applicant);
    // 权限层 require_role 先校验成员身份: 非成员返回 not-in-team
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
                   check_permission(env, room, members.outsider, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.outsider, action));
  }
  expect_no_write(fake, env, before);
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  // 默认任意成员(NORMAL)可拒绝
  {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_reject_join_request()->mutable_requester(), applicant);
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 自定义 ADMIN 后: NORMAL 失败、ADMIN 成功
  CASE_EXPECT_EQ(0, env.run("add_join2", add_join_fn));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  atfw::team::DTeamConfigure configure;
  configure.set_approve_join_request_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
  atfw::team::DTeamAction config_action;
  protobuf_copy_message(*config_action.mutable_team_update()->mutable_configure(), configure);
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, config_action));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  before = snapshot_counters(env, fake);
  {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_approve_join_request()->mutable_requester(), applicant);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
  }
  expect_no_write(fake, env, before);

  {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_approve_join_request()->mutable_requester(), applicant);
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, action));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(applicant, false) != nullptr);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-12: action case 缺失/未知 ============
CASE_TEST(teamsvr_room_permission, unknown_action_case) {
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

  // 空 action(无 command case): invalid param 且零写入
  auto& fake = env.channel(team_id);
  auto before = snapshot_counters(env, fake);
  atfw::team::DTeamAction empty_action;
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, check_permission(env, room, members.owner, empty_action));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, empty_action));
  expect_no_write(fake, env, before);

  // check_action_permission 直接路径同样返回 invalid param
  (void)env.run("check_direct",
                [room, members, empty_action](ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) -> rpc::result_code_type {
                  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                                 RPC_AWAIT_CODE_RESULT(room->check_action_permission(members.owner, empty_action)));
                  RPC_RETURN_CODE(0);
                });

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-14: 角色门槛按阶梯大小比较(未来可插入新档位) ============
// GUEST/NORMAL/ADMIN/OWNER 是档位参考点: 门槛比较一律用 >=/<，不用 ==；
// 配置值不高于 GUEST 视为未配置(默认门槛)，其余值(含 NORMAL/ADMIN 之间或高于 OWNER 的自定义档位)按数值生效。
CASE_TEST(teamsvr_room_permission, role_threshold_ordering) {
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
  auto write_configure = [&](atfw::team::EnTeamPermissionRole manage_role) {
    atfw::team::DTeamConfigure configure;
    configure.set_manage_member_role(manage_role);
    atfw::team::DTeamAction config_action;
    protobuf_copy_message(*config_action.mutable_team_update()->mutable_configure(), configure);
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, config_action));
    CASE_EXPECT_EQ(0, env.sync(team_id));
  };

  // GUEST(0) 使用默认门槛: NORMAL 不能删除他人(默认 ADMIN)
  write_configure(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST);
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.normal, make_remove_action(members.admin)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_remove_action(members.admin)));
  expect_no_write(fake, env, before);

  // 低于 NORMAL 的自定义门槛 50 按大小生效: NORMAL(100) >= 50 可以删除他人
  write_configure(static_cast<atfw::team::EnTeamPermissionRole>(50));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_remove_action(members.admin)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.admin, false).get());
  // 加回 admin 供后续断言
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner,
                                            make_add_member_action(members.admin, members.admin_channel,
                                                                   atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN)));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 档位间自定义角色 150(介于 NORMAL 与 ADMIN): 阈值比较按大小，不需要 == 命中某个已定义档位
  auto vip = make_user_key(1, 7105);
  CASE_EXPECT_EQ(0,
                 run_send_message_action(env, team_id, members.owner,
                                         make_add_member_action(vip, make_personal_channel(7105),
                                                                static_cast<atfw::team::EnTeamPermissionRole>(150))));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 默认门槛(ADMIN=200)下 150 < 200 不能删除他人
  write_configure(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST);
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, vip, make_remove_action(members.normal)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, vip, make_remove_action(members.normal)));
  expect_no_write(fake, env, before);

  // 自定义门槛 150 下 150 >= 150 可以删除他人(>= 而非 >，含等于档位本身)
  write_configure(static_cast<atfw::team::EnTeamPermissionRole>(150));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, vip, make_remove_action(members.normal)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());

  // 高于 OWNER 的自定义档位 350 同样按大小生效: 350 >= 默认 ADMIN(200)
  auto super = make_user_key(1, 7106);
  env.inject_team_action(team_id, [&super]() {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), super);
    protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(7106));
    add_member->set_role(static_cast<atfw::team::EnTeamPermissionRole>(350));
    return action;
  }());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  write_configure(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST);
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, super, make_remove_action(vip)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(vip, false).get());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

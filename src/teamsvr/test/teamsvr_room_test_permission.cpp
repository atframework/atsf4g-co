// Copyright 2026 atframework
//
// teamsvr-room 权限与"拒绝时零写入"用例(TEAM_ROOM_TEST_PLAN.md §4.2 PERM-01~14、16~18)。
// COND-01~06 覆盖 member_update/team_update 的 DTeamConditionChecker 数据条件检查
// (共享队伍/成员数据等值含 Any 语义比较、成员数量/百分比范围、成员条件组 scope、checker 或关系/内部
// 与关系)与通过后裁剪。
// 所有失败断言均带统一零写入门禁: team 房间频道 send/update/reset_lock/destroy 调用数与
// 个人频道通知数保持不变。

#include "teamsvr_room_test_common.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/wrappers.pb.h>

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
  return env.run("check_permission", [room, sender, action](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, sender, action)));
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
// member_update 并写入一条共享成员数据(Any 只设置 value，与 make_team_update_action 的写法一致)
atfw::team::DTeamAction make_member_update_data_action(const PROJECT_NAMESPACE_ID::DUserIDKey& key, int64_t data_key,
                                                       const std::string& value) {
  atfw::team::DTeamAction action;
  auto* update = action.mutable_member_update();
  protobuf_copy_message(*update->mutable_user_key(), key);
  (*update->mutable_shared_member_data())[data_key].mutable_data()->set_value(value);
  return action;
}

// team_update 并写入一条共享队伍数据
atfw::team::DTeamAction make_team_update_data_action(int64_t data_key, const std::string& value) {
  atfw::team::DTeamAction action;
  (*action.mutable_team_update()->mutable_shared_team_data())[data_key].mutable_data()->set_value(value);
  return action;
}

// 断言 fake journal 最后一条 kEvent 日志中的 member_update/team_update 不含 condition(通过后按协议裁剪)
void expect_last_event_condition_trimmed(const fake_team_room_channel& fake) {
  const atfw::dtmq::DChannelMessage* last_event = nullptr;
  for (const auto& message : fake.journal()) {
    if (message.detail().command_case() == atfw::dtmq::DChannelMessageDetail::kEvent) {
      last_event = &message;
    }
  }
  CASE_EXPECT_TRUE(nullptr != last_event);
  if (nullptr == last_event) {
    return;
  }
  atfw::team::DTeamAction action;
  CASE_EXPECT_TRUE(last_event->detail().event().UnpackTo(&action));
  if (action.has_member_update()) {
    CASE_EXPECT_EQ(0, action.member_update().condition_size());
  }
  if (action.has_team_update()) {
    CASE_EXPECT_EQ(0, action.team_update().condition_size());
  }
}

atfw::team::DTeamAction make_election_action(const PROJECT_NAMESPACE_ID::DUserIDKey& key) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_election_captain()->mutable_user_key(), key);
  return action;
}

atfw::team::DTeamAction make_member_set_role_action(const PROJECT_NAMESPACE_ID::DUserIDKey& key,
                                                    atfw::team::EnTeamPermissionRole role) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_member_set_role()->mutable_user_key(), key);
  action.mutable_member_set_role()->set_role(role);
  return action;
}

// team_update 仅携带配置变更
atfw::team::DTeamAction make_team_update_configure_action(const atfw::team::DTeamConfigure& configure) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_team_update()->mutable_configure(), configure);
  return action;
}

// 查找 fake journal 最后一条携带配置变更的 team_update 事件(用于断言下发给订阅者的配置载荷)
bool find_last_team_update_configure(const fake_team_room_channel& fake, atfw::team::DTeamConfigure& out) {
  for (auto it = fake.journal().rbegin(); it != fake.journal().rend(); ++it) {
    if (it->detail().command_case() != atfw::dtmq::DChannelMessageDetail::kEvent) {
      continue;
    }
    atfw::team::DTeamAction action;
    if (!it->detail().event().UnpackTo(&action)) {
      continue;
    }
    if (action.has_team_update() && action.team_update().has_configure()) {
      out = action.team_update().configure();
      return true;
    }
  }
  return false;
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
  (void)env.run("check_direct", [room, members, empty_action](rpc::context& ctx) -> rpc::result_code_type {
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                   RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, members.owner, empty_action)));
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

// ============ PERM-16: member_set_role 默认门槛与授权上限 ============
// 默认门槛 ADMIN；目标必须是成员；不能授予 GUEST(无效)或高于操作者自身的角色。
CASE_TEST(teamsvr_room_permission, member_set_role_default_gates) {
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

  // 非成员操作者: not in team
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM,
                 check_permission(env, room, members.outsider,
                                  make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.outsider,
                                 make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  expect_no_write(fake, env, before);

  // 目标不是成员: member not found
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND,
      check_permission(env, room, members.admin,
                       make_member_set_role_action(members.outsider, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, run_send_message_action(
                        env, team_id, members.admin,
                        make_member_set_role_action(members.outsider, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  expect_no_write(fake, env, before);

  // 目标角色不高于 GUEST: invalid param(GUEST 是"未配置/非成员"语义，不是合法的成员角色)
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                 check_permission(env, room, members.admin,
                                  make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_GUEST)));
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.admin,
                                 make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_GUEST)));
  expect_no_write(fake, env, before);

  // NORMAL 操作者低于默认门槛(ADMIN): no permission
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.normal,
                                  make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.normal,
                                 make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  expect_no_write(fake, env, before);

  // ADMIN 授予高于自身的角色(OWNER): no permission
  before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.admin,
                                  make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_OWNER)));
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.admin,
                                 make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_OWNER)));
  expect_no_write(fake, env, before);

  // ADMIN 授予与自身同级的角色(ADMIN)成功: normal -> ADMIN
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.admin,
                                 make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    auto member = room->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!member);
    if (member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, member->member_data.role());
    }
  }

  // OWNER 将其降回 NORMAL 成功
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.owner,
                                 make_member_set_role_action(members.normal, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    auto member = room->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!member);
    if (member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, member->member_data.role());
    }
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-17: member_set_role 自定义门槛 ============
CASE_TEST(teamsvr_room_permission, member_set_role_custom_threshold) {
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

  // 自定义门槛 NORMAL: NORMAL 成员也可设置角色(授予上限仍不能高于自身)
  atfw::team::DTeamConfigure configure;
  configure.set_set_member_role_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, make_team_update_configure_action(configure)));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // NORMAL 操作者授予不高于自身的角色(NORMAL): 降级 admin -> NORMAL 成功
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.normal,
                                 make_member_set_role_action(members.admin, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    auto member = room->find_member(members.admin, false);
    CASE_EXPECT_TRUE(!!member);
    if (member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, member->member_data.role());
    }
  }

  // 授予高于自身的角色仍被拒绝(自定义门槛不改变授权上限)
  auto& fake = env.channel(team_id);
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION,
                 check_permission(env, room, members.normal,
                                  make_member_set_role_action(members.admin, atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN)));
  CASE_EXPECT_EQ(
      0, run_send_message_action(env, team_id, members.normal,
                                 make_member_set_role_action(members.admin, atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN)));
  expect_no_write(fake, env, before);

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ PERM-18: 配置默认门槛修订后随快照/事件下发 ============
// storage_.configure 在 create_team/apply_team_update 时被就地修订(见 revise_configure_default_permission)，
// 订阅者收到的快照 custom_data 与 team_update 增量事件都必须携带完整门槛(不允许出现 GUEST 占位)。
CASE_TEST(teamsvr_room_permission, configure_default_revision_published) {
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

  // create 路径: 未配置任何门槛时，快照 custom_data 下发全部默认门槛
  {
    atfw::team::DTeamStorage snapshot;
    CASE_EXPECT_TRUE(fake.custom_data().UnpackTo(&snapshot));
    const auto& configure = snapshot.configure();
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, configure.manage_member_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, configure.approve_join_request_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, configure.invite_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, configure.update_team_data_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, configure.reject_invitation_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, configure.set_member_role_role());
  }

  // team_update 路径: 只自定义一个门槛，WAL 事件中的配置必须包含全部修订后的门槛
  atfw::team::DTeamConfigure configure;
  configure.set_set_member_role_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.owner, make_team_update_configure_action(configure)));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  atfw::team::DTeamConfigure published;
  CASE_EXPECT_TRUE(find_last_team_update_configure(fake, published));
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, published.set_member_role_role());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, published.manage_member_role());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, published.approve_join_request_role());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, published.invite_role());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, published.update_team_data_role());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, published.reject_invitation_role());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}
// ============ COND-01: member_update 成员共享数据条件(指定成员 scope + 通过后裁剪 condition) ============
CASE_TEST(teamsvr_room_permission, member_update_condition_member_data) {
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

  // 预置成员共享数据 [7]="ready"(不带条件)
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal,
                                            make_member_update_data_action(members.normal, 7, "ready")));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    auto member = room->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!member);
    if (member) {
      auto it = member->member_data.shared_member_data().find(7);
      CASE_EXPECT_TRUE(it != member->member_data.shared_member_data().end());
      if (it != member->member_data.shared_member_data().end()) {
        CASE_EXPECT_EQ(std::string("ready"), it->second.data().value());
      }
    }
  }

  // 条件: 指定成员(normal)的 shared_member_data[7] == 期望值
  auto make_conditioned_update = [&members](const std::string& expected) {
    atfw::team::DTeamAction action = make_member_update_data_action(members.normal, 8, "lv2");
    auto* group = action.mutable_member_update()->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.normal);
    (*group->mutable_member_condition()->mutable_shared_member_data())[7].set_value(expected);
    return action;
  };

  // 条件匹配: 通过并应用更新，最终事件数据裁剪掉 condition
  CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, make_conditioned_update("ready")));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_conditioned_update("ready")));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  expect_last_event_condition_trimmed(fake);
  {
    auto member = room->find_member(members.normal, false);
    CASE_EXPECT_TRUE(!!member);
    if (member) {
      auto it = member->member_data.shared_member_data().find(8);
      CASE_EXPECT_TRUE(it != member->member_data.shared_member_data().end());
      if (it != member->member_data.shared_member_data().end()) {
        CASE_EXPECT_EQ(std::string("lv2"), it->second.data().value());
      }
    }
  }

  // 条件值不匹配: 拒绝且零写入
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                 check_permission(env, room, members.normal, make_conditioned_update("not-ready")));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_conditioned_update("not-ready")));
  expect_no_write(fake, env, before);

  // 条件 key 不存在: 拒绝且零写入
  {
    atfw::team::DTeamAction missing_key_action = make_member_update_data_action(members.normal, 8, "lv3");
    auto* group = missing_key_action.mutable_member_update()->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.normal);
    (*group->mutable_member_condition()->mutable_shared_member_data())[99].set_value("x");
    before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, missing_key_action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, missing_key_action));
    expect_no_write(fake, env, before);
  }

  // 指定成员不存在: 拒绝且零写入
  {
    atfw::team::DTeamAction no_member_action = make_member_update_data_action(members.normal, 8, "lv3");
    auto* group = no_member_action.mutable_member_update()->add_condition()->add_member_condition_group();
    protobuf_copy_message(*group->mutable_user_key(), members.outsider);
    before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, no_member_action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, no_member_action));
    expect_no_write(fake, env, before);
  }

  // ADMIN 更新他人数据(manage_member_role 路径)同样执行条件检查
  {
    before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.admin, make_conditioned_update("not-ready")));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.admin, make_conditioned_update("not-ready")));
    expect_no_write(fake, env, before);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.admin, make_conditioned_update("ready")));
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ COND-02: team_update 队伍共享数据条件(等值/缺失 key + 通过后裁剪 condition) ============
CASE_TEST(teamsvr_room_permission, team_update_condition_team_data) {
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

  // 预置队伍共享数据 [42]="ut-team-data"(不带条件)
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_team_update_action()));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 条件匹配([42]=="ut-team-data") + 写入 [43]="v2": 通过并应用，最终事件数据裁剪掉 condition
  {
    atfw::team::DTeamAction action = make_team_update_data_action(43, "v2");
    (*action.mutable_team_update()->add_condition()->mutable_shared_team_data())[42].set_value("ut-team-data");
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    expect_last_event_condition_trimmed(fake);
  }

  // 应用生效验证: 以新值 [43]=="v2" 为条件可通过(证明上一次 team_update 已应用)
  {
    atfw::team::DTeamAction action = make_team_update_data_action(44, "v3");
    (*action.mutable_team_update()->add_condition()->mutable_shared_team_data())[43].set_value("v2");
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, action));
  }

  // 条件值不匹配: 拒绝且零写入
  {
    atfw::team::DTeamAction action = make_team_update_data_action(44, "v3");
    (*action.mutable_team_update()->add_condition()->mutable_shared_team_data())[42].set_value("v2");
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    expect_no_write(fake, env, before);
  }

  // 条件 key 不存在: 拒绝且零写入
  {
    atfw::team::DTeamAction action = make_team_update_data_action(44, "v3");
    (*action.mutable_team_update()->add_condition()->mutable_shared_team_data())[99].set_value("x");
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    expect_no_write(fake, env, before);
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ COND-03: 成员数量范围 + checker 内部与关系/checker 之间或关系 ============
CASE_TEST(teamsvr_room_permission, team_update_condition_count_and_or) {
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

  // 标准队伍 3 名成员(owner/admin/normal)
  auto count_condition_action = [](int64_t min_value, int64_t max_value) {
    atfw::team::DTeamAction action = make_team_update_data_action(50, "x");
    auto* range = action.mutable_team_update()->add_condition()->mutable_members_count();
    range->set_min_value(min_value);
    range->set_max_value(max_value);
    return action;
  };

  // 数量范围内: min=2 / max=3 通过; 0 表示该方向不限制
  CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, count_condition_action(2, 0)));
  CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, count_condition_action(0, 3)));
  CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, count_condition_action(0, 0)));

  // 数量范围外: min=4 / max=2 拒绝且零写入
  auto before = snapshot_counters(env, fake);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                 check_permission(env, room, members.normal, count_condition_action(4, 0)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, count_condition_action(4, 0)));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                 check_permission(env, room, members.normal, count_condition_action(0, 2)));
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, count_condition_action(0, 2)));
  expect_no_write(fake, env, before);

  // checker 内部与关系: 共享队伍数据匹配但成员数量不满足 -> 整体拒绝
  CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, make_team_update_data_action(60, "and")));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  {
    atfw::team::DTeamAction action = make_team_update_data_action(61, "y");
    auto* checker = action.mutable_team_update()->add_condition();
    (*checker->mutable_shared_team_data())[60].set_value("and");
    checker->mutable_members_count()->set_min_value(4);
    before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    expect_no_write(fake, env, before);
  }

  // checker 之间或关系: [数量不满足, 数量满足] -> 通过
  {
    atfw::team::DTeamAction action = make_team_update_data_action(62, "z");
    action.mutable_team_update()->add_condition()->mutable_members_count()->set_min_value(4);
    action.mutable_team_update()->add_condition()->mutable_members_count()->set_min_value(2);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, action));
  }
  // 或关系全部失败: [min=4, max=2] -> 拒绝且零写入
  {
    atfw::team::DTeamAction action = make_team_update_data_action(62, "z");
    action.mutable_team_update()->add_condition()->mutable_members_count()->set_min_value(4);
    action.mutable_team_update()->add_condition()->mutable_members_count()->set_max_value(2);
    before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    expect_no_write(fake, env, before);
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ COND-04: 成员条件组 scope(全员/任意/指定成员/数量门槛)与角色范围 ============
CASE_TEST(teamsvr_room_permission, member_condition_group_scopes) {
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
  // 角色: owner=OWNER(300), admin=ADMIN(200), normal=NORMAL(100)
  const int64_t kNormal = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
  const int64_t kAdmin = atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN;
  const int64_t kOwner = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER;

  auto make_group_action = []() { return make_team_update_data_action(51, "s"); };

  // all_members: 全员角色 >= NORMAL 通过; >= ADMIN 拒绝(normal 只有 100)
  {
    atfw::team::DTeamAction pass_action = make_group_action();
    auto* group = pass_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->set_all_members(true);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kNormal);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, pass_action));

    atfw::team::DTeamAction fail_action = make_group_action();
    group = fail_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->set_all_members(true);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kAdmin);
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, fail_action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, fail_action));
    expect_no_write(fake, env, before);
  }

  // any_members: 存在 OWNER 通过; 不存在角色 >= 400 的成员则拒绝
  {
    atfw::team::DTeamAction pass_action = make_group_action();
    auto* group = pass_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->set_any_members(true);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kOwner);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, pass_action));

    atfw::team::DTeamAction fail_action = make_group_action();
    group = fail_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->set_any_members(true);
    group->mutable_member_condition()->mutable_permission()->set_min_value(400);
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, fail_action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, fail_action));
    expect_no_write(fake, env, before);
  }

  // 数量门槛: 角色 >= NORMAL 满足全部 3 人
  // members_count.min=2 通过; min=4 拒绝
  {
    atfw::team::DTeamAction pass_action = make_group_action();
    auto* group = pass_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->mutable_members_count()->set_min_value(2);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kNormal);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, pass_action));

    atfw::team::DTeamAction fail_action = make_group_action();
    group = fail_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->mutable_members_count()->set_min_value(4);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kNormal);
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, fail_action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, fail_action));
    expect_no_write(fake, env, before);
  }

  // members_count.max: 满足 3 人 <= 3 通过; <= 2 拒绝; 无人满足(>=400)时 <=1 通过
  {
    atfw::team::DTeamAction pass_action = make_group_action();
    auto* group = pass_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->mutable_members_count()->set_max_value(3);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kNormal);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, pass_action));

    atfw::team::DTeamAction fail_action = make_group_action();
    group = fail_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->mutable_members_count()->set_max_value(2);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kNormal);
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, fail_action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, fail_action));
    expect_no_write(fake, env, before);

    atfw::team::DTeamAction zero_pass_action = make_group_action();
    group = zero_pass_action.mutable_team_update()->add_condition()->add_member_condition_group();
    // DTeamConditionMinMaxValue 语义: max=0 表示不限制(无人满足时 <=1 也通过)
    group->mutable_members_count()->set_max_value(1);
    group->mutable_member_condition()->mutable_permission()->set_min_value(400);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, zero_pass_action));

    // max=0 不限制: 全员满足也通过
    atfw::team::DTeamAction unlimited_action = make_group_action();
    group = unlimited_action.mutable_team_update()->add_condition()->add_member_condition_group();
    group->mutable_members_count()->set_max_value(0);
    group->mutable_member_condition()->mutable_permission()->set_min_value(kNormal);
    CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, unlimited_action));
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ COND-05: 百分比 scope、多条件组与关系、空 scope 失败关闭 ============
CASE_TEST(teamsvr_room_permission, member_condition_group_percent) {
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
  const int64_t kNormal = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
  const int64_t kAdmin = atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN;

  auto percent_action = [](uint32_t percent, bool is_min, int64_t min_role) {
    atfw::team::DTeamAction action = make_team_update_data_action(52, "p");
    auto* group = action.mutable_team_update()->add_condition()->add_member_condition_group();
    if (is_min) {
      group->mutable_members_percent()->set_min_value(percent);
    } else {
      group->mutable_members_percent()->set_max_value(percent);
    }
    group->mutable_member_condition()->mutable_permission()->set_min_value(min_role);
    return action;
  };

  // 3 名成员中角色 >= ADMIN 满足 2 人(66.67%): min=66 -> 2*100 >= 3*66 通过; min=67 -> 拒绝
  CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, percent_action(66, true, kAdmin)));
  {
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, percent_action(67, true, kAdmin)));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, percent_action(67, true, kAdmin)));
    expect_no_write(fake, env, before);
  }

  // max=67 -> 2*100 <= 3*67 通过; max=66 -> 拒绝; 全员满足时 max=0 -> 拒绝
  CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, percent_action(67, false, kAdmin)));
  {
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, percent_action(66, false, kAdmin)));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, percent_action(66, false, kAdmin)));
    // max=33 -> 上限 floor(3*33/100)=0，全员满足时 3 > 0 拒绝
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, percent_action(33, false, kNormal)));
    expect_no_write(fake, env, before);
  }

  // DTeamConditionMinMaxValue 语义: percent max=0 表示不限制，全员满足也通过
  CASE_EXPECT_EQ(0, check_permission(env, room, members.normal, percent_action(0, false, kNormal)));

  // 多条件组与关系: [全员 >= NORMAL 通过] + [指定 normal >= ADMIN 拒绝] -> 整体拒绝
  {
    atfw::team::DTeamAction action = make_team_update_data_action(53, "m");
    auto* checker = action.mutable_team_update()->add_condition();
    auto* pass_group = checker->add_member_condition_group();
    pass_group->set_all_members(true);
    pass_group->mutable_member_condition()->mutable_permission()->set_min_value(kNormal);
    auto* fail_group = checker->add_member_condition_group();
    protobuf_copy_message(*fail_group->mutable_user_key(), members.normal);
    fail_group->mutable_member_condition()->mutable_permission()->set_min_value(kAdmin);
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    expect_no_write(fake, env, before);
  }

  // 未设置 scope 的空条件组: 失败关闭，拒绝且零写入
  {
    atfw::team::DTeamAction action = make_team_update_data_action(54, "e");
    action.mutable_team_update()->add_condition()->add_member_condition_group();
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_permission(env, room, members.normal, action));
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    expect_no_write(fake, env, before);
  }

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}
// ============ COND-06: Any 语义比较(打包字节布局不同但语义相同必须判定相等) ============
CASE_TEST(teamsvr_room_permission, condition_any_semantic_equal) {
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
  const std::string key_type_url =
      "type.googleapis.com/" + std::string(atfw::team::DTeamKey::descriptor()->full_name());

  // DTeamKey(team_id=1, zone_id=2) 的规范字节是 [field1, field2]；逆序字节语义相同但字节不同
  atfw::team::DTeamKey key;
  key.set_team_id(1);
  key.set_zone_id(2);
  const std::string canonical = key.SerializeAsString();
  CASE_EXPECT_EQ(4u, canonical.size());
  const std::string reversed = canonical.substr(2) + canonical.substr(0, 2);
  CASE_EXPECT_NE(canonical, reversed);

  auto write_any_data = [&env, team_id, &members](int64_t data_key, const std::string& type_url,
                                                  const std::string& value) {
    atfw::team::DTeamAction action;
    auto* data = (*action.mutable_team_update()->mutable_shared_team_data())[data_key].mutable_data();
    data->set_type_url(type_url);
    data->set_value(value);
    CASE_EXPECT_EQ(0, run_send_message_action(env, team_id, members.normal, action));
    CASE_EXPECT_EQ(0, env.sync(team_id));
  };

  auto check_any_condition = [&env, &room, &members](int64_t data_key, const std::string& type_url,
                                                     const std::string& value) {
    atfw::team::DTeamAction action = make_team_update_data_action(60, "probe");
    auto& expect = (*action.mutable_team_update()->add_condition()->mutable_shared_team_data())[data_key];
    expect.set_type_url(type_url);
    expect.set_value(value);
    return check_permission(env, room, members.normal, action);
  };

  // 预置: [42] 存放字段逆序打包的 DTeamKey
  write_any_data(42, key_type_url, reversed);

  // 条件用规范字节序: 字节不同但语义相同 -> 通过
  CASE_EXPECT_EQ(0, check_any_condition(42, key_type_url, canonical));
  // 条件字节与存储一致(逆序) -> 快路径通过
  CASE_EXPECT_EQ(0, check_any_condition(42, key_type_url, reversed));
  // 语义不同(team_id=9): 拒绝且零写入
  {
    atfw::team::DTeamKey other_key;
    other_key.set_team_id(9);
    other_key.set_zone_id(2);
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_any_condition(42, key_type_url, other_key.SerializeAsString()));
    expect_no_write(fake, env, before);
  }

  // 未知类型(type_url 无法解析)回退字节比较: 同字节通过，不同字节拒绝
  const std::string unknown_type_url = "type.googleapis.com/unknown.NotExist";
  write_any_data(43, unknown_type_url, "abc");
  CASE_EXPECT_EQ(0, check_any_condition(43, unknown_type_url, "abc"));
  {
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_any_condition(43, unknown_type_url, "abd"));
    expect_no_write(fake, env, before);
  }

  // 单标量字段类型(包装类型 google.protobuf.Int64Value): 字节即规范形式，直接字节比较
  const std::string int64_type_url =
      "type.googleapis.com/" + std::string(google::protobuf::Int64Value::descriptor()->full_name());
  google::protobuf::Int64Value five;
  five.set_value(5);
  write_any_data(44, int64_type_url, five.SerializeAsString());
  // 字节相等 -> 通过
  CASE_EXPECT_EQ(0, check_any_condition(44, int64_type_url, five.SerializeAsString()));
  // 默认值 0 序列化为空字节: 与已存值 5 语义不同 -> 拒绝且零写入
  {
    google::protobuf::Int64Value zero;
    CASE_EXPECT_TRUE(zero.SerializeAsString().empty());
    auto before = snapshot_counters(env, fake);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH,
                   check_any_condition(44, int64_type_url, zero.SerializeAsString()));
    expect_no_write(fake, env, before);
  }
  // 存储默认值(空字节)与条件默认值(空字节)字节相等 -> 通过
  google::protobuf::Int64Value zero;
  write_any_data(45, int64_type_url, zero.SerializeAsString());
  CASE_EXPECT_EQ(0, check_any_condition(45, int64_type_url, zero.SerializeAsString()));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

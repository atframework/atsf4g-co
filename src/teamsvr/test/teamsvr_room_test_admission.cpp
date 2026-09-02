// Copyright 2026 atframework
//
// teamsvr-room 邀请、加入请求与个人通知用例(TEAM_ROOM_TEST_PLAN.md §4.3 ADM-01~16)。
// 断言维度: 频道日志形状(add_member/approve/reject 顺序与完整负载)、新成员数据完整性(版本/路由/
// 共享成员数据/入队来源)、可观察快照完整性(create custom_data+journal 与压缩后 custom_data)、
// 个人频道 DTeamMemberAction 的目标/类型/内容、PUBLIC 数据过滤(ADM-03)、通知频道以 room 本地记录为准、
// 准入内存缓存清理(approve/reject 后 not-found 且零写入)、重复请求幂等(ADM-02/10/16)。

#include "teamsvr_room_test_common.h"  // NOLINT: build/include_subdir

#include <string>

namespace {
using teamsvr_room_test::add_team_any_data_entry;
using teamsvr_room_test::count_personal_actions;
using teamsvr_room_test::fake_team_room_channel;
using teamsvr_room_test::global_now_offset_guard;
using teamsvr_room_test::kTestZoneId;
using teamsvr_room_test::make_personal_channel;
using teamsvr_room_test::make_team_key;
using teamsvr_room_test::make_user_key;
using teamsvr_room_test::make_standard_team_configure;
using teamsvr_room_test::next_test_team_id;
using teamsvr_room_test::room_test_cfg_values;
using teamsvr_room_test::room_test_env;
using teamsvr_room_test::setup_standard_team;
using teamsvr_room_test::standard_team_members;
using teamsvr_room_test::update_request_record;

// 追加一条合法 admission 数据项(Any 同时携带 type_url+payload)。admission 数据按新请求全量覆盖，
// 不使用共享数据更新入口的删除标记；测试数据按真实流程构造完整 Any。
atfw::team::DTeamAnyDataWithKey* add_admission_data_entry(
    google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* field, int64_t key, const std::string& value) {
  auto* entry = add_team_any_data_entry(field, key, value);
  entry->mutable_value()->mutable_data()->set_type_url("type.googleapis.com/atframework.team.ut_admission_data");
  return entry;
}

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
  // 入队来源由邀请方携带(好友邀请)，批准入队时随邀请记录写入 add_member.team_source_type
  invitation->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
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
  // 入队来源(匹配)与申请人共享数据由申请人本人携带，批准入队时写入 add_member(见 approve_join_request)
  join_request->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH);
  add_admission_data_entry(join_request->mutable_member_admission_data(), 8, "ut-join-admission-data");
  if (expire_after_seconds > 0) {
    *join_request->mutable_expired_timepoint() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{expire_after_seconds});
  }
  return req;
}

// ---- 完整成员数据准入流程夹具 ----
// 每名成员的完整数据(客户端版本/通知路由/共享成员数据)，由本人经真实流程上报:
// owner 在 create_team 时上报(写入频道 custom_data); 其他成员在 approve/apply 准入流程中上报
// (写入各自的 add_member 频道日志)。user_channel 由 room 按邀请/申请记录规范化填充。
struct admission_member_data {
  PROJECT_NAMESPACE_ID::DUserIDKey user_key;
  atfw::dtmq::DChannelIdKey personal_channel;
  std::string client_version;
  uint64_t user_router_server_id = 0;
  int64_t shared_data_key = 0;
  std::string shared_data_value;
};

admission_member_data make_admission_member_data(uint32_t zone_id, uint64_t user_id, gsl::string_view tag) {
  admission_member_data ret;
  ret.user_key = make_user_key(zone_id, user_id);
  ret.personal_channel = make_personal_channel(user_id);
  ret.client_version = "ut-" + std::string(tag) + "-v1";
  ret.user_router_server_id = 0x70000000 + user_id;
  ret.shared_data_key = 100 + static_cast<int64_t>(user_id % 1000);
  ret.shared_data_value = "ut-shared-" + std::string(tag);
  return ret;
}

// 断言 DTeamMember 携带指定成员的完整数据(版本/路由/个人频道/角色/入队与心跳时间/共享数据)
void expect_complete_member_fields(const atfw::team::DTeamMember& member, const admission_member_data& data,
                                   atfw::team::EnTeamPermissionRole role) {
  CASE_EXPECT_EQ(data.user_key.user_id(), member.user_key().user_id());
  CASE_EXPECT_EQ(data.user_key.zone_id(), member.user_key().zone_id());
  CASE_EXPECT_EQ(role, member.role());
  CASE_EXPECT_EQ(data.client_version, member.client_version());
  CASE_EXPECT_EQ(data.user_router_server_id, member.user_router_server_id());
  CASE_EXPECT_EQ(data.personal_channel.channel_id(), member.user_channel().channel_id());
  CASE_EXPECT_GT(member.joined_timepoint().seconds(), 0);
  CASE_EXPECT_GT(member.last_heartbeat_timepoint().seconds(), 0);
  auto shared_it = std::find_if(member.shared_member_data().begin(), member.shared_member_data().end(),
                                [&data](const atfw::team::DTeamAnyDataWithKey& entry) {
                                  return entry.key() == data.shared_data_key;
                                });
  CASE_EXPECT_TRUE(shared_it != member.shared_member_data().end());
  if (shared_it != member.shared_member_data().end()) {
    CASE_EXPECT_EQ(data.shared_data_value, shared_it->value().data().value());
  }
}

// 在频道 journal 中定位指定成员的 add_member 日志并断言其携带完整数据；返回是否找到
bool expect_journal_add_member_complete(const fake_team_room_channel& fake, const admission_member_data& data,
                                        atfw::team::EnTeamPermissionRole role) {
  bool found = false;
  fake.foreach_team_action([&found, &data, role](const atfw::dtmq::DChannelMessage&,
                                                 const atfw::team::DTeamAction& action) {
    if (action.action_case() != atfw::team::DTeamAction::kAddMember ||
        action.add_member().user_key().user_id() != data.user_key.user_id()) {
      return true;
    }
    found = true;
    expect_complete_member_fields(action.add_member(), data, role);
    return true;
  });
  return found;
}

// 在 DTeamStorage 快照中定位指定成员并断言其携带完整数据
void expect_storage_member_complete(const atfw::team::DTeamStorage& storage, const admission_member_data& data,
                                    atfw::team::EnTeamPermissionRole role) {
  const atfw::team::DTeamMember* found = nullptr;
  for (const auto& member : storage.member()) {
    if (member.user_key().user_id() == data.user_key.user_id() &&
        member.user_key().zone_id() == data.user_key.zone_id()) {
      found = &member;
      break;
    }
  }
  CASE_EXPECT_TRUE(nullptr != found);
  if (nullptr != found) {
    expect_complete_member_fields(*found, data, role);
  }
}

// 断言新订户可观察的频道快照(create 时的 custom_data + 后续全部 journal)携带每个成员的完整数据:
// owner 的完整数据由 create_team 写入 custom_data(DTeamStorage)；经批准入队成员的完整数据
// 由各自的 add_member 日志携带。新成员订阅队伍频道后据此重建完整的队伍状态。
void expect_snapshot_member_data(const fake_team_room_channel& fake, const admission_member_data& owner,
                                 std::initializer_list<const admission_member_data*> joined_members) {
  auto snapshot = fake.dump_snapshot();
  CASE_EXPECT_TRUE(snapshot.channel_metadata().has_custom_data());
  atfw::team::DTeamStorage storage;
  CASE_EXPECT_TRUE(snapshot.channel_metadata().custom_data().UnpackTo(&storage));
  CASE_EXPECT_EQ(owner.user_key.user_id(), storage.captain_user_key().user_id());
  expect_storage_member_complete(storage, owner, atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  for (const auto* member : joined_members) {
    CASE_EXPECT_TRUE(expect_journal_add_member_complete(fake, *member, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  }
}

// 断言压缩维护写出的 custom_data 快照(DTeamStorage)携带每个成员的完整数据，
// 且已处理的邀请/加入请求不再残留于待处理列表(内存缓存清理已落入快照)
void expect_compacted_storage_complete(const fake_team_room_channel& fake, const admission_member_data& owner,
                                       std::initializer_list<const admission_member_data*> all_members) {
  const atfw::dtmq::SSChannelUpdateReq* compact_update = nullptr;
  for (const auto& record : fake.update_requests()) {
    if (record.request.compact_sequence() > 0) {
      compact_update = &record.request;
    }
  }
  CASE_EXPECT_TRUE(nullptr != compact_update);
  if (nullptr == compact_update) {
    return;
  }
  atfw::team::DTeamStorage storage;
  CASE_EXPECT_TRUE(compact_update->custom_data().UnpackTo(&storage));
  CASE_EXPECT_EQ(owner.user_key.user_id(), storage.captain_user_key().user_id());
  expect_storage_member_complete(storage, owner, atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  for (const auto* member : all_members) {
    expect_storage_member_complete(storage, *member, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  }
  CASE_EXPECT_EQ(0, storage.pending_invitation_size());
  CASE_EXPECT_EQ(0, storage.pending_join_request_size());
}

// 断言指定用户的个人频道恰好收到一条内容完整的 joined_team 通知
// (team_key/team_channel/captain/role 齐全，对端据此订阅队伍频道接收快照)
void expect_joined_team_notification(const room_test_env& env, const admission_member_data& data, int64_t team_id,
                                     const PROJECT_NAMESPACE_ID::DUserIDKey& captain) {
  size_t count = 0;
  atfw::team::DTeamMemberAction joined_action;
  for (const auto& record : env.personal_messages()) {
    if (record.channel.channel_id() == data.personal_channel.channel_id() &&
        record.action.action_case() == atfw::team::DTeamMemberAction::kJoinedTeam) {
      ++count;
      joined_action = record.action;
    }
  }
  CASE_EXPECT_EQ(1u, count);
  if (1u != count) {
    return;
  }
  const auto& joined_team = joined_action.joined_team();
  CASE_EXPECT_EQ(team_id, joined_team.team_key().team_id());
  CASE_EXPECT_EQ(data.user_key.zone_id(), joined_team.team_key().zone_id());
  CASE_EXPECT_EQ(data.user_key.user_id(), joined_team.user_key().user_id());
  CASE_EXPECT_EQ(data.user_key.zone_id(), joined_team.user_key().zone_id());
  auto room_channel = rpc::team::team_api::make_team_room_channel_key(make_team_key(team_id, data.user_key.zone_id()));
  CASE_EXPECT_EQ(room_channel.channel_type(), joined_team.team_channel().channel_type());
  CASE_EXPECT_EQ(room_channel.channel_id(), joined_team.team_channel().channel_id());
  CASE_EXPECT_EQ(captain.user_id(), joined_team.captain_user_key().user_id());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, joined_team.user_role());
}

// 通过真实准入流程搭建每个成员都携带完整数据的队伍:
// owner 经 create_team 上报；member_a 经 邀请-同意 入队；member_b 经 申请-批准 入队。
// 之后用例聚焦目标成员的准入流程、日志/通知/快照断言
bool setup_full_data_team(room_test_env& env, int64_t team_id, team_room::ptr_t& out_room,
                          admission_member_data& owner, admission_member_data& member_a,
                          admission_member_data& member_b, uint64_t user_id_base = 7000) {
  owner = make_admission_member_data(kTestZoneId, user_id_base + 1, "owner");
  member_a = make_admission_member_data(kTestZoneId, user_id_base + 2, "member-a");
  member_b = make_admission_member_data(kTestZoneId, user_id_base + 3, "member-b");

  google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey> owner_shared;
  add_team_any_data_entry(&owner_shared, owner.shared_data_key, owner.shared_data_value);
  // 上限留出余量: excel 默认上限为 3 成员，准入用例需要在 3 名成员基础上继续审批新成员
  auto configure = teamsvr_room_test::make_standard_team_configure();
  if (0 != env.setup_created_team(team_id, owner.user_key, owner.personal_channel, &out_room, &configure,
                                  &owner.client_version, owner.user_router_server_id, &owner_shared)) {
    return false;
  }
  if (0 != env.sync(team_id)) {
    return false;
  }
  team_room::ptr_t room = out_room;
  if (!room) {
    return false;
  }

  // member_a: owner 邀请 -> 本人携带完整数据同意
  {
    auto invite_req = make_add_invitation_req(owner.user_key, owner.user_key, member_a.user_key);
    if (0 != env.run("setup_invite_member_a", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
        })) {
      return false;
    }
    if (0 != env.sync(team_id)) {
      return false;
    }
    atfw::team::SSTeamRoomApproveInvitationReq approve_req;
    protobuf_copy_message(*approve_req.mutable_sender_user_key(), member_a.user_key);
    protobuf_copy_message(*approve_req.mutable_invitee(), member_a.user_key);
    approve_req.set_client_version(member_a.client_version);
    approve_req.set_user_router_server_id(member_a.user_router_server_id);
    add_team_any_data_entry(approve_req.mutable_shared_member_data(), member_a.shared_data_key,
                            member_a.shared_data_value);
    if (0 != env.run("setup_approve_member_a", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
        })) {
      return false;
    }
    if (0 != env.sync(team_id)) {
      return false;
    }
  }

  // member_b: 本人携带完整数据申请 -> owner 批准
  {
    auto join_req = make_add_join_request_req(member_b.user_key);
    join_req.mutable_join_request()->set_client_version(member_b.client_version);
    join_req.mutable_join_request()->set_user_router_server_id(member_b.user_router_server_id);
    join_req.mutable_join_request()->mutable_member_admission_data()->Clear();
    add_admission_data_entry(join_req.mutable_join_request()->mutable_member_admission_data(),
                            member_b.shared_data_key, member_b.shared_data_value);
    if (0 != env.run("setup_join_member_b", [room, &join_req](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, join_req)));
        })) {
      return false;
    }
    if (0 != env.sync(team_id)) {
      return false;
    }
    atfw::team::SSTeamRoomApproveJoinRequestReq approve_req;
    protobuf_copy_message(*approve_req.mutable_sender_user_key(), owner.user_key);
    protobuf_copy_message(*approve_req.mutable_applicant(), member_b.user_key);
    if (0 != env.run("setup_approve_member_b", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
        })) {
      return false;
    }
    if (0 != env.sync(team_id)) {
      return false;
    }
  }

  return nullptr != room->find_member(member_a.user_key, false) &&
         nullptr != room->find_member(member_b.user_key, false);
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
    // invited 负载回显规范化后的邀请记录(邀请人/team key/过期时间)
    CASE_EXPECT_EQ(members.normal.user_id(), record.action.invited().inviter().user_id());
    CASE_EXPECT_EQ(team_id, record.action.invited().team_key().team_id());
    CASE_EXPECT_GT(record.action.invited().expired_timepoint().seconds(), 0);
  }

  // 再次同步无新事件时不产生第二条通知
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, env.personal_message_count());

  room_test_env::clear_rooms();
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
  // GAP-09 刷新语义(2026-08-29 澄清): admission 数据全量覆盖(以新请求携带的列表为准，不做
  // 按键合并、无删除标记)。内容变化时写新日志并在回环后补发 invited 通知；
  // inviter/invitee/已顺延的过期时间不可变
  auto req_data = make_add_invitation_req(members.normal, members.normal, invitee);
  add_admission_data_entry(req_data.mutable_invitation()->mutable_team_admission_data(), 1, "ut-team-admission-1");
  CASE_EXPECT_EQ(0, env.run("add_invitation_5", [room, &req_data](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req_data)));
  }));
  CASE_EXPECT_EQ(sends_after_first + 2, fake.send_message_calls());
  // 回环让 room 记录纳入 key 1，后续删除才构成内容变化
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto req_delete = make_add_invitation_req(members.normal, members.normal, invitee);
  // 全量覆盖: 携带空 admission 列表刷新 -> key 1 被移除(admission 不做按键合并、无删除标记)
  req_delete.mutable_invitation()->mutable_team_admission_data()->Clear();
  CASE_EXPECT_EQ(0, env.run("add_invitation_6", [room, &req_delete](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req_delete)));
  }));
  CASE_EXPECT_EQ(sends_after_first + 3, fake.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 最终邀请记录: key 1 已删除，inviter/invitee 不变，过期时间保持已顺延的值
  atfw::team::DTeamInvitation refreshed;
  size_t invitation_logs = 0;
  fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddInvitation) {
      ++invitation_logs;
      refreshed = action.add_invitation();
    }
    return true;
  });
  CASE_EXPECT_EQ(4u, invitation_logs);  // 首次 + 过期顺延 + admission upsert + admission 删除
  CASE_EXPECT_EQ(0, refreshed.team_admission_data_size());
  CASE_EXPECT_EQ(invitee.user_id(), refreshed.invitee().user_id());
  CASE_EXPECT_EQ(members.normal.user_id(), refreshed.inviter().user_id());
  CASE_EXPECT_EQ(req_later.invitation().expired_timepoint().seconds(), refreshed.expired_timepoint().seconds());
  // 每次内容变化的刷新都补发一次 invited 通知
  CASE_EXPECT_EQ(personal_after_first + 3, env.personal_message_count());

  room_test_env::clear_rooms();
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

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-04/05: 接受邀请写 add_member + approve；成员数据来自 invitee 本人请求 ============
// 数据完整性契约:
// - 邀请人 A 与所有成员经队伍频道日志观察到的 add_member 携带新成员 B 本人上报的完整
//   版本/路由/共享数据；角色/入队时间/个人频道由 room 按邀请记录规范化填充，入队来源来自邀请记录
// - B 可观察的频道快照(create 时的 custom_data + 全部 journal)携带 A、B 与其他成员的完整数据；
//   压缩维护后的 custom_data 快照同样完整，且待处理邀请/申请列表为空(内存缓存清理已落快照)
// - B 的个人频道恰好收到一次 invited 与一次内容完整的 joined_team
// - 批准生效后邀请内存缓存被清理: 重复 approve 返回 not-found 且零写入
CASE_TEST(teamsvr_room_admission, approve_invitation_writes_and_notifies) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  admission_member_data owner;
  admission_member_data member_a;
  admission_member_data member_b;
  CASE_EXPECT_TRUE(setup_full_data_team(env, team_id, room, owner, member_a, member_b));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // A(owner) 邀请 B
  auto invitee_data = make_admission_member_data(kTestZoneId, 8031, "invitee");
  auto invite_req = make_add_invitation_req(owner.user_key, owner.user_key, invitee_data.user_key);
  CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // B 的个人频道恰好收到一次 invited 通知
  CASE_EXPECT_EQ(1u, count_personal_actions(env, invitee_data.user_key.user_id(),
                                            atfw::team::DTeamMemberAction::kInvited));

  // 从 add_invitation 日志取房间规范化后的过期时间，供 approve 事件负载断言(事件负载必须与本地记录一致)
  auto& fake = env.channel(team_id);
  google::protobuf::Timestamp normalized_expired;
  fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddInvitation &&
        action.add_invitation().invitee().user_id() == invitee_data.user_key.user_id()) {
      normalized_expired = action.add_invitation().expired_timepoint();
    }
    return true;
  });
  CASE_EXPECT_GT(normalized_expired.seconds(), 0);

  // B 本人接受: 携带自己的版本/路由/共享数据(ADM-05: 不能由 inviter 伪造)
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee_data.user_key);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee_data.user_key);
  approve_req.set_client_version(invitee_data.client_version);
  approve_req.set_user_router_server_id(invitee_data.user_router_server_id);
  add_team_any_data_entry(approve_req.mutable_shared_member_data(), invitee_data.shared_data_key,
                          invitee_data.shared_data_value);

  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("approve_invitation", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  }));

  // add_member 和 approve_invitation 两个事件合并为一次请求写入，日志按顺序追加
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());
  int add_member_index = -1;
  int approve_index = -1;
  int visit_index = 0;
  fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddMember &&
        action.add_member().user_key().user_id() == invitee_data.user_key.user_id()) {
      add_member_index = visit_index;
      // A 与所有成员可观察的 add_member 携带 B 的完整数据(版本/路由/共享数据来自 B 本人请求)
      expect_complete_member_fields(action.add_member(), invitee_data, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
      // 入队来源从邀请记录获取
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, action.add_member().team_source_type());
    } else if (action.action_case() == atfw::team::DTeamAction::kApproveInvitation &&
               action.approve_invitation().invitee().user_id() == invitee_data.user_key.user_id()) {
      approve_index = visit_index;
      // approve 事件负载 = room 本地邀请记录(team key/过期时间以规范化记录为准)
      CASE_EXPECT_EQ(owner.user_key.user_id(), action.approve_invitation().inviter().user_id());
      CASE_EXPECT_EQ(team_id, action.approve_invitation().team_key().team_id());
      CASE_EXPECT_EQ(normalized_expired.seconds(), action.approve_invitation().expired_timepoint().seconds());
      CASE_EXPECT_EQ(normalized_expired.nanos(), action.approve_invitation().expired_timepoint().nanos());
    }
    ++visit_index;
    return true;
  });
  CASE_EXPECT_TRUE(add_member_index >= 0 && approve_index >= 0);
  CASE_EXPECT_LT(add_member_index, approve_index);

  // 事件回环后成员入队，内存中的成员数据与共享数据索引完整
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto new_member = room->find_member(invitee_data.user_key, false);
  CASE_EXPECT_TRUE(!!new_member);
  if (new_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, new_member->member_data.role());
    CASE_EXPECT_EQ(invitee_data.client_version, new_member->member_data.client_version());
    CASE_EXPECT_EQ(invitee_data.user_router_server_id, new_member->member_data.user_router_server_id());
    auto shared_it = new_member->shared_member_data.find(invitee_data.shared_data_key);
    CASE_EXPECT_TRUE(shared_it != new_member->shared_member_data.end());
    if (shared_it != new_member->shared_member_data.end()) {
      CASE_EXPECT_EQ(invitee_data.shared_data_value, shared_it->second.data().value());
    }
  }

  // B 的个人频道恰好收到一条内容完整的 joined_team(team_key/team_channel/captain/role)
  expect_joined_team_notification(env, invitee_data, team_id, owner.user_key);

  // 批准生效后邀请内存缓存已清理: 重复 approve 返回 not-found 且零写入
  size_t sends_after_approve = fake.send_message_calls();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                 env.run("approve_again", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                 }));
  CASE_EXPECT_EQ(sends_after_approve, fake.send_message_calls());

  // B 可观察的频道快照(create custom_data + 全部 journal)携带每个成员的完整数据
  expect_snapshot_member_data(fake, owner, {&member_a, &member_b, &invitee_data});

  // 压缩维护后的 custom_data 快照同样携带全部成员的完整数据，待处理邀请/申请列表为空
  {
    global_now_offset_guard guard(std::chrono::seconds{6});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  expect_compacted_storage_complete(fake, owner, {&member_a, &member_b, &invitee_data});

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-07: 拒绝/撤回邀请写 reject_invitation，invitee 收个人回执 ============
// 契约: 队伍成员经队伍频道日志感知否决(reject_invitation 事件负载 = room 本地邀请记录，
// 含规范化的 team key、开始/过期时间与入队来源); 被邀请人收到内容完整的个人回执(admission 数据被裁剪);
// 否决生效后邀请内存缓存被清理(approve 返回 not-found、重复 reject 幂等成功，均零写入)。
// scenario 0: 被邀请人本人拒绝; scenario 1: 管理员撤回(需 reject_invitation_role, 默认 ADMIN)。
CASE_TEST(teamsvr_room_admission, reject_invitation_notify) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  for (uint64_t scenario = 0; scenario < 2; ++scenario) {
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    standard_team_members members;
    CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
    if (!room) {
      break;
    }

    auto invitee = make_user_key(1, 8041u + scenario);
    auto invite_req = make_add_invitation_req(members.normal, members.normal, invitee);
    // 邀请携带 admission 数据: 频道日志负载保留完整记录，个人回执裁剪 admission 数据
    // (记录里有数据，"已裁剪"断言才非空转)
    add_admission_data_entry(invite_req.mutable_invitation()->mutable_team_admission_data(), 1, "ut-team-adm-1");
    auto* member_admission = invite_req.mutable_invitation()->add_member_admission_data();
    protobuf_copy_message(*member_admission->mutable_user_key(), members.normal);
    add_admission_data_entry(member_admission->mutable_member_admission_data(), 3, "ut-member-adm-3");
    CASE_EXPECT_EQ(0, env.run("add_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
    }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    size_t personal_after_invite = env.personal_message_count();

    // 房间规范化后的邀请记录(开始/过期时间由 room 填充)，供 reject 日志与回执断言
    auto& fake = env.channel(team_id);
    google::protobuf::Timestamp record_start;
    google::protobuf::Timestamp record_expired;
    fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
      if (action.action_case() == atfw::team::DTeamAction::kAddInvitation &&
          action.add_invitation().invitee().user_id() == invitee.user_id()) {
        record_start = action.add_invitation().start_timepoint();
        record_expired = action.add_invitation().expired_timepoint();
      }
      return true;
    });
    CASE_EXPECT_GT(record_start.seconds(), 0);
    CASE_EXPECT_GT(record_expired.seconds(), 0);

    const auto& sender = (0 == scenario) ? invitee : members.admin;
    atfw::team::SSTeamRoomRejectInvitationReq reject_req;
    protobuf_copy_message(*reject_req.mutable_sender_user_key(), sender);
    protobuf_copy_message(*reject_req.mutable_invitee(), invitee);
    size_t sends_before = fake.send_message_calls();
    CASE_EXPECT_EQ(0, env.run("reject_invitation", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, reject_req)));
    }));
    CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

    // 队伍成员经队伍频道日志感知否决: reject_invitation 负载 = room 本地邀请记录
    bool found_reject = false;
    fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
      if (action.action_case() == atfw::team::DTeamAction::kRejectInvitation &&
          action.reject_invitation().invitee().user_id() == invitee.user_id()) {
        found_reject = true;
        const auto& rejected = action.reject_invitation();
        CASE_EXPECT_EQ(members.normal.user_id(), rejected.inviter().user_id());
        CASE_EXPECT_EQ(team_id, rejected.team_key().team_id());
        CASE_EXPECT_EQ(kTestZoneId, rejected.team_key().zone_id());
        CASE_EXPECT_EQ(record_start.seconds(), rejected.start_timepoint().seconds());
        CASE_EXPECT_EQ(record_expired.seconds(), rejected.expired_timepoint().seconds());
        CASE_EXPECT_EQ(record_expired.nanos(), rejected.expired_timepoint().nanos());
        CASE_EXPECT_EQ(make_personal_channel(invitee.user_id()).channel_id(),
                       rejected.invitee_private_channel().channel_id());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, rejected.team_source_type());
        // 频道日志负载 = room 本地完整邀请记录(含 admission 数据)
        CASE_EXPECT_EQ(1, rejected.team_admission_data_size());
        CASE_EXPECT_EQ(1, rejected.member_admission_data_size());
      }
      return true;
    });
    CASE_EXPECT_TRUE(found_reject);

    // invitee 恰好收到一条内容完整的个人回执(admission 数据被裁剪); 邀请人与其他成员不收个人通知
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_EQ(personal_after_invite + 1, env.personal_message_count());
    if (personal_after_invite + 1 == env.personal_message_count()) {
      const auto& record = env.personal_messages().back();
      CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kRejectInvitation, record.action.action_case());
      CASE_EXPECT_EQ(make_personal_channel(invitee.user_id()).channel_id(), record.channel.channel_id());
      const auto& rejected = record.action.reject_invitation();
      CASE_EXPECT_EQ(invitee.user_id(), rejected.invitee().user_id());
      CASE_EXPECT_EQ(members.normal.user_id(), rejected.inviter().user_id());
      CASE_EXPECT_EQ(team_id, rejected.team_key().team_id());
      CASE_EXPECT_EQ(record_expired.seconds(), rejected.expired_timepoint().seconds());
      CASE_EXPECT_EQ(0, rejected.team_admission_data_size());
      CASE_EXPECT_EQ(0, rejected.member_admission_data_size());
    }
    CASE_EXPECT_EQ(0u, count_personal_actions(env, members.normal.user_id(),
                                              atfw::team::DTeamMemberAction::kRejectInvitation));
    CASE_EXPECT_EQ(0u, count_personal_actions(env, members.owner.user_id(),
                                              atfw::team::DTeamMemberAction::kRejectInvitation));

    // 否决生效后邀请内存缓存已清理: approve 返回 not-found、重复 reject 幂等成功，均零写入
    size_t sends_after_reject = fake.send_message_calls();
    atfw::team::SSTeamRoomApproveInvitationReq approve_req;
    protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
    protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                   env.run("approve_after_reject", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                   }));
    CASE_EXPECT_EQ(sends_after_reject, fake.send_message_calls());
    CASE_EXPECT_EQ(0, env.run("reject_again", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_invitation(ctx, reject_req)));
    }));
    CASE_EXPECT_EQ(sends_after_reject, fake.send_message_calls());

    room_test_env::clear_rooms();
  }

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
  int64_t first_expired_seconds = 0;
  fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddJoinRequest) {
      found = true;
      const auto& join_request = action.add_join_request();
      CASE_EXPECT_EQ(team_id, join_request.team_key().team_id());
      CASE_EXPECT_EQ(applicant.user_id(), join_request.requester().user_id());
      CASE_EXPECT_GT(join_request.expired_timepoint().seconds(), 0);
      first_expired_seconds = join_request.expired_timepoint().seconds();
      CASE_EXPECT_EQ("ut-join-v1", join_request.client_version());
      CASE_EXPECT_EQ(0x87654321u, join_request.user_router_server_id());
      CASE_EXPECT_EQ(make_personal_channel(8051).channel_id(), join_request.requester_private_channel().channel_id());
      // 入队来源与申请人共享数据(member_admission_data)完整保留
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, join_request.team_source_type());
      CASE_EXPECT_EQ(1, join_request.member_admission_data_size());
      if (1 == join_request.member_admission_data_size()) {
        CASE_EXPECT_EQ(8, join_request.member_admission_data(0).key());
        CASE_EXPECT_EQ("ut-join-admission-data", join_request.member_admission_data(0).value().data().value());
      }
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
    // 受理回执回显申请人的版本/路由/共享数据(对端据此维护本地待处理列表)
    CASE_EXPECT_EQ("ut-join-v1", record.action.apply_join_request().client_version());
    CASE_EXPECT_EQ(0x87654321u, record.action.apply_join_request().user_router_server_id());
    CASE_EXPECT_EQ(1, record.action.apply_join_request().member_admission_data_size());
  }

  // 完全重复的加入请求不追加日志，也不重复发送受理回执
  size_t sends_after_first = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("add_join_2", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
  }));
  CASE_EXPECT_EQ(sends_after_first, fake.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, env.personal_message_count());
  // GAP-09 刷新语义(2026-08-29 澄清): 内容变化的重复请求全量覆盖 admission 数据
  // (以新请求携带的列表为准，不做按键合并、无删除标记)并刷新可变字段，
  // requester/team_key/规范化过期时间保持原值
  auto req_refresh = make_add_join_request_req(applicant);
  req_refresh.mutable_join_request()->set_client_version("ut-join-v2");
  // 全量覆盖: 刷新只携带 key 9 -> key 8 被移除(admission 不做按键合并、无删除标记)
  req_refresh.mutable_join_request()->mutable_member_admission_data()->Clear();
  add_admission_data_entry(req_refresh.mutable_join_request()->mutable_member_admission_data(), 9,
                           "ut-join-admission-data-9");
  CASE_EXPECT_EQ(0, env.run("add_join_3", [room, &req_refresh](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req_refresh)));
  }));
  CASE_EXPECT_EQ(sends_after_first + 1, fake.send_message_calls());

  // 刷新日志 payload: 版本已刷新，全量覆盖后仅剩 key 9；不可变字段保持规范化原值
  atfw::team::DTeamJoinRequest refreshed;
  size_t join_logs = 0;
  fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kAddJoinRequest) {
      ++join_logs;
      refreshed = action.add_join_request();
    }
    return true;
  });
  CASE_EXPECT_EQ(2u, join_logs);  // 首次 + 刷新各一条
  CASE_EXPECT_EQ("ut-join-v2", refreshed.client_version());
  CASE_EXPECT_EQ(applicant.user_id(), refreshed.requester().user_id());
  CASE_EXPECT_EQ(team_id, refreshed.team_key().team_id());
  CASE_EXPECT_EQ(first_expired_seconds, refreshed.expired_timepoint().seconds());
  CASE_EXPECT_EQ(1, refreshed.member_admission_data_size());
  if (1 == refreshed.member_admission_data_size()) {
    CASE_EXPECT_EQ(9, refreshed.member_admission_data(0).key());
    CASE_EXPECT_EQ("ut-join-admission-data-9", refreshed.member_admission_data(0).value().data().value());
  }

  // 内容变化的刷新在回环后补发一次受理回执(对端据此更新本地待处理记录)
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(2u,
                 count_personal_actions(env, applicant.user_id(), atfw::team::DTeamMemberAction::kApplyJoinRequest));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-11/12: 批准/拒绝加入请求 ============
// 数据完整性契约与 ADM-04/05 对齐: 批准人 A 与所有成员经队伍频道日志观察到的 add_member 携带
// 申请人 C 本人申请中的完整版本/路由/共享数据(member_admission_data -> shared_member_data)与入队来源;
// C 可观察的频道快照(含压缩后的 custom_data)携带每个成员的完整数据; C 的个人频道恰好收到一次
// 受理回执与一次内容完整的 joined_team; 批准生效后申请内存缓存被清理(重复 approve 返回 not-found 且零写入)。
// 拒绝路径: reject_join_request 事件负载 = room 本地申请记录; 申请人收到内容完整的拒绝通知
// (member_admission_data 被裁剪); 队伍成员不额外收个人通知; 否决生效后申请内存缓存被清理。
CASE_TEST(teamsvr_room_admission, approve_and_reject_join_request) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  {
    // 批准: 依次写 add_member + approve_join_request；成员数据来自原申请；申请人收 joined_team
    int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    admission_member_data owner;
    admission_member_data member_a;
    admission_member_data member_b;
    CASE_EXPECT_TRUE(setup_full_data_team(env, team_id, room, owner, member_a, member_b));
    if (room) {
      // C 携带本人完整数据(版本/路由/共享数据)申请加入 A 的队伍，入队来源为匹配
      auto applicant_data = make_admission_member_data(kTestZoneId, 8061, "applicant");
      auto req = make_add_join_request_req(applicant_data.user_key);
      req.mutable_join_request()->set_client_version(applicant_data.client_version);
      req.mutable_join_request()->set_user_router_server_id(applicant_data.user_router_server_id);
      req.mutable_join_request()->mutable_member_admission_data()->Clear();
      add_admission_data_entry(req.mutable_join_request()->mutable_member_admission_data(),
                              applicant_data.shared_data_key, applicant_data.shared_data_value);
      CASE_EXPECT_EQ(0, env.run("add_join", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
      }));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      // 申请人恰好收到一次受理回执
      CASE_EXPECT_EQ(1u, count_personal_actions(env, applicant_data.user_key.user_id(),
                                                atfw::team::DTeamMemberAction::kApplyJoinRequest));

      // 房间规范化后的申请记录过期时间，供 approve 事件负载断言
      auto& fake = env.channel(team_id);
      google::protobuf::Timestamp normalized_expired;
      fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kAddJoinRequest &&
            action.add_join_request().requester().user_id() == applicant_data.user_key.user_id()) {
          normalized_expired = action.add_join_request().expired_timepoint();
        }
        return true;
      });
      CASE_EXPECT_GT(normalized_expired.seconds(), 0);

      // A(owner) 批准
      atfw::team::SSTeamRoomApproveJoinRequestReq approve_req;
      protobuf_copy_message(*approve_req.mutable_sender_user_key(), owner.user_key);
      protobuf_copy_message(*approve_req.mutable_applicant(), applicant_data.user_key);

      size_t sends_before = fake.send_message_calls();
      CASE_EXPECT_EQ(0, env.run("approve_join", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
      }));
      // add_member + approve_join_request 两个事件合并为一次请求写入，日志按顺序追加
      CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

      int add_member_index = -1;
      int approve_index = -1;
      int visit_index = 0;
      fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kAddMember &&
            action.add_member().user_key().user_id() == applicant_data.user_key.user_id()) {
          add_member_index = visit_index;
          // A 与所有成员可观察的 add_member 携带 C 的完整数据(版本/路由/共享数据来自 C 的申请)
          expect_complete_member_fields(action.add_member(), applicant_data, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
          // 入队来源从申请记录获取
          CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, action.add_member().team_source_type());
        } else if (action.action_case() == atfw::team::DTeamAction::kApproveJoinRequest &&
                   action.approve_join_request().requester().user_id() == applicant_data.user_key.user_id()) {
          approve_index = visit_index;
          // approve 事件负载 = room 本地申请记录(team key/过期时间以规范化记录为准)
          CASE_EXPECT_EQ(team_id, action.approve_join_request().team_key().team_id());
          CASE_EXPECT_EQ(normalized_expired.seconds(), action.approve_join_request().expired_timepoint().seconds());
          CASE_EXPECT_EQ(normalized_expired.nanos(), action.approve_join_request().expired_timepoint().nanos());
        }
        ++visit_index;
        return true;
      });
      CASE_EXPECT_TRUE(add_member_index >= 0 && approve_index >= 0);
      CASE_EXPECT_LT(add_member_index, approve_index);

      // 事件回环后成员入队，内存中的成员数据与共享数据索引完整
      CASE_EXPECT_EQ(0, env.sync(team_id));
      auto new_member = room->find_member(applicant_data.user_key, false);
      CASE_EXPECT_TRUE(!!new_member);
      if (new_member) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, new_member->member_data.role());
        CASE_EXPECT_EQ(applicant_data.client_version, new_member->member_data.client_version());
        CASE_EXPECT_EQ(applicant_data.user_router_server_id, new_member->member_data.user_router_server_id());
        auto shared_it = new_member->shared_member_data.find(applicant_data.shared_data_key);
        CASE_EXPECT_TRUE(shared_it != new_member->shared_member_data.end());
        if (shared_it != new_member->shared_member_data.end()) {
          CASE_EXPECT_EQ(applicant_data.shared_data_value, shared_it->second.data().value());
        }
      }

      // C 的个人频道恰好收到一条内容完整的 joined_team(team_key/team_channel/captain/role)
      expect_joined_team_notification(env, applicant_data, team_id, owner.user_key);

      // 批准生效后申请内存缓存已清理: 重复 approve 返回 not-found 且零写入
      size_t sends_after_approve = fake.send_message_calls();
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND,
                     env.run("approve_join_again", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
                     }));
      CASE_EXPECT_EQ(sends_after_approve, fake.send_message_calls());

      // C 可观察的频道快照(create custom_data + 全部 journal)携带每个成员的完整数据
      expect_snapshot_member_data(fake, owner, {&member_a, &member_b, &applicant_data});

      // 压缩维护后的 custom_data 快照同样携带全部成员的完整数据，待处理邀请/申请列表为空
      {
        global_now_offset_guard guard(std::chrono::seconds{6});
        env.drive_timer_ticks();
        CASE_EXPECT_EQ(0, env.sync(team_id));
      }
      expect_compacted_storage_complete(fake, owner, {&member_a, &member_b, &applicant_data});

      room_test_env::clear_rooms();
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
      size_t personal_after_apply = env.personal_message_count();

      // 房间规范化后的申请记录过期时间，供 reject 日志与通知断言
      auto& fake = env.channel(team_id);
      google::protobuf::Timestamp record_expired;
      fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kAddJoinRequest &&
            action.add_join_request().requester().user_id() == applicant.user_id()) {
          record_expired = action.add_join_request().expired_timepoint();
        }
        return true;
      });
      CASE_EXPECT_GT(record_expired.seconds(), 0);

      atfw::team::SSTeamRoomRejectJoinRequestReq reject_req;
      protobuf_copy_message(*reject_req.mutable_sender_user_key(), members.normal);
      protobuf_copy_message(*reject_req.mutable_applicant(), applicant);

      size_t sends_before = fake.send_message_calls();
      CASE_EXPECT_EQ(0, env.run("reject_join", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_join_request(ctx, reject_req)));
      }));
      CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());

      // 队伍成员经队伍频道日志感知否决: reject_join_request 负载 = room 本地申请记录
      bool found_reject = false;
      fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kRejectJoinRequest &&
            action.reject_join_request().requester().user_id() == applicant.user_id()) {
          found_reject = true;
          const auto& rejected = action.reject_join_request();
          CASE_EXPECT_EQ(team_id, rejected.team_key().team_id());
          CASE_EXPECT_EQ(kTestZoneId, rejected.team_key().zone_id());
          CASE_EXPECT_EQ(record_expired.seconds(), rejected.expired_timepoint().seconds());
          CASE_EXPECT_EQ(record_expired.nanos(), rejected.expired_timepoint().nanos());
          CASE_EXPECT_EQ(make_personal_channel(applicant.user_id()).channel_id(),
                         rejected.requester_private_channel().channel_id());
          CASE_EXPECT_EQ("ut-join-v1", rejected.client_version());
          CASE_EXPECT_EQ(0x87654321u, rejected.user_router_server_id());
          // 频道日志负载 = room 本地完整申请记录(含 admission 数据)
          CASE_EXPECT_EQ(1, rejected.member_admission_data_size());
        }
        return true;
      });
      CASE_EXPECT_TRUE(found_reject);

      CASE_EXPECT_EQ(0, env.sync(team_id));
      CASE_EXPECT_EQ(nullptr, room->find_member(applicant, false).get());

      // 申请人恰好收到一条内容完整的拒绝通知(member_admission_data 被裁剪); 队伍成员不额外收个人通知
      CASE_EXPECT_EQ(personal_after_apply + 1, env.personal_message_count());
      if (personal_after_apply + 1 == env.personal_message_count()) {
        const auto& record = env.personal_messages().back();
        CASE_EXPECT_EQ(atfw::team::DTeamMemberAction::kRejectJoinRequest, record.action.action_case());
        CASE_EXPECT_EQ(make_personal_channel(applicant.user_id()).channel_id(), record.channel.channel_id());
        const auto& rejected = record.action.reject_join_request();
        CASE_EXPECT_EQ(applicant.user_id(), rejected.requester().user_id());
        CASE_EXPECT_EQ(team_id, rejected.team_key().team_id());
        CASE_EXPECT_EQ(record_expired.seconds(), rejected.expired_timepoint().seconds());
        CASE_EXPECT_EQ(0, rejected.member_admission_data_size());
      }
      CASE_EXPECT_EQ(0u, count_personal_actions(env, members.owner.user_id(),
                                                atfw::team::DTeamMemberAction::kRejectJoinRequest));
      CASE_EXPECT_EQ(0u, count_personal_actions(env, members.normal.user_id(),
                                                atfw::team::DTeamMemberAction::kRejectJoinRequest));

      // 否决生效后申请内存缓存已清理: approve 返回 not-found、重复 reject 幂等成功，均零写入
      size_t sends_after_reject = fake.send_message_calls();
      atfw::team::SSTeamRoomApproveJoinRequestReq approve_req;
      protobuf_copy_message(*approve_req.mutable_sender_user_key(), members.normal);
      protobuf_copy_message(*approve_req.mutable_applicant(), applicant);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND,
                     env.run("approve_after_reject", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
                     }));
      CASE_EXPECT_EQ(sends_after_reject, fake.send_message_calls());
      CASE_EXPECT_EQ(0, env.run("reject_join_again", [room, &reject_req](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->reject_join_request(ctx, reject_req)));
      }));
      CASE_EXPECT_EQ(sends_after_reject, fake.send_message_calls());

      room_test_env::clear_rooms();
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

  room_test_env::clear_rooms();
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

  room_test_env::clear_rooms();
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

      room_test_env::clear_rooms();
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
      CASE_EXPECT_EQ(0, env.sync(team_id));

      // applicant 的 add_member 只出现一次(直接添加那次)，approve 未重复添加
      size_t add_member_count = 0;
      fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (action.action_case() == atfw::team::DTeamAction::kAddMember &&
            action.add_member().user_key().user_id() == applicant.user_id()) {
          ++add_member_count;
        }
        return true;
      });
      CASE_EXPECT_EQ(1u, add_member_count);

      // 已是成员: 申请人仍收到一次 joined_team 通知(与邀请路径契约一致)
      CASE_EXPECT_EQ(1u,
                     count_personal_actions(env, applicant.user_id(), atfw::team::DTeamMemberAction::kJoinedTeam));

      // 加入请求内存缓存已清理: 重复 approve 返回 not-found 且零写入
      size_t sends_after_approve = fake.send_message_calls();
      CASE_EXPECT_EQ(
          PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND,
          env.run("approve_join_again", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
          }));
      CASE_EXPECT_EQ(sends_after_approve, fake.send_message_calls());

      room_test_env::clear_rooms();
    }
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-02/10(GAP-09): 重复 admission 按可变字段刷新并全量覆盖 ============
// 决策(2026-08-29): 邀请者/被邀请人/发起请求者/team_key(及邀请开始时间)不可变；
// 来源/版本/路由/频道/过期时间/admission 数据可更新；admission repeated 数据以新请求全量覆盖，
// 判重时按 key 归一化并忽略元素顺序，语义结果与现有记录一致时不追加日志。
CASE_TEST(teamsvr_room_admission, admission_refresh_updates_mutable_fields) {
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
  // foreach_team_action 的 action 参数是逐迭代解包的局部变量，必须拷出而不能留存指针
  auto last_join_request_action = [&fake](atfw::team::DTeamJoinRequest& out) {
    bool found = false;
    fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
      if (action.action_case() == atfw::team::DTeamAction::kAddJoinRequest) {
        found = true;
        out = action.add_join_request();
      }
      return true;
    });
    return found;
  };

  // 首次加入请求: v1 + admission{8: data-1}(类型化条目)
  auto applicant = make_user_key(1, 8951);
  auto req = make_add_join_request_req(applicant);
  req.mutable_join_request()->mutable_member_admission_data()->Clear();
  add_admission_data_entry(req.mutable_join_request()->mutable_member_admission_data(), 8, "ut-data-1");
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("join_first", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());
  size_t personal_after_first = env.personal_message_count();

  // 完全重复: 不追加日志、不重复发送受理回执
  CASE_EXPECT_EQ(0, env.run("join_duplicate", [room, &req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
                 }));
  CASE_EXPECT_EQ(sends_before + 1, fake.send_message_calls());
  CASE_EXPECT_EQ(personal_after_first, env.personal_message_count());

  // 可变字段刷新: 版本/路由更新 + admission 新增 key 9
  auto refresh = make_add_join_request_req(applicant);
  refresh.mutable_join_request()->set_client_version("ut-join-v2");
  refresh.mutable_join_request()->set_user_router_server_id(0x9999FFFF);
  refresh.mutable_join_request()->mutable_member_admission_data()->Clear();
  add_admission_data_entry(refresh.mutable_join_request()->mutable_member_admission_data(), 8, "ut-data-1");
  add_admission_data_entry(refresh.mutable_join_request()->mutable_member_admission_data(), 9, "ut-data-9");
  CASE_EXPECT_EQ(0, env.run("join_refresh", [room, &refresh](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, refresh)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(sends_before + 2, fake.send_message_calls());
  // 事件回环后重发一次受理回执(apply_add_join_request 每次应用都通知申请人)
  CASE_EXPECT_EQ(personal_after_first + 1, env.personal_message_count());
  {
    // 最新 add_join_request 事件携带刷新后的可变字段与全量 admission 数据
    atfw::team::DTeamJoinRequest last;
    bool found_last = last_join_request_action(last);
    CASE_EXPECT_TRUE(found_last);
    if (found_last) {
      CASE_EXPECT_EQ("ut-join-v2", last.client_version());
      CASE_EXPECT_EQ(0x9999FFFFu, last.user_router_server_id());
      // 归一化结果确定性有序: key 8、key 9 均由本次请求携带
      CASE_EXPECT_EQ(2, last.member_admission_data_size());
      if (2 == last.member_admission_data_size()) {
        CASE_EXPECT_EQ(8, last.member_admission_data(0).key());
        CASE_EXPECT_EQ("ut-data-1", last.member_admission_data(0).value().data().value());
        CASE_EXPECT_EQ(9, last.member_admission_data(1).key());
        CASE_EXPECT_EQ("ut-data-9", last.member_admission_data(1).value().data().value());
      }
    }
  }

  // 顺序无关判重: admission 数据允许无序，同集合不同顺序(且其余字段不变)的刷新
  // 不追加日志、不重复发送受理回执
  {
    auto reorder = make_add_join_request_req(applicant);
    reorder.mutable_join_request()->set_client_version("ut-join-v2");
    reorder.mutable_join_request()->set_user_router_server_id(0x9999FFFF);
    reorder.mutable_join_request()->mutable_member_admission_data()->Clear();
    add_admission_data_entry(reorder.mutable_join_request()->mutable_member_admission_data(), 9, "ut-data-9");
    add_admission_data_entry(reorder.mutable_join_request()->mutable_member_admission_data(), 8, "ut-data-1");
    CASE_EXPECT_EQ(0, env.run("join_reorder_nochange", [room, &reorder](rpc::context& ctx) -> rpc::result_code_type {
                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, reorder)));
                   }));
    CASE_EXPECT_EQ(sends_before + 2, fake.send_message_calls());
    CASE_EXPECT_EQ(personal_after_first + 1, env.personal_message_count());
  }

  // 全量覆盖语义: 刷新请求只携带 key 9 -> key 8 被移除(admission 不做按键合并、无删除标记)
  auto del_req = make_add_join_request_req(applicant);
  del_req.mutable_join_request()->mutable_member_admission_data()->Clear();
  add_admission_data_entry(del_req.mutable_join_request()->mutable_member_admission_data(), 9, "ut-data-9");
  CASE_EXPECT_EQ(0, env.run("join_delete_key", [room, &del_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, del_req)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(sends_before + 3, fake.send_message_calls());
  {
    atfw::team::DTeamJoinRequest last;
    bool found_last = last_join_request_action(last);
    CASE_EXPECT_TRUE(found_last);
    if (found_last) {
      // 注意: del_req 携带版本 v1 与路由默认值，刷新后可变字段以新请求为准
      CASE_EXPECT_EQ(1, last.member_admission_data_size());
      if (1 == last.member_admission_data_size()) {
        CASE_EXPECT_EQ(9, last.member_admission_data(0).key());
        CASE_EXPECT_EQ("ut-data-9", last.member_admission_data(0).value().data().value());
      }
    }
  }

  // 不可变字段与顺序无关判重: 重复邀请的邀请人保持原值；admission 数据(含成员级)允许无序，
  // 同集合不同顺序的刷新不追加日志
  auto invitee = make_user_key(1, 8952);
  auto invite_first = make_add_invitation_req(members.normal, members.normal, invitee);
  add_admission_data_entry(invite_first.mutable_invitation()->mutable_team_admission_data(), 11, "ut-inv-11");
  add_admission_data_entry(invite_first.mutable_invitation()->mutable_team_admission_data(), 12, "ut-inv-12");
  {
    auto* member_data = invite_first.mutable_invitation()->add_member_admission_data();
    protobuf_copy_message(*member_data->mutable_user_key(), members.normal);
    add_admission_data_entry(member_data->mutable_member_admission_data(), 21, "ut-inv-m-21");
    add_admission_data_entry(member_data->mutable_member_admission_data(), 22, "ut-inv-m-22");
  }
  CASE_EXPECT_EQ(0, env.run("invite_first", [room, &invite_first](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_first)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  size_t invite_sends_after_first = fake.send_message_calls();

  // 同一集合乱序刷新(team 12/11、成员级 22/21)：内容未变化 -> 不追加日志
  auto invite_refresh = make_add_invitation_req(members.admin, members.admin, invitee);
  add_admission_data_entry(invite_refresh.mutable_invitation()->mutable_team_admission_data(), 12, "ut-inv-12");
  add_admission_data_entry(invite_refresh.mutable_invitation()->mutable_team_admission_data(), 11, "ut-inv-11");
  {
    auto* member_data = invite_refresh.mutable_invitation()->add_member_admission_data();
    protobuf_copy_message(*member_data->mutable_user_key(), members.normal);
    add_admission_data_entry(member_data->mutable_member_admission_data(), 22, "ut-inv-m-22");
    add_admission_data_entry(member_data->mutable_member_admission_data(), 21, "ut-inv-m-21");
  }
  CASE_EXPECT_EQ(0, env.run("invite_refresh", [room, &invite_refresh](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_refresh)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(invite_sends_after_first, fake.send_message_calls());
  {
    atfw::team::DTeamInvitation last;
    bool found_last = false;
    fake.foreach_team_action([&](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
      if (action.action_case() == atfw::team::DTeamAction::kAddInvitation) {
        found_last = true;
        last = action.add_invitation();
      }
      return true;
    });
    CASE_EXPECT_TRUE(found_last);
    if (found_last) {
      // 邀请者不可变: 刷新事件仍携带首位邀请人
      CASE_EXPECT_EQ(members.normal.user_id(), last.inviter().user_id());
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-08/13(GAP-08): 快照保存过滤过期 admission，恢复时剔除过期项 ============
// 决策(2026-08-29): 快照保存时不写入已过期数据；快照加载时直接剔除过期 admission。
// 恢复侧验证: 快照在 admission 有效期内保存、恢复发生在过期之后 -> 过期项不重建本地待处理缓存
CASE_TEST(teamsvr_room_admission, expired_admission_snapshot_filtering) {
  // 数量维度关闭 + start=1s(keep 缺省为 start/2=0.5s): +1s 即可触发时间维度压缩保存
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 0;
  cfg.compact_log_keep_percent = 0;
  cfg.compact_log_start_seconds = 1;
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

  // 短有效期邀请(3s，压缩保存时尚有效)与长有效期加入请求(60s)
  auto invitee = make_user_key(1, 8961);
  auto invite_req = make_add_invitation_req(members.normal, members.normal, invitee, 3);
  CASE_EXPECT_EQ(0, env.run("add_short_invitation", [room, &invite_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invite_req)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto applicant = make_user_key(1, 8962);
  auto join_req = make_add_join_request_req(applicant, 60);
  CASE_EXPECT_EQ(0, env.run("add_long_join_request", [room, &join_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, join_req)));
                 }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 在邀请仍有效时触发一次压缩保存: 快照携带(当时)有效的邀请与加入请求
  {
    global_now_offset_guard guard(std::chrono::seconds{1});
    env.drive_timer_ticks();
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_GT(room->debug_last_compact_sequence(), 0);
  bool snapshot_has_admissions = false;
  for (const auto& record : fake.update_requests()) {
    if (!record.request.has_custom_data()) {
      continue;
    }
    atfw::team::DTeamStorage storage;
    if (record.request.custom_data().UnpackTo(&storage)) {
      snapshot_has_admissions = storage.pending_invitation_size() + storage.pending_join_request_size() > 0;
    }
  }
  CASE_EXPECT_TRUE(snapshot_has_admissions);

  // 推进到邀请过期之后(不驱动维护，保持频道快照为旧内容)，丢弃房间后恢复:
  // 恢复按恢复时刻剔除过期邀请，长有效期加入请求保留
  {
    global_now_offset_guard guard(std::chrono::seconds{5});
    room_test_env::clear_rooms();
    room.reset();
    auto restored = env.setup_ready_room(team_id);
    CASE_EXPECT_TRUE(!!restored);
    if (!restored) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));

    size_t sends_before = fake.send_message_calls();
    // 过期邀请 approve 返回 not-found 且零写入(未重建本地缓存)
    atfw::team::SSTeamRoomApproveInvitationReq approve_req;
    protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
    protobuf_copy_message(*approve_req.mutable_invitee(), invitee);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                   env.run("approve_expired_after_restore",
                           [restored, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                             RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(restored->approve_invitation(ctx, approve_req)));
                           }));
    CASE_EXPECT_EQ(sends_before, fake.send_message_calls());

    // 长有效期加入请求仍可被批准(成员数据来自原申请)
    atfw::team::SSTeamRoomApproveJoinRequestReq approve_join;
    protobuf_copy_message(*approve_join.mutable_sender_user_key(), members.owner);
    protobuf_copy_message(*approve_join.mutable_applicant(), applicant);
    CASE_EXPECT_EQ(0, env.run("approve_valid_after_restore",
                               [restored, &approve_join](rpc::context& ctx) -> rpc::result_code_type {
                                 RPC_RETURN_CODE(
                                     RPC_AWAIT_CODE_RESULT(restored->approve_join_request(ctx, approve_join)));
                               }));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    CASE_EXPECT_TRUE(nullptr != restored->find_member(applicant, false));
  }

  // 无有效期的 admission 不进入快照(决策澄清: 邀请/加入请求总是必须有有效期)。
  // 注入一条不带 expired_timepoint 的邀请事件(apply 原样入 pending)，维护的过期清理
  // (无判空，epoch-0 视为已过期)先于 dump 移除，压缩快照中不残留任何 admission
  {
    auto room2 = env.setup_ready_room(team_id);
    CASE_EXPECT_TRUE(!!room2);
    if (room2) {
      auto no_expiry_invitee = make_user_key(1, 8963);
      atfw::team::DTeamAction inject_action;
      auto* inject_invitation = inject_action.mutable_add_invitation();
      protobuf_copy_message(*inject_invitation->mutable_inviter(), members.normal);
      protobuf_copy_message(*inject_invitation->mutable_invitee(), no_expiry_invitee);
      protobuf_copy_message(*inject_invitation->mutable_team_key(), make_team_key(team_id));
      CASE_EXPECT_TRUE(nullptr != env.inject_team_action(team_id, inject_action));
      CASE_EXPECT_EQ(0, env.sync(team_id));

      {
        // 恢复后的续租维护点在恢复时刻+5s(恢复块推进 5s 后回退)，推进 11s 确保越过
        global_now_offset_guard guard(std::chrono::seconds{11});
        env.drive_timer_ticks();
      }
      CASE_EXPECT_EQ(0, env.sync(team_id));

      // 无有效期邀请已被清理: approve 视为不存在且零写入
      size_t sends_before = fake.send_message_calls();
      atfw::team::SSTeamRoomApproveInvitationReq approve_no_expiry;
      protobuf_copy_message(*approve_no_expiry.mutable_sender_user_key(), no_expiry_invitee);
      protobuf_copy_message(*approve_no_expiry.mutable_invitee(), no_expiry_invitee);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND,
                     env.run("approve_no_expiry",
                             [room2, &approve_no_expiry](rpc::context& ctx) -> rpc::result_code_type {
                       RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room2->approve_invitation(ctx, approve_no_expiry)));
                     }));
      CASE_EXPECT_EQ(sends_before, fake.send_message_calls());

      // 维护后的压缩快照不携带任何 admission(无有效期记录未进入 dump)。
      // 只检查最后一条携带 custom_data 的 update: 早前的压缩快照合法包含当时有效的 admission
      const update_request_record* last_custom_request = nullptr;
      for (const auto& record : fake.update_requests()) {
        if (record.request.has_custom_data()) {
          last_custom_request = &record;
        }
      }
      bool saw_compact_snapshot = false;
      if (nullptr != last_custom_request) {
        atfw::team::DTeamStorage storage;
        if (last_custom_request->request.custom_data().UnpackTo(&storage)) {
          saw_compact_snapshot = true;
          CASE_EXPECT_EQ(0, storage.pending_invitation_size());
          CASE_EXPECT_EQ(0, storage.pending_join_request_size());
        }
      }
      CASE_EXPECT_TRUE(saw_compact_snapshot);
    }
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-17: add_invitation 待处理邀请数量上限(DTeamConfigure.max_invitation_count) ============
// 超过上限拒绝且不写日志、不发通知; 已存在的邀请不受影响; 受理释放名额后可再次邀请
CASE_TEST(teamsvr_room_admission, add_invitation_count_limit_gate) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner = make_user_key(1, 8701);
  team_room::ptr_t room;
  atfw::team::DTeamConfigure configure = make_standard_team_configure();
  configure.set_max_invitation_count(1);
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(8701), &room, &configure));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);

  auto invitee_a = make_user_key(1, 8702);
  auto invitee_b = make_user_key(1, 8703);
  CASE_EXPECT_EQ(0, env.run("invite_a", [room, &owner, &invitee_a](rpc::context& ctx) -> rpc::result_code_type {
    auto req = make_add_invitation_req(owner, owner, invitee_a);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, count_personal_actions(env, invitee_a.user_id(), atfw::team::DTeamMemberAction::kInvited));

  // 超过上限: 精确错误码、零写入、无个人通知
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_COUNT_LIMIT,
                 env.run("invite_b_over_limit", [room, &owner, &invitee_b](rpc::context& ctx) -> rpc::result_code_type {
                   auto req = make_add_invitation_req(owner, owner, invitee_b);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
                 }));
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  CASE_EXPECT_EQ(0u, count_personal_actions(env, invitee_b.user_id(), atfw::team::DTeamMemberAction::kInvited));

  // 已存在的邀请仍可受理(上限不阻塞 approve)
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee_a);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee_a);
  CASE_EXPECT_EQ(0, env.run("approve_a", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(invitee_a, false) != nullptr);

  // 受理后名额释放, 可再次邀请
  CASE_EXPECT_EQ(0, env.run("invite_b_after_slot_freed",
                            [room, &owner, &invitee_b](rpc::context& ctx) -> rpc::result_code_type {
                              auto req = make_add_invitation_req(owner, owner, invitee_b);
                              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
                            }));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-18: add_join_request 待处理申请数量上限(DTeamConfigure.max_join_request_count) ============
CASE_TEST(teamsvr_room_admission, add_join_request_count_limit_gate) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner = make_user_key(1, 8711);
  team_room::ptr_t room;
  atfw::team::DTeamConfigure configure = make_standard_team_configure();
  configure.set_max_join_request_count(1);
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(8711), &room, &configure));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);

  auto applicant_a = make_user_key(1, 8712);
  auto applicant_b = make_user_key(1, 8713);
  CASE_EXPECT_EQ(0, env.run("join_a", [room, &applicant_a](rpc::context& ctx) -> rpc::result_code_type {
    auto req = make_add_join_request_req(applicant_a);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(1u, count_personal_actions(env, applicant_a.user_id(), atfw::team::DTeamMemberAction::kApplyJoinRequest));

  // 超过上限: 精确错误码、零写入、无受理回执
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_COUNT_LIMIT,
                 env.run("join_b_over_limit", [room, &applicant_b](rpc::context& ctx) -> rpc::result_code_type {
                   auto req = make_add_join_request_req(applicant_b);
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
                 }));
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  CASE_EXPECT_EQ(0u,
                 count_personal_actions(env, applicant_b.user_id(), atfw::team::DTeamMemberAction::kApplyJoinRequest));

  // 已存在的申请仍可受理, 释放名额后可再次申请
  atfw::team::SSTeamRoomApproveJoinRequestReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), owner);
  protobuf_copy_message(*approve_req.mutable_applicant(), applicant_a);
  CASE_EXPECT_EQ(0, env.run("approve_a", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(applicant_a, false) != nullptr);

  CASE_EXPECT_EQ(0, env.run("join_b_after_slot_freed", [room, &applicant_b](rpc::context& ctx) -> rpc::result_code_type {
    auto req = make_add_join_request_req(applicant_b);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
  }));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-19: approve_invitation 满员门禁(DTeamConfigure.max_member_count) ============
// 满员时接受邀请返回精确错误码且不消耗邀请; 有成员离队后可再次接受
CASE_TEST(teamsvr_room_admission, approve_invitation_team_full_gate) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner = make_user_key(1, 8721);
  auto admin = make_user_key(1, 8722);
  team_room::ptr_t room;
  // 小上限: 队长 + 1 名成员即满员
  atfw::team::DTeamConfigure configure = make_standard_team_configure();
  configure.set_max_member_count(2);
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(8721), &room, &configure));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 第二名成员入队(与 setup_standard_team 相同的原始写入路径), 队伍满员
  CASE_EXPECT_EQ(0, env.run("add_admin", [room, &admin](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), admin);
    protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(admin.user_id()));
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(admin, false) != nullptr);

  auto invitee = make_user_key(1, 8723);
  CASE_EXPECT_EQ(0, env.run("invite", [room, &owner, &invitee](rpc::context& ctx) -> rpc::result_code_type {
    auto req = make_add_invitation_req(owner, owner, invitee);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);
  atfw::team::SSTeamRoomApproveInvitationReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), invitee);
  protobuf_copy_message(*approve_req.mutable_invitee(), invitee);

  // 满员: 专用 RPC 与 check_action_permission(send_message 前置校验)返回一致的精确错误码, 零写入
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MAX_MEMBER_COUNT_REACHED,
                 env.run("approve_full", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                 }));
  {
    atfw::team::DTeamAction approve_action;
    protobuf_copy_message(*approve_action.mutable_approve_invitation()->mutable_invitee(), invitee);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MAX_MEMBER_COUNT_REACHED,
                   env.run("check_approve_full",
                           [room, &invitee, &approve_action](rpc::context& ctx) -> rpc::result_code_type {
                             RPC_RETURN_CODE(
                                 RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, invitee, approve_action)));
                           }));
  }
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  CASE_EXPECT_TRUE(room->find_member(invitee, false) == nullptr);

  // 邀请不被满员拒绝消耗: 再次尝试仍报满员而不是 not-found
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MAX_MEMBER_COUNT_REACHED,
                 env.run("approve_full_again", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
                 }));

  // 有成员离队后名额释放, 同一邀请可正常接受
  CASE_EXPECT_EQ(0, env.run("remove_admin", [room, &admin](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), admin);
    action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  CASE_EXPECT_EQ(0, env.run("approve_after_free", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_invitation(ctx, approve_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(invitee, false) != nullptr);
  CASE_EXPECT_EQ(1u, count_personal_actions(env, invitee.user_id(), atfw::team::DTeamMemberAction::kJoinedTeam));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-20: approve_join_request 满员门禁(DTeamConfigure.max_member_count) ============
CASE_TEST(teamsvr_room_admission, approve_join_request_team_full_gate) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner = make_user_key(1, 8731);
  auto admin = make_user_key(1, 8732);
  team_room::ptr_t room;
  atfw::team::DTeamConfigure configure = make_standard_team_configure();
  configure.set_max_member_count(2);
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(8731), &room, &configure));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  CASE_EXPECT_EQ(0, env.run("add_admin", [room, &admin](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    auto* add_member = action.mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), admin);
    protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(admin.user_id()));
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto applicant = make_user_key(1, 8733);
  CASE_EXPECT_EQ(0, env.run("join_request", [room, &applicant](rpc::context& ctx) -> rpc::result_code_type {
    auto req = make_add_join_request_req(applicant);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);
  atfw::team::SSTeamRoomApproveJoinRequestReq approve_req;
  protobuf_copy_message(*approve_req.mutable_sender_user_key(), owner);
  protobuf_copy_message(*approve_req.mutable_applicant(), applicant);

  // 满员: 专用 RPC 与 check_action_permission 返回一致的精确错误码, 零写入
  size_t sends_before = fake.send_message_calls();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MAX_MEMBER_COUNT_REACHED,
                 env.run("approve_full", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
                 }));
  {
    atfw::team::DTeamAction approve_action;
    protobuf_copy_message(*approve_action.mutable_approve_join_request()->mutable_requester(), applicant);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MAX_MEMBER_COUNT_REACHED,
                   env.run("check_approve_full",
                           [room, &owner, &approve_action](rpc::context& ctx) -> rpc::result_code_type {
                             RPC_RETURN_CODE(
                                 RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, owner, approve_action)));
                           }));
  }
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  CASE_EXPECT_TRUE(room->find_member(applicant, false) == nullptr);

  // 申请不被满员拒绝消耗
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MAX_MEMBER_COUNT_REACHED,
                 env.run("approve_full_again", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
                 }));

  // 有成员离队后名额释放, 同一申请可正常批准
  CASE_EXPECT_EQ(0, env.run("remove_admin", [room, &admin](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), admin);
    action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));

  CASE_EXPECT_EQ(0, env.run("approve_after_free", [room, &approve_req](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->approve_join_request(ctx, approve_req)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(applicant, false) != nullptr);
  CASE_EXPECT_EQ(1u, count_personal_actions(env, applicant.user_id(), atfw::team::DTeamMemberAction::kJoinedTeam));

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ADM-21: 配置上限下调后由主控维护经 remove_member 日志事件淘汰多余成员 ============
// 淘汰不在事件应用(apply)路径本地发生: 回放节点必须经同样的日志事件收敛, 避免节点间状态分叉
CASE_TEST(teamsvr_room_admission, config_shrink_trims_overlimit_members_via_maintenance) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner = make_user_key(1, 8741);
  team_room::ptr_t room;
  atfw::team::DTeamConfigure configure = make_standard_team_configure();
  configure.set_max_member_count(4);
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(8741), &room, &configure));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& fake = env.channel(team_id);

  // 真实写入路径补齐到 4 名成员(owner + 3 名普通成员)
  for (uint64_t user_id = 8742; user_id <= 8744; ++user_id) {
    CASE_EXPECT_EQ(0, env.run("add_member", [room, user_id](rpc::context& ctx) -> rpc::result_code_type {
      atfw::team::DTeamAction action;
      auto* add_member = action.mutable_add_member();
      protobuf_copy_message(*add_member->mutable_user_key(), make_user_key(1, user_id));
      protobuf_copy_message(*add_member->mutable_user_channel(), make_personal_channel(user_id));
      add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    }));
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8744), false) != nullptr);

  // 下调上限到 2: 事件应用路径不淘汰成员, 等待主控维护统一处理
  CASE_EXPECT_EQ(0, env.run("shrink_max_member_count", [room](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::DTeamAction action;
    action.mutable_team_update()->mutable_configure()->set_max_member_count(2);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8742), false) != nullptr);
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8743), false) != nullptr);
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8744), false) != nullptr);

  // 推进到下一维护点: 主控维护写入 remove_member 日志事件淘汰多余的非队长成员
  size_t sends_before = fake.send_message_calls();
  {
    global_now_offset_guard guard(std::chrono::seconds{12});
    env.drive_timer_ticks();
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_GT(fake.send_message_calls(), sends_before);

  // 按加入时间(相同则按 user id)从旧到新淘汰: 8742/8743 离队, 队长与最新成员保留
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8742), false) == nullptr);
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8743), false) == nullptr);
  CASE_EXPECT_TRUE(room->find_member(owner, false) != nullptr);
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 8744), false) != nullptr);

  // 淘汰经 remove_member 日志事件下发(回放节点可收敛到同一状态), reason 与管理移除一致
  std::vector<uint64_t> removed_ids;
  fake.foreach_team_action([&removed_ids](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
    if (action.action_case() == atfw::team::DTeamAction::kRemoveMember &&
        action.remove_member().remove_member_reason() == atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER) {
      removed_ids.push_back(action.remove_member().user_key().user_id());
    }
    return true;
  });
  std::sort(removed_ids.begin(), removed_ids.end());
  CASE_EXPECT_EQ(2u, removed_ids.size());
  if (2 == removed_ids.size()) {
    CASE_EXPECT_EQ(static_cast<uint64_t>(8742), removed_ids[0]);
    CASE_EXPECT_EQ(static_cast<uint64_t>(8743), removed_ids[1]);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

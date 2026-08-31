// Copyright 2026 atframework
// Offline regression tests for the lobbysvr team CS task actions (USER_TEAM_TEST_PLAN.md §5.5):
// request-side result codes, SS RPC payloads and observable post-conditions of every entry point.
//
// Every case drives the real generated CS dispatcher (mock_client::post -> cs_msg_dispatcher -> task action),
// asserts the response head error_code for both success and precheck branches, counts the outbound TeamRoomService
// calls captured by team_room_ss_capture (zero-uplink branches must not grow the counters), and checks the full
// uplink payload (team_key/sender/invitee/requester/operator, derived private channels, client_version, router and
// pass-through source data / shared-data conditions) whenever an uplink is required.
// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.team.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <string>
#include <vector>

#include "app/handle_cs_rpc_lobbysvrclientservice.atfw.gen.h"
#include "lobbysvr_test_runtime_helper.h"    // NOLINT: build/include_subdir
#include "lobbysvr_test_user_team_common.h"  // NOLINT: build/include_subdir

namespace {
// Distinctive client version reported through user::set_client_info, asserted in every uplink payload field that
// carries it (create / approve_invitation / add_join_request / member_update).
constexpr char kCsClientVersion[] = "cs-test-9.9.9";

void set_cs_client_version(const user::ptr_t& user_inst) {
  PROJECT_NAMESPACE_ID::DClientDeviceInfo info;
  info.set_client_version(kCsClientVersion);
  user_inst->set_client_info(info);
}

// Count downstream responses for one rpc name addressed to one session (used to wait for a NEW response; the raw
// find helper matches the latest record and would otherwise return a stale response from an earlier post of the
// same rpc in the same case).
size_t count_responses_for(atfw::testing::runtime& test, uint64_t session_id, gsl::string_view rpc_full_name) {
  size_t ret = 0;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const auto* record = test.cs().call_at(i);
    if (nullptr == record || atfw::testing::cs_downstream_record::op_type::post != record->op ||
        record->session_id != session_id) {
      continue;
    }
    atframework::CSMsg cs_msg;
    if (!cs_msg.ParseFromString(record->message.body().post().content())) {
      continue;
    }
    if (cs_msg.head().has_rpc_response() && rpc_full_name == cs_msg.head().rpc_response().rpc_name()) {
      ++ret;
    }
  }
  return ret;
}

// Post one typed team CS request through the real dispatcher and wait for the full downstream response message
// recorded AFTER this post (the response body is empty for all team RPCs; the result code lives in
// head.error_code).
template <class TRequest>
bool post_team_cs_request(atfw::testing::runtime& test, const atfw::testing::mock_client& client,
                          gsl::string_view rpc_full_name, const TRequest& req_body, atframework::CSMsg& out_rsp_msg) {
  const size_t responses_before = count_responses_for(test, client.session_id(), rpc_full_name);
  auto packed = team_test::pack_cs_request(rpc_full_name, req_body);
  int32_t post_res = client.post(packed);
  if (0 != post_res) {
    CASE_MSG_INFO() << "post failed, code=" << post_res << '\n';
    return false;
  }
  if (!team_test::pump_until(
          test, [&] { return count_responses_for(test, client.session_id(), rpc_full_name) > responses_before; })) {
    CASE_MSG_INFO() << "no response\n";
    return false;
  }
  return nullptr != team_test::find_downstream_response(test, client.session_id(), rpc_full_name, out_rsp_msg);
}

// Expected key of the packed member battle(ready) entry produced by user_team_manager::pack_team_member_shared_data.
int64_t member_ready_data_key() {
  return user_team_algorithm::make_team_member_shared_data_key(team_test::make_member_ready_module(false));
}

// Expected key of the packed team battle(matching) entry produced by user_team_manager::pack_team_shared_data.
int64_t team_matching_data_key() {
  return user_team_algorithm::make_team_shared_data_key(team_test::make_team_matching_module(false));
}

// Assert one DTeamAnyDataWithKey entry carries the battle module with the given flag (MEMBER permission fixed by
// the production pack helpers) and unpacks back to the same module content.
void expect_packed_member_ready_entry(const atfw::team::DTeamAnyDataWithKey& entry, bool ready) {
  CASE_EXPECT_EQ(member_ready_data_key(), entry.key());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER, entry.value().permission());
  PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule unpacked;
  CASE_EXPECT_TRUE(entry.value().data().UnpackTo(&unpacked));
  CASE_EXPECT_TRUE(unpacked.has_battle());
  if (unpacked.has_battle()) {
    CASE_EXPECT_EQ(ready, unpacked.battle().ready());
  }
}

void expect_packed_team_matching_entry(const atfw::team::DTeamAnyDataWithKey& entry, bool matching) {
  CASE_EXPECT_EQ(team_matching_data_key(), entry.key());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER, entry.value().permission());
  PROJECT_NAMESPACE_ID::DTeamSharedDataModule unpacked;
  CASE_EXPECT_TRUE(entry.value().data().UnpackTo(&unpacked));
  CASE_EXPECT_TRUE(unpacked.has_battle());
  if (unpacked.has_battle()) {
    CASE_EXPECT_EQ(matching, unpacked.battle().matching());
  }
}

// Join a team through the personal-channel notification, then make the team channel ready with a snapshot whose
// member list contains the captain (kCaptainUserId as OWNER unless self is the captain) and self with self_role.
// Reused for re-configure: pass a higher custom_data_sequence so the subscriber accepts the new custom data.
bool join_team_with_snapshot(atfw::testing::runtime& test, const user::ptr_t& user_inst,
                             team_test::channel_event_chain& private_chain, int64_t team_id,
                             atfw::team::EnTeamPermissionRole self_role, bool self_captain,
                             const atfw::team::DTeamConfigure* configure, const std::vector<uint64_t>& other_members,
                             int64_t custom_data_sequence = 1) {
  if (!team_test::join_team_via_notification(test, user_inst, private_chain, team_id)) {
    return false;
  }
  auto storage = team_test::make_team_storage(team_id);
  uint64_t captain_id = self_captain ? user_inst->get_user_id() : team_test::kCaptainUserId;
  protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(captain_id));
  if (!self_captain) {
    team_test::add_storage_member(storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  }
  team_test::add_storage_member(storage, user_inst->get_user_id(), team_test::role_options(self_role));
  for (uint64_t other_id : other_members) {
    team_test::add_storage_member(storage, other_id, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  }
  if (nullptr != configure) {
    protobuf_copy_message(*storage.mutable_configure(), *configure);
  }
  if (!team_test::receive_channel_event(
          test, team_test::make_snapshot_event(team_test::make_team_channel_key(team_id), 1,
                                               storage.saved_action_sequence(), &storage, custom_data_sequence))) {
    return false;
  }
  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(team_id));
  CASE_EXPECT_TRUE(!!team_ptr);
  if (!team_ptr) {
    return false;
  }
  // Observable readiness: the applied snapshot drives the cached permission role.
  return team_test::pump_until(test, [&] { return team_ptr->get_cached_permission_role() == self_role; });
}

// Full identity assertion for a send_message uplink envelope (team_key + sender).
void expect_send_message_envelope(const atfw::team::SSTeamRoomSendMessageReq& req, int64_t team_id,
                                  uint64_t sender_id) {
  CASE_EXPECT_EQ(team_test::kZoneId, req.team_key().zone_id());
  CASE_EXPECT_EQ(team_id, req.team_key().team_id());
  CASE_EXPECT_EQ(team_test::kZoneId, req.sender_user_key().zone_id());
  CASE_EXPECT_EQ(sender_id, req.sender_user_key().user_id());
}

// An unbound session (no user attached) drives every task action into the not-logined branch.
template <class TRequest>
bool expect_not_logined(atfw::testing::runtime& test, uint64_t session_id, gsl::string_view rpc_full_name,
                        const TRequest& req_body) {
  auto client = test.cs().create_client(team_test::kGatewayNodeId, session_id);
  if (!client || 0 != client.add()) {
    CASE_MSG_INFO() << "not-logined client add failed\n";
    return false;
  }
  atframework::CSMsg rsp_msg;
  if (!post_team_cs_request(test, client, rpc_full_name, req_body, rsp_msg)) {
    return false;
  }
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED, rsp_msg.head().error_code());
  return PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED == rsp_msg.head().error_code();
}
}  // namespace

// CS-INVITE-01: team_send_invitation — invalid invitee / unknown explicit team / current-team reuse /
// create-on-demand with full create+add_invitation payloads / permission-denied zero uplink.
CASE_TEST(lobbysvr_user_team, cs_invite_01_send_invitation_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));
  ss_capture.next_allocated_team_id = 710101;

  constexpr uint64_t kUserId = 91001;
  constexpr uint64_t kInviteeId = 91002;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9100101, client));

  // 未登录: 不入会话的请求直接 EN_ERR_LOGIN_NOT_LOGINED 且零上行
  {
    atframework::shared::CSTeamSendInvitationReq req;
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kInviteeId));
    CASE_EXPECT_TRUE(expect_not_logined(
        test, 9100901, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.create_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_invitation_reqs.size()));
  }

  // invitee 无效(user_id/zone_id 缺 0): EN_ERR_INVALID_PARAM 且零上行
  {
    atframework::shared::CSTeamSendInvitationReq req;
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.create_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_invitation_reqs.size()));
  }

  // 显式指定不存在的队伍: EN_ERR_TEAM_NOT_IN_TEAM 且零上行(不会为该 team_key 创建队伍)
  {
    atframework::shared::CSTeamSendInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(799999));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kInviteeId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.create_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_invitation_reqs.size()));
  }

  // 不指定 team 且无 current: create(team_id=0) -> 本地注册 -> add_invitation, 两次上行 payload 完整
  atframework::shared::CSTeamSendInvitationReq create_flow_req;
  protobuf_copy_message(*create_flow_req.mutable_user_key(), team_test::make_user_key(kInviteeId));
  create_flow_req.set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
  {
    PROJECT_NAMESPACE_ID::DTeamSharedDataModule source_filler;
    source_filler.mutable_battle()->set_matching(true);
    CASE_EXPECT_TRUE(create_flow_req.mutable_team_source_data()->PackFrom(source_filler));
  }
  {
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(test, client,
                                          rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(),
                                          create_flow_req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
  }
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.create_reqs.size()));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.add_invitation_reqs.size()));
  const atfw::team::DTeamKey allocated_key = team_test::make_team_key(710101);
  if (1 == ss_capture.create_reqs.size()) {
    const auto& create_req = ss_capture.create_reqs.front();
    // team_id=0 由 room 分配, zone 参与路由; sender/频道/版本/路由完整上报
    CASE_EXPECT_EQ(0, create_req.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, create_req.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, create_req.sender_user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, create_req.sender_user_key().zone_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), create_req.sender_user_channel().channel_id());
    CASE_EXPECT_EQ(private_channel_key.channel_type(), create_req.sender_user_channel().channel_type());
    CASE_EXPECT_EQ(std::string(kCsClientVersion), create_req.client_version());
    CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), create_req.user_router_server_id());
    // configure 保持默认空值(默认门槛由 room 修订)
    CASE_EXPECT_FALSE(create_req.has_configure());
    // 初始队伍/成员共享数据: battle matching=false / ready=false
    CASE_EXPECT_EQ(1, create_req.shared_team_data_size());
    if (1 == create_req.shared_team_data_size()) {
      expect_packed_team_matching_entry(create_req.shared_team_data(0), false);
    }
    CASE_EXPECT_EQ(1, create_req.shared_member_data_size());
    if (1 == create_req.shared_member_data_size()) {
      expect_packed_member_ready_entry(create_req.shared_member_data(0), false);
    }
  }
  if (1 == ss_capture.add_invitation_reqs.size()) {
    const auto& invite_req = ss_capture.add_invitation_reqs.front();
    const auto& invitation = invite_req.invitation();
    // 完整 team_key(精确等于 create 响应分配的 key)、inviter/invitee、派生私有频道、source 透传;
    // 开始/过期时间留给 room 填充
    CASE_EXPECT_EQ(allocated_key.team_id(), invitation.team_key().team_id());
    CASE_EXPECT_EQ(allocated_key.zone_id(), invitation.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, invitation.inviter().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, invitation.inviter().zone_id());
    CASE_EXPECT_EQ(kInviteeId, invitation.invitee().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, invitation.invitee().zone_id());
    auto expected_invitee_channel = team_test::make_private_channel_key(kInviteeId);
    CASE_EXPECT_EQ(expected_invitee_channel.channel_id(), invitation.invitee_private_channel().channel_id());
    CASE_EXPECT_EQ(expected_invitee_channel.channel_type(), invitation.invitee_private_channel().channel_type());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, invitation.team_source_type());
    CASE_EXPECT_EQ(create_flow_req.team_source_data().SerializeAsString(),
                   invitation.team_source_data().SerializeAsString());
    CASE_EXPECT_FALSE(invitation.has_start_timepoint());
    CASE_EXPECT_FALSE(invitation.has_expired_timepoint());
    CASE_EXPECT_EQ(kUserId, invite_req.sender_user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, invite_req.sender_user_key().zone_id());
  }

  // create 成功后无需 joined_team 即本地注册: 创建者即队长(OWNER), 输出 key 精确等于响应
  {
    auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(allocated_key);
    CASE_EXPECT_TRUE(!!team_ptr);
    if (team_ptr) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, team_ptr->get_cached_permission_role());
      CASE_EXPECT_EQ(kUserId, team_ptr->get_cached_captain_user_key().user_id());
      CASE_EXPECT_EQ(
          team_ptr.get(),
          user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL).get());
    }
    // 发送邀请不建立任何自己的 pending
    CASE_EXPECT_TRUE(nullptr == user_inst->get_user_team_manager().get_pending_invitation(allocated_key));
    CASE_EXPECT_TRUE(nullptr == user_inst->get_user_team_manager().get_pending_join_request(allocated_key));
  }

  // 显式指定已存在的队伍: 复用而不重复 create
  {
    atframework::shared::CSTeamSendInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), allocated_key);
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kInviteeId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.create_reqs.size()));
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.add_invitation_reqs.size()));
    if (2 == ss_capture.add_invitation_reqs.size()) {
      CASE_EXPECT_EQ(allocated_key.team_id(), ss_capture.add_invitation_reqs.back().invitation().team_key().team_id());
    }
  }

  // channel ready 后 configure 生效: invite_role=ADMIN 且自己为 NORMAL -> 权限不足零上行;
  // 同时验证快照就绪后客户端同步确实发生
  {
    auto storage = team_test::make_team_storage(allocated_key.team_id());
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(kUserId));
    team_test::add_storage_member(storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    storage.mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test,
        team_test::make_snapshot_event(team_test::make_team_channel_key(allocated_key.team_id()), 1, 0, &storage)));
    auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(allocated_key);
    CASE_EXPECT_TRUE(!!team_ptr);
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return team_ptr && atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team_ptr->get_cached_permission_role();
    }));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return !team_test::collect_team_dirty(test, client.session_id(), allocated_key.team_id()).snapshots.empty();
    }));

    atframework::shared::CSTeamSendInvitationReq req;
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kInviteeId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.add_invitation_reqs.size()));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// CS-INVITE-02: team_approve_invitation / team_reject_invitation — no-pending / expired zero-uplink branches,
// full uplink payload, client_result pass-through, success/not-found cache final state.
CASE_TEST(lobbysvr_user_team, cs_invite_02_approve_reject_invitation_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 92001;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9200101, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  team_test::now_offset_guard time_guard;

  auto inject_invited = [&](int64_t team_id, std::chrono::system_clock::time_point expired_timepoint) {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kUserId));
    protobuf_copy_message(*invited->mutable_invitee_private_channel(), team_test::make_private_channel_key(kUserId));
    *invited->mutable_start_timepoint() = protobuf_from_system_clock(team_test::now_offset_guard::logical_now());
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
    invited->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return nullptr != user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(team_id));
    }));
  };

  // 无 pending: approve/reject 均 EN_ERR_TEAM_INVITATION_NOT_FOUND 且零上行
  {
    atframework::shared::CSTeamApproveInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(820101));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_approve_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND, rsp_msg.head().error_code());

    atframework::shared::CSTeamRejectInvitationReq reject_req;
    protobuf_copy_message(*reject_req.mutable_team_key(), team_test::make_team_key(820101));
    atframework::CSMsg reject_rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(test, client,
                                          rpc::lobbysvrclientservice::packer::get_full_name_of_team_reject_invitation(),
                                          reject_req, reject_rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND, reject_rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.approve_invitation_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.reject_invitation_reqs.size()));
  }

  // 已过期(expired <= logical_now): 记录尚在但上行被拒绝
  {
    inject_invited(820104, team_test::now_offset_guard::logical_now() + std::chrono::seconds{2});
    team_test::now_offset_guard::advance(std::chrono::seconds{3});
    atframework::shared::CSTeamApproveInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(820104));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_approve_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.approve_invitation_reqs.size()));
  }

  // 正常 approve: sender/invitee/version/router/shared_member_data 完整, 成功后本地 pending 删除
  inject_invited(820101, team_test::now_offset_guard::logical_now() + std::chrono::seconds{600});
  {
    atframework::shared::CSTeamApproveInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(820101));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_approve_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.approve_invitation_reqs.size()));
    const auto& approve_req = ss_capture.approve_invitation_reqs.front();
    CASE_EXPECT_EQ(820101, approve_req.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, approve_req.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, approve_req.sender_user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, approve_req.sender_user_key().zone_id());
    // invitee 来自本地 pending 记录(被邀请人本人)
    CASE_EXPECT_EQ(kUserId, approve_req.invitee().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, approve_req.invitee().zone_id());
    CASE_EXPECT_EQ(std::string(kCsClientVersion), approve_req.client_version());
    CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), approve_req.user_router_server_id());
    CASE_EXPECT_EQ(1, approve_req.shared_member_data_size());
    if (1 == approve_req.shared_member_data_size()) {
      expect_packed_member_ready_entry(approve_req.shared_member_data(0), false);
    }
    CASE_EXPECT_TRUE(nullptr ==
                     user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(820101)));
  }

  // room 频道已不存在(DTMQ_CHANNEL_NOT_FOUND): 映射为邀请不存在并删除确定失效的本地记录
  inject_invited(820102, team_test::now_offset_guard::logical_now() + std::chrono::seconds{600});
  ss_capture.reject_invitation_responder = [](const atfw::team::SSTeamRoomRejectInvitationReq&,
                                              atfw::team::SSTeamRoomRejectInvitationRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND;
  };
  {
    atframework::shared::CSTeamRejectInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(820102));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_reject_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.reject_invitation_reqs.size()));
    const auto& reject_req = ss_capture.reject_invitation_reqs.front();
    CASE_EXPECT_EQ(820102, reject_req.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, reject_req.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, reject_req.sender_user_key().user_id());
    CASE_EXPECT_EQ(kUserId, reject_req.invitee().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, reject_req.invitee().zone_id());
    CASE_EXPECT_TRUE(nullptr ==
                     user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(820102)));
  }
  ss_capture.reject_invitation_responder = nullptr;

  // 普通业务失败: client_result 精确透传且本地 pending 保留以便重试; 重试成功后删除
  inject_invited(820103, team_test::now_offset_guard::logical_now() + std::chrono::seconds{600});
  ss_capture.approve_invitation_responder = [](const atfw::team::SSTeamRoomApproveInvitationReq&,
                                               atfw::team::SSTeamRoomApproveInvitationRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
  };
  {
    atframework::shared::CSTeamApproveInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(820103));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_approve_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.approve_invitation_reqs.size()));
    CASE_EXPECT_TRUE(nullptr !=
                     user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(820103)));
  }
  ss_capture.approve_invitation_responder = nullptr;
  {
    atframework::shared::CSTeamApproveInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(820103));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_approve_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(3, static_cast<int>(ss_capture.approve_invitation_reqs.size()));
    CASE_EXPECT_TRUE(nullptr ==
                     user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(820103)));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// CS-JOIN-01: team_send_join_request — already-in-team / valid-pending zero-uplink branches, full uplink payload,
// channel-not-found mapping, and the local cache only appearing via the personal apply_join_request receipt.
CASE_TEST(lobbysvr_user_team, cs_join_01_send_join_request_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 93001;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9300101, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  // 未登录分支(JOIN 组)
  {
    atframework::shared::CSTeamSendJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(830101));
    CASE_EXPECT_TRUE(expect_not_logined(
        test, 9300901, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_join_request(), req));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_join_request_reqs.size()));
  }

  // 已在队: EN_ERR_TEAM_ALREADY_IN_TEAM 且零上行
  CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, 830101,
                                           atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));
  {
    atframework::shared::CSTeamSendJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(830101));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_join_request_reqs.size()));
  }

  // 正常申请: payload 含 team_key/requester/本人私有频道/source 透传/client_version/router/member_admission_data
  atframework::shared::CSTeamSendJoinRequestReq join_req;
  protobuf_copy_message(*join_req.mutable_team_key(), team_test::make_team_key(830102));
  join_req.set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH);
  {
    PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule source_filler;
    source_filler.mutable_battle()->set_ready(true);
    CASE_EXPECT_TRUE(join_req.mutable_team_source_data()->PackFrom(source_filler));
  }
  {
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(test, client,
                                          rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_join_request(),
                                          join_req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.add_join_request_reqs.size()));
    const auto& add_req = ss_capture.add_join_request_reqs.front();
    // sender_user_key 不上行(生产实现不填): 申请人身份由 join_request.requester 携带
    CASE_EXPECT_EQ(0, add_req.sender_user_key().user_id());
    const auto& join_request = add_req.join_request();
    CASE_EXPECT_EQ(830102, join_request.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, join_request.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, join_request.requester().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, join_request.requester().zone_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), join_request.requester_private_channel().channel_id());
    CASE_EXPECT_EQ(private_channel_key.channel_type(), join_request.requester_private_channel().channel_type());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, join_request.team_source_type());
    CASE_EXPECT_EQ(join_req.team_source_data().SerializeAsString(),
                   join_request.team_source_data().SerializeAsString());
    CASE_EXPECT_EQ(std::string(kCsClientVersion), join_request.client_version());
    CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), join_request.user_router_server_id());
    CASE_EXPECT_EQ(1, join_request.member_admission_data_size());
    if (1 == join_request.member_admission_data_size()) {
      expect_packed_member_ready_entry(join_request.member_admission_data(0), false);
    }
    // 缓存只由个人频道 apply_join_request 建立: send 成功本身不提前插入
    CASE_EXPECT_TRUE(nullptr ==
                     user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(830102)));
  }

  // 存在有效 pending: EN_ERR_TEAM_JOIN_REQUEST_ALREADY_EXISTS 且零上行
  {
    atfw::team::DTeamMemberAction action;
    auto* apply = action.mutable_apply_join_request();
    protobuf_copy_message(*apply->mutable_team_key(), team_test::make_team_key(830103));
    protobuf_copy_message(*apply->mutable_requester(), team_test::make_user_key(kUserId));
    protobuf_copy_message(*apply->mutable_requester_private_channel(), private_channel_key);
    *apply->mutable_expired_timepoint() =
        protobuf_from_system_clock(std::chrono::system_clock::now() + std::chrono::seconds{600});
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return nullptr != user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(830103));
    }));

    atframework::shared::CSTeamSendJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(830103));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_ALREADY_EXISTS, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.add_join_request_reqs.size()));
  }

  // room 频道不存在(DTMQ_CHANNEL_NOT_FOUND)映射为 EN_ERR_TEAM_ROOM_NOT_FOUND
  ss_capture.add_join_request_responder = [](const atfw::team::SSTeamRoomAddJoinRequestReq&,
                                             atfw::team::SSTeamRoomAddJoinRequestRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND;
  };
  {
    atframework::shared::CSTeamSendJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(830104));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ROOM_NOT_FOUND, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.add_join_request_reqs.size()));
  }
  ss_capture.add_join_request_responder = nullptr;

  CASE_EXPECT_EQ(0, test.stop());
}

// CS-JOIN-02: team_accept_join_request / team_reject_join_request — team-missing / role-denied zero-uplink
// branches, full action payload through the team channel and business client_result pass-through.
CASE_TEST(lobbysvr_user_team, cs_join_02_accept_reject_join_request_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 94001;
  constexpr uint64_t kRequesterA = 94002;
  constexpr uint64_t kRequesterB = 94003;
  constexpr int64_t kTeamId = 840101;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9400101, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  // team 不存在: accept/reject 均 EN_ERR_TEAM_NOT_IN_TEAM 且零上行
  {
    atframework::shared::CSTeamAcceptJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(849999));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kRequesterA));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_accept_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, rsp_msg.head().error_code());

    atframework::shared::CSTeamRejectJoinRequestReq reject_req;
    protobuf_copy_message(*reject_req.mutable_team_key(), team_test::make_team_key(849999));
    protobuf_copy_message(*reject_req.mutable_user_key(), team_test::make_user_key(kRequesterA));
    atframework::CSMsg reject_rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_reject_join_request(), reject_req,
        reject_rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, reject_rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 角色不足(approve_join_request_role=ADMIN, 自己 NORMAL): 零上行
  {
    atfw::team::DTeamConfigure configure;
    configure.set_approve_join_request_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                             atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, &configure, {}));

    atframework::shared::CSTeamAcceptJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kRequesterA));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_accept_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());

    atframework::shared::CSTeamRejectJoinRequestReq reject_req;
    protobuf_copy_message(*reject_req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*reject_req.mutable_user_key(), team_test::make_user_key(kRequesterA));
    atframework::CSMsg reject_rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_reject_join_request(), reject_req,
        reject_rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, reject_rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 门槛降为 NORMAL 后: accept 经队伍频道上行 approve_join_request, action 目标完整
  {
    atfw::team::DTeamConfigure configure;
    configure.set_approve_join_request_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    auto storage = team_test::make_team_storage(kTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    team_test::add_storage_member(storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    protobuf_copy_message(*storage.mutable_configure(), configure);
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 0, &storage, 2)));
    auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!team_ptr);
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return team_ptr &&
             team_ptr->get_configure().approve_join_request_role() == atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
    }));

    atframework::shared::CSTeamAcceptJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kRequesterA));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_accept_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(
        1, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kApproveJoinRequest)));
    const auto& action_req = ss_capture.send_message_reqs.back();
    expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& approve = action_req.action().approve_join_request();
    CASE_EXPECT_EQ(kTeamId, approve.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, approve.team_key().zone_id());
    CASE_EXPECT_EQ(kRequesterA, approve.requester().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, approve.requester().zone_id());
  }

  // 业务结果透传: room 返回 EN_ERR_TEAM_NO_PERMISSION 时响应头精确等于该码; 恢复后 reject 正常上行
  ss_capture.send_message_responder = [](const atfw::team::SSTeamRoomSendMessageReq&,
                                         atfw::team::SSTeamRoomSendMessageRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
  };
  {
    atframework::shared::CSTeamRejectJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kRequesterB));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_reject_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1,
                   static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kRejectJoinRequest)));
  }
  ss_capture.send_message_responder = nullptr;
  {
    atframework::shared::CSTeamRejectJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kRequesterB));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_reject_join_request(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2,
                   static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kRejectJoinRequest)));
    const auto& action_req = ss_capture.send_message_reqs.back();
    expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& reject = action_req.action().reject_join_request();
    CASE_EXPECT_EQ(kTeamId, reject.team_key().team_id());
    CASE_EXPECT_EQ(kRequesterB, reject.requester().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, reject.requester().zone_id());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// CS-MEMBER-01: team_exit / team_remove_member / team_update_member_role — self remove goes through the manager
// exit path with reason EXIT_TEAM, removing others needs manage_member_role and carries REMOVE_MEMBER, and
// set-role enforces both the configure threshold and the not-above-self boundary.
CASE_TEST(lobbysvr_user_team, cs_member_01_exit_remove_role_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 95001;
  constexpr uint64_t kMemberB = 95002;
  constexpr int64_t kTeamId = 850101;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9500101, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  // 未登录分支(MEMBER 组)
  {
    atframework::shared::CSTeamRemoveMemberReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kMemberB));
    CASE_EXPECT_TRUE(expect_not_logined(
        test, 9500901, rpc::lobbysvrclientservice::packer::get_full_name_of_team_remove_member(), req));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // team 不存在: remove/exit/set-role 均 EN_ERR_TEAM_NOT_IN_TEAM 且零上行
  {
    atframework::shared::CSTeamRemoveMemberReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(859999));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kMemberB));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_remove_member(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, rsp_msg.head().error_code());

    atframework::shared::CSTeamExitReq exit_req;
    protobuf_copy_message(*exit_req.mutable_team_key(), team_test::make_team_key(859999));
    atframework::CSMsg exit_rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_exit(), exit_req, exit_rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, exit_rsp_msg.head().error_code());

    atframework::shared::CSTeamUpdateMemberRoleReq role_req;
    protobuf_copy_message(*role_req.mutable_team_key(), team_test::make_team_key(859999));
    protobuf_copy_message(*role_req.mutable_user_key(), team_test::make_user_key(kMemberB));
    role_req.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    atframework::CSMsg role_rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_member_role(), role_req,
        role_rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, role_rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 自己 NORMAL + configure 门槛 ADMIN: 移除他人/设置角色均权限不足零上行
  {
    atfw::team::DTeamConfigure configure;
    configure.set_manage_member_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    configure.set_set_member_role_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                             atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, &configure, {kMemberB}));

    atframework::shared::CSTeamRemoveMemberReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kMemberB));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_remove_member(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());

    atframework::shared::CSTeamUpdateMemberRoleReq role_req;
    protobuf_copy_message(*role_req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*role_req.mutable_user_key(), team_test::make_user_key(kMemberB));
    role_req.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    atframework::CSMsg role_rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_member_role(), role_req,
        role_rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, role_rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 移除自己(team_remove_member 指向本人)走 manager exit: reason=EXIT_TEAM, 队伍转 pending-to-exit
  {
    atframework::shared::CSTeamRemoveMemberReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kUserId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_remove_member(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_TRUE(team_test::pump_until(
        test, [&] { return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 1; }));
    bool reason_seen = false;
    for (const auto& send_req : ss_capture.send_message_reqs) {
      if (send_req.action().has_remove_member() && send_req.action().remove_member().user_key().user_id() == kUserId) {
        expect_send_message_envelope(send_req, kTeamId, kUserId);
        CASE_EXPECT_EQ(kTeamId, send_req.action().remove_member().team_key().team_id());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM,
                       send_req.action().remove_member().remove_member_reason());
        reason_seen = true;
      }
    }
    CASE_EXPECT_TRUE(reason_seen);
    auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!team_ptr);
    if (team_ptr) {
      CASE_EXPECT_TRUE(team_ptr->is_exiting());
    }
    CASE_EXPECT_TRUE(
        nullptr == user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  }

  // 重新入队恢复 current, 自己成为 OWNER 队长
  CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                           atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, true, nullptr, {kMemberB}, 2));
  {
    auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!team_ptr);
    if (team_ptr) {
      CASE_EXPECT_FALSE(team_ptr->is_exiting());
    }
  }

  // 设置角色: 业务失败透传, 成功后 action payload 完整; 授予高于自身的角色被拒绝且零上行
  ss_capture.send_message_responder = [](const atfw::team::SSTeamRoomSendMessageReq&,
                                         atfw::team::SSTeamRoomSendMessageRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
  };
  {
    atframework::shared::CSTeamUpdateMemberRoleReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kMemberB));
    req.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_member_role(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kMemberSetRole)));
  }
  ss_capture.send_message_responder = nullptr;
  {
    atframework::shared::CSTeamUpdateMemberRoleReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kMemberB));
    req.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_member_role(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kMemberSetRole)));
    const auto& action_req = ss_capture.send_message_reqs.back();
    expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& set_role = action_req.action().member_set_role();
    CASE_EXPECT_EQ(kMemberB, set_role.user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, set_role.user_key().zone_id());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, set_role.role());

    // 不能授予高于操作者自身的角色(OWNER=300, 之上拒绝)
    atframework::shared::CSTeamUpdateMemberRoleReq over_req;
    protobuf_copy_message(*over_req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*over_req.mutable_user_key(), team_test::make_user_key(kMemberB));
    // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
    over_req.set_role(static_cast<atfw::team::EnTeamPermissionRole>(400));
    // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
    atframework::CSMsg over_rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_member_role(), over_req,
        over_rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, over_rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kMemberSetRole)));
  }

  // 移除他人: reason=REMOVE_MEMBER, action 目标完整
  {
    atframework::shared::CSTeamRemoveMemberReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kMemberB));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_remove_member(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    const auto& action_req = ss_capture.send_message_reqs.back();
    CASE_EXPECT_TRUE(action_req.action().has_remove_member());
    expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& remove_data = action_req.action().remove_member();
    CASE_EXPECT_EQ(kTeamId, remove_data.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, remove_data.team_key().zone_id());
    CASE_EXPECT_EQ(kMemberB, remove_data.user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, remove_data.user_key().zone_id());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, remove_data.remove_member_reason());
  }

  // team_exit 入口同样走 manager exit(reason=EXIT_TEAM)
  {
    atframework::shared::CSTeamExitReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_exit(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_TRUE(team_test::pump_until(
        test, [&] { return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 2; }));
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM,
                   ss_capture.send_message_reqs.back().action().remove_member().remove_member_reason());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// CS-CAPTAIN-01: team_transfer_captain — self-captain always allowed, non-captain requires OWNER, the action goes
// through exactly one team-channel send_message (channel-only path) with the full election target, and the business
// result passes through.
CASE_TEST(lobbysvr_user_team, cs_captain_01_transfer_captain_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 96001;
  constexpr uint64_t kMemberB = 96002;
  constexpr int64_t kTeamId = 860101;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9600101, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  auto post_transfer = [&](int64_t team_id, uint64_t target_id, atframework::CSMsg& out_rsp) {
    atframework::shared::CSTeamTransferCaptainReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(target_id));
    return post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_transfer_captain(), req, out_rsp);
  };

  // 未登录分支(CAPTAIN 组)
  {
    atframework::shared::CSTeamTransferCaptainReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kMemberB));
    CASE_EXPECT_TRUE(expect_not_logined(
        test, 9600901, rpc::lobbysvrclientservice::packer::get_full_name_of_team_transfer_captain(), req));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // team 不存在
  {
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_transfer(869999, kMemberB, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 非队长且非 OWNER(NORMAL/ADMIN): 均 EN_ERR_TEAM_NO_PERMISSION 且零上行
  CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                           atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {kMemberB}));
  {
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_transfer(kTeamId, kMemberB, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
  }
  {
    // 仅重配缓存角色: 重放快照把self提升为ADMIN(仍非队长)
    auto storage = team_test::make_team_storage(kTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    team_test::add_storage_member(storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));
    team_test::add_storage_member(storage, kMemberB, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 0, &storage, 2)));
    auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!team_ptr);
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return team_ptr && atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN == team_ptr->get_cached_permission_role();
    }));

    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_transfer(kTeamId, kMemberB, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 非队长但 OWNER: 允许强制改队长; 仅频道路径(恰好一条 send_message, 无其他上行)
  {
    CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                             atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, false, nullptr, {kMemberB}, 3));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_transfer(kTeamId, kMemberB, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.create_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.heartbeat_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_invitation_reqs.size()));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_join_request_reqs.size()));
    const auto& action_req = ss_capture.send_message_reqs.back();
    CASE_EXPECT_TRUE(action_req.action().has_election_captain());
    expect_send_message_envelope(action_req, kTeamId, kUserId);
    CASE_EXPECT_EQ(kMemberB, action_req.action().election_captain().user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, action_req.action().election_captain().user_key().zone_id());
  }

  // 自己是队长(即便角色只是 NORMAL): 总是允许转移; 业务失败透传后重试成功
  CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                           atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, true, nullptr, {kMemberB}, 4));
  ss_capture.send_message_responder = [](const atfw::team::SSTeamRoomSendMessageReq&,
                                         atfw::team::SSTeamRoomSendMessageRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
  };
  {
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_transfer(kTeamId, kMemberB, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.send_message_reqs.size()));
  }
  ss_capture.send_message_responder = nullptr;
  {
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_transfer(kTeamId, kMemberB, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(3, static_cast<int>(ss_capture.send_message_reqs.size()));
    const auto& action_req = ss_capture.send_message_reqs.back();
    CASE_EXPECT_TRUE(action_req.action().has_election_captain());
    CASE_EXPECT_EQ(kMemberB, action_req.action().election_captain().user_key().user_id());
    expect_send_message_envelope(action_req, kTeamId, kUserId);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// CS-DATA-01: team_update_member_data — empty list / non-writable module / duplicate key zero-uplink branches;
// the uplink auto-fills self user/channel/version/router, uses unique MEMBER-permission keys and appends the
// team-not-matching condition for the ready update.
CASE_TEST(lobbysvr_user_team, cs_data_01_update_member_data_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 97001;
  constexpr int64_t kTeamId = 870101;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9700101, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  auto post_update = [&](const atframework::shared::CSTeamUpdateMemberDataReq& req, atframework::CSMsg& out_rsp) {
    return post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_member_data(), req, out_rsp);
  };

  // 未登录分支(DATA 组)
  {
    atframework::shared::CSTeamUpdateMemberDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_member_ready_module(true));
    CASE_EXPECT_TRUE(expect_not_logined(
        test, 9700901, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_member_data(), req));
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // team 不存在
  {
    atframework::shared::CSTeamUpdateMemberDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(879999));
    protobuf_copy_message(*req.add_data(), team_test::make_member_ready_module(true));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                           atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));

  // 空列表: EN_ERR_INVALID_PARAM 零上行
  {
    atframework::shared::CSTeamUpdateMemberDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 不可写模块(未设置任何 module oneof): EN_ERR_TEAM_NO_PERMISSION 零上行
  {
    atframework::shared::CSTeamUpdateMemberDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    req.add_data();  // 空 module, module_type_case == NOT_SET
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 同批重复 key(ready=true 与 ready=false 同 key): EN_ERR_INVALID_PARAM 零上行
  {
    atframework::shared::CSTeamUpdateMemberDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_member_ready_module(true));
    protobuf_copy_message(*req.add_data(), team_test::make_member_ready_module(false));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 业务失败透传
  ss_capture.send_message_responder = [](const atfw::team::SSTeamRoomSendMessageReq&,
                                         atfw::team::SSTeamRoomSendMessageRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
  };
  {
    atframework::shared::CSTeamUpdateMemberDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_member_ready_module(true));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kMemberUpdate)));
  }
  ss_capture.send_message_responder = nullptr;

  // 正常 ready 更新: 自动填充 self user/channel/version/router, key 唯一且 permission=MEMBER,
  // 附加 team-not-matching 更新条件
  {
    atframework::shared::CSTeamUpdateMemberDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_member_ready_module(true));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kMemberUpdate)));
    const auto& action_req = ss_capture.send_message_reqs.back();
    expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& member_update = action_req.action().member_update();
    CASE_EXPECT_EQ(kUserId, member_update.user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, member_update.user_key().zone_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), member_update.user_channel().channel_id());
    CASE_EXPECT_EQ(private_channel_key.channel_type(), member_update.user_channel().channel_type());
    CASE_EXPECT_EQ(std::string(kCsClientVersion), member_update.client_version());
    CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), member_update.user_router_server_id());
    CASE_EXPECT_EQ(1, member_update.shared_member_data_size());
    if (1 == member_update.shared_member_data_size()) {
      expect_packed_member_ready_entry(member_update.shared_member_data(0), true);
    }
    // ready 更新附加 "队伍不在匹配中" 条件
    CASE_EXPECT_EQ(1, member_update.condition_size());
    if (1 == member_update.condition_size()) {
      const auto& rule = member_update.condition(0);
      CASE_EXPECT_EQ(1, rule.shared_team_data_size());
      if (1 == rule.shared_team_data_size()) {
        CASE_EXPECT_EQ(team_matching_data_key(), rule.shared_team_data(0).key());
        PROJECT_NAMESPACE_ID::DTeamSharedDataModule checked;
        CASE_EXPECT_TRUE(rule.shared_team_data(0).value().UnpackTo(&checked));
        CASE_EXPECT_TRUE(checked.has_battle());
        if (checked.has_battle()) {
          CASE_EXPECT_FALSE(checked.battle().matching());
        }
      }
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// CS-DATA-02: team_update_team_data — empty list / role-denied / non-writable module / duplicate key zero-uplink
// branches; matching=true appends the all-members-ready condition, matching=false appends none, and the business
// result passes through.
CASE_TEST(lobbysvr_user_team, cs_data_02_update_team_data_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 98001;
  constexpr int64_t kTeamId = 880101;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, 9800101, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  auto post_update = [&](const atframework::shared::CSTeamUpdateTeamDataReq& req, atframework::CSMsg& out_rsp) {
    return post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_update_team_data(), req, out_rsp);
  };

  // team 不存在
  {
    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(889999));
    protobuf_copy_message(*req.add_data(), team_test::make_team_matching_module(false));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 角色不足(update_team_data_role=ADMIN, 自己 NORMAL): 零上行
  {
    atfw::team::DTeamConfigure configure;
    configure.set_update_team_data_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                             atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, &configure, {}));

    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_team_matching_module(false));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 门槛放开后(configure 默认空): 空列表 / 不可写模块 / 同批重复 key 均零上行
  CASE_EXPECT_TRUE(join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                           atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}, 2));
  {
    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, rsp_msg.head().error_code());
  }
  {
    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    req.add_data();  // 空 module, module_type_case == NOT_SET
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
  }
  {
    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_team_matching_module(true));
    protobuf_copy_message(*req.add_data(), team_test::make_team_matching_module(false));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // matching=true: 附加 all-members-ready 条件
  {
    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_team_matching_module(true));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate)));
    const auto& action_req = ss_capture.send_message_reqs.back();
    expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& team_update = action_req.action().team_update();
    CASE_EXPECT_EQ(1, team_update.shared_team_data_size());
    if (1 == team_update.shared_team_data_size()) {
      expect_packed_team_matching_entry(team_update.shared_team_data(0), true);
    }
    CASE_EXPECT_EQ(1, team_update.condition_size());
    if (1 == team_update.condition_size()) {
      const auto& rule = team_update.condition(0);
      CASE_EXPECT_EQ(1, rule.member_condition_group_size());
      if (1 == rule.member_condition_group_size()) {
        const auto& group = rule.member_condition_group(0);
        CASE_EXPECT_TRUE(group.all_members());
        CASE_EXPECT_EQ(1, group.member_condition().shared_member_data_size());
        if (1 == group.member_condition().shared_member_data_size()) {
          const auto& checked_item = group.member_condition().shared_member_data(0);
          CASE_EXPECT_EQ(member_ready_data_key(), checked_item.key());
          PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule checked;
          CASE_EXPECT_TRUE(checked_item.value().UnpackTo(&checked));
          CASE_EXPECT_TRUE(checked.has_battle());
          if (checked.has_battle()) {
            CASE_EXPECT_TRUE(checked.battle().ready());
          }
        }
      }
    }
  }

  // matching=false: 不附加条件
  {
    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_team_matching_module(false));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    const auto& action_req = ss_capture.send_message_reqs.back();
    CASE_EXPECT_TRUE(action_req.action().has_team_update());
    CASE_EXPECT_EQ(0, action_req.action().team_update().condition_size());
    CASE_EXPECT_EQ(1, action_req.action().team_update().shared_team_data_size());
    if (1 == action_req.action().team_update().shared_team_data_size()) {
      expect_packed_team_matching_entry(action_req.action().team_update().shared_team_data(0), false);
    }
  }

  // 业务失败透传
  ss_capture.send_message_responder = [](const atfw::team::SSTeamRoomSendMessageReq&,
                                         atfw::team::SSTeamRoomSendMessageRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
  };
  {
    atframework::shared::CSTeamUpdateTeamDataReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.add_data(), team_test::make_team_matching_module(false));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_update(req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(3, static_cast<int>(ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate)));
  }
  ss_capture.send_message_responder = nullptr;

  CASE_EXPECT_EQ(0, test.stop());
}

using lobbysvr_test::find_stream_post_indices;
using lobbysvr_test::flush_pending_chat_messages;

// CS-INVITE-03: approve 响应先于个人频道 joined_team 投递时(同节点队长使队伍频道共享订阅已 ready)，
// add_team 的 try_load_snapshot 同步登记快照脏标记但没有 CS 任务收尾来下发(回归点: 修复前要等下一个
// CS 请求)。修复后由聊天通知推送顺带 flush: 客户端在下发的 chat_channel_sync 之前收到 dirty_team.snapshot。
CASE_TEST(lobbysvr_user_team, cs_invite_03_approve_dirty_snapshot_flushed_with_chat_sync) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr int64_t kTeamId = 890101;
  constexpr uint64_t kCaptainId = 99002;
  constexpr uint64_t kInviteeId = 99001;
  constexpr uint64_t kInviteeSessionId = 9900101;
  team_test::now_offset_guard time_guard;

  // 同节点队长: 恢复队伍并应用快照, 让 team 频道的共享订阅实例进入 ready;
  // 之后被邀请人 add_team 时 try_load_snapshot 才能同步命中(不再等订阅回调)
  user::ptr_t captain_inst;
  std::string captain_subscriber_key;
  atframework::dtmq::DChannelIdKey captain_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kCaptainId, captain_inst, captain_subscriber_key, captain_private_channel_key));
  if (!captain_inst) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::restore_team_from_table(test, captain_inst, kTeamId));
  {
    auto storage = team_test::make_team_storage(kTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(kCaptainId));
    team_test::add_storage_member(storage, kCaptainId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, storage));
  }
  // Observable readiness: 队长视角的角色已由快照驱动
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto team_ptr = captain_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    return team_ptr && atfw::team::EN_TEAM_MEMBER_ROLE_OWNER == team_ptr->get_cached_permission_role();
  }));

  // 被邀请人: 会话绑定 + 待处理邀请(个人频道 invited 事件)
  user::ptr_t invitee_inst;
  std::string invitee_subscriber_key;
  atframework::dtmq::DChannelIdKey invitee_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kInviteeId, invitee_inst, invitee_subscriber_key, invitee_private_channel_key));
  if (!invitee_inst) {
    test.stop();
    return;
  }
  atfw::testing::mock_client invitee_client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, invitee_inst, kInviteeSessionId, invitee_client));

  team_test::channel_event_chain invitee_private_chain;
  invitee_private_chain.channel_key = invitee_private_channel_key;
  {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(kCaptainId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kInviteeId));
    protobuf_copy_message(*invited->mutable_invitee_private_channel(), invitee_private_channel_key);
    *invited->mutable_start_timepoint() = protobuf_from_system_clock(team_test::now_offset_guard::logical_now());
    *invited->mutable_expired_timepoint() =
        protobuf_from_system_clock(team_test::now_offset_guard::logical_now() + std::chrono::seconds{600});
    invited->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, invitee_private_chain, action));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return nullptr != invitee_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamId));
    }));
  }

  // room 在 approve 后把双成员快照写进频道 custom data(被邀请人尚未注册订阅, 只推进共享实例内容)
  {
    auto storage = team_test::make_team_storage(kTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(kCaptainId));
    team_test::add_storage_member(storage, kCaptainId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(storage, kInviteeId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 0, &storage, 2)));
  }

  // approve 先于 joined_team 完成: 响应成功时还没有任何 team 脏数据待下发
  {
    atframework::shared::CSTeamApproveInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, invitee_client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_approve_invitation(), req,
        rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.approve_invitation_reqs.size()));
    CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(test, kInviteeSessionId).empty());
  }

  // joined_team 在个人频道投递: add_team -> try_load_snapshot 同步命中已 ready 的共享订阅,
  // 快照脏标记登记成功但没有 CS 任务收尾来触发下发; 修复前要等下一个 CS 请求(如 ping)
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, invitee_inst, invitee_private_chain, kTeamId));
  CASE_EXPECT_TRUE(!!invitee_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  team_test::pump_rounds(test, 4);
  CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(test, kInviteeSessionId).empty());

  // 聊天推送 flush 顺带下发脏数据: dirty 在 chat sync 之前, 无需任何新的 CS 请求
  flush_pending_chat_messages(test);
  const auto dirty_indices = find_stream_post_indices(
      test, kInviteeSessionId, rpc::lobbysvrclientservice::packer::get_full_name_of_user_dirty_chg_sync());
  const auto chat_indices = find_stream_post_indices(
      test, kInviteeSessionId, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync());
  CASE_EXPECT_EQ(1, static_cast<int>(dirty_indices.size()));
  CASE_EXPECT_EQ(1, static_cast<int>(chat_indices.size()));
  if (!dirty_indices.empty() && !chat_indices.empty()) {
    CASE_EXPECT_LT(dirty_indices.front(), chat_indices.front());
  }

  // 快照内容: 双成员 + 角色 + 队长(内部路由字段裁剪契约由 DIRTY 组用例覆盖, 这里不重复)
  auto dirty_view = team_test::collect_team_dirty(test, kInviteeSessionId, kTeamId);
  CASE_EXPECT_EQ(1, static_cast<int>(dirty_view.snapshots.size()));
  if (!dirty_view.snapshots.empty()) {
    const auto& snapshot = dirty_view.snapshots.front();
    CASE_EXPECT_EQ(kTeamId, snapshot.snapshot().team_key().team_id());
    CASE_EXPECT_EQ(kCaptainId, snapshot.snapshot().captain_user_key().user_id());
    const auto* captain_member = team_test::find_snapshot_member(snapshot, kCaptainId);
    const auto* invitee_member = team_test::find_snapshot_member(snapshot, kInviteeId);
    CASE_EXPECT_TRUE(nullptr != captain_member);
    CASE_EXPECT_TRUE(nullptr != invitee_member);
    if (nullptr != captain_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
    }
    if (nullptr != invitee_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, invitee_member->role());
    }
  }

  // 结算残留聊天推送, 避免跨用例污染(process-global 队列)
  flush_pending_chat_messages(test);
  CASE_EXPECT_EQ(0, test.stop());
}

// CS-INVITE-04: 当前队伍是已销毁(同节点另一订阅先把队伍频道销毁, 本用户 joined_team 迟到, on_destroyed
// 不重放注册前的 destroy, 已销毁占住 current)时: 显式指定已销毁队伍直接 EN_ERR_TEAM_NOT_IN_TEAM 并收编已销毁
// (索引移除 + 客户端收到 destroy); 未指定 team 的邀请不复用已销毁, 直接 create 新队伍且邀请落在新 team_id 上;
// 显式指定存活队伍仍正常复用(对照组, 防止 destroyed 判断写反)。
// 注意: 首个 CS 请求的 task 前置 refresh 会把 destroyed current 已销毁按 EXPIRED 收编(task_action_cs_req_base.cpp:113
// -> user::refresh_feature_limit -> user_team_manager::refresh_feature_limit_minute), 用例先手动耗尽首次
// 分钟 refresh, 已销毁才能存活到 task body 命中 is_destroyed 分支。
CASE_TEST(lobbysvr_user_team, cs_invite_04_destroyed_current_team_replaced_on_invite) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // The CS dispatcher registrations live in the generated handle unit and are per-runtime (dispatcher state resets
  // on each runtime start), so every case must register them explicitly like the chat manager cases do.
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));
  ss_capture.next_allocated_team_id = 710401;
  team_test::now_offset_guard time_guard;

  constexpr int64_t kTeamId = 710402;
  constexpr uint64_t kOtherMemberId = 91041;  // 同节点另一用户: 先把队伍频道订阅到 ready 再销毁
  constexpr uint64_t kUserId = 91042;         // 被测用户(邀请者)
  constexpr uint64_t kInviteeId = 91043;
  constexpr uint64_t kSessionId = 9104201;

  // 另一用户把队伍频道订阅到 ready, 然后 WAL destroy 日志销毁(共享实例销毁但仍缓存, 其本地队伍被收编)
  user::ptr_t other_inst;
  std::string other_subscriber_key;
  atframework::dtmq::DChannelIdKey other_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kOtherMemberId, other_inst, other_subscriber_key, other_private_channel_key));
  if (!other_inst) {
    test.stop();
    return;
  }
  {
    team_test::channel_event_chain other_private_chain;
    other_private_chain.channel_key = other_private_channel_key;
    CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, other_inst, other_private_chain, kTeamId));
  }
  {
    auto storage = team_test::make_team_storage(kTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(kOtherMemberId));
    team_test::add_storage_member(storage, kOtherMemberId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, storage));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      auto team_ptr = other_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
      return team_ptr && atfw::team::EN_TEAM_MEMBER_ROLE_OWNER == team_ptr->get_cached_permission_role();
    }));
  }
  {
    team_test::channel_event_chain team_chain;
    team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
    CASE_EXPECT_TRUE(team_test::inject_log_message(test, team_chain, team_test::make_destroy_log_message(0)));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return !other_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    }));
  }

  // 被测用户: 先手动耗尽首次分钟 refresh(首个 CS 请求的 task 前置 refresh 会把 destroyed 已销毁按 EXPIRED 收编,
  // 见 task_action_cs_req_base.cpp:113; 耗尽后同一分钟内不再触发, 已销毁才能存活到 task body 命中新分支)
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  set_cs_client_version(user_inst);
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));
  CASE_EXPECT_TRUE(team_test::run_sync_task(test, "team.drain_minute_refresh",
                                            [&user_inst](rpc::context& ctx) -> rpc::result_code_type {
                                              user_inst->refresh_feature_limit(ctx);
                                              RPC_RETURN_CODE(0);
                                            }));

  // 被测用户 joined_team 迟到: add_team 挂在已销毁的共享实例上, 已销毁成为 current
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  {
    auto corpse = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!corpse);
    if (!corpse) {
      test.stop();
      return;
    }
    CASE_EXPECT_TRUE(corpse->is_destroyed());
    CASE_EXPECT_EQ(
        corpse.get(),
        user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL).get());
  }

  // 显式指定已销毁队伍: 直接 EN_ERR_TEAM_NOT_IN_TEAM, 零新增上行, 已销毁被收编且客户端收到 destroy
  {
    atframework::shared::CSTeamSendInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kInviteeId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM, rsp_msg.head().error_code());
  }
  CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.create_reqs.size()));
  CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.add_invitation_reqs.size()));
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  {
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
      return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam).empty();
    }));
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto destroys = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam);
    CASE_EXPECT_EQ(1, static_cast<int>(destroys.size()));
    if (!destroys.empty()) {
      CASE_EXPECT_EQ(kTeamId, destroys.front()->action().destroy_team().team_id());
    }
  }

  // 再次 joined_team 复活已销毁(共享实例仍在缓存且 destroyed), 覆盖未指定 team 的邀请分支
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  {
    auto corpse = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!corpse);
    if (!corpse) {
      test.stop();
      return;
    }
    CASE_EXPECT_TRUE(corpse->is_destroyed());
  }

  // 未指定 team 的邀请: 不复用已销毁, create 新队伍且邀请落在 room 新分配的 team_id 上
  const auto allocated_key = team_test::make_team_key(710401);
  {
    atframework::shared::CSTeamSendInvitationReq req;
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kInviteeId));
    req.set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
  }
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.create_reqs.size()));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.add_invitation_reqs.size()));
  if (1 == ss_capture.add_invitation_reqs.size()) {
    const auto& invitation = ss_capture.add_invitation_reqs.front().invitation();
    CASE_EXPECT_EQ(allocated_key.team_id(), invitation.team_key().team_id());
    CASE_EXPECT_EQ(allocated_key.zone_id(), invitation.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, invitation.inviter().user_id());
    CASE_EXPECT_EQ(kInviteeId, invitation.invitee().user_id());
  }
  {
    auto new_team = user_inst->get_user_team_manager().get_team_by_team_key(allocated_key);
    CASE_EXPECT_TRUE(!!new_team);
    if (new_team) {
      CASE_EXPECT_FALSE(new_team->is_destroyed());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, new_team->get_cached_permission_role());
      CASE_EXPECT_EQ(
          new_team.get(),
          user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL).get());
    }
  }

  // 对照组: 显式指定存活队伍, 正常复用且不被误删(防止 destroyed 判断写反)
  {
    atframework::shared::CSTeamSendInvitationReq req;
    protobuf_copy_message(*req.mutable_team_key(), allocated_key);
    protobuf_copy_message(*req.mutable_user_key(), team_test::make_user_key(kInviteeId));
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_team_cs_request(
        test, client, rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_invitation(), req, rsp_msg));
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.add_invitation_reqs.size()));
    if (2 == ss_capture.add_invitation_reqs.size()) {
      CASE_EXPECT_EQ(allocated_key.team_id(), ss_capture.add_invitation_reqs.back().invitation().team_key().team_id());
    }
    CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(allocated_key));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

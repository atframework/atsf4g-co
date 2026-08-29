// Copyright 2026 atframework
//
// teamsvr SDK 封装用例:
//   SDK-DTMQ-01~04: rpc::dtmq::update/reset_lock/destroy_channel 从 req.channel_key 提取目标节点
//   SDK-TEAM-01~07: rpc::team::team_api 封装从 req 提取 DTeamKey(zone_id + team_id) 并内嵌一致性哈希路由

#include "teamsvr_room_test_common.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/dtmq/dtmq_client_api.h>
#include <rpc/team/team_common_api.h>
#include <rpc/team/team_room_client_api.h>
#include <rpc/team/teamroomservice.atfw.gen.h>

namespace {
using namespace teamsvr_room_test;

constexpr uint64_t kRemoteRoomNodeId = 0x11000002;

// 记录某 RPC 最近一次调用的目标节点
uint64_t last_call_target(atframework::testing::runtime& rt, gsl::string_view full_rpc_name, size_t before) {
  size_t total = rt.ss().calls(full_rpc_name);
  if (total <= before) {
    return 0;
  }
  const auto* record = rt.ss().call_at(total - 1);
  return nullptr == record ? 0 : record->target_node_id;
}
}  // namespace

// ============ SDK-DTMQ-01: rpc::dtmq::reset_lock 从 req.channel_key 提取目标并下发 CAS ============
CASE_TEST(teamsvr_room_sdk_api, dtmq_reset_lock_routed_by_req_channel_key) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto& fake = env.channel(team_id);
  fake.ensure_created();

  CASE_EXPECT_EQ(0, env.run("reset_lock", [&fake](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::dtmq::SSChannelResetLockReq> req{ctx};
    rpc::context::message_holder<atfw::dtmq::SSChannelResetLockRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_channel_key(), fake.channel_key());
    auto* checker = req->mutable_compare_and_maybe_reset_lock();
    checker->set_allow_empty_real_value(true);
    checker->mutable_reset_value()->set_lock_holder("sdk-test");
    *checker->mutable_reset_value()->mutable_timeout() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{30});
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::reset_lock(ctx, *req, *rsp)));
  }));
  CASE_EXPECT_EQ(1u, fake.reset_lock_calls());
  CASE_EXPECT_EQ("sdk-test", fake.lock().lock_holder());
  // 路由断言: 目标节点从 req.channel_key 提取(dtmq-proxy 节点)，而非仅本地处理器命中
  CASE_EXPECT_EQ(kDtmqProxyNodeId,
                 last_call_target(env.runtime(), rpc::dtmq::packer::get_full_name_of_reset_lock(), 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-DTMQ-02: rpc::dtmq::update / destroy_channel 从 req.channel_key 提取目标 ============
CASE_TEST(teamsvr_room_sdk_api, dtmq_update_and_destroy_routed_by_req_channel_key) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto& fake = env.channel(team_id);
  fake.ensure_created();

  CASE_EXPECT_EQ(0, env.run("update", [&fake](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::dtmq::SSChannelUpdateReq> req{ctx};
    rpc::context::message_holder<atfw::dtmq::SSChannelUpdateRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_channel_key(), fake.channel_key());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::update(ctx, *req, *rsp)));
  }));
  CASE_EXPECT_EQ(1u, fake.update_calls());
  CASE_EXPECT_EQ(kDtmqProxyNodeId, last_call_target(env.runtime(), rpc::dtmq::packer::get_full_name_of_update(), 0));

  CASE_EXPECT_EQ(0, env.run("destroy", [&fake](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::dtmq::SSChannelDestroyChannelReq> req{ctx};
    rpc::context::message_holder<google::protobuf::Empty> rsp{ctx};
    protobuf_copy_message(*req->mutable_channel_key(), fake.channel_key());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::destroy_channel(ctx, *req, *rsp)));
  }));
  CASE_EXPECT_EQ(1u, fake.destroy_calls());
  CASE_EXPECT_EQ(kDtmqProxyNodeId,
                 last_call_target(env.runtime(), rpc::dtmq::packer::get_full_name_of_destroy_channel(), 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-DTMQ-03: req.channel_key 为空时返回 INVALID_PARAM 且不发 RPC ============
CASE_TEST(teamsvr_room_sdk_api, dtmq_wrappers_reject_empty_channel_key) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto& fake = env.channel(team_id);

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                 env.run("empty_update", [](rpc::context& ctx) -> rpc::result_code_type {
                   rpc::context::message_holder<atfw::dtmq::SSChannelUpdateReq> req{ctx};
                   rpc::context::message_holder<atfw::dtmq::SSChannelUpdateRsp> rsp{ctx};
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::update(ctx, *req, *rsp)));
                 }));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                 env.run("empty_reset_lock", [](rpc::context& ctx) -> rpc::result_code_type {
                   rpc::context::message_holder<atfw::dtmq::SSChannelResetLockReq> req{ctx};
                   rpc::context::message_holder<atfw::dtmq::SSChannelResetLockRsp> rsp{ctx};
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::reset_lock(ctx, *req, *rsp)));
                 }));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                 env.run("empty_destroy", [](rpc::context& ctx) -> rpc::result_code_type {
                   rpc::context::message_holder<atfw::dtmq::SSChannelDestroyChannelReq> req{ctx};
                   rpc::context::message_holder<google::protobuf::Empty> rsp{ctx};
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::destroy_channel(ctx, *req, *rsp)));
                 }));
  CASE_EXPECT_EQ(0u, fake.update_calls());
  CASE_EXPECT_EQ(0u, fake.reset_lock_calls());
  CASE_EXPECT_EQ(0u, fake.destroy_calls());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-01: create/send_message/heartbeat 按完整 team key 路由到对应 zone 的哈希节点 ============
// 本进程节点改为非房间节点(app_id_override)，使本地哈希目标的 RPC 也经过 mock transport 以便断言
CASE_TEST(teamsvr_room_sdk_api, team_api_basic_rpcs_route_to_hash_node) {
  room_test_env env;
  env.app_id_override = 0x11000009;
  if (!env.start()) {
    return;
  }

  auto create_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_create(), atfw::team::SSTeamRoomCreateReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomCreateRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomCreateRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!create_rule);
  auto send_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_send_message(),
      atfw::team::SSTeamRoomSendMessageReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomSendMessageRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomSendMessageRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!send_rule);
  auto heartbeat_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_heartbeat(), atfw::team::SSTeamRoomHeartbeatReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomHeartbeatRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomHeartbeatRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!heartbeat_rule);

  int64_t team_id = next_test_team_id();  // 哈希到 kLocalRoomNodeId
  auto sender = make_user_key(1, 9101);

  CASE_EXPECT_EQ(0, env.run("create", [team_id, &sender](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomCreateReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomCreateRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req->mutable_sender_user_key(), sender);
    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::create(ctx, *req, *rsp));
    CASE_EXPECT_EQ(0, rsp->client_result());
    RPC_RETURN_CODE(ret);
  }));
  CASE_EXPECT_EQ(kLocalRoomNodeId, last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_create(), 0));

  CASE_EXPECT_EQ(0, env.run("send_message", [team_id, &sender](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req->mutable_sender_user_key(), sender);
    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::send_message(ctx, *req, *rsp));
    CASE_EXPECT_EQ(0, rsp->client_result());
    RPC_RETURN_CODE(ret);
  }));
  CASE_EXPECT_EQ(kLocalRoomNodeId,
                 last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_send_message(), 0));

  CASE_EXPECT_EQ(0, env.run("heartbeat", [team_id, &sender](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomHeartbeatReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomHeartbeatRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req->mutable_user_key(), sender);
    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::heartbeat(ctx, *req, *rsp));
    CASE_EXPECT_EQ(0, rsp->client_result());
    RPC_RETURN_CODE(ret);
  }));
  CASE_EXPECT_EQ(kLocalRoomNodeId, last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_heartbeat(), 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-01b/CRT-02: create 的 team_id=0 必须到达服务端 UUID 生成分支 ============
CASE_TEST(teamsvr_room_sdk_api, team_api_create_allows_server_generated_team_id) {
  room_test_env env;
  env.app_id_override = 0x11000009;
  if (!env.start()) {
    return;
  }

  constexpr int64_t kGeneratedTeamId = 0x33000001;
  atfw::team::DTeamKey received_key;
  auto create_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_create(), atfw::team::SSTeamRoomCreateReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomCreateRsp::descriptor()->full_name(),
      [&received_key](const atframework::testing::ss_request_view& request,
                      google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::team::SSTeamRoomCreateReq&>(request.body);
        auto& typed_response = static_cast<atfw::team::SSTeamRoomCreateRsp&>(response);
        protobuf_copy_message(received_key, typed_request.team_key());
        typed_response.set_client_result(0);
        protobuf_copy_message(*typed_response.mutable_team_key(), make_team_key(kGeneratedTeamId));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!create_rule);

  const auto full_name = rpc::team::packer::get_full_name_of_create();
  const size_t calls_before = env.runtime().ss().calls(full_name);
  atfw::team::SSTeamRoomCreateRsp response;
  int32_t create_result =
      env.run("create_generate_team_id", [&response](rpc::context& ctx) -> rpc::result_code_type {
        rpc::context::message_holder<atfw::team::SSTeamRoomCreateReq> request{ctx};
        request->mutable_team_key()->set_zone_id(kTestZoneId);
        protobuf_copy_message(*request->mutable_sender_user_key(), make_user_key(kTestZoneId, 9100));
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::team::team_api::create(ctx, *request, response)));
      });
  CASE_EXPECT_EQ(0, create_result);
  if (0 != create_result) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  CASE_EXPECT_EQ(calls_before + 1, env.runtime().ss().calls(full_name));
  CASE_EXPECT_EQ(kTestZoneId, received_key.zone_id());
  CASE_EXPECT_EQ(0, received_key.team_id());
  CASE_EXPECT_EQ(0, response.client_result());
  CASE_EXPECT_EQ(kTestZoneId, response.team_key().zone_id());
  CASE_EXPECT_EQ(kGeneratedTeamId, response.team_key().team_id());

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-02: 哈希到远端节点的 team 路由到远端 ============
CASE_TEST(teamsvr_room_sdk_api, team_api_routes_to_remote_node) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // 注入第二个 teamsvr-room 节点，找一个哈希到远端的 team id(与 ROU-02 同法)
  {
    atframework::testing::mock_node node;
    node.set_id(kRemoteRoomNodeId)
        .set_name("unit-test-teamsvr-room-remote")
        .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kTeamRoomSvr))
        .set_type_name("teamsvr-room")
        .set_zone_id(1)
        .add_label("hpa_scaling_ready", "1")
        .add_label("hpa_scaling_target", "1");
    CASE_EXPECT_TRUE(!!env.runtime().discovery().add_node(node));
    if (nullptr != logic_server_last_common_module()) {
      logic_server_last_common_module()->reload();
    }
  }

  int64_t remote_team = 0;
  for (int64_t i = 1; i <= 4096 && 0 == remote_team; ++i) {
    int64_t candidate = 0x3000000 + i;
    if (rpc::team::team_api::get_teamsvr_room_server_id_of_zone(make_team_key(candidate)) == kRemoteRoomNodeId) {
      remote_team = candidate;
    }
  }
  CASE_EXPECT_NE(0, remote_team);
  if (0 == remote_team) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto send_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_send_message(),
      atfw::team::SSTeamRoomSendMessageReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomSendMessageRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomSendMessageRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!send_rule);

  auto sender = make_user_key(1, 9102);
  CASE_EXPECT_EQ(0, env.run("send_remote", [remote_team, &sender](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_team_key(), make_team_key(remote_team));
    protobuf_copy_message(*req->mutable_sender_user_key(), sender);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::team::team_api::send_message(ctx, *req, *rsp)));
  }));
  CASE_EXPECT_EQ(kRemoteRoomNodeId,
                 last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_send_message(), 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-03: add_invitation 从嵌套 invitation.team_key/inviter 提取路由键 ============
CASE_TEST(teamsvr_room_sdk_api, team_api_add_invitation_nested_key_routing) {
  room_test_env env;
  env.app_id_override = 0x11000009;
  if (!env.start()) {
    return;
  }

  auto rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_add_invitation(),
      atfw::team::SSTeamRoomAddInvitationReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomAddInvitationRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomAddInvitationRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);

  int64_t team_id = next_test_team_id();
  CASE_EXPECT_EQ(0, env.run("add_invitation", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomAddInvitationReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomAddInvitationRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_invitation()->mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req->mutable_invitation()->mutable_inviter(), make_user_key(1, 9103));
    protobuf_copy_message(*req->mutable_invitation()->mutable_invitee(), make_user_key(1, 9104));
    protobuf_copy_message(*req->mutable_sender_user_key(), make_user_key(1, 9103));
    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::add_invitation(ctx, *req, *rsp));
    CASE_EXPECT_EQ(0, rsp->client_result());
    RPC_RETURN_CODE(ret);
  }));
  CASE_EXPECT_EQ(kLocalRoomNodeId,
                 last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_add_invitation(), 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-04: add_join_request 从嵌套 join_request.team_key/requester 提取路由键 ============
CASE_TEST(teamsvr_room_sdk_api, team_api_add_join_request_nested_key_routing) {
  room_test_env env;
  env.app_id_override = 0x11000009;
  if (!env.start()) {
    return;
  }

  auto rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_add_join_request(),
      atfw::team::SSTeamRoomAddJoinRequestReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomAddJoinRequestRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomAddJoinRequestRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);

  int64_t team_id = next_test_team_id();
  CASE_EXPECT_EQ(0, env.run("add_join_request", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomAddJoinRequestReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomAddJoinRequestRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_join_request()->mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req->mutable_join_request()->mutable_requester(), make_user_key(1, 9105));
    protobuf_copy_message(*req->mutable_sender_user_key(), make_user_key(1, 9105));
    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::add_join_request(ctx, *req, *rsp));
    CASE_EXPECT_EQ(0, rsp->client_result());
    RPC_RETURN_CODE(ret);
  }));
  CASE_EXPECT_EQ(kLocalRoomNodeId,
                 last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_add_join_request(), 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-05: approve/reject 按 team_key 路由(与对方 applicant/invitee 的 zone 无关) ============
CASE_TEST(teamsvr_room_sdk_api, team_api_approve_reject_route_by_team_key) {
  room_test_env env;
  env.app_id_override = 0x11000009;
  if (!env.start()) {
    return;
  }

  auto approve_join_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_approve_join_request(),
      atfw::team::SSTeamRoomApproveJoinRequestReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomApproveJoinRequestRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomApproveJoinRequestRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!approve_join_rule);
  auto reject_invitation_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_reject_invitation(),
      atfw::team::SSTeamRoomRejectInvitationReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomRejectInvitationRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomRejectInvitationRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!reject_invitation_rule);

  int64_t team_id = next_test_team_id();
  CASE_EXPECT_EQ(0, env.run("approve_join", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomApproveJoinRequestReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomApproveJoinRequestRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req->mutable_sender_user_key(), make_user_key(1, 9106));
    protobuf_copy_message(*req->mutable_applicant(), make_user_key(1, 9107));
    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::approve_join_request(ctx, *req, *rsp));
    CASE_EXPECT_EQ(0, rsp->client_result());
    RPC_RETURN_CODE(ret);
  }));
  CASE_EXPECT_EQ(kLocalRoomNodeId,
                 last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_approve_join_request(), 0));

  CASE_EXPECT_EQ(0, env.run("reject_invitation", [team_id](rpc::context& ctx) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::team::SSTeamRoomRejectInvitationReq> req{ctx};
    rpc::context::message_holder<atfw::team::SSTeamRoomRejectInvitationRsp> rsp{ctx};
    protobuf_copy_message(*req->mutable_team_key(), make_team_key(team_id));
    protobuf_copy_message(*req->mutable_sender_user_key(), make_user_key(1, 9106));
    protobuf_copy_message(*req->mutable_invitee(), make_user_key(1, 9108));
    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::reject_invitation(ctx, *req, *rsp));
    CASE_EXPECT_EQ(0, rsp->client_result());
    RPC_RETURN_CODE(ret);
  }));
  CASE_EXPECT_EQ(kLocalRoomNodeId,
                 last_call_target(env.runtime(), rpc::team::packer::get_full_name_of_reject_invitation(), 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-06: 无可用节点的 zone 返回 NOT_AVAILABLE 且不发 RPC ============
CASE_TEST(teamsvr_room_sdk_api, team_api_no_ready_node_returns_not_available) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  const auto full_name = rpc::team::packer::get_full_name_of_send_message();
  size_t calls_before = env.runtime().ss().calls(full_name);

  // zone 999 没有任何 teamsvr-room 节点(路由 zone 取自 team_key)
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE,
                 env.run("send_no_node", [team_id](rpc::context& ctx) -> rpc::result_code_type {
                   rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req{ctx};
                   rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp{ctx};
                   protobuf_copy_message(*req->mutable_team_key(), make_team_key(team_id, 999));
                   protobuf_copy_message(*req->mutable_sender_user_key(), make_user_key(1, 9109));
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::team::team_api::send_message(ctx, *req, *rsp)));
                 }));
  CASE_EXPECT_EQ(calls_before, env.runtime().ss().calls(full_name));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ SDK-TEAM-07: team_id 为 0 返回 INVALID_PARAM 且不发 RPC;zone_id 为 0 视为不分区队伍,走全局发现集正常路由 ============
CASE_TEST(teamsvr_room_sdk_api, team_api_reject_invalid_routing_key) {
  room_test_env env;
  // 本进程不作为房间节点,zone 0(不分区队伍)经全局发现集路由到远端 mock 节点
  env.app_id_override = 0x11000009;
  if (!env.start()) {
    return;
  }

  const auto full_name = rpc::team::packer::get_full_name_of_send_message();
  size_t calls_before = env.runtime().ss().calls(full_name);
  auto send_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_send_message(),
      atfw::team::SSTeamRoomSendMessageReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomSendMessageRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atfw::team::SSTeamRoomSendMessageRsp&>(response).set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!send_rule);

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                 env.run("send_zero_team", [](rpc::context& ctx) -> rpc::result_code_type {
                   rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req{ctx};
                   rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp{ctx};
                   protobuf_copy_message(*req->mutable_sender_user_key(), make_user_key(1, 9110));
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::team::team_api::send_message(ctx, *req, *rsp)));
                 }));
  CASE_EXPECT_EQ(calls_before, env.runtime().ss().calls(full_name));

  // zone_id 为 0 表示不分区的全局队伍,从全局发现集(含 zone 1 的节点)路由,RPC 正常下发
  CASE_EXPECT_EQ(0,
                 env.run("send_zero_zone", [](rpc::context& ctx) -> rpc::result_code_type {
                   rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req{ctx};
                   rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp{ctx};
                   protobuf_copy_message(*req->mutable_team_key(), make_team_key(next_test_team_id(), 0));
                   protobuf_copy_message(*req->mutable_sender_user_key(), make_user_key(1, 9110));
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::team::team_api::send_message(ctx, *req, *rsp)));
                 }));
  CASE_EXPECT_EQ(calls_before + 1, env.runtime().ss().calls(full_name));
  CASE_EXPECT_EQ(kLocalRoomNodeId, last_call_target(env.runtime(), full_name, 0));

  env.clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

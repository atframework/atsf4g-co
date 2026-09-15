// Copyright 2026 atframework

#include "teamsvr_room_legacy_wire.h"  // NOLINT: build/include_subdir
#include "teamsvr_room_test_common.h"  // NOLINT: build/include_subdir

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on
#include <google/protobuf/wrappers.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/ss_action.h>
#include <atgateway/protocol/libatgw_protocol_api.h>
#include <logic/action/task_action_add_invitation.h>
#include <logic/action/task_action_add_join_request.h>
#include <logic/action/task_action_create.h>
#include <logic/action/task_action_send_message.h>
#include <rpc/db/uuid.h>
#include <rpc/team/teamroomservice.atfw.gen.h>

#include <limits>
#include <map>
#include <random>
#include <string>
#include <utility>

namespace {
using namespace teamsvr_room_test;  // NOLINT(build/namespaces)

static bool start_env(room_test_env& env) {
  const bool started = env.start();
  CASE_EXPECT_TRUE(started);
  return started;
}

template <class Action>
static int32_t invoke_action(room_test_env& env, gsl::string_view rpc_name,
                             const typename Action::rpc_request_type& request) {
  static uint64_t sequence = 49000;
  atfw::testing::ss_action_invoke_options options{rpc_name};
  options.source.node_id = kDtmqProxyNodeId;
  options.source.node_name = "room-contract-caller";
  options.source.source_task_id = 51001;
  options.source.sequence = ++sequence;
  const size_t before = env.runtime().transport().outbound_count();
  const int32_t result =
      env.run("room_contract_action", [request, options](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<Action>(ctx, request, options)));
      });
  for (size_t index = before; index < env.runtime().transport().outbound_count(); ++index) {
    const auto* record = env.runtime().transport().outbound_at(index);
    if (!record || record->target_node_id != options.source.node_id || record->sequence != options.source.sequence) {
      continue;
    }
    atframework::SSMsg response;
    CASE_EXPECT_TRUE(response.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size())));
    return response.head().error_code();
  }
  CASE_MSG_INFO() << "missing action response, task result=" << result << '\n';
  CASE_EXPECT_TRUE(false);
  return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
}

static atfw::team::SSTeamRoomCreateReq create_request(const atfw::team::DTeamKey& key) {
  atfw::team::SSTeamRoomCreateReq request;
  *request.mutable_team_key() = key;
  *request.mutable_sender_user_key() = make_user_key(1, 49001);
  *request.mutable_sender_user_channel() = make_personal_channel(49001);
  request.set_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
  return request;
}

static int32_t send_action(room_test_env& env, const team_room::ptr_t& room, const atfw::team::DTeamAction& action) {
  return env.run("contract_send", [room, action](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  });
}

static int32_t flush(room_test_env& env) {
  return env.run("contract_flush", [](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(team_room_manager::me()->flush_pending_channel_message(ctx)));
  });
}

static void add_remote_room_node(room_test_env& env, uint64_t node_id, uint32_t zone_id) {
  atfw::testing::mock_node node;
  node.set_id(node_id)
      .set_name("room-contract-remote")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kTeamRoomSvr))
      .set_type_name("teamsvr-room")
      .set_zone_id(zone_id)
      .add_label("hpa_scaling_ready", "1")
      .add_label("hpa_scaling_target", "1");
  CASE_EXPECT_TRUE(!!env.runtime().discovery().add_node(node));
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
}

static void expect_keyed_data(const google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& actual,
                              const std::map<int64_t, std::string>& expected) {
  CASE_EXPECT_EQ(expected.size(), static_cast<size_t>(actual.size()));
  std::map<int64_t, std::string> observed;
  for (const auto& entry : actual) {
    CASE_EXPECT_TRUE(observed.emplace(entry.key(), entry.value().data().value()).second);
    auto found = expected.find(entry.key());
    CASE_EXPECT_TRUE(found != expected.end());
    if (found != expected.end()) {
      CASE_EXPECT_EQ(found->second, entry.value().data().value());
    }
  }
}
}  // namespace

CASE_TEST(teamsvr_room_contract, uuid_failure_does_not_create_room) {
  room_test_env env;
  CASE_EXPECT_TRUE(env.start());
  if (!env.runtime().is_running()) {
    return;
  }
  auto failure = env.runtime().db().mock_table("uuid_allocator");
  failure.on(atfw::testing::db_table_context::op_type::kv_inc_field, [](atfw::testing::db_table_context& context) {
    context.return_code = PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED;
    return true;
  });
  // The allocator caches 13-bit ID blocks across cases. Exhaust the existing block through its public API.
  const auto exhausted = env.run("exhaust_uuid_block", [](rpc::context& ctx) -> rpc::result_code_type {
    for (size_t index = 0; index <= 8192; ++index) {
      auto id = RPC_AWAIT_TYPE_RESULT(rpc::db::uuid::generate_global_unique_id(
          ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_DEFAULT, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MIT_DEFAULT, 0));
      if (id < 0) {
        RPC_RETURN_CODE(static_cast<int32_t>(id));
      }
    }
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED, exhausted);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED,
                 invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(),
                                                   create_request(make_team_key(0))));
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_EQ(0u, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_update()));
  CASE_EXPECT_EQ(0u, env.personal_message_count());
  failure.reset();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, create_invalid_type_and_missing_zone_do_not_write) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  auto request = create_request(make_team_key(next_test_team_id()));
  request.set_team_type(std::numeric_limits<uint32_t>::max());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVALID_TEAM_TYPE,
                 invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(), request));
  request.set_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
  request.mutable_team_key()->set_zone_id(912);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE,
                 invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(), request));
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_EQ(0u, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_update()));
  CASE_EXPECT_EQ(0u, env.personal_message_count());
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, create_duplicate_member_keys_rejected_before_write) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  const auto key = make_team_key(next_test_team_id());
  auto request = create_request(key);
  add_team_any_data_entry(request.mutable_shared_member_data(), 7, "first");
  add_team_any_data_entry(request.mutable_shared_member_data(), 7, "second");
  auto room = env.setup_ready_room(key);
  CASE_EXPECT_TRUE(!!room);
  if (room) {
    auto& channel = env.channel(key);
    const size_t updates = channel.update_calls();
    const size_t resets = channel.reset_lock_calls();
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                   invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(), request));
    CASE_EXPECT_EQ(updates, channel.update_calls());
    CASE_EXPECT_EQ(resets, channel.reset_lock_calls());
    CASE_EXPECT_EQ(0u, channel.send_message_calls());
    CASE_EXPECT_EQ(0u, env.personal_message_count());
    CASE_EXPECT_EQ(nullptr, room->find_member(request.sender_user_key(), false).get());
    request.mutable_shared_member_data()->RemoveLast();
    CASE_EXPECT_EQ(0, invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(), request));
    CASE_EXPECT_EQ(0, env.sync(key));
    CASE_EXPECT_TRUE(!!room->find_member(request.sender_user_key(), false));
  }
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, cross_zone_admission_forwards_complete_request_and_error) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  constexpr uint64_t remote = 0x11000012;
  add_remote_room_node(env, remote, 12);
  const auto key = make_team_key(std::numeric_limits<int64_t>::max(), 12);
  size_t received = 0;
  atfw::testing::ss_rule_options remote_options;
  remote_options.match_node_id = remote;
  atfw::team::SSTeamRoomAddInvitationReq forwarded_invitation;
  auto invitation_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_add_invitation(),
      atfw::team::SSTeamRoomAddInvitationReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomAddInvitationRsp::descriptor()->full_name(),
      [&received, &forwarded_invitation, key, remote](const atfw::testing::ss_request_view& view,
                                                      google::protobuf::Message&) -> rpc::result_code_type {
        ++received;
        const auto& request = static_cast<const atfw::team::SSTeamRoomAddInvitationReq&>(view.body);
        forwarded_invitation = request;
        CASE_EXPECT_EQ(remote, view.target_node_id);
        CASE_EXPECT_EQ(key.team_id(), request.invitation().team_key().team_id());
        CASE_EXPECT_EQ(12u, request.invitation().team_key().zone_id());
        CASE_EXPECT_EQ(1u, request.sender_user_key().zone_id());
        CASE_EXPECT_EQ(33u, request.invitation().invitee().zone_id());
        CASE_EXPECT_EQ("foreign-player", request.invitation().invitee_private_channel().channel_id());
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_COUNT_LIMIT);
      },
      remote_options);
  atfw::team::SSTeamRoomAddInvitationReq invitation;
  *invitation.mutable_sender_user_key() = make_user_key(1, 49001);
  *invitation.mutable_invitation()->mutable_team_key() = key;
  *invitation.mutable_invitation()->mutable_inviter() = invitation.sender_user_key();
  *invitation.mutable_invitation()->mutable_invitee() = make_user_key(33, std::numeric_limits<uint64_t>::max());
  invitation.mutable_invitation()->mutable_invitee_private_channel()->set_channel_type(91);
  invitation.mutable_invitation()->mutable_invitee_private_channel()->set_channel_id("foreign-player");
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_COUNT_LIMIT,
      invoke_action<task_action_add_invitation>(env, rpc::team::packer::get_full_name_of_add_invitation(), invitation));
  CASE_EXPECT_EQ(invitation.SerializeAsString(), forwarded_invitation.SerializeAsString());
  atfw::team::SSTeamRoomAddJoinRequestReq forwarded_join;
  auto join_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_add_join_request(),
      atfw::team::SSTeamRoomAddJoinRequestReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomAddJoinRequestRsp::descriptor()->full_name(),
      [&received, &forwarded_join, remote](const atfw::testing::ss_request_view& view,
                                           google::protobuf::Message&) -> rpc::result_code_type {
        ++received;
        const auto& request = static_cast<const atfw::team::SSTeamRoomAddJoinRequestReq&>(view.body);
        forwarded_join = request;
        CASE_EXPECT_EQ(remote, view.target_node_id);
        CASE_EXPECT_EQ(12u, request.join_request().team_key().zone_id());
        CASE_EXPECT_EQ(33u, request.join_request().requester().zone_id());
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL);
      },
      remote_options);
  atfw::team::SSTeamRoomAddJoinRequestReq join;
  *join.mutable_sender_user_key() = invitation.invitation().invitee();
  *join.mutable_join_request()->mutable_requester() = join.sender_user_key();
  *join.mutable_join_request()->mutable_team_key() = key;
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL,
      invoke_action<task_action_add_join_request>(env, rpc::team::packer::get_full_name_of_add_join_request(), join));
  CASE_EXPECT_EQ(join.SerializeAsString(), forwarded_join.SerializeAsString());
  CASE_EXPECT_EQ(2u, received);
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_EQ(0u, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_update()));
  CASE_EXPECT_EQ(0u, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_send_message()));
  invitation_rule.reset();
  join_rule.reset();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, write_failure_and_response_order_preserve_committed_state) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  auto& channel = env.channel(team_id);
  const auto member = room->find_member(members.normal, false);
  const auto original_version = member->member_data.client_version();
  const size_t personal = env.personal_message_count();
  const int64_t sequence = channel.last_sequence();
  atfw::team::DTeamAction action;
  *action.mutable_member_update()->mutable_user_key() = members.normal;
  action.mutable_member_update()->set_client_version("committed-after-failure");
  channel.next_send_fault.present = true;
  channel.next_send_fault.commit_first = false;
  channel.next_send_fault.error_code = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL, send_action(env, room, action));
  CASE_EXPECT_EQ(sequence, channel.last_sequence());
  CASE_EXPECT_EQ(original_version, member->member_data.client_version());
  CASE_EXPECT_EQ(0, send_action(env, room, action));
  CASE_EXPECT_EQ(original_version, member->member_data.client_version());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ("committed-after-failure", member->member_data.client_version());

  action.mutable_member_update()->set_client_version("event-before-response");
  env.send_message_response_gate.armed = true;
  auto pending = env.runtime().run_task("event_before_response", std::chrono::seconds{8},
                                        [room, action](rpc::context& ctx) -> rpc::result_code_type {
                                          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
                                        });
  CASE_EXPECT_TRUE(env.wait_for([&env]() { return room_test_env::gate_parked(env.send_message_response_gate); }));
  CASE_EXPECT_EQ("committed-after-failure", member->member_data.client_version());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ("event-before-response", member->member_data.client_version());
  CASE_EXPECT_TRUE(room_test_env::release_gate(env.send_message_response_gate));
  const auto completed = env.runtime().wait(pending, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(completed.task_exited);
  CASE_EXPECT_EQ(0, completed.result_code);

  action.mutable_member_update()->set_client_version("committed-response-lost");
  channel.next_send_fault.present = true;
  channel.next_send_fault.commit_first = true;
  channel.next_send_fault.error_code = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL, send_action(env, room, action));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ("committed-response-lost", member->member_data.client_version());
  const auto committed = channel.last_sequence();
  CASE_EXPECT_EQ(0, send_action(env, room, action));
  CASE_EXPECT_EQ(committed, channel.last_sequence());
  CASE_EXPECT_EQ(0, flush(env));
  CASE_EXPECT_EQ(personal, env.personal_message_count());
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, invalid_snapshots_recover_without_partial_writes_or_notifications) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  for (int variant = 0; variant < 4; ++variant) {
    const int64_t team_id = next_test_team_id();
    auto& channel = env.channel(team_id);
    channel.ensure_created();
    auto owner = make_user_key(1, 49100 + variant);
    atfw::team::DTeamStorage valid;
    *valid.mutable_team_key() = make_team_key(team_id);
    *valid.mutable_captain_user_key() = owner;
    valid.set_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    auto* member = valid.add_member();
    *member->mutable_user_key() = owner;
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    member->set_client_version("intact-snapshot");
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    *member->mutable_last_heartbeat_timepoint() = member->joined_timepoint();
    atfw::team::DTeamRoomPrivateData private_data;
    private_data.set_team_created(true);
    channel.set_custom_data(valid);
    channel.set_private_data(private_data);
    switch (variant) {
      case 0:
        channel.set_corrupt_custom_data("type.googleapis.com/atframework.team.DTeamStorage", "\xff");
        break;
      case 1:
        channel.set_private_data(google::protobuf::Empty{});
        break;
      case 2: {
        auto invalid = valid;
        invalid.set_saved_action_sequence(-1);
        channel.set_custom_data(invalid);
        break;
      }
      default:
        private_data.set_last_compact_sequence(-1);
        channel.set_private_data(private_data);
        private_data.set_last_compact_sequence(0);
        break;
    }
    CASE_EXPECT_FALSE(!!env.setup_ready_room(team_id));
    auto room = team_room_manager::me()->get_room(make_team_key(team_id));
    CASE_EXPECT_TRUE(!!room);
    if (!room) {
      continue;
    }
    atfw::team::DTeamAction action;
    *action.mutable_member_update()->mutable_user_key() = owner;
    action.mutable_member_update()->set_client_version("after-repair");
    CASE_EXPECT_FALSE(room->is_lock_holder());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE, send_action(env, room, action));
    CASE_EXPECT_EQ(0u, channel.send_message_calls());
    CASE_EXPECT_EQ(0u, channel.update_calls());
    CASE_EXPECT_EQ(0u, channel.reset_lock_calls());
    CASE_EXPECT_EQ(0u, env.personal_message_count());
    // A repaired snapshot is a new publication, with newer metadata than the rejected one.
    channel.append_log([](atfw::dtmq::DChannelMessageDetail& detail) { detail.set_noop(true); });
    channel.set_custom_data(valid);
    channel.set_private_data(private_data);
    CASE_EXPECT_EQ(0, env.sync(team_id, true));
    CASE_EXPECT_EQ(0, env.run("await_repaired_snapshot", [room](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
    }));
    auto restored = room->find_member(owner, false);
    CASE_EXPECT_TRUE(!!restored);
    if (restored) {
      CASE_EXPECT_EQ("intact-snapshot", restored->member_data.client_version());
    }
    CASE_EXPECT_EQ(0, send_action(env, room, action));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    if (restored) {
      CASE_EXPECT_EQ("after-repair", restored->member_data.client_version());
    }
    CASE_EXPECT_EQ(0u, env.personal_message_count());
    room_test_env::clear_rooms();
  }
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, failed_heartbeat_snapshot_is_saved_by_next_maintenance) {
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 1000;
  cfg.compact_log_start_seconds = 3600;
  cfg.compact_log_keep_seconds = 1800;
  room_test_env env(cfg);
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  auto owner = make_user_key(1, 49201);
  team_room::ptr_t room;
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(owner.user_id()), &room));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto& channel = env.channel(team_id);
  const auto saved = channel.custom_data().SerializeAsString();
  CASE_EXPECT_EQ(0, env.run("heartbeat_dirty", [room, owner](rpc::context& ctx) -> rpc::result_code_type {
    atfw::team::SSTeamRoomHeartbeatReq heartbeat;
    *heartbeat.mutable_user_key() = owner;
    heartbeat.set_sequence(901);
    heartbeat.set_hash_code(902);
    heartbeat.set_user_router_server_id(903);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, heartbeat)));
  }));
  channel.next_update_fault.present = true;
  channel.next_update_fault.commit_first = false;
  channel.next_update_fault.error_code = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  global_now_offset_guard time(std::chrono::seconds{6});
  env.drive_timer_ticks();
  CASE_EXPECT_FALSE(channel.next_update_fault.present);
  CASE_EXPECT_EQ(saved, channel.custom_data().SerializeAsString());
  CASE_EXPECT_TRUE(room->is_lock_holder());
  global_now_offset_guard::advance(std::chrono::seconds{1});
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(0, env.sync(team_id));
  atfw::team::DTeamStorage snapshot;
  CASE_EXPECT_TRUE(channel.custom_data().UnpackTo(&snapshot));
  CASE_EXPECT_EQ(1, snapshot.member_size());
  if (snapshot.member_size() == 1) {
    CASE_EXPECT_EQ(901, snapshot.member(0).acknowledge_action_sequence());
    CASE_EXPECT_EQ(902u, snapshot.member(0).acknowledge_action_hash_code());
    CASE_EXPECT_EQ(903u, snapshot.member(0).user_router_server_id());
  }
  global_now_offset_guard::advance(std::chrono::seconds{6});
  env.drive_timer_ticks();
  CASE_EXPECT_FALSE(channel.update_requests().back().request.has_custom_data());
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, destroy_failure_retries_and_lock_conflict_stops_retry) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  global_now_offset_guard time;
  for (bool lock_conflict : {false, true}) {
    const int64_t team_id = next_test_team_id();
    team_room::ptr_t room;
    auto owner = make_user_key(1, 49301);
    CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(owner.user_id()), &room));
    if (!room) {
      continue;
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));
    atfw::team::DTeamAction destroy;
    *destroy.mutable_destroy_team() = make_team_key(team_id);
    CASE_EXPECT_EQ(0, send_action(env, room, destroy));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    auto& channel = env.channel(team_id);
    channel.next_destroy_fault.present = true;
    channel.next_destroy_fault.commit_first = false;
    channel.next_destroy_fault.error_code =
        lock_conflict ? static_cast<int32_t>(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED)
                      : static_cast<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL);
    const size_t before = env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_destroy_channel());
    global_now_offset_guard::advance(std::chrono::seconds{1});
    env.drive_timer_ticks();
    CASE_EXPECT_FALSE(channel.next_destroy_fault.present);
    CASE_EXPECT_FALSE(channel.is_destroyed());
    CASE_EXPECT_EQ(before + 1, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_destroy_channel()));
    if (lock_conflict) {
      CASE_EXPECT_FALSE(room->is_lock_holder());
      channel.set_lock(make_foreign_lock("destroy-new-owner", 120));
      CASE_EXPECT_EQ(0, env.sync(team_id));
    }
    global_now_offset_guard::advance(std::chrono::seconds{1});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(before + (lock_conflict ? 1 : 2),
                   env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_destroy_channel()));
    CASE_EXPECT_EQ(!lock_conflict, channel.is_destroyed());
    CASE_EXPECT_EQ(0, env.sync(team_id));
    const size_t completed = env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_destroy_channel());
    global_now_offset_guard::advance(std::chrono::seconds{1});
    env.drive_timer_ticks();
    CASE_EXPECT_EQ(completed, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_destroy_channel()));
    room_test_env::clear_rooms();
  }
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, removed_room_drops_pending_notification_after_maintenance_returns) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  atfw::team::DTeamAction remove;
  *remove.mutable_remove_member()->mutable_user_key() = members.normal;
  CASE_EXPECT_EQ(0, send_action(env, room, remove));
  CASE_EXPECT_EQ(0, env.run("apply_without_flush", [&env, team_id](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(env.push_channel_events(ctx, make_team_key(team_id), false)));
  }));
  CASE_EXPECT_EQ(1u, room->debug_pending_notification_count());
  const size_t personal = env.personal_message_count();
  env.update_response_gate.armed = true;
  global_now_offset_guard time(std::chrono::seconds{6});
  CASE_EXPECT_EQ(0, env.run("start_maintenance", [](rpc::context& ctx) -> rpc::result_code_type {
    team_room_manager::me()->tick(ctx);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_TRUE(env.wait_for([&env]() { return room_test_env::gate_parked(env.update_response_gate); }));
  team_room_manager::me()->remove_room(make_team_key(team_id), room.get());
  auto replacement = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!replacement);
  CASE_EXPECT_NE(room.get(), replacement.get());
  CASE_EXPECT_TRUE(room_test_env::release_gate(env.update_response_gate));
  CASE_EXPECT_TRUE(env.wait_for([room]() { return !room->debug_maintenance_task_running(); }));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(personal, env.personal_message_count());
  CASE_EXPECT_EQ(0u, room->debug_pending_notification_count());
  CASE_EXPECT_FALSE(room->is_lock_holder());
  CASE_EXPECT_EQ(replacement.get(), team_room_manager::me()->get_room(make_team_key(team_id)).get());
  if (replacement) {
    CASE_EXPECT_TRUE(replacement->is_lock_holder());
    CASE_EXPECT_EQ(nullptr, replacement->find_member(members.normal, false).get());
  }
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, legacy_map_wire_decodes_and_restores_team_state) {
  namespace wire = teamsvr_room_legacy_wire;
  atfw::team::DTeamMember member;
  atfw::team::DTeamStorage storage;
  atfw::team::DTeamInvitation invitation;
  atfw::team::DTeamJoinRequest join_request;
  atfw::team::DTeamRoomPrivateData private_data;
  atfw::team::DTeamConditionChecker condition;
  CASE_EXPECT_TRUE(member.ParseFromArray(wire::member, sizeof(wire::member)));
  CASE_EXPECT_TRUE(storage.ParseFromArray(wire::storage, sizeof(wire::storage)));
  CASE_EXPECT_TRUE(invitation.ParseFromArray(wire::invitation, sizeof(wire::invitation)));
  CASE_EXPECT_TRUE(join_request.ParseFromArray(wire::join_request, sizeof(wire::join_request)));
  CASE_EXPECT_TRUE(private_data.ParseFromArray(wire::private_data, sizeof(wire::private_data)));
  CASE_EXPECT_TRUE(condition.ParseFromArray(wire::condition, sizeof(wire::condition)));
  const auto expect_value = [](const google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& entries,
                               int64_t key, const char* expected) {
    CASE_EXPECT_EQ(1, entries.size());
    if (entries.size() != 1) {
      return;
    }
    CASE_EXPECT_EQ(key, entries.Get(0).key());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC, entries.Get(0).value().permission());
    google::protobuf::StringValue value;
    CASE_EXPECT_TRUE(entries.Get(0).value().data().UnpackTo(&value));
    CASE_EXPECT_EQ(expected, value.value());
  };
  expect_value(member.shared_member_data(), 17, "member");
  expect_value(storage.shared_team_data(), 23, "team");
  expect_value(invitation.team_admission_data(), 29, "invitation");
  CASE_EXPECT_EQ(1, invitation.member_admission_data_size());
  if (invitation.member_admission_data_size() == 1) {
    expect_value(invitation.member_admission_data(0).member_admission_data(), 31, "admission");
  }
  expect_value(join_request.member_admission_data(), 37, "join");
  expect_value(private_data.private_team_data(), 41, "private");

  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  *storage.mutable_team_key() = make_team_key(team_id);
  auto& channel = env.channel(team_id);
  channel.ensure_created();
  channel.set_custom_data(storage);
  channel.set_private_data(private_data);
  auto room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (room) {
    auto restored = room->find_member(member.user_key(), false);
    CASE_EXPECT_TRUE(!!restored);
    if (restored) {
      CASE_EXPECT_EQ("legacy", restored->member_data.client_version());
      CASE_EXPECT_EQ(1u, restored->shared_member_data.size());
    }
    atfw::team::DTeamAction update;
    *update.mutable_team_update()->add_condition() = std::move(condition);
    add_team_any_data_entry(update.mutable_team_update()->mutable_shared_team_data(), 47, "condition-accepted");
    CASE_EXPECT_EQ(0, env.run("legacy_condition", [room, member, update](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->check_action_permission(ctx, member.user_key(), update)));
    }));
    CASE_EXPECT_EQ(0, send_action(env, room, update));
    CASE_EXPECT_EQ(0, env.sync(team_id));
    global_now_offset_guard time(std::chrono::seconds{6});
    env.drive_timer_ticks();
    atfw::team::DTeamRoomPrivateData saved_private;
    CASE_EXPECT_TRUE(channel.private_data().UnpackTo(&saved_private));
    expect_value(saved_private.private_team_data(), 41, "private");
    CASE_EXPECT_EQ(0u, env.personal_message_count());
  }
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, large_create_payload_and_maximum_identity_survive_restore) {
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = 1000;
  room_test_env env(cfg);
  if (!start_env(env)) {
    return;
  }
  const auto key = make_team_key(std::numeric_limits<int64_t>::max());
  auto request = create_request(key);
  request.mutable_sender_user_key()->set_user_id(std::numeric_limits<uint64_t>::max());
  request.mutable_sender_user_key()->set_zone_id(std::numeric_limits<uint32_t>::max());
  request.set_client_version("large-create");
  const size_t buffer_capacity =
      atfw::gateway::libatgw_protocol_api::get_tls_buffer(atfw::gateway::libatgw_protocol_api::tls_buffer_t::kCustom)
          .size();
  std::map<int64_t, std::string> expected;
  for (int64_t index = 0; index < 64; ++index) {
    const auto data_key = index == 0 ? std::numeric_limits<int64_t>::min() : index;
    const std::string value =
        std::to_string(index) + std::string((buffer_capacity / 64) + 1, static_cast<char>('a' + (index % 26)));
    expected[data_key] = value;
    add_team_any_data_entry(request.mutable_shared_member_data(), data_key, value);
    add_team_any_data_entry(request.mutable_shared_team_data(), data_key, value);
  }
  // The SS send buffer rejects this oversized initial snapshot before any channel update is sent.
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND,
                 invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(), request));
  auto& channel = env.channel(key);
  CASE_EXPECT_EQ(0u, channel.update_calls());
  CASE_EXPECT_TRUE(channel.custom_data().type_url().empty());
  for (auto& entry : expected) {
    // Two 64-entry fields use half the send buffer, leaving space for keys, Any types and the RPC envelope.
    entry.second.resize(buffer_capacity / 256);
  }
  for (auto& entry : *request.mutable_shared_member_data()) {
    entry.mutable_value()->mutable_data()->set_value(expected.at(entry.key()));
  }
  for (auto& entry : *request.mutable_shared_team_data()) {
    entry.mutable_value()->mutable_data()->set_value(expected.at(entry.key()));
  }
  CASE_EXPECT_EQ(0, invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(), request));
  CASE_EXPECT_EQ(0, env.sync(key));
  atfw::team::DTeamStorage snapshot;
  CASE_EXPECT_TRUE(channel.custom_data().UnpackTo(&snapshot));
  CASE_EXPECT_EQ(1, snapshot.member_size());
  expect_keyed_data(snapshot.shared_team_data(), expected);
  if (snapshot.member_size() == 1) {
    expect_keyed_data(snapshot.member(0).shared_member_data(), expected);
  }
  room_test_env::clear_rooms();
  auto room = env.setup_ready_room(key);
  CASE_EXPECT_TRUE(!!room);
  if (room) {
    auto restored = room->find_member(request.sender_user_key(), false);
    CASE_EXPECT_TRUE(!!restored);
    if (restored) {
      CASE_EXPECT_EQ(expected.size(), restored->shared_member_data.size());
      for (const auto& entry : expected) {
        auto found = restored->shared_member_data.find(entry.first);
        CASE_EXPECT_TRUE(found != restored->shared_member_data.end());
        if (found != restored->shared_member_data.end()) {
          CASE_EXPECT_EQ(entry.second, found->second.data().value());
        }
      }
    }
    CASE_EXPECT_EQ(key.team_id(), room->get_team_key().team_id());
  }
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, seeded_keyed_updates_match_independent_model_across_restarts) {
  for (uint32_t seed : {17u, 901u, 65537u}) {
    room_test_cfg_values cfg;
    cfg.member_offline_expire_seconds = 3600;
    room_test_env env(cfg);
    if (!start_env(env)) {
      return;
    }
    const int64_t team_id = next_test_team_id();
    const auto owner = make_user_key(1, 49601);
    team_room::ptr_t room;
    CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(owner.user_id()), &room));
    if (!room) {
      CASE_EXPECT_EQ(0, env.stop());
      return;
    }
    CASE_EXPECT_EQ(0, env.sync(team_id));
    std::mt19937 random(seed);
    std::map<int64_t, std::string> expected_member;
    std::map<int64_t, std::string> expected_team;
    global_now_offset_guard time;
    for (size_t index = 0; index < 160; ++index) {
      const auto choice = random();
      const bool team_data = (choice & 1u) != 0;
      const bool erase = (choice & 6u) == 0;
      const bool denied = index % 11 == 0;
      const int64_t key = static_cast<int64_t>((choice >> 3u) % 12u) - 6;
      const std::string value = erase ? std::string{} : std::to_string(seed) + ":" + std::to_string(index);
      CASE_MSG_INFO() << "seed=" << seed << " operation=" << index << " key=" << key << " team=" << team_data
                      << " erase=" << erase << " denied=" << denied << '\n';
      atfw::team::SSTeamRoomSendMessageReq request;
      *request.mutable_team_key() = make_team_key(team_id);
      *request.mutable_sender_user_key() = denied ? make_user_key(1, 49699) : owner;
      if (team_data) {
        add_team_any_data_entry(request.mutable_action()->mutable_team_update()->mutable_shared_team_data(), key,
                                value);
      } else {
        auto* update = request.mutable_action()->mutable_member_update();
        *update->mutable_user_key() = owner;
        add_team_any_data_entry(update->mutable_shared_member_data(), key, value);
      }
      const auto before = env.channel(team_id).last_sequence();
      CASE_EXPECT_EQ(
          denied ? PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM : 0,
          invoke_action<task_action_send_message>(env, rpc::team::packer::get_full_name_of_send_message(), request));
      if (denied) {
        CASE_EXPECT_EQ(before, env.channel(team_id).last_sequence());
      } else {
        auto& expected = team_data ? expected_team : expected_member;
        if (erase) {
          expected.erase(key);
        } else {
          expected[key] = value;
        }
      }
      CASE_EXPECT_EQ(0, env.sync(team_id));
      auto member = room->find_member(owner, false);
      CASE_EXPECT_TRUE(!!member);
      if (!member) {
        break;
      }
      CASE_EXPECT_EQ(expected_member.size(), member->shared_member_data.size());
      for (const auto& entry : expected_member) {
        auto found = member->shared_member_data.find(entry.first);
        CASE_EXPECT_TRUE(found != member->shared_member_data.end());
        if (found != member->shared_member_data.end()) {
          CASE_EXPECT_EQ(entry.second, found->second.data().value());
        }
      }
      if (index % 40 == 39) {
        global_now_offset_guard::advance(std::chrono::seconds{6});
        env.drive_timer_ticks();
        CASE_EXPECT_EQ(0, env.sync(team_id));
        auto& channel = env.channel(team_id);
        CASE_EXPECT_TRUE(channel.update_requests().back().request.has_custom_data());
        atfw::team::DTeamStorage snapshot;
        CASE_EXPECT_TRUE(channel.custom_data().UnpackTo(&snapshot));
        expect_keyed_data(snapshot.shared_team_data(), expected_team);
        CASE_EXPECT_EQ(1, snapshot.member_size());
        if (snapshot.member_size() == 1) {
          expect_keyed_data(snapshot.member(0).shared_member_data(), expected_member);
        }
        room_test_env::clear_rooms();
        room.reset();
        room = env.setup_ready_room(team_id);
        CASE_EXPECT_TRUE(!!room);
        if (!room) {
          break;
        }
        CASE_EXPECT_EQ(0, env.sync(team_id, true));
      }
    }
    CASE_EXPECT_EQ(0u, env.personal_message_count());
    room_test_env::clear_rooms();
    CASE_EXPECT_EQ(0, env.stop());
  }
}

CASE_TEST(teamsvr_room_contract, delayed_personal_delivery_is_not_resubmitted_by_flush) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  standard_team_members members;
  CASE_EXPECT_TRUE(setup_standard_team(env, team_id, room, members));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  const size_t personal = env.personal_message_count();
  atfw::team::DTeamAction remove;
  *remove.mutable_remove_member()->mutable_user_key() = members.normal;
  CASE_EXPECT_EQ(0, send_action(env, room, remove));
  env.personal_send_gate.armed = true;
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(env.wait_for([&env]() { return room_test_env::gate_parked(env.personal_send_gate); }));
  CASE_EXPECT_EQ(nullptr, room->find_member(members.normal, false).get());
  CASE_EXPECT_EQ(personal, env.personal_message_count());
  const size_t sends = env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_send_message());
  CASE_EXPECT_EQ(0, flush(env));
  CASE_EXPECT_EQ(0, flush(env));
  CASE_EXPECT_EQ(sends, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_send_message()));
  CASE_EXPECT_EQ(0u, room->debug_pending_notification_count());
  CASE_EXPECT_TRUE(room_test_env::release_gate(env.personal_send_gate));
  CASE_EXPECT_TRUE(env.wait_for([&env, personal]() { return env.personal_message_count() == personal + 1; }));
  CASE_EXPECT_EQ(1u,
                 count_personal_actions(env, members.normal.user_id(), atfw::team::DTeamMemberAction::kRemoveMember));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(personal + 1, env.personal_message_count());
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, forward_transport_failure_and_late_success_never_execute_locally) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  constexpr uint64_t remote = 0x11000013;
  add_remote_room_node(env, remote, 13);
  auto request = create_request(make_team_key(49801, 13));
  atfw::testing::transport_send_behavior failure;
  failure.immediate_error = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  auto transport_rule = env.runtime().transport().add_rule(remote, -1, failure);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL,
                 invoke_action<task_action_create>(env, rpc::team::packer::get_full_name_of_create(), request));
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  transport_rule.reset();

  response_gate_t gate;
  size_t received = 0;
  atfw::testing::ss_rule_options remote_options;
  remote_options.match_node_id = remote;
  remote_options.times = 1;
  auto remote_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_create(), atfw::team::SSTeamRoomCreateReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomCreateRsp::descriptor()->full_name(),
      [&gate, &received](const atfw::testing::ss_request_view& view,
                         google::protobuf::Message& response) -> rpc::result_code_type {
        ++received;
        const auto& input = static_cast<const atfw::team::SSTeamRoomCreateReq&>(view.body);
        auto& output = static_cast<atfw::team::SSTeamRoomCreateRsp&>(response);
        *output.mutable_team_key() = input.team_key();
        output.set_client_result(0);
        CASE_EXPECT_TRUE(view.head.has_rpc_forward());
        CASE_EXPECT_EQ(49802u, view.head.rpc_forward().forward_for_sequence());
        gate.parked_task = view.context->get_task_context().task_id;
        gate.sequence = 1;
        auto options = dispatcher_make_default<dispatcher_await_options>();
        options.sequence = gate.sequence;
        options.timeout = std::chrono::seconds{4};
        RPC_AWAIT_IGNORE_RESULT(rpc::custom_wait(*view.context, &gate.token, options));
        gate.parked_task = 0;
        RPC_RETURN_CODE(0);
      },
      remote_options);
  atfw::testing::ss_action_invoke_options options{rpc::team::packer::get_full_name_of_create()};
  options.source.node_id = kDtmqProxyNodeId;
  options.source.source_task_id = 49803;
  options.source.sequence = 49802;
  auto pending = env.runtime().run_task(
      "late_forward", std::chrono::seconds{8}, [request, options](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(
            RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_create>(ctx, request, options)));
      });
  CASE_EXPECT_TRUE(env.wait_for([&gate]() { return room_test_env::gate_parked(gate); }));
  CASE_EXPECT_EQ(1u, received);
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_TRUE(room_test_env::release_gate(gate));
  const auto result = env.runtime().wait(pending, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_EQ(0u, env.runtime().ss().calls(rpc::dtmq::packer::get_full_name_of_update()));
  CASE_EXPECT_EQ(0u, env.personal_message_count());
  remote_rule.reset();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, admission_deadline_is_exclusive_and_lock_deadline_is_inclusive) {
  room_test_env env;
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  team_room::ptr_t room;
  const auto owner = make_user_key(1, 49901);
  const auto applicant = make_user_key(2, 49902);
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(owner.user_id()), &room));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto& channel = env.channel(team_id);
  for (int offset : {-1, 0, 1}) {
    const auto before = channel.last_sequence();
    CASE_EXPECT_EQ(
        0, env.run("admission_deadline", [room, owner, applicant, offset](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::SSTeamRoomAddInvitationReq invitation;
          *invitation.mutable_sender_user_key() = owner;
          *invitation.mutable_invitation()->mutable_inviter() = owner;
          *invitation.mutable_invitation()->mutable_invitee() = applicant;
          *invitation.mutable_invitation()->mutable_expired_timepoint() =
              protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{offset});
          auto result = RPC_AWAIT_CODE_RESULT(room->add_invitation(ctx, invitation));
          CASE_EXPECT_EQ(0, result);
          atfw::team::SSTeamRoomAddJoinRequestReq join;
          *join.mutable_sender_user_key() = applicant;
          *join.mutable_join_request()->mutable_requester() = applicant;
          *join.mutable_join_request()->mutable_expired_timepoint() =
              protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{offset});
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->add_join_request(ctx, join)));
        }));
    CASE_EXPECT_EQ(before + (offset > 0 ? 2 : 0), channel.last_sequence());
    CASE_EXPECT_EQ(0, env.sync(team_id));
  }
  auto foreign_lock = make_foreign_lock("deadline-owner", 20);
  channel.set_lock(foreign_lock);
  CASE_EXPECT_EQ(0, env.sync(team_id));
  const size_t resets = channel.reset_lock_calls();
  global_now_offset_guard time;
  CASE_EXPECT_EQ(0, env.run("lock_exact_deadline", [room, foreign_lock](rpc::context& ctx) -> rpc::result_code_type {
    const auto deadline = protobuf_to_system_clock(foreign_lock.timeout());
    global_now_offset_guard::advance(deadline - atfw::util::time::time_utility::now());
    atfw::team::DTeamAction action;
    add_team_any_data_entry(action.mutable_team_update()->mutable_shared_team_data(), 1, "after-lock-expiry");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED,
                   RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
    global_now_offset_guard::advance(std::chrono::seconds{1});
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, action)));
  }));
  CASE_EXPECT_EQ(resets + 1, channel.reset_lock_calls());
  CASE_EXPECT_TRUE(room->is_lock_holder());
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, offline_and_empty_room_actions_start_at_their_deadlines) {
  room_test_cfg_values cfg;
  cfg.lock_lease_seconds = 120;
  cfg.compact_log_start_seconds = 600;
  cfg.compact_log_keep_count = 1000;
  room_test_env env(cfg);
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  const auto owner = make_user_key(1, 49911);
  team_room::ptr_t room;
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(owner.user_id()), &room));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  auto& channel = env.channel(team_id);
  const auto offline = room->get_next_timer_event(atfw::util::time::time_utility::now());
  CASE_EXPECT_EQ(static_cast<int>(team_room_timer_event_type::kKickOfflineMember), static_cast<int>(offline.type));
  const auto before = channel.send_message_calls();
  global_now_offset_guard time;
  CASE_EXPECT_EQ(0, env.run("before_offline_deadline", [room, offline](rpc::context& ctx) -> rpc::result_code_type {
    global_now_offset_guard::advance(offline.timeout - std::chrono::system_clock::duration(1) -
                                     atfw::util::time::time_utility::now());
    room->on_timer(ctx);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_EQ(before, channel.send_message_calls());
  CASE_EXPECT_EQ(0, env.run("at_offline_deadline", [room, offline](rpc::context& ctx) -> rpc::result_code_type {
    global_now_offset_guard::advance(offline.timeout - atfw::util::time::time_utility::now());
    room->on_timer(ctx);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_TRUE(env.wait_for([room]() { return !room->debug_maintenance_task_running(); }));
  CASE_EXPECT_TRUE(env.wait_for([&channel, before]() { return channel.send_message_calls() == before + 1; }));
  CASE_EXPECT_EQ(before + 1, channel.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(nullptr, room->find_member(owner, false).get());
  const auto empty = room->get_next_timer_event(atfw::util::time::time_utility::now());
  CASE_EXPECT_EQ(static_cast<int>(team_room_timer_event_type::kDestroyEmptyRoom), static_cast<int>(empty.type));
  const auto after_remove = channel.send_message_calls();
  CASE_EXPECT_EQ(0, env.run("before_empty_deadline", [room, empty](rpc::context& ctx) -> rpc::result_code_type {
    global_now_offset_guard::advance(empty.timeout - std::chrono::system_clock::duration(1) -
                                     atfw::util::time::time_utility::now());
    room->on_timer(ctx);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_EQ(after_remove, channel.send_message_calls());
  CASE_EXPECT_EQ(0, env.run("at_empty_deadline", [room, empty](rpc::context& ctx) -> rpc::result_code_type {
    global_now_offset_guard::advance(empty.timeout - atfw::util::time::time_utility::now());
    room->on_timer(ctx);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_TRUE(env.wait_for([room]() { return !room->debug_maintenance_task_running(); }));
  CASE_EXPECT_TRUE(
      env.wait_for([&channel, after_remove]() { return channel.send_message_calls() == after_remove + 1; }));
  CASE_EXPECT_EQ(after_remove + 1, channel.send_message_calls());
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED,
                 env.run("heartbeat_after_destroy", [room, owner](rpc::context& ctx) -> rpc::result_code_type {
                   atfw::team::SSTeamRoomHeartbeatReq heartbeat;
                   *heartbeat.mutable_user_key() = owner;
                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->heartbeat(ctx, heartbeat)));
                 }));
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

CASE_TEST(teamsvr_room_contract, extreme_config_retains_state_and_schedules_future_maintenance) {
  room_test_cfg_values cfg;
  cfg.compact_log_keep_count = std::numeric_limits<uint32_t>::max();
  cfg.channel_gc_log_count = std::numeric_limits<uint32_t>::max();
  cfg.compact_log_keep_percent = std::numeric_limits<int32_t>::max();
  cfg.compact_log_over_percent = std::numeric_limits<int32_t>::max();
  cfg.compact_log_start_seconds = std::numeric_limits<int32_t>::max();
  cfg.compact_log_keep_seconds = std::numeric_limits<int32_t>::max();
  cfg.member_offline_expire_seconds = std::numeric_limits<int32_t>::max();
  cfg.member_notification_retry_interval_seconds = std::numeric_limits<int32_t>::max();
  cfg.member_notification_retry_times = std::numeric_limits<uint32_t>::max();
  cfg.invitation_expire_seconds = std::numeric_limits<int32_t>::max();
  cfg.join_request_expire_seconds = std::numeric_limits<int32_t>::max();
  cfg.empty_room_destroy_delay_seconds = std::numeric_limits<int32_t>::max();
  room_test_env env(cfg);
  if (!start_env(env)) {
    return;
  }
  const int64_t team_id = next_test_team_id();
  const auto owner = make_user_key(1, 49921);
  team_room::ptr_t room;
  auto configure = make_standard_team_configure();
  configure.set_max_member_count(std::numeric_limits<uint32_t>::max());
  configure.set_max_invitation_count(std::numeric_limits<uint32_t>::max());
  configure.set_max_join_request_count(std::numeric_limits<uint32_t>::max());
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner, make_personal_channel(owner.user_id()), &room, &configure));
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));
  atfw::team::DTeamAction update;
  *update.mutable_member_update()->mutable_user_key() = owner;
  update.mutable_member_update()->set_client_version("retained-at-maximum-config");
  CASE_EXPECT_EQ(0, send_action(env, room, update));
  CASE_EXPECT_EQ(0, env.sync(team_id));
  const auto event = room->get_next_timer_event(atfw::util::time::time_utility::now());
  CASE_EXPECT_EQ(static_cast<int>(team_room_timer_event_type::kMaintenance), static_cast<int>(event.type));
  CASE_EXPECT_GT(event.timeout, atfw::util::time::time_utility::now());
  global_now_offset_guard time(std::chrono::seconds{6});
  env.drive_timer_ticks();
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_EQ(0, room->debug_last_compact_sequence());
  CASE_EXPECT_TRUE(!!room->find_member(owner, false));
  CASE_EXPECT_GT(room->get_next_timer_event(atfw::util::time::time_utility::now()).timeout,
                 atfw::util::time::time_utility::now());
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

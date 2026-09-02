// Copyright 2026 atframework
//
// Shared offline fixture for lobbysvr user_team / user_team_manager / team task-action unit tests
// (plan: src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md).
//
// Building blocks:
//   - channel event injection: make_snapshot_event / make_incremental_event /
//     make_channel_destroyed_event + channel_event_chain (valid dtmq hash chains), delivered through the real
//     rpc::dtmq::client_subscriber::global_receive_channel_event entry point;
//   - user/session setup: setup_team_user (chat login_init + team login_init + private channel ready),
//     bind_client_session (mock CS client bound to the user so dirty pushes are captured);
//   - team_room_ss_capture: typed mock of atframework.team.TeamRoomService, recording full request payloads and
//     answering with configurable business results (one-way calls stay record-only, per engine default);
//   - client projection collection: collect_team_dirty flattens SCUserDirtyChgSync pushes into snapshots and
//     increase actions keyed by team_key so cases assert payload sets, not push counts;
//   - pump_until(predicate, hard_limit): pump generations until an observable condition holds; the hard limit only
//     guards against hangs. Fixed pump_rounds is kept only for settling work that has no observable condition.
//   - now_offset_guard: monotonic virtual-time driver (the subscriber timer wheel is shared process-wide, so the
//     global now offset must never go backwards across cases; teardown restores the previous offset).
//
// Scenario inputs and key expectations stay in the case files; this header only carries mechanics.

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/any.h>

#include <protocol/config/lobbysvr_config.pb.h>
#include <protocol/pbdesc/com.struct.chat.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/com.struct.team.shared.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_cs.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <config/logic_config.h>

#include <rpc/internal/rpc_template_cs_message.h>
#include <rpc/rpc_context.h>
#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

#include <atframe/atapp.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "atframe/atapp_conf.h"  // IWYU pragma: keep
#include "data/session.h"
#include "data/user.h"
#include "frame/test_macros.h"
#include "logic/chat/user_chat_manager.h"
#include "logic/logic_server_setup.h"
#include "logic/session_manager.h"
#include "logic/team/user_team_algorithm.h"
#include "logic/team/user_team_manager.h"
#include "rpc/dtmq/dtmq_client_subscriber.h"
#include "rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h"
#include "rpc/team/team_common_api.h"
#include "rpc/team/teamroomservice.atfw.gen.h"

namespace team_test {

// Register the lobbysvr server-instance config loader (same shape as lobbysvr_main.cpp) so tests observe the
// production annotation defaults (team.wait_add_member_timeout=5s, exit_retry_interval=10s, exit_timeout=35s,
// heartbeat_interval=120s) instead of the in-code fallbacks used when no loader is installed. Must be called in
// setup_callback (before app init). Captureless lambda converts to the function pointer; registration is
// process-global and idempotent, and no case in this executable depends on the no-loader fallbacks.
inline void register_lobbysvr_config_loader() {
  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app& app_, logic_config& /*cfg*/, logic_config::server_instance_config_ptr& to) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
        app_.parse_configures_into(*config_ptr, "lobbysvr", "ATAPP_LOBBYSVR");
        to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });
}

// Start the fixture runtime with the lobbysvr config loader installed. Team cases use {ss, cs}.
inline bool start_team_runtime(atfw::testing::runtime& test,
                               std::vector<atfw::testing::feature> features = {atfw::testing::feature::ss,
                                                                               atfw::testing::feature::cs}) {
  atfw::testing::runtime_options options;
  options.features = std::move(features);
  options.setup_callback = [](atfw::testing::runtime&) -> int {
    register_lobbysvr_config_loader();
    return 0;
  };
  CASE_EXPECT_EQ(0, test.start(options));
  return test.is_running();
}

// ---- Well-known ids ---------------------------------------------------------------
inline constexpr uint64_t kDtmqProxyNodeId = 0x1C0001;
inline constexpr uint32_t kZoneId = 1;
inline constexpr uint64_t kCaptainUserId = 90001;
inline constexpr uint64_t kGatewayNodeId = 0x82000001;
inline constexpr uint64_t kTeamRoomNodeId = 0x12000001;

// ---- Key builders -----------------------------------------------------------------
inline atfw::team::DTeamKey make_team_key(int64_t team_id, uint32_t zone_id = kZoneId) {
  atfw::team::DTeamKey team_key;
  team_key.set_zone_id(zone_id);
  team_key.set_team_id(team_id);
  return team_key;
}

inline PROJECT_NAMESPACE_ID::DUserIDKey make_user_key(uint64_t user_id, uint32_t zone_id = kZoneId) {
  PROJECT_NAMESPACE_ID::DUserIDKey user_key;
  user_key.set_zone_id(zone_id);
  user_key.set_user_id(user_id);
  return user_key;
}

inline atfw::dtmq::DChannelIdKey make_private_channel_key(uint64_t user_id) {
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE));
  channel_key.set_channel_id(rpc::dtmq::make_unicast_channel_id(channel_key.channel_type(), kZoneId, user_id));
  return channel_key;
}

inline atfw::dtmq::DChannelIdKey make_team_channel_key(int64_t team_id) {
  return rpc::team::team_api::make_team_room_channel_key(make_team_key(team_id));
}

inline std::string make_user_subscriber_key(uint64_t user_id) {
  return "user:" + std::to_string(kZoneId) + ":" + std::to_string(user_id);
}

// Events from the dtmq proxy are addressed to the shared subscriber key ("server:<name>"), not to the per-user
// subscriber keys (see shared_subscriber's subscriber_info_ and the chat manager tests).
inline std::string get_shared_subscriber_key() {
  return std::string{"server:"} + std::string{logic_config::me()->get_local_server_name()};
}

// ---- Discovery ----------------------------------------------------------------------
// Inject a ready teamsvr-room node so consistent-hash routing (rpc::team::team_api::*) resolves and outbound
// SS calls actually reach the mock engine; without a ready node the calls fail locally with no outbound record.
inline bool setup_team_room_node(atfw::testing::runtime& test, uint64_t node_id = kTeamRoomNodeId) {
  atfw::testing::mock_node node;
  node.set_id(node_id)
      .set_name("unit-test-teamsvr-room")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kTeamRoomSvr))
      .set_type_name("teamsvr-room")
      .set_zone_id(kZoneId)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  if (!remote) {
    CASE_MSG_INFO() << "add teamsvr-room node failed\n";
    return false;
  }
  // Mock injection writes the global discovery set directly; the common-module discovery index only replays
  // existing nodes on reload.
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
  return true;
}

// ---- Shared-data module helpers -----------------------------------------------------
inline PROJECT_NAMESPACE_ID::DTeamSharedDataModule make_team_matching_module(bool matching) {
  PROJECT_NAMESPACE_ID::DTeamSharedDataModule module;
  module.mutable_battle()->set_matching(matching);
  return module;
}

inline PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule make_member_ready_module(bool ready) {
  PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule module;
  module.mutable_battle()->set_ready(ready);
  return module;
}

// Pack a module into a keyed Any entry (permission fixed to MEMBER, mirroring the production pack helpers).
inline atfw::team::DTeamAnyDataWithKey pack_team_module(const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& module) {
  atfw::team::DTeamAnyDataWithKey entry;
  entry.set_key(user_team_algorithm::make_team_shared_data_key(module));
  entry.mutable_value()->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
  CASE_EXPECT_TRUE(entry.mutable_value()->mutable_data()->PackFrom(module));
  return entry;
}

inline atfw::team::DTeamAnyDataWithKey pack_member_module(
    const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& module) {
  atfw::team::DTeamAnyDataWithKey entry;
  entry.set_key(user_team_algorithm::make_team_member_shared_data_key(module));
  entry.mutable_value()->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
  CASE_EXPECT_TRUE(entry.mutable_value()->mutable_data()->PackFrom(module));
  return entry;
}

// ---- DTeamStorage / join-data builders ----------------------------------------------
inline atfw::team::DTeamMemberJoinData make_join_data(uint64_t user_id, int64_t team_id) {
  atfw::team::DTeamMemberJoinData join_data;
  protobuf_copy_message(*join_data.mutable_team_key(), make_team_key(team_id));
  join_data.set_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
  protobuf_copy_message(*join_data.mutable_user_key(), make_user_key(user_id));
  protobuf_copy_message(*join_data.mutable_team_channel(), make_team_channel_key(team_id));
  join_data.set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  protobuf_copy_message(*join_data.mutable_captain_user_key(), make_user_key(kCaptainUserId));
  return join_data;
}

// Optional member decorations for rich snapshots (internal routing fields stay unset unless the scenario
// explicitly pollutes them to prove projection trimming).
struct storage_member_options {
  atfw::team::EnTeamPermissionRole role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
  std::chrono::system_clock::time_point joined_timepoint = std::chrono::system_clock::from_time_t(0);
  std::string client_version;
  atfw::team::EnTeamSourceType team_source_type = atfw::team::EN_TEAM_SOURCE_TYPE_NONE;
  // Packed shared_member_data entries (use pack_member_module / a foreign-typed entry for negative cases).
  std::vector<atfw::team::DTeamAnyDataWithKey> shared_member_data;
  bool pollute_internal_fields = false;

  storage_member_options& set_joined_timepoint(std::chrono::system_clock::time_point value) {
    joined_timepoint = value;
    return *this;
  }

  storage_member_options& set_client_version(gsl::string_view value) {
    client_version = std::string(value);
    return *this;
  }

  storage_member_options& set_team_source_type(atfw::team::EnTeamSourceType value) {
    team_source_type = value;
    return *this;
  }

  storage_member_options& set_shared_member_data(std::vector<atfw::team::DTeamAnyDataWithKey> value) {
    shared_member_data = std::move(value);
    return *this;
  }

  storage_member_options& set_pollute_internal_fields(bool value) {
    pollute_internal_fields = value;
    return *this;
  }
};

// 按角色构造成员装饰项(其余字段保持默认值，需要时用链式 set_* 追加)
inline storage_member_options role_options(atfw::team::EnTeamPermissionRole role) {
  storage_member_options options;
  options.role = role;
  return options;
}

inline atfw::team::DTeamMember* add_storage_member(atfw::team::DTeamStorage& storage, uint64_t user_id,
                                                   const storage_member_options& options = {}) {
  auto* member = storage.add_member();
  protobuf_copy_message(*member->mutable_user_key(), make_user_key(user_id));
  member->set_role(options.role);
  if (options.joined_timepoint > std::chrono::system_clock::from_time_t(0)) {
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(options.joined_timepoint);
  }
  if (!options.client_version.empty()) {
    member->set_client_version(options.client_version);
  }
  member->set_team_source_type(options.team_source_type);
  for (const auto& data : options.shared_member_data) {
    protobuf_copy_message(*member->add_shared_member_data(), data);
  }
  if (options.pollute_internal_fields) {
    member->mutable_user_channel()->set_channel_id("polluted-member-channel-" + std::to_string(user_id));
    member->set_user_router_server_id(0x4000 + static_cast<uint32_t>(user_id % 0x1000));
    member->set_acknowledge_action_sequence(41);
    member->set_acknowledge_action_hash_code(0xDEAD);
  }
  return member;
}

inline atfw::team::DTeamInvitation* add_storage_invitation(
    atfw::team::DTeamStorage& storage, uint64_t invitee_id, std::chrono::system_clock::time_point expired_timepoint,
    bool pollute_internal_fields = false) {
  auto* invitation = storage.add_pending_invitation();
  protobuf_copy_message(*invitation->mutable_team_key(), storage.team_key());
  protobuf_copy_message(*invitation->mutable_inviter(), make_user_key(kCaptainUserId));
  protobuf_copy_message(*invitation->mutable_invitee(), make_user_key(invitee_id));
  *invitation->mutable_start_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  *invitation->mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  if (pollute_internal_fields) {
    invitation->mutable_invitee_private_channel()->set_channel_id("polluted-invitee-channel-" +
                                                                  std::to_string(invitee_id));
  }
  return invitation;
}

inline atfw::team::DTeamJoinRequest* add_storage_join_request(
    atfw::team::DTeamStorage& storage, uint64_t requester_id, std::chrono::system_clock::time_point expired_timepoint,
    bool pollute_internal_fields = false) {
  auto* join_request = storage.add_pending_join_request();
  protobuf_copy_message(*join_request->mutable_team_key(), storage.team_key());
  protobuf_copy_message(*join_request->mutable_requester(), make_user_key(requester_id));
  *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  if (pollute_internal_fields) {
    join_request->mutable_requester_private_channel()->set_channel_id("polluted-requester-channel-" +
                                                                      std::to_string(requester_id));
    join_request->set_user_router_server_id(0x5000 + static_cast<uint32_t>(requester_id % 0x1000));
  }
  return join_request;
}

// Base team storage: team_key + captain(OWNER) + saved_action_sequence. Members/pending/shared data are added
// explicitly per scenario.
inline atfw::team::DTeamStorage make_team_storage(int64_t team_id, int64_t saved_action_sequence = 0) {
  atfw::team::DTeamStorage storage;
  protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
  protobuf_copy_message(*storage.mutable_captain_user_key(), make_user_key(kCaptainUserId));
  storage.set_saved_action_sequence(saved_action_sequence);
  return storage;
}

// ---- Virtual time -------------------------------------------------------------------
// Timeouts below mirror the production accessors in user_team.cpp. With register_lobbysvr_config_loader()
// installed (start_team_runtime), the configured values are the proto annotation defaults (5s/10s/35s/120s);
// without it the in-code fallbacks apply. Cases must advance logical time relative to these helpers, never a
// hardcoded duration.
// Process-wide floor for the global now offset: client_subscriber's timer wheel is shared across cases in one
// process and never ticks backwards, so a later case must not observe an offset below a previous case's peak.
inline std::chrono::system_clock::duration& mutable_now_offset_floor() {
  static std::chrono::system_clock::duration value = std::chrono::system_clock::duration::zero();
  return value;
}

// RAII virtual-time driver. Construction lifts the offset to the process floor (never backwards), advance()
// moves logical now forward and raises the floor, destruction restores the entry offset (teardown contract:
// no leaked offset). All mutations call time_utility::update() so logical_now observes them immediately.
class now_offset_guard {
 public:
  now_offset_guard() : previous_(atfw::util::time::time_utility::get_global_now_offset()) {
    if (atfw::util::time::time_utility::get_global_now_offset() < mutable_now_offset_floor()) {
      atfw::util::time::time_utility::set_global_now_offset(mutable_now_offset_floor());
    }
    atfw::util::time::time_utility::update();
  }
  explicit now_offset_guard(std::chrono::system_clock::duration advance_by) : now_offset_guard() {
    advance(advance_by);
  }
  ~now_offset_guard() {
    atfw::util::time::time_utility::set_global_now_offset(previous_);
    atfw::util::time::time_utility::update();
  }

  now_offset_guard(const now_offset_guard&) = delete;
  now_offset_guard& operator=(const now_offset_guard&) = delete;

  static void advance(std::chrono::system_clock::duration by) {
    auto next = atfw::util::time::time_utility::get_global_now_offset() + by;
    atfw::util::time::time_utility::set_global_now_offset(next);
    if (next > mutable_now_offset_floor()) {
      mutable_now_offset_floor() = next;
    }
    atfw::util::time::time_utility::update();
  }

  static std::chrono::system_clock::time_point logical_now() {
    return std::chrono::system_clock::now() + atfw::util::time::time_utility::get_global_now_offset();
  }

 private:
  std::chrono::system_clock::duration previous_;
};

// The configured wait_add_member_timeout (proto default 5s; do not hardcode the legacy 30s code fallback).
inline std::chrono::system_clock::duration get_wait_add_member_timeout() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().wait_add_member_timeout().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().wait_add_member_timeout());
  }
  return std::chrono::seconds(30);
}

inline std::chrono::system_clock::duration get_exit_retry_interval() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().exit_retry_interval().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().exit_retry_interval());
  }
  return std::chrono::seconds(5);
}

inline std::chrono::system_clock::duration get_exit_timeout() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().exit_timeout().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().exit_timeout());
  }
  return std::chrono::seconds(30);
}

inline std::chrono::system_clock::duration get_heartbeat_interval() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().heartbeat_interval().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().heartbeat_interval());
  }
  return std::chrono::seconds(120);
}

// ---- Runtime drivers ------------------------------------------------------------------
inline bool run_sync_task(atfw::testing::runtime& test, const char* name,
                          std::function<rpc::result_code_type(rpc::context&)> fn) {
  auto task = test.run_task(name, std::chrono::seconds{2}, std::move(fn));
  if (task.empty()) {
    CASE_MSG_INFO() << name << " start failed: " << task.get_diagnostic() << '\n';
    return false;
  }
  auto result = test.wait(task, std::chrono::seconds{5});
  if (!result.task_exited || 0 != result.result_code) {
    CASE_MSG_INFO() << name << " failed, exited=" << result.task_exited << " code=" << result.result_code << '\n';
    return false;
  }
  return true;
}

// Pump until an observable condition holds (cache state, captured RPC, received dirty push). The hard limit only
// prevents a hung case; reaching it without the condition is a test failure (CASE_EXPECT_TRUE at the callsite).
inline bool pump_until(atfw::testing::runtime& test, const std::function<bool()>& pred, int max_pumps = 64) {
  for (int i = 0; i < max_pumps && !pred(); ++i) {
    test.pump_once();
  }
  return pred();
}

// Settling helper for work with no observable condition (e.g. letting fire-and-forget child tasks finish before
// teardown). Never use a fixed count as business evidence; prefer pump_until with a real predicate.
inline void pump_rounds(atfw::testing::runtime& test, int count) {
  for (int i = 0; i < count; ++i) {
    test.pump_once();
  }
}

inline bool receive_channel_event(atfw::testing::runtime& test,
                                  const atframework::dtmq::SSChannelEventSync& event_sync) {
  return run_sync_task(test, "team.receive_event", [&event_sync](rpc::context& ctx) -> rpc::result_code_type {
    int32_t res = RPC_AWAIT_CODE_RESULT(
        rpc::dtmq::client_subscriber::global_receive_channel_event(ctx, kDtmqProxyNodeId, event_sync));
    RPC_RETURN_CODE(res);
  });
}

// ---- Channel event builders -------------------------------------------------------------
// Snapshot event: makes the subscriber ready and installs the channel custom data (the team storage).
// custom_data_sequence defaults to 1 because the subscriber ignores custom data whose sequence is not greater
// than the current one (0 initially).
inline atframework::dtmq::SSChannelEventSync make_snapshot_event(const atframework::dtmq::DChannelIdKey& channel_key,
                                                                 int64_t create_sequence, int64_t last_sequence,
                                                                 const ::google::protobuf::Message* custom_data,
                                                                 int64_t custom_data_sequence = 1,
                                                                 uint64_t last_hash_code = 0) {
  atframework::dtmq::SSChannelEventSync event_sync;
  auto* metadata = event_sync.mutable_channel_snapshot()->mutable_channel_metadata();
  metadata->mutable_channel_key()->CopyFrom(channel_key);
  metadata->set_create_sequence(create_sequence);
  *metadata->mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  metadata->set_last_sequence(last_sequence);
  metadata->set_last_hash_code(last_hash_code);
  if (nullptr != custom_data) {
    metadata->set_custom_data_sequence(custom_data_sequence);
    CASE_EXPECT_TRUE(metadata->mutable_custom_data()->PackFrom(*custom_data));
  }
  event_sync.add_subscriber_keys(get_shared_subscriber_key());
  return event_sync;
}

inline atframework::dtmq::DChannelMessage make_event_message(int64_t sequence,
                                                             const ::google::protobuf::Message& event) {
  atframework::dtmq::DChannelMessage msg;
  msg.set_sequence(sequence);
  CASE_EXPECT_TRUE(msg.mutable_detail()->mutable_event()->PackFrom(event));
  *msg.mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  return msg;
}

// Channel-destroy log message (subscriber treats it as the authoritative destruction of the channel).
inline atframework::dtmq::DChannelMessage make_destroy_log_message(int64_t sequence) {
  atframework::dtmq::DChannelMessage msg;
  msg.set_sequence(sequence);
  *msg.mutable_detail()->mutable_destroy()->mutable_removed_timepoint() =
      protobuf_from_system_clock(std::chrono::system_clock::now());
  *msg.mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  return msg;
}

// custom-data refresh log message (subscriber reloads the latest custom data afterwards).
inline atframework::dtmq::DChannelMessage make_update_custom_data_log_message(int64_t sequence) {
  atframework::dtmq::DChannelMessage msg;
  msg.set_sequence(sequence);
  msg.mutable_detail()->set_update_custom_data(true);
  *msg.mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  return msg;
}

// Build a message list with a valid hash chain (the WAL validates incremental messages via
// rpc::dtmq::calculate_hash_code(previous_hash, msg)). Returns the last message's hash code.
inline uint64_t chain_message_hashes(std::vector<atframework::dtmq::DChannelMessage>& msgs, uint64_t previous_hash) {
  for (auto& msg : msgs) {
    uint64_t hash = rpc::dtmq::calculate_hash_code(previous_hash, msg);
    msg.set_hash_code(hash);
    previous_hash = hash;
  }
  return msgs.empty() ? previous_hash : msgs.back().hash_code();
}

inline atframework::dtmq::SSChannelEventSync make_incremental_event(
    const atframework::dtmq::DChannelIdKey& channel_key, int64_t last_sequence,
    const std::vector<atframework::dtmq::DChannelMessage>& msgs) {
  atframework::dtmq::SSChannelEventSync event_sync;
  auto* metadata = event_sync.mutable_channel_metadata();
  metadata->mutable_channel_key()->CopyFrom(channel_key);
  metadata->set_last_sequence(last_sequence);
  if (!msgs.empty()) {
    metadata->set_last_hash_code(msgs.back().hash_code());
  }
  for (const auto& msg : msgs) {
    *event_sync.add_channel_message() = msg;
  }
  event_sync.add_subscriber_keys(get_shared_subscriber_key());
  return event_sync;
}

// Metadata-only event marking the channel destroyed (destroy_sequence >= create_sequence triggers
// set_destroyed -> on_destroyed on the subscriber, without any WAL destroy log).
inline atframework::dtmq::SSChannelEventSync make_channel_destroyed_event(
    const atframework::dtmq::DChannelIdKey& channel_key, int64_t create_sequence, int64_t destroy_sequence,
    int64_t last_sequence, uint64_t last_hash_code = 0) {
  atframework::dtmq::SSChannelEventSync event_sync;
  auto* metadata = event_sync.mutable_channel_metadata();
  metadata->mutable_channel_key()->CopyFrom(channel_key);
  metadata->set_create_sequence(create_sequence);
  metadata->set_destroy_sequence(destroy_sequence);
  *metadata->mutable_destroy_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  metadata->set_last_sequence(last_sequence);
  metadata->set_last_hash_code(last_hash_code);
  event_sync.add_subscriber_keys(get_shared_subscriber_key());
  return event_sync;
}

// Per-channel monotonic event chain state: sequence + WAL hash, so consecutive injections on the same channel
// always form a valid chain. One chain per logical channel (private channel, each team channel).
struct channel_event_chain {
  atfw::dtmq::DChannelIdKey channel_key;
  int64_t sequence = 0;
  uint64_t hash_code = 0;
};

// Inject one event-carrying message (DTeamAction / DTeamMemberAction / any proto event) on the chain's channel.
inline bool inject_event_message(atfw::testing::runtime& test, channel_event_chain& chain,
                                 const ::google::protobuf::Message& event) {
  std::vector<atframework::dtmq::DChannelMessage> msgs{make_event_message(++chain.sequence, event)};
  chain.hash_code = chain_message_hashes(msgs, chain.hash_code);
  return receive_channel_event(test, make_incremental_event(chain.channel_key, chain.sequence, msgs));
}

// Inject a batch of event messages in one sync (same chain semantics).
inline bool inject_event_messages(atfw::testing::runtime& test, channel_event_chain& chain,
                                  const std::vector<const ::google::protobuf::Message*>& events) {
  std::vector<atframework::dtmq::DChannelMessage> msgs;
  msgs.reserve(events.size());
  for (const auto* event : events) {
    msgs.push_back(make_event_message(++chain.sequence, *event));
  }
  chain.hash_code = chain_message_hashes(msgs, chain.hash_code);
  return receive_channel_event(test, make_incremental_event(chain.channel_key, chain.sequence, msgs));
}

// Inject one non-event log message (destroy / update_custom_data) on the chain's channel.
inline bool inject_log_message(atfw::testing::runtime& test, channel_event_chain& chain,
                               const atframework::dtmq::DChannelMessage& msg_template) {
  std::vector<atframework::dtmq::DChannelMessage> msgs{msg_template};
  msgs.back().set_sequence(++chain.sequence);
  chain.hash_code = chain_message_hashes(msgs, chain.hash_code);
  return receive_channel_event(test, make_incremental_event(chain.channel_key, chain.sequence, msgs));
}

// ---- User / session setup -------------------------------------------------------------
// The join flow as production drives it: chat login_init (creates the private channel and the subscriber key),
// team manager login_init (registers the private channel event dispatcher), then a snapshot pull mirroring the
// CS chat_get_channel_snapshot flow, then the private channel ready snapshot event.
inline bool setup_team_user(atfw::testing::runtime& test, uint64_t user_id, user::ptr_t& out_user,
                            std::string& out_subscriber_key,
                            atframework::dtmq::DChannelIdKey& out_private_channel_key) {
  out_user = user::create(user_id, kZoneId, "team-test-user-" + std::to_string(user_id));
  if (!out_user) {
    return false;
  }

  user::ptr_t user_ptr = out_user;
  bool login_ok = run_sync_task(test, "team.login_init", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
    int32_t chat_res = user_ptr->get_user_chat_manager().login_init(ctx);
    if (0 != chat_res) {
      RPC_RETURN_CODE(chat_res);
    }
    RPC_RETURN_CODE(user_ptr->get_user_team_manager().login_init(ctx));
  });
  if (!login_ok) {
    return false;
  }

  out_subscriber_key = make_user_subscriber_key(user_id);
  out_private_channel_key = make_private_channel_key(user_id);

  // The private channel only installs its event callbacks (which dispatch DTeamMemberAction to the team manager)
  // after the client pulled a snapshot once, mirroring the CS chat_get_channel_snapshot flow.
  bool snapshot_pulled = run_sync_task(
      test, "team.get_snapshot", [&user_ptr, &out_private_channel_key](rpc::context& ctx) -> rpc::result_code_type {
        atframework::chat::DChatChannelData data;
        RPC_RETURN_CODE(
            user_ptr->get_user_chat_manager().get_snapshot(ctx, out_private_channel_key.channel_id(), data));
      });
  if (!snapshot_pulled) {
    return false;
  }

  // The private channel must be ready before incremental events can be applied.
  if (!receive_channel_event(test, make_snapshot_event(out_private_channel_key, 1, 0, nullptr))) {
    return false;
  }
  // Settle fire-and-forget subscription bookkeeping; readiness itself is proven by the first
  // applied incremental event in each scenario (observable-condition pumping happens there).
  pump_rounds(test, 4);
  return true;
}

// Restores a current team the way the login flow does (init_from_table_data -> add_team).
inline bool restore_team_from_table(atfw::testing::runtime& test, const user::ptr_t& user_ptr, int64_t team_id) {
  return run_sync_task(
      test, "team.init_from_table_data", [user_ptr, team_id](rpc::context& ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::table_user table;
        auto* group = table.mutable_team_data()->add_group();
        if (nullptr == group) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        group->set_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
        protobuf_copy_message(*group->mutable_current(), make_join_data(user_ptr->get_user_id(), team_id));
        user_ptr->get_user_team_manager().init_from_table_data(ctx, table);
        RPC_RETURN_CODE(0);
      });
}

// Bind a mock CS client session to the user so dirty pushes and CS responses are captured per session.
inline bool bind_client_session(atfw::testing::runtime& test, const user::ptr_t& user_ptr, uint64_t session_id,
                                atfw::testing::mock_client& out_client) {
  out_client = test.cs().create_client(kGatewayNodeId, session_id);
  if (!out_client || 0 != out_client.add()) {
    CASE_MSG_INFO() << "client add failed\n";
    return false;
  }
  session::key_t session_key;
  session_key.node_id = kGatewayNodeId;
  session_key.session_id = session_id;
  auto sess = session_manager::me()->find(session_key);
  if (!sess) {
    CASE_MSG_INFO() << "session not found\n";
    return false;
  }
  return run_sync_task(test, "team.bind_session", [&user_ptr, &sess](rpc::context& ctx) -> rpc::result_code_type {
    sess->set_user(user_ptr);
    user_ptr->set_session(ctx, sess);
    RPC_RETURN_CODE(0);
  });
}

// Join a team through the real personal-channel joined_team notification (sequence/hash chained on the private
// channel), then pump until the manager registers the team.
inline bool join_team_via_notification(atfw::testing::runtime& test, const user::ptr_t& user_ptr,
                                       channel_event_chain& private_chain, int64_t team_id) {
  atfw::team::DTeamMemberAction action;
  protobuf_copy_message(*action.mutable_joined_team(), make_join_data(user_ptr->get_user_id(), team_id));
  if (!inject_event_message(test, private_chain, action)) {
    return false;
  }
  return pump_until(test, [&user_ptr, team_id] {
    return !!user_ptr->get_user_team_manager().get_team_by_team_key(make_team_key(team_id));
  });
}

// Make the team channel ready with the given storage snapshot, then pump until the subscriber reports ready
// (observable: the team applies the snapshot and is_member_ reflects the member list).
inline bool apply_team_snapshot(atfw::testing::runtime& test, int64_t team_id,
                                const atfw::team::DTeamStorage& storage, int64_t create_sequence = 1) {
  return receive_channel_event(test, make_snapshot_event(make_team_channel_key(team_id), create_sequence,
                                                         storage.saved_action_sequence(), &storage));
}

// ---- Typed SS capture (atframework.team.TeamRoomService) ------------------------------
// Records the full typed request of every call and answers with configurable business results. The default
// create handler mimics teamsvr-room: team_id 0 is replaced by an allocated id and room_channel is derived.
// Set a responder to inject business failures (return value becomes rsp.client_result) or leave the request
// unanswered is not supported here: use ss_rule_options.no_response via raw engine for lost-response scenarios.
struct team_room_ss_capture {
  std::vector<atfw::team::SSTeamRoomCreateReq> create_reqs;
  std::vector<atfw::team::SSTeamRoomSendMessageReq> send_message_reqs;
  std::vector<atfw::team::SSTeamRoomHeartbeatReq> heartbeat_reqs;
  std::vector<atfw::team::SSTeamRoomAddInvitationReq> add_invitation_reqs;
  std::vector<atfw::team::SSTeamRoomApproveInvitationReq> approve_invitation_reqs;
  std::vector<atfw::team::SSTeamRoomRejectInvitationReq> reject_invitation_reqs;
  std::vector<atfw::team::SSTeamRoomAddJoinRequestReq> add_join_request_reqs;
  std::vector<atfw::team::SSTeamRoomApproveJoinRequestReq> approve_join_request_reqs;
  std::vector<atfw::team::SSTeamRoomRejectJoinRequestReq> reject_join_request_reqs;

  int64_t next_allocated_team_id = 700001;

  // Business result hooks (default 0 = success). Return value becomes rsp.client_result.
  std::function<int32_t(const atfw::team::SSTeamRoomCreateReq&, atfw::team::SSTeamRoomCreateRsp&)> create_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomSendMessageReq&, atfw::team::SSTeamRoomSendMessageRsp&)>
      send_message_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomHeartbeatReq&, atfw::team::SSTeamRoomHeartbeatRsp&)>
      heartbeat_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomAddInvitationReq&, atfw::team::SSTeamRoomAddInvitationRsp&)>
      add_invitation_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomApproveInvitationReq&,
                        atfw::team::SSTeamRoomApproveInvitationRsp&)>
      approve_invitation_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomRejectInvitationReq&, atfw::team::SSTeamRoomRejectInvitationRsp&)>
      reject_invitation_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomAddJoinRequestReq&, atfw::team::SSTeamRoomAddJoinRequestRsp&)>
      add_join_request_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomApproveJoinRequestReq&,
                        atfw::team::SSTeamRoomApproveJoinRequestRsp&)>
      approve_join_request_responder;
  std::function<int32_t(const atfw::team::SSTeamRoomRejectJoinRequestReq&,
                        atfw::team::SSTeamRoomRejectJoinRequestRsp&)>
      reject_join_request_responder;

  std::vector<atfw::testing::ss_rule_handle> rules;

  size_t send_message_action_count(atfw::team::DTeamAction::ActionCase action_case) const {
    size_t ret = 0;
    for (const auto& req : send_message_reqs) {
      if (req.action().action_case() == action_case) {
        ++ret;
      }
    }
    return ret;
  }
};
// Count outbound remove_member requests for one team/user pair (e.g. exit-request sends and retries).
inline size_t count_remove_member_requests(const team_room_ss_capture& capture, int64_t team_id, uint64_t user_id) {
  size_t ret = 0;
  for (const auto& req : capture.send_message_reqs) {
    if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == team_id &&
        req.action().remove_member().user_key().user_id() == user_id) {
      ++ret;
    }
  }
  return ret;
}

template <class TReq, class TRsp>
atfw::testing::ss_rule_handle register_team_room_rpc(
    atfw::testing::runtime& test, gsl::string_view full_name, std::vector<TReq>& capture,
    std::function<int32_t(const TReq&, TRsp&)>& responder, std::function<void(TReq&, TRsp&)> default_fill) {
  return test.ss().mock(
      full_name, TReq::descriptor()->full_name(), TRsp::descriptor()->full_name(),
      [&capture, &responder, default_fill = std::move(default_fill)](
          const atfw::testing::ss_request_view& view, google::protobuf::Message& rsp_msg) -> rpc::result_code_type {
        const auto& req = static_cast<const TReq&>(view.body);
        capture.push_back(req);
        auto& rsp = static_cast<TRsp&>(rsp_msg);
        if (responder) {
          rsp.set_client_result(responder(req, rsp));
        } else {
          default_fill(capture.back(), rsp);
          rsp.set_client_result(0);
        }
        RPC_RETURN_CODE(0);
      });
}

// Register typed mock rules for all nine TeamRoomService methods. Keeps the rules alive for the case lifetime.
inline bool setup_team_room_ss_capture(atfw::testing::runtime& test, team_room_ss_capture& capture) {
  capture.rules.push_back(register_team_room_rpc<atfw::team::SSTeamRoomCreateReq, atfw::team::SSTeamRoomCreateRsp>(
      test, rpc::team::packer::get_full_name_of_create(), capture.create_reqs, capture.create_responder,
      [&capture](atfw::team::SSTeamRoomCreateReq& req, atfw::team::SSTeamRoomCreateRsp& rsp) {
        // Mirror teamsvr-room: team_id 0 means server-allocated. The captured request keeps the raw
        // team_id=0 so cases can assert what lobbysvr actually sent; only the response carries the id.
        atfw::team::DTeamKey allocated_key = req.team_key();
        if (allocated_key.team_id() == 0) {
          allocated_key.set_team_id(capture.next_allocated_team_id++);
        }
        protobuf_copy_message(*rsp.mutable_team_key(), allocated_key);
        protobuf_copy_message(*rsp.mutable_room_channel(),
                              rpc::team::team_api::make_team_room_channel_key(allocated_key));
      }));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomSendMessageReq, atfw::team::SSTeamRoomSendMessageRsp>(
          test, rpc::team::packer::get_full_name_of_send_message(), capture.send_message_reqs,
          capture.send_message_responder,
          [](atfw::team::SSTeamRoomSendMessageReq&, atfw::team::SSTeamRoomSendMessageRsp&) {}));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomHeartbeatReq, atfw::team::SSTeamRoomHeartbeatRsp>(
          test, rpc::team::packer::get_full_name_of_heartbeat(), capture.heartbeat_reqs, capture.heartbeat_responder,
          [](atfw::team::SSTeamRoomHeartbeatReq&, atfw::team::SSTeamRoomHeartbeatRsp&) {}));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomAddInvitationReq, atfw::team::SSTeamRoomAddInvitationRsp>(
          test, rpc::team::packer::get_full_name_of_add_invitation(), capture.add_invitation_reqs,
          capture.add_invitation_responder,
          [](atfw::team::SSTeamRoomAddInvitationReq&, atfw::team::SSTeamRoomAddInvitationRsp&) {}));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomApproveInvitationReq, atfw::team::SSTeamRoomApproveInvitationRsp>(
          test, rpc::team::packer::get_full_name_of_approve_invitation(), capture.approve_invitation_reqs,
          capture.approve_invitation_responder,
          [](atfw::team::SSTeamRoomApproveInvitationReq&, atfw::team::SSTeamRoomApproveInvitationRsp&) {}));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomRejectInvitationReq, atfw::team::SSTeamRoomRejectInvitationRsp>(
          test, rpc::team::packer::get_full_name_of_reject_invitation(), capture.reject_invitation_reqs,
          capture.reject_invitation_responder,
          [](atfw::team::SSTeamRoomRejectInvitationReq&, atfw::team::SSTeamRoomRejectInvitationRsp&) {}));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomAddJoinRequestReq, atfw::team::SSTeamRoomAddJoinRequestRsp>(
          test, rpc::team::packer::get_full_name_of_add_join_request(), capture.add_join_request_reqs,
          capture.add_join_request_responder,
          [](atfw::team::SSTeamRoomAddJoinRequestReq&, atfw::team::SSTeamRoomAddJoinRequestRsp&) {}));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomApproveJoinRequestReq,
                             atfw::team::SSTeamRoomApproveJoinRequestRsp>(
          test, rpc::team::packer::get_full_name_of_approve_join_request(), capture.approve_join_request_reqs,
          capture.approve_join_request_responder,
          [](atfw::team::SSTeamRoomApproveJoinRequestReq&, atfw::team::SSTeamRoomApproveJoinRequestRsp&) {}));
  capture.rules.push_back(
      register_team_room_rpc<atfw::team::SSTeamRoomRejectJoinRequestReq, atfw::team::SSTeamRoomRejectJoinRequestRsp>(
          test, rpc::team::packer::get_full_name_of_reject_join_request(), capture.reject_join_request_reqs,
          capture.reject_join_request_responder,
          [](atfw::team::SSTeamRoomRejectJoinRequestReq&, atfw::team::SSTeamRoomRejectJoinRequestRsp&) {}));

  for (const auto& rule : capture.rules) {
    if (!rule) {
      CASE_MSG_INFO() << "team room ss capture rule registration failed: " << test.ss().get_diagnostic() << '\n';
      return false;
    }
  }
  return true;
}

// ---- Client dirty-push collection -------------------------------------------------------
// Collect every downstream user_dirty_chg_sync push addressed to the session.
inline std::vector<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync> collect_dirty_sync_pushes(
    atfw::testing::runtime& test, uint64_t session_id) {
  std::vector<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync> ret;
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
    if (!cs_msg.head().has_rpc_stream() ||
        cs_msg.head().rpc_stream().rpc_name() !=
            rpc::lobbysvrclientservice::packer::get_full_name_of_user_dirty_chg_sync()) {
      continue;
    }
    PROJECT_NAMESPACE_ID::SCUserDirtyChgSync body;
    if (body.ParseFromString(cs_msg.body_bin())) {
      ret.push_back(std::move(body));
    }
  }
  return ret;
}

// Normalized dirty view: pushes may batch several actions and several teams, so cases assert on this flattened
// projection (per team: ordered snapshot/increase payloads) instead of on raw push counts.
struct team_dirty_view {
  std::vector<PROJECT_NAMESPACE_ID::DUserTeamSnapshot> snapshots;  // arrival order
  std::vector<PROJECT_NAMESPACE_ID::DUserTeamDirty::OneAction> actions;  // arrival order across all increases
};

inline team_dirty_view collect_team_dirty(atfw::testing::runtime& test, uint64_t session_id, int64_t team_id) {
  team_dirty_view ret;
  for (const auto& push : collect_dirty_sync_pushes(test, session_id)) {
    for (const auto& dirty_team : push.dirty_team()) {
      if (dirty_team.has_snapshot() && dirty_team.snapshot().snapshot().team_key().team_id() == team_id) {
        ret.snapshots.push_back(dirty_team.snapshot());
      }
      if (dirty_team.has_increase() && dirty_team.increase().team_key().team_id() == team_id) {
        for (const auto& action : dirty_team.increase().actions()) {
          ret.actions.push_back(action);
        }
      }
    }
  }
  return ret;
}

inline size_t count_actions_of_case(const team_dirty_view& view, atfw::team::DTeamAction::ActionCase action_case) {
  size_t ret = 0;
  for (const auto& one_action : view.actions) {
    if (one_action.action().action_case() == action_case) {
      ++ret;
    }
  }
  return ret;
}

// All actions of one case type, in arrival order.
inline std::vector<const PROJECT_NAMESPACE_ID::DUserTeamDirty::OneAction*> find_actions_of_case(
    const team_dirty_view& view, atfw::team::DTeamAction::ActionCase action_case) {
  std::vector<const PROJECT_NAMESPACE_ID::DUserTeamDirty::OneAction*> ret;
  for (const auto& one_action : view.actions) {
    if (one_action.action().action_case() == action_case) {
      ret.push_back(&one_action);
    }
  }
  return ret;
}

// Find a snapshot member by business key (unordered_map output order must not leak into assertions).
inline const atfw::team::DTeamMember* find_snapshot_member(const PROJECT_NAMESPACE_ID::DUserTeamSnapshot& snapshot,
                                                           uint64_t user_id) {
  for (const auto& member : snapshot.snapshot().member()) {
    if (member.user_key().user_id() == user_id) {
      return &member;
    }
  }
  return nullptr;
}

inline const PROJECT_NAMESPACE_ID::DUserTeamSnapshot::UnpackedMemberSharedData* find_unpacked_member(
    const PROJECT_NAMESPACE_ID::DUserTeamSnapshot& snapshot, uint64_t user_id) {
  for (const auto& unpacked : snapshot.unpacked_member_data()) {
    if (unpacked.user_key().user_id() == user_id) {
      return &unpacked;
    }
  }
  return nullptr;
}

// Member projection contract for client-bound data: internal routing/ack fields stripped, raw packed shared data
// never echoed (unpacked module data travels separately).
inline void expect_member_projection_clean(const atfw::team::DTeamMember& member) {
  CASE_EXPECT_TRUE(member.user_channel().channel_id().empty());
  CASE_EXPECT_EQ(0, member.user_router_server_id());
  CASE_EXPECT_EQ(0, member.acknowledge_action_sequence());
  CASE_EXPECT_EQ(0, member.acknowledge_action_hash_code());
  CASE_EXPECT_EQ(0, member.shared_member_data_size());
}

// ---- CS request driver (real dispatcher entry) -------------------------------------------
template <class TRequest>
atframework::CSMsg pack_cs_request(gsl::string_view rpc_full_name, const TRequest& req_body) {
  atframework::CSMsg msg;
  rpc::internal::setup_cs_rpc_request_header(*msg.mutable_head(), "1.0.0.0", "unit-test-client",
                                             "atframework.shared.LobbysvrClientService", rpc_full_name,
                                             TRequest::descriptor()->full_name());
  CASE_EXPECT_TRUE(req_body.SerializeToString(msg.mutable_body_bin()));
  msg.mutable_head()->set_timestamp(atfw::util::time::time_utility::get_now());
  return msg;
}

// Post one CS request and pump until the matching response is recorded downstream.
inline const atfw::testing::cs_downstream_record* find_downstream_response(atfw::testing::runtime& test,
                                                                           uint64_t session_id,
                                                                           gsl::string_view rpc_full_name,
                                                                           atframework::CSMsg& out_msg) {
  const atfw::testing::cs_downstream_record* ret = nullptr;
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
      out_msg = std::move(cs_msg);
      ret = record;
    }
  }
  return ret;
}

// Post a CS request through the real dispatcher and wait for its response; returns false when the response never
// arrives (with CASE diagnostics).
template <class TResponse>
bool post_cs_request(atfw::testing::runtime& test, const atfw::testing::mock_client& client,
                     const atframework::CSMsg& msg, gsl::string_view rpc_full_name, TResponse& out_rsp) {
  if (0 != client.post(msg)) {
    CASE_MSG_INFO() << "post failed for " << rpc_full_name.data() << '\n';
    return false;
  }
  atframework::CSMsg rsp_msg;
  if (!pump_until(test, [&] { return nullptr != find_downstream_response(test, client.session_id(), rpc_full_name,
                                                                         rsp_msg); })) {
    CASE_MSG_INFO() << "no response for " << rpc_full_name.data() << '\n';
    return false;
  }
  // Re-resolve after pump: rsp_msg was filled by the predicate.
  return out_rsp.ParseFromString(rsp_msg.body_bin());
}

}  // namespace team_test

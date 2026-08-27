// Copyright 2026 atframework

// Offline regression tests for lobbysvr user_team_manager:
// - rejoining a team that is still in the pending-exit queue must restore it as the current team (the previous
//   current team moves into the pending-exit queue) instead of leaking it in the exit queue forever;
// - the per-minute cleanup walks team_group_ with a range-for, so a current team that never became a member must
//   be removed only after the iteration completes (remove_team may erase the group being iterated);
// - a member restored from a channel snapshot must never be treated as wait-to-be-member timeout.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/runtime.h>

#include <config/logic_config.h>

#include <rpc/rpc_context.h>
#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "data/user.h"
#include "frame/test_macros.h"
#include "logic/chat/user_chat_manager.h"
#include "logic/team/user_team_manager.h"
#include "rpc/dtmq/dtmq_client_subscriber.h"
#include "rpc/team/team_common_api.h"

namespace {

constexpr uint64_t kDtmqProxyNodeId = 0x1C0001;
constexpr uint32_t kZoneId = 1;
constexpr uint64_t kCaptainUserId = 90001;
constexpr int64_t kFirstTeamId = 101;
constexpr int64_t kSecondTeamId = 102;

atfw::team::DTeamKey make_team_key(int64_t team_id) {
  atfw::team::DTeamKey team_key;
  team_key.set_zone_id(kZoneId);
  team_key.set_team_id(team_id);
  return team_key;
}

atfw::dtmq::DChannelIdKey make_private_channel_key(uint64_t user_id) {
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE));
  channel_key.set_channel_id(rpc::dtmq::make_unicast_channel_id(channel_key.channel_type(), kZoneId, user_id));
  return channel_key;
}

std::string make_user_subscriber_key(uint64_t user_id) {
  return "user:" + std::to_string(kZoneId) + ":" + std::to_string(user_id);
}

// Events from the dtmq proxy are addressed to the shared subscriber key ("server:<name>"), not to the per-user
// subscriber keys (see shared_subscriber's subscriber_info_ and the chat manager tests).
std::string get_shared_subscriber_key() {
  return std::string{"server:"} + std::string{logic_config::me()->get_local_server_name()};
}

atfw::team::DTeamMemberJoinData make_join_data(uint64_t user_id, int64_t team_id) {
  atfw::team::DTeamMemberJoinData join_data;
  protobuf_copy_message(*join_data.mutable_team_key(), make_team_key(team_id));
  join_data.mutable_user_key()->set_zone_id(kZoneId);
  join_data.mutable_user_key()->set_user_id(user_id);
  protobuf_copy_message(*join_data.mutable_team_channel(),
                        rpc::team::team_api::make_team_room_channel_key(make_team_key(team_id)));
  join_data.set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  join_data.mutable_captain_user_key()->set_zone_id(kZoneId);
  join_data.mutable_captain_user_key()->set_user_id(kCaptainUserId);
  return join_data;
}

// Team channel snapshot payload. The member list decides whether the subscribing user is a team member.
atfw::team::DTeamStorage make_team_storage(int64_t team_id, bool with_user, uint64_t user_id) {
  atfw::team::DTeamStorage storage;
  protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
  storage.mutable_captain_user_key()->set_zone_id(kZoneId);
  storage.mutable_captain_user_key()->set_user_id(kCaptainUserId);
  storage.set_saved_action_sequence(0);

  auto* captain = storage.add_member();
  captain->mutable_user_key()->set_zone_id(kZoneId);
  captain->mutable_user_key()->set_user_id(kCaptainUserId);
  captain->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  if (with_user) {
    auto* member = storage.add_member();
    member->mutable_user_key()->set_zone_id(kZoneId);
    member->mutable_user_key()->set_user_id(user_id);
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  }
  return storage;
}

bool run_sync_task(atfw::testing::runtime& test, const char* name,
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

void pump_rounds(atfw::testing::runtime& test, int count) {
  for (int i = 0; i < count; ++i) {
    test.pump_once();
  }
}

bool receive_channel_event(atfw::testing::runtime& test, const atframework::dtmq::SSChannelEventSync& event_sync) {
  return run_sync_task(test, "team.receive_event", [&event_sync](rpc::context& ctx) -> rpc::result_code_type {
    int32_t res = RPC_AWAIT_CODE_RESULT(
        rpc::dtmq::client_subscriber::global_receive_channel_event(ctx, kDtmqProxyNodeId, event_sync));
    RPC_RETURN_CODE(res);
  });
}

// Snapshot event: makes the subscriber ready and installs the channel custom data (the team storage).
atframework::dtmq::SSChannelEventSync make_snapshot_event(const atframework::dtmq::DChannelIdKey& channel_key,
                                                          int64_t create_sequence, int64_t last_sequence,
                                                          const ::google::protobuf::Message* custom_data) {
  atframework::dtmq::SSChannelEventSync event_sync;
  auto* metadata = event_sync.mutable_channel_snapshot()->mutable_channel_metadata();
  metadata->mutable_channel_key()->CopyFrom(channel_key);
  metadata->set_create_sequence(create_sequence);
  *metadata->mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  metadata->set_last_sequence(last_sequence);
  if (nullptr != custom_data) {
    // The subscriber ignores custom data whose sequence is not greater than the current one (0 initially).
    metadata->set_custom_data_sequence(1);
    CASE_EXPECT_TRUE(metadata->mutable_custom_data()->PackFrom(*custom_data));
  }
  event_sync.add_subscriber_keys(get_shared_subscriber_key());
  return event_sync;
}

atframework::dtmq::DChannelMessage make_event_message(int64_t sequence, const ::google::protobuf::Message& event) {
  atframework::dtmq::DChannelMessage msg;
  msg.set_sequence(sequence);
  CASE_EXPECT_TRUE(msg.mutable_detail()->mutable_event()->PackFrom(event));
  *msg.mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  return msg;
}

// Build a message list with a valid hash chain (the WAL validates incremental messages via
// rpc::dtmq::calculate_hash_code(previous_hash, msg)). Returns the last message's hash code.
uint64_t chain_message_hashes(std::vector<atframework::dtmq::DChannelMessage>& msgs, uint64_t previous_hash) {
  for (auto& msg : msgs) {
    uint64_t hash = rpc::dtmq::calculate_hash_code(previous_hash, msg);
    msg.set_hash_code(hash);
    previous_hash = hash;
  }
  return msgs.empty() ? previous_hash : msgs.back().hash_code();
}

atframework::dtmq::SSChannelEventSync make_incremental_event(
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

// The join flow as production drives it: chat login_init (creates the private channel and the subscriber key),
// team manager login_init (registers the private channel event dispatcher), then a DTeamMemberAction kJoinedTeam
// event on the private channel.
bool setup_team_user(atfw::testing::runtime& test, uint64_t user_id, user::ptr_t& out_user,
                     std::string& out_subscriber_key, atframework::dtmq::DChannelIdKey& out_private_channel_key) {
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
  pump_rounds(test, 4);
  return true;
}

// Restores a current team the way the login flow does (init_from_table_data -> add_team).
bool restore_team_from_table(atfw::testing::runtime& test, const user::ptr_t& user_ptr, int64_t team_id) {
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

}  // namespace

// Rejoining a team that is still in the pending-exit queue (fast A->B switch with the exit request in flight, then
// a kJoinedTeam event for A arrives) must restore A as the current team. Leaving A in the exit queue leaks it:
// retry_send_exit_team_request and can_be_removed both early-return once the exit timepoint is reset to zero.
CASE_TEST(lobbysvr_user_team, rejoin_pending_exit_team_restores_current) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30001;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  int64_t event_sequence = 0;
  uint64_t previous_hash = 0;
  auto send_joined_team_event = [&](int64_t team_id) {
    atfw::team::DTeamMemberAction action;
    protobuf_copy_message(*action.mutable_joined_team(), make_join_data(kUserId, team_id));
    std::vector<atframework::dtmq::DChannelMessage> msgs{make_event_message(++event_sequence, action)};
    previous_hash = chain_message_hashes(msgs, previous_hash);
    return receive_channel_event(test, make_incremental_event(private_channel_key, event_sequence, msgs));
  };
  auto current_team_id = [&user_inst]() -> int64_t {
    auto current = user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    return current ? current->get_team_key().team_id() : 0;
  };

  // join A, then switch to B: A moves into the pending-exit queue while its exit request is in flight.
  CASE_EXPECT_TRUE(send_joined_team_event(kFirstTeamId));
  pump_rounds(test, 4);
  CASE_EXPECT_EQ(kFirstTeamId, current_team_id());

  CASE_EXPECT_TRUE(send_joined_team_event(kSecondTeamId));
  pump_rounds(test, 4);
  CASE_EXPECT_EQ(kSecondTeamId, current_team_id());
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kFirstTeamId)));

  // A is rejoined while its exit request is still in flight: A must become current again and B must move into the
  // pending-exit queue, mirroring the plain switch flow.
  CASE_EXPECT_TRUE(send_joined_team_event(kFirstTeamId));
  pump_rounds(test, 4);
  CASE_EXPECT_EQ(kFirstTeamId, current_team_id());

  // A is current again; B is still tracked (it moved into the pending-exit queue) and is exiting.
  auto restored = user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kFirstTeamId));
  CASE_EXPECT_TRUE(!!restored);
  if (restored) {
    CASE_EXPECT_FALSE(restored->is_exiting());
  }
  auto displaced = user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kSecondTeamId));
  CASE_EXPECT_TRUE(!!displaced);
  if (displaced) {
    CASE_EXPECT_TRUE(displaced->is_exiting());
  }

  // Only the current team is persisted, and it must be A again.
  PROJECT_NAMESPACE_ID::table_user dumped_table;
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(0, user_inst->get_user_team_manager().dump(ctx, dumped_table));
  }
  CASE_EXPECT_EQ(1, dumped_table.team_data().group_size());
  if (dumped_table.team_data().group_size() > 0) {
    CASE_EXPECT_TRUE(dumped_table.team_data().group(0).has_current());
    CASE_EXPECT_EQ(kFirstTeamId, dumped_table.team_data().group(0).current().team_key().team_id());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// A current team whose channel reports the user as a member (from the channel snapshot custom data) must survive
// the per-minute cleanup even long after the join, and must not be reported as wait-to-be-member timeout.
CASE_TEST(lobbysvr_user_team, minute_refresh_keeps_member_current_team) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30002;
  constexpr int64_t kTeamId = 201;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  // Team channel snapshot with the user in the member list: the subscriber becomes ready and is_member_ is set.
  auto team_channel_key = rpc::team::team_api::make_team_room_channel_key(make_team_key(kTeamId));
  atfw::team::DTeamStorage team_storage = make_team_storage(kTeamId, true, kUserId);
  CASE_EXPECT_TRUE(receive_channel_event(test, make_snapshot_event(team_channel_key, 1, 0, &team_storage)));
  pump_rounds(test, 4);

  // Fast-forward far past wait_add_member_timeout (30s by default): a member is never reported as timeout.
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{61});
  atfw::util::time::time_utility::update();
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_FALSE(current->wait_to_be_member_but_timeout(ctx));
  }

  user::ptr_t user_ptr = user_inst;
  CASE_EXPECT_TRUE(
      run_sync_task(test, "team.refresh_feature_limit_minute", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
        user_ptr->get_user_team_manager().refresh_feature_limit_minute(ctx);
        RPC_RETURN_CODE(0);
      }));
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();

  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));

  CASE_EXPECT_EQ(0, test.stop());
}

// A current team whose channel never reports the user as a member must be removed by the per-minute cleanup once
// wait_add_member_timeout elapses. remove_team may erase the group from team_group_, which the cleanup is
// iterating with a range-for, so the removal must be deferred until after the iteration completes.
CASE_TEST(lobbysvr_user_team, minute_refresh_removes_never_member_current_team) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30003;
  constexpr int64_t kTeamId = 202;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  // Team channel snapshot without the user in the member list: ready, but never a member.
  auto team_channel_key = rpc::team::team_api::make_team_room_channel_key(make_team_key(kTeamId));
  atfw::team::DTeamStorage team_storage = make_team_storage(kTeamId, false, kUserId);
  CASE_EXPECT_TRUE(receive_channel_event(test, make_snapshot_event(team_channel_key, 1, 0, &team_storage)));
  pump_rounds(test, 4);

  // Fast-forward far past wait_add_member_timeout (30s by default).
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{61});
  atfw::util::time::time_utility::update();
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_TRUE(current->wait_to_be_member_but_timeout(ctx));
  }

  user::ptr_t user_ptr = user_inst;
  CASE_EXPECT_TRUE(
      run_sync_task(test, "team.refresh_feature_limit_minute", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
        user_ptr->get_user_team_manager().refresh_feature_limit_minute(ctx);
        RPC_RETURN_CODE(0);
      }));

  CASE_EXPECT_TRUE(nullptr == user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(nullptr ==
                   user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));

  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  CASE_EXPECT_EQ(0, test.stop());
}

// A member_set_role event on the team channel must refresh the subscribing user's cached permission role (only
// when it targets the user), and a team_update event must refresh the cached configure; both feed the access-layer
// permission pre-checks (task_action_team_update_member_role).
CASE_TEST(lobbysvr_user_team, member_set_role_updates_cached_data) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30004;
  constexpr int64_t kTeamId = 203;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  // Team channel snapshot with the user as a NORMAL member.
  auto team_channel_key = rpc::team::team_api::make_team_room_channel_key(make_team_key(kTeamId));
  atfw::team::DTeamStorage team_storage = make_team_storage(kTeamId, true, kUserId);
  CASE_EXPECT_TRUE(receive_channel_event(test, make_snapshot_event(team_channel_key, 1, 0, &team_storage)));
  pump_rounds(test, 4);
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_cached_permission_role());

  int64_t event_sequence = 0;
  uint64_t previous_hash = 0;
  auto send_team_action_event = [&](const atfw::team::DTeamAction& team_action) {
    std::vector<atframework::dtmq::DChannelMessage> msgs{make_event_message(++event_sequence, team_action)};
    previous_hash = chain_message_hashes(msgs, previous_hash);
    return receive_channel_event(test, make_incremental_event(team_channel_key, event_sequence, msgs));
  };

  // member_set_role targeting the user: cached role is refreshed.
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    set_role->mutable_user_key()->set_zone_id(kZoneId);
    set_role->mutable_user_key()->set_user_id(kUserId);
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
    pump_rounds(test, 4);
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, current->get_cached_permission_role());
  }

  // member_set_role targeting another member: cached role is unchanged.
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    set_role->mutable_user_key()->set_zone_id(kZoneId);
    set_role->mutable_user_key()->set_user_id(kCaptainUserId);
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
    pump_rounds(test, 4);
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, current->get_cached_permission_role());
  }

  // team_update with configure: cached configure is refreshed.
  {
    atfw::team::DTeamAction team_action;
    team_action.mutable_team_update()->mutable_configure()->set_set_member_role_role(
        atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
    pump_rounds(test, 4);
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, current->get_configure().set_member_role_role());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

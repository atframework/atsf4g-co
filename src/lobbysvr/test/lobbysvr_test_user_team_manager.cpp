// Copyright 2026 atframework

// Offline regression tests for lobbysvr user_team_manager:
// - rejoining a team that is still in the pending-exit queue must restore it as the current team (the previous
//   current team moves into the pending-exit queue) instead of leaking it in the exit queue forever;
// - the per-minute cleanup walks team_group_ with a range-for, so a current team that never became a member must
//   be removed only after the iteration completes (remove_team may erase the group being iterated);
// - a member restored from a channel snapshot must never be treated as wait-to-be-member timeout.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/com.struct.team.shared.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_cs.h>
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

#include "data/session.h"
#include "data/user.h"
#include "frame/test_macros.h"
#include "logic/chat/user_chat_manager.h"
#include "logic/session_manager.h"
#include "logic/team/user_team_algorithm.h"
#include "logic/team/user_team_manager.h"
#include "rpc/dtmq/dtmq_client_subscriber.h"
#include "rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h"
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
    // team_type 必须落地: init_from_table_data 依赖它恢复分组，缺失会被 add_team 当作非法类型丢弃
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL, dumped_table.team_data().group(0).team_type());
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

// user_team::dump 导出的快照必须反映频道快照建立的全部缓存状态:
// - 成员列表回填按 key 索引的共享成员数据(打包为 DTeamAnyDataWithKey)，且不下发 user_channel/
//   user_router_server_id/acknowledge_* 等内部路由与确认字段;
// - 待处理邀请/加入请求中已过期的条目不下发;
// - 队伍共享数据以解包后的模块数据写到 DUserTeamSnapshot.shared_team_data，snapshot.shared_team_data 的
//   原始打包数据不导出。
CASE_TEST(lobbysvr_user_team, dump_snapshot_exports_cached_state) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30005;
  constexpr int64_t kTeamId = 204;
  constexpr uint64_t kValidInviteeId = 91001;
  constexpr uint64_t kExpiredInviteeId = 91002;
  constexpr uint64_t kJoinRequesterId = 91003;
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

  auto now = std::chrono::system_clock::now();

  // 队伍共享数据: 战斗模块 matching=true(打包进 DTeamAnyDataWithKey)
  PROJECT_NAMESPACE_ID::DTeamSharedDataModule matching_module;
  matching_module.mutable_battle()->set_matching(true);
  const int64_t matching_key = user_team_algorithm::make_team_shared_data_key(matching_module);

  // 成员共享数据: 队长的 ready=true(打包进 DTeamAnyDataWithKey)
  PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule ready_module;
  ready_module.mutable_battle()->set_ready(true);
  const int64_t ready_key = user_team_algorithm::make_team_member_shared_data_key(ready_module);

  atfw::team::DTeamStorage team_storage = make_team_storage(kTeamId, true, kUserId);
  {
    auto* shared_data = team_storage.add_shared_team_data();
    shared_data->set_key(matching_key);
    CASE_EXPECT_TRUE(shared_data->mutable_value()->mutable_data()->PackFrom(matching_module));
  }
  {
    // 队长的成员共享数据 + 内部字段(快照导出时必须剥掉内部字段、按 key 回填共享数据)
    auto* captain = team_storage.mutable_member(0);
    auto* member_data = captain->add_shared_member_data();
    member_data->set_key(ready_key);
    CASE_EXPECT_TRUE(member_data->mutable_value()->mutable_data()->PackFrom(ready_module));
    captain->mutable_user_channel()->set_channel_id("polluted-captain-channel");
    captain->set_user_router_server_id(0x5678);
    captain->set_acknowledge_action_sequence(42);
  }
  {
    // 未过期的邀请(携带内部路由字段，导出时必须剥掉)
    auto* invitation = team_storage.add_pending_invitation();
    invitation->mutable_invitee()->set_zone_id(kZoneId);
    invitation->mutable_invitee()->set_user_id(kValidInviteeId);
    *invitation->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
    invitation->mutable_invitee_private_channel()->set_channel_id("polluted-invitee-channel");
  }
  {
    // 已过期的邀请: 快照加载时即被过滤，不会出现在导出中
    auto* invitation = team_storage.add_pending_invitation();
    invitation->mutable_invitee()->set_zone_id(kZoneId);
    invitation->mutable_invitee()->set_user_id(kExpiredInviteeId);
    *invitation->mutable_expired_timepoint() = protobuf_from_system_clock(now - std::chrono::seconds(60));
  }
  {
    // 未过期的加入请求
    auto* join_request = team_storage.add_pending_join_request();
    join_request->mutable_requester()->set_zone_id(kZoneId);
    join_request->mutable_requester()->set_user_id(kJoinRequesterId);
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
    join_request->mutable_requester_private_channel()->set_channel_id("polluted-requester-channel");
    join_request->set_user_router_server_id(0x4321);
  }

  auto team_channel_key = rpc::team::team_api::make_team_room_channel_key(make_team_key(kTeamId));
  CASE_EXPECT_TRUE(receive_channel_event(test, make_snapshot_event(team_channel_key, 1, 0, &team_storage)));
  pump_rounds(test, 4);

  PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
  {
    rpc::context ctx{rpc::context::create_without_task()};
    current->dump(ctx, snapshot);
  }

  CASE_EXPECT_EQ(kTeamId, snapshot.snapshot().team_key().team_id());
  CASE_EXPECT_EQ(kCaptainUserId, snapshot.snapshot().captain_user_key().user_id());

  // 成员: 队长 + 自己。内部字段必须被剥掉，共享成员数据按 key 回填且仍是打包的 Any
  CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
  const atfw::team::DTeamMember* captain_member = nullptr;
  for (const auto& member : snapshot.snapshot().member()) {
    CASE_EXPECT_TRUE(member.user_channel().channel_id().empty());
    CASE_EXPECT_EQ(0, member.user_router_server_id());
    CASE_EXPECT_EQ(0, member.acknowledge_action_sequence());
    if (member.user_key().user_id() == kCaptainUserId) {
      captain_member = &member;
    }
  }
  CASE_EXPECT_TRUE(nullptr != captain_member);
  if (nullptr != captain_member) {
    CASE_EXPECT_EQ(1, captain_member->shared_member_data_size());
    if (captain_member->shared_member_data_size() > 0) {
      CASE_EXPECT_EQ(ready_key, captain_member->shared_member_data(0).key());
      PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule unpacked;
      CASE_EXPECT_TRUE(captain_member->shared_member_data(0).value().data().UnpackTo(&unpacked));
      CASE_EXPECT_TRUE(unpacked.battle().ready());
    }
  }

  // 邀请: 只剩未过期的一个，且剥掉了内部通知频道
  CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
  if (snapshot.snapshot().pending_invitation_size() > 0) {
    CASE_EXPECT_EQ(kValidInviteeId, snapshot.snapshot().pending_invitation(0).invitee().user_id());
    CASE_EXPECT_TRUE(snapshot.snapshot().pending_invitation(0).invitee_private_channel().channel_id().empty());
  }
  CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
  if (snapshot.snapshot().pending_join_request_size() > 0) {
    CASE_EXPECT_EQ(kJoinRequesterId, snapshot.snapshot().pending_join_request(0).requester().user_id());
    CASE_EXPECT_TRUE(snapshot.snapshot().pending_join_request(0).requester_private_channel().channel_id().empty());
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_join_request(0).user_router_server_id());
  }

  // 队伍共享数据: 解包后的模块数据导出到 shared_team_data，snapshot 内不保留打包的原始数据
  CASE_EXPECT_EQ(0, snapshot.snapshot().shared_team_data_size());
  CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
  if (snapshot.shared_team_data_size() > 0) {
    CASE_EXPECT_TRUE(snapshot.shared_team_data(0).has_battle());
    CASE_EXPECT_TRUE(snapshot.shared_team_data(0).battle().matching());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

namespace {

constexpr uint64_t kGatewayNodeId = 0x82000001;

// Find the latest downstream post record addressed to the session whose CSMsg head matches the rpc name (response
// or stream). Multiple pushes of the same rpc name are distinguished by taking the last match.
const atfw::testing::cs_downstream_record *find_downstream_by_rpc_name(atfw::testing::runtime &test,
                                                                       uint64_t session_id,
                                                                       gsl::string_view rpc_full_name,
                                                                       atframework::CSMsg &out_msg) {
  const atfw::testing::cs_downstream_record *ret = nullptr;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const auto *record = test.cs().call_at(i);
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
      continue;
    }
    if (cs_msg.head().has_rpc_stream() && rpc_full_name == cs_msg.head().rpc_stream().rpc_name()) {
      out_msg = std::move(cs_msg);
      ret = record;
    }
  }
  return ret;
}

// Collect every downstream user_dirty_chg_sync push addressed to the session.
std::vector<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync> collect_dirty_sync_pushes(atfw::testing::runtime &test,
                                                                                uint64_t session_id) {
  std::vector<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync> ret;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const auto *record = test.cs().call_at(i);
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

}  // namespace

// 频道增量事件必须持续维护本地缓存并通过 SCUserDirtyChgSync 推送增量脏数据给客户端:
// - add_join_request/team_update/add_member/member_set_role/election_captain 各自更新缓存，
//   缓存内容可通过 user_team::dump 观察;
// - election_captain 同步新旧队长的成员角色与自己的 cached_permission_role_;
// - 过期准入数据由 cleanup_expired_admissions 清理且 dump 不下发;
// - 快照加载后客户端收到 snapshot 脏数据推送; 之后的每个增量动作触发 increase 推送，
//   内部路由字段被剥掉、成员共享数据被解包到 OneAction.shared_member_data。
CASE_TEST(lobbysvr_user_team, incremental_actions_update_cache_and_dirty_push) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30006;
  constexpr uint64_t kSessionId = 0x10006;
  constexpr int64_t kTeamId = 205;
  constexpr uint64_t kMemberUserId = 91011;
  constexpr uint64_t kJoinRequesterId = 91012;
  constexpr uint64_t kShortJoinRequesterId = 91013;
  constexpr uint64_t kLongInviteeId = 91014;
  constexpr uint64_t kShortInviteeId = 91015;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  // 绑定 mock 客户端会话，让脏数据推送真正下发到 CS 通道
  auto client = test.cs().create_client(kGatewayNodeId, kSessionId);
  CASE_EXPECT_TRUE(!!client);
  if (!client || 0 != client.add()) {
    CASE_MSG_INFO() << "client add failed\n";
    test.stop();
    return;
  }
  session::key_t session_key;
  session_key.node_id = kGatewayNodeId;
  session_key.session_id = kSessionId;
  auto sess = session_manager::me()->find(session_key);
  CASE_EXPECT_TRUE(!!sess);
  if (!sess) {
    test.stop();
    return;
  }
  {
    user::ptr_t user_ptr = user_inst;
    std::shared_ptr<session> sess_ptr = sess;
    CASE_EXPECT_TRUE(
        run_sync_task(test, "team.bind_session", [&user_ptr, &sess_ptr](rpc::context &ctx) -> rpc::result_code_type {
          sess_ptr->set_user(user_ptr);
          user_ptr->set_session(ctx, sess_ptr);
          RPC_RETURN_CODE(0);
        }));
  }

  CASE_EXPECT_TRUE(restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  auto team_channel_key = rpc::team::team_api::make_team_room_channel_key(make_team_key(kTeamId));
  atfw::team::DTeamStorage team_storage = make_team_storage(kTeamId, true, kUserId);
  CASE_EXPECT_TRUE(receive_channel_event(test, make_snapshot_event(team_channel_key, 1, 0, &team_storage)));
  pump_rounds(test, 4);
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_cached_permission_role());

  // 快照加载后客户端应收到一条携带完整快照的脏数据推送
  {
    auto pushes = collect_dirty_sync_pushes(test, kSessionId);
    CASE_EXPECT_EQ(1, static_cast<int>(pushes.size()));
    if (!pushes.empty()) {
      CASE_EXPECT_EQ(1, pushes[0].dirty_team_size());
      if (pushes[0].dirty_team_size() > 0) {
        CASE_EXPECT_TRUE(pushes[0].dirty_team(0).has_snapshot());
        CASE_EXPECT_EQ(2, pushes[0].dirty_team(0).snapshot().snapshot().member_size());
      }
    }
  }
  test.cs().clear_history();

  int64_t event_sequence = 0;
  uint64_t previous_hash = 0;
  auto send_team_action_event = [&](const atfw::team::DTeamAction &team_action) {
    std::vector<atframework::dtmq::DChannelMessage> msgs{make_event_message(++event_sequence, team_action)};
    previous_hash = chain_message_hashes(msgs, previous_hash);
    return receive_channel_event(test, make_incremental_event(team_channel_key, event_sequence, msgs));
  };

  auto now = std::chrono::system_clock::now();

  // 1. add_join_request: 进入待处理加入请求缓存(携带内部字段验证剥除)
  {
    atfw::team::DTeamAction team_action;
    auto *join_request = team_action.mutable_add_join_request();
    join_request->mutable_requester()->set_zone_id(kZoneId);
    join_request->mutable_requester()->set_user_id(kJoinRequesterId);
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
    join_request->mutable_requester_private_channel()->set_channel_id("polluted-requester-channel");
    join_request->set_user_router_server_id(0x2222);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }

  // 1b. 有效期更短的加入请求: 排序后必须排在更长有效期的条目之前
  {
    atfw::team::DTeamAction team_action;
    auto *join_request = team_action.mutable_add_join_request();
    join_request->mutable_requester()->set_zone_id(kZoneId);
    join_request->mutable_requester()->set_user_id(kShortJoinRequesterId);
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(120));
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }

  // 1c. 两个不同有效期的邀请
  {
    atfw::team::DTeamAction team_action;
    auto *invitation = team_action.mutable_add_invitation();
    invitation->mutable_invitee()->set_zone_id(kZoneId);
    invitation->mutable_invitee()->set_user_id(kLongInviteeId);
    *invitation->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(600));
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }
  {
    atfw::team::DTeamAction team_action;
    auto *invitation = team_action.mutable_add_invitation();
    invitation->mutable_invitee()->set_zone_id(kZoneId);
    invitation->mutable_invitee()->set_user_id(kShortInviteeId);
    *invitation->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(60));
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }

  // 2. team_update: 队伍共享数据 matching=true
  PROJECT_NAMESPACE_ID::DTeamSharedDataModule matching_module;
  matching_module.mutable_battle()->set_matching(true);
  {
    atfw::team::DTeamAction team_action;
    auto *shared_data = team_action.mutable_team_update()->add_shared_team_data();
    shared_data->set_key(user_team_algorithm::make_team_shared_data_key(matching_module));
    CASE_EXPECT_TRUE(shared_data->mutable_value()->mutable_data()->PackFrom(matching_module));
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }

  // 3. add_member: 新成员携带打包的 ready 共享数据与内部字段
  PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule ready_module;
  ready_module.mutable_battle()->set_ready(true);
  {
    atfw::team::DTeamAction team_action;
    auto *member = team_action.mutable_add_member();
    member->mutable_user_key()->set_zone_id(kZoneId);
    member->mutable_user_key()->set_user_id(kMemberUserId);
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(now);
    auto *member_data = member->add_shared_member_data();
    member_data->set_key(user_team_algorithm::make_team_member_shared_data_key(ready_module));
    CASE_EXPECT_TRUE(member_data->mutable_value()->mutable_data()->PackFrom(ready_module));
    member->mutable_user_channel()->set_channel_id("polluted-member-channel");
    member->set_user_router_server_id(0x4444);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }

  // 4. member_set_role: 新成员提升为 ADMIN
  {
    atfw::team::DTeamAction team_action;
    auto *set_role = team_action.mutable_member_set_role();
    set_role->mutable_user_key()->set_zone_id(kZoneId);
    set_role->mutable_user_key()->set_user_id(kMemberUserId);
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }

  // 5. election_captain 选自己再选回原队长: 自己的角色 OWNER -> NORMAL，对方 NORMAL -> OWNER
  {
    atfw::team::DTeamAction team_action;
    auto *election = team_action.mutable_election_captain();
    election->mutable_user_key()->set_zone_id(kZoneId);
    election->mutable_user_key()->set_user_id(kUserId);
    election->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }
  {
    atfw::team::DTeamAction team_action;
    auto *election = team_action.mutable_election_captain();
    election->mutable_user_key()->set_zone_id(kZoneId);
    election->mutable_user_key()->set_user_id(kCaptainUserId);
    election->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }
  // 6. 同 key 同过期时间的重复 add_join_request: 原位覆盖(内容更新但不新增条目、不改变顺序)
  {
    atfw::team::DTeamAction team_action;
    auto *join_request = team_action.mutable_add_join_request();
    join_request->mutable_requester()->set_zone_id(kZoneId);
    join_request->mutable_requester()->set_user_id(kJoinRequesterId);
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
    join_request->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    CASE_EXPECT_TRUE(send_team_action_event(team_action));
  }
  pump_rounds(test, 4);

  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_cached_permission_role());
  CASE_EXPECT_EQ(kCaptainUserId, current->get_cached_captain_user_key().user_id());

  // 每个增量动作都应触发一条 increase 推送; 内部字段被剥掉，成员共享数据解包后随动作下发
  {
    auto pushes = collect_dirty_sync_pushes(test, kSessionId);
    CASE_EXPECT_EQ(10, static_cast<int>(pushes.size()));
    size_t total_actions = 0;
    bool checked_add_member = false;
    bool checked_add_join_request = false;
    for (const auto &push : pushes) {
      CASE_EXPECT_EQ(1, push.dirty_team_size());
      if (0 == push.dirty_team_size()) {
        continue;
      }
      const auto &increase = push.dirty_team(0).increase();
      CASE_EXPECT_EQ(kTeamId, increase.team_key().team_id());
      total_actions += increase.actions_size();
      for (const auto &one_action : increase.actions()) {
        const auto &action = one_action.actions();
        if (action.has_add_member()) {
          checked_add_member = true;
          CASE_EXPECT_TRUE(action.add_member().user_channel().channel_id().empty());
          CASE_EXPECT_EQ(0, action.add_member().user_router_server_id());
          CASE_EXPECT_EQ(0, action.add_member().acknowledge_action_sequence());
          // 打包的成员共享数据解包后随动作下发
          CASE_EXPECT_EQ(1, one_action.shared_member_data_size());
          if (one_action.shared_member_data_size() > 0) {
            CASE_EXPECT_TRUE(one_action.shared_member_data(0).battle().ready());
          }
        }
        if (action.has_add_join_request()) {
          checked_add_join_request = true;
          CASE_EXPECT_TRUE(action.add_join_request().requester_private_channel().channel_id().empty());
          CASE_EXPECT_EQ(0, action.add_join_request().user_router_server_id());
        }
      }
    }
    CASE_EXPECT_EQ(10, static_cast<int>(total_actions));
    CASE_EXPECT_TRUE(checked_add_member);
    CASE_EXPECT_TRUE(checked_add_join_request);
  }

  // 缓存状态经 dump 可观察: matching、成员角色与待处理加入请求
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    current->dump(ctx, snapshot);

    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    const atfw::team::DTeamMember *new_member = nullptr;
    for (const auto &member : snapshot.snapshot().member()) {
      if (member.user_key().user_id() == kMemberUserId) {
        new_member = &member;
      }
      if (member.user_key().user_id() == kCaptainUserId) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, member.role());
      }
    }
    CASE_EXPECT_TRUE(nullptr != new_member);
    if (nullptr != new_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, new_member->role());
      CASE_EXPECT_EQ(1, new_member->shared_member_data_size());
      if (new_member->shared_member_data_size() > 0) {
        PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule unpacked;
        CASE_EXPECT_TRUE(new_member->shared_member_data(0).value().data().UnpackTo(&unpacked));
        CASE_EXPECT_TRUE(unpacked.battle().ready());
      }
    }

    // 准入数据按过期时间升序下发: 短有效期的加入请求/邀请排在前面
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_join_request_size());
    if (snapshot.snapshot().pending_join_request_size() >= 2) {
      CASE_EXPECT_EQ(kShortJoinRequesterId, snapshot.snapshot().pending_join_request(0).requester().user_id());
      CASE_EXPECT_EQ(kJoinRequesterId, snapshot.snapshot().pending_join_request(1).requester().user_id());
      // 同 key 同过期时间的重复 add_join_request 原位覆盖: 内容已更新为 FRIEND
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND,
                     snapshot.snapshot().pending_join_request(1).team_source_type());
    }
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_invitation_size());
    if (snapshot.snapshot().pending_invitation_size() >= 2) {
      CASE_EXPECT_EQ(kShortInviteeId, snapshot.snapshot().pending_invitation(0).invitee().user_id());
      CASE_EXPECT_EQ(kLongInviteeId, snapshot.snapshot().pending_invitation(1).invitee().user_id());
    }
    CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
    if (snapshot.shared_team_data_size() > 0) {
      CASE_EXPECT_TRUE(snapshot.shared_team_data(0).battle().matching());
    }
  }

  // 部分过期清理: 只移除过期前缀(短有效期的加入请求和邀请)，遇到第一个未过期条目即停止
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds(150));
  atfw::util::time::time_utility::update();
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(2, static_cast<int>(current->cleanup_expired_admissions(ctx)));

    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    current->dump(ctx, snapshot);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
    if (snapshot.snapshot().pending_join_request_size() > 0) {
      CASE_EXPECT_EQ(kJoinRequesterId, snapshot.snapshot().pending_join_request(0).requester().user_id());
    }
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    if (snapshot.snapshot().pending_invitation_size() > 0) {
      CASE_EXPECT_EQ(kLongInviteeId, snapshot.snapshot().pending_invitation(0).invitee().user_id());
    }
  }
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();

  // 继续快进到 300s 有效期的加入请求也过期: 只剩最长有效期的邀请
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds(400));
  atfw::util::time::time_utility::update();
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(1, static_cast<int>(current->cleanup_expired_admissions(ctx)));

    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    current->dump(ctx, snapshot);
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    if (snapshot.snapshot().pending_invitation_size() > 0) {
      CASE_EXPECT_EQ(kLongInviteeId, snapshot.snapshot().pending_invitation(0).invitee().user_id());
    }
  }
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();

  CASE_EXPECT_EQ(0, test.stop());
}

// The personal-channel receipt events drive the manager-level pending admission lists:
// invited/apply_join_request populate them (expired entries filtered), reject_invitation/reject_join_request
// remove them. apply_join_request is sent by teamsvr-room apply_add_join_request as the requester's receipt.
CASE_TEST(lobbysvr_user_team, member_events_manage_pending_admissions) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 31001;
  constexpr int64_t kInviteTeamId = 8101;
  constexpr int64_t kJoinTeamId = 8102;
  constexpr int64_t kExpiredTeamId = 8103;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  auto self_key = [kUserId]() {
    PROJECT_NAMESPACE_ID::DUserIDKey key;
    key.set_zone_id(kZoneId);
    key.set_user_id(kUserId);
    return key;
  };

  int64_t event_sequence = 0;
  uint64_t previous_hash = 0;
  auto send_member_event = [&](const atfw::team::DTeamMemberAction& action) {
    std::vector<atframework::dtmq::DChannelMessage> msgs{make_event_message(++event_sequence, action)};
    previous_hash = chain_message_hashes(msgs, previous_hash);
    return receive_channel_event(test, make_incremental_event(private_channel_key, event_sequence, msgs));
  };

  auto valid_expiry = []() {
    return protobuf_from_system_clock(std::chrono::system_clock::now() + std::chrono::seconds{60});
  };

  // invited -> 待处理邀请入列
  {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), make_team_key(kInviteTeamId));
    invited->mutable_inviter()->set_zone_id(kZoneId);
    invited->mutable_inviter()->set_user_id(kCaptainUserId);
    protobuf_copy_message(*invited->mutable_invitee(), self_key());
    *invited->mutable_expired_timepoint() = valid_expiry();
    CASE_EXPECT_TRUE(send_member_event(action));
    pump_rounds(test, 4);
  }
  auto invitation = user_inst->get_user_team_manager().get_pending_invitation(make_team_key(kInviteTeamId));
  CASE_EXPECT_TRUE(!!invitation);
  if (invitation) {
    CASE_EXPECT_EQ(kUserId, invitation->invitee().user_id());
    CASE_EXPECT_GT(invitation->expired_timepoint().seconds(), 0);
  }

  // apply_join_request -> 待处理加入请求入列(房间受理回执)
  {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), make_team_key(kJoinTeamId));
    protobuf_copy_message(*applied->mutable_requester(), self_key());
    *applied->mutable_expired_timepoint() = valid_expiry();
    CASE_EXPECT_TRUE(send_member_event(action));
    pump_rounds(test, 4);
  }
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_join_request(make_team_key(kJoinTeamId)));

  // 已过期的申请回执直接过滤，不入列
  {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), make_team_key(kExpiredTeamId));
    protobuf_copy_message(*applied->mutable_requester(), self_key());
    *applied->mutable_expired_timepoint() =
        protobuf_from_system_clock(std::chrono::system_clock::now() - std::chrono::seconds{1});
    CASE_EXPECT_TRUE(send_member_event(action));
    pump_rounds(test, 4);
  }
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(make_team_key(kExpiredTeamId)));

  // reject_invitation -> 待处理邀请出列
  {
    atfw::team::DTeamMemberAction action;
    auto* rejected = action.mutable_reject_invitation();
    protobuf_copy_message(*rejected->mutable_team_key(), make_team_key(kInviteTeamId));
    protobuf_copy_message(*rejected->mutable_invitee(), self_key());
    CASE_EXPECT_TRUE(send_member_event(action));
    pump_rounds(test, 4);
  }
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_invitation(make_team_key(kInviteTeamId)));

  // reject_join_request -> 待处理加入请求出列
  {
    atfw::team::DTeamMemberAction action;
    auto* rejected = action.mutable_reject_join_request();
    protobuf_copy_message(*rejected->mutable_team_key(), make_team_key(kJoinTeamId));
    protobuf_copy_message(*rejected->mutable_requester(), self_key());
    CASE_EXPECT_TRUE(send_member_event(action));
    pump_rounds(test, 4);
  }
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(make_team_key(kJoinTeamId)));

  CASE_EXPECT_EQ(0, test.stop());
}

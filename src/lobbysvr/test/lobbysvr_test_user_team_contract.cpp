// Copyright 2026 atframework

#include <rpc/rpc_shared_message.h>

#include <cstdint>
#include <string>

#include "lobbysvr_test_user_team_common.h"                // NOLINT: build/include_subdir
#include "logic/team/user_team_battle_library_function.h"  // NOLINT: build/include_subdir

namespace {
// Fixed wire keys from com.struct.team.shared.proto, independent of the production key helper.
constexpr int64_t kBattleOnlyKey = 0x100000000LL;
constexpr int64_t kStateKey = 0x100000001LL;
constexpr int64_t kDerivedKey = 0x100000002LL;
constexpr int64_t kLevelKey = 0x100000003LL;

struct contract_fixture {
  atfw::testing::runtime runtime;
  team_test::team_room_ss_capture room;
  user::ptr_t player;
  atfw::testing::mock_client client;
  team_test::channel_event_chain personal;
  team_test::channel_event_chain channel;
  user_team::ptr_t team;
  int64_t team_id = 0;

  bool start(uint64_t identifier) {
    team_id = static_cast<int64_t>(identifier);
    if (!team_test::start_team_runtime(runtime) || !team_test::setup_team_room_node(runtime) ||
        !team_test::setup_team_room_ss_capture(runtime, room)) {
      return false;
    }
    std::string subscriber;
    if (!team_test::setup_team_user(runtime, identifier, player, subscriber, personal.channel_key) ||
        !team_test::bind_client_session(runtime, player, identifier + 100000, client) ||
        !team_test::join_team_with_snapshot(runtime, player, personal, team_id, atfw::team::EN_TEAM_MEMBER_ROLE_OWNER,
                                            true, nullptr, {})) {
      return false;
    }
    channel.channel_key = team_test::make_team_channel_key(team_id);
    team = player->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(team_id));
    return team && team->is_member();
  }

  // Keep captured state alive until teardown, including when a test assertion throws.
  ~contract_fixture() { runtime.stop(); }
};

const atfw::team::DTeamAnyDataWithKey* find_entry(
    const google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& entries, int64_t key) {
  const atfw::team::DTeamAnyDataWithKey* found = nullptr;
  for (const auto& entry : entries) {
    if (entry.key() == key) {
      CASE_EXPECT_EQ(nullptr, found);
      found = &entry;
    }
  }
  CASE_EXPECT_NE(nullptr, found);
  return found;
}
}  // namespace

CASE_TEST(lobbysvr_user_team, shared_module_keys_follow_wire_fields_and_client_permissions) {
  PROJECT_NAMESPACE_ID::DTeamSharedDataModule team_data;
  PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule member_data;
  CASE_EXPECT_EQ(0, user_team_algorithm::make_team_shared_data_key(team_data));
  CASE_EXPECT_EQ(0, user_team_algorithm::make_team_member_shared_data_key(member_data));
  CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_shared_data(team_data));
  CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_member_shared_data(member_data));
  team_data.mutable_battle();
  member_data.mutable_battle();
  CASE_EXPECT_EQ(kBattleOnlyKey, user_team_algorithm::make_team_shared_data_key(team_data));
  CASE_EXPECT_EQ(kBattleOnlyKey, user_team_algorithm::make_team_member_shared_data_key(member_data));
  CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_shared_data(team_data));
  CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_member_shared_data(member_data));
  for (bool state : {false, true}) {
    team_data.mutable_battle()->set_matching(state);
    member_data.mutable_battle()->set_ready(state);
    CASE_EXPECT_EQ(kStateKey, user_team_algorithm::make_team_shared_data_key(team_data));
    CASE_EXPECT_EQ(kStateKey, user_team_algorithm::make_team_member_shared_data_key(member_data));
    CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_shared_data(team_data));
    CASE_EXPECT_TRUE(user_team_algorithm::allow_client_update_team_member_shared_data(member_data));
  }
  team_data.mutable_battle()->mutable_matching_team_view();
  member_data.mutable_battle()->mutable_matching_parameter();
  CASE_EXPECT_EQ(kDerivedKey, user_team_algorithm::make_team_shared_data_key(team_data));
  CASE_EXPECT_EQ(kDerivedKey, user_team_algorithm::make_team_member_shared_data_key(member_data));
  CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_shared_data(team_data));
  CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_member_shared_data(member_data));
  team_data.mutable_battle()->mutable_matching_start_data();
  CASE_EXPECT_EQ(kLevelKey, user_team_algorithm::make_team_shared_data_key(team_data));
  CASE_EXPECT_FALSE(user_team_algorithm::allow_client_update_team_shared_data(team_data));
}

CASE_TEST(lobbysvr_user_team, normalize_team_update_clears_existing_view_independent_of_order) {
  contract_fixture fixture;
  const bool started = fixture.start(991201);
  CASE_EXPECT_TRUE(started);
  if (!started) {
    CASE_EXPECT_EQ(0, fixture.runtime.stop());
    return;
  }
  const size_t initial_pushes =
      team_test::collect_dirty_sync_pushes(fixture.runtime, fixture.client.session_id()).size();
  const auto initial_view =
      team_test::collect_team_dirty(fixture.runtime, fixture.client.session_id(), fixture.team_id);
  size_t delivered = 0;
  for (bool derived_first : {false, true}) {
    const size_t before = fixture.room.send_message_reqs.size();
    CASE_EXPECT_TRUE(team_test::run_sync_task(
        fixture.runtime, "team.normalize_existing_view",
        [&fixture, derived_first](rpc::context& ctx) -> rpc::result_code_type {
          auto request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSTeamUpdateTeamDataReq>(ctx);
          auto* first = request->add_data();
          auto* second = request->add_data();
          auto* state = derived_first ? second : first;
          auto* derived = derived_first ? first : second;
          state->mutable_battle()->set_matching(false);
          derived->mutable_battle()->mutable_matching_team_view()->set_unit_id(987654);
          derived->mutable_battle()->mutable_matching_team_view()->set_subscriber_server_id(123456);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(fixture.team->update_team_shared_data(ctx, *request->mutable_data())));
        }));
    CASE_EXPECT_EQ(before + 1, fixture.room.send_message_reqs.size());
    if (fixture.room.send_message_reqs.size() != before + 1) {
      continue;
    }
    const auto request = fixture.room.send_message_reqs.back();
    team_test::expect_send_message_envelope(request, fixture.team_id, fixture.player->get_user_id());
    CASE_EXPECT_EQ(2, request.action().team_update().shared_team_data_size());
    // matching=false 附加 "队伍匹配中" 更新条件
    CASE_EXPECT_EQ(1, request.action().team_update().condition_size());
    if (request.action().team_update().condition_size() == 1) {
      CASE_EXPECT_EQ(1, request.action().team_update().condition(0).shared_team_data_size());
      if (request.action().team_update().condition(0).shared_team_data_size() == 1) {
        CASE_EXPECT_EQ(kStateKey, request.action().team_update().condition(0).shared_team_data(0).key());
        PROJECT_NAMESPACE_ID::DTeamSharedDataModule condition;
        CASE_EXPECT_TRUE(request.action().team_update().condition(0).shared_team_data(0).value().UnpackTo(&condition));
        CASE_EXPECT_TRUE(condition.has_battle());
        CASE_EXPECT_TRUE(condition.battle().matching());
      }
    }
    const auto* state = find_entry(request.action().team_update().shared_team_data(), kStateKey);
    const auto* derived = find_entry(request.action().team_update().shared_team_data(), kDerivedKey);
    if (nullptr == state || nullptr == derived) {
      continue;
    }
    team_test::expect_packed_team_matching_entry(*state, false);
    team_test::expect_packed_team_matching_team_view_entry(*derived);
    PROJECT_NAMESPACE_ID::DTeamSharedDataModule decoded;
    CASE_EXPECT_TRUE(derived->value().data().UnpackTo(&decoded));
    if (decoded.battle().matching_team_view().ByteSizeLong() != 0) {
      continue;  // A bad uplink already failed; do not trigger unrelated matching work with that invalid result.
    }
    CASE_EXPECT_EQ(initial_pushes + delivered,
                   team_test::collect_dirty_sync_pushes(fixture.runtime, fixture.client.session_id()).size());
    auto committed = request.action();
    committed.mutable_team_update()->clear_condition();
    CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.runtime, fixture.channel, committed));
    ++delivered;
    CASE_EXPECT_EQ(initial_pushes + delivered,
                   team_test::collect_dirty_sync_pushes(fixture.runtime, fixture.client.session_id()).size());
    const auto view = team_test::collect_team_dirty(fixture.runtime, fixture.client.session_id(), fixture.team_id);
    CASE_EXPECT_EQ(initial_view.snapshots.size(), view.snapshots.size());
    CASE_EXPECT_EQ(initial_view.actions.size() + delivered, view.actions.size());
    if (!view.actions.empty()) {
      CASE_EXPECT_EQ(committed.SerializeAsString(), view.actions.back().action().SerializeAsString());
    }
    CASE_EXPECT_FALSE(fixture.team->is_matching());
    CASE_EXPECT_EQ(0u, user_team_battle_library_function::get_matching_team_sync_view(*fixture.team).ByteSizeLong());
  }
  CASE_EXPECT_EQ(2u, delivered);
  CASE_EXPECT_EQ(0, fixture.runtime.stop());
}

CASE_TEST(lobbysvr_user_team, normalize_member_update_clears_existing_parameter_independent_of_order) {
  contract_fixture fixture;
  const bool started = fixture.start(991202);
  CASE_EXPECT_TRUE(started);
  if (!started) {
    CASE_EXPECT_EQ(0, fixture.runtime.stop());
    return;
  }
  const size_t initial_pushes =
      team_test::collect_dirty_sync_pushes(fixture.runtime, fixture.client.session_id()).size();
  const auto initial_view =
      team_test::collect_team_dirty(fixture.runtime, fixture.client.session_id(), fixture.team_id);
  size_t delivered = 0;
  for (bool derived_first : {false, true}) {
    const size_t before = fixture.room.send_message_reqs.size();
    CASE_EXPECT_TRUE(team_test::run_sync_task(
        fixture.runtime, "team.normalize_existing_parameter",
        [&fixture, derived_first](rpc::context& ctx) -> rpc::result_code_type {
          auto request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSTeamUpdateMemberDataReq>(ctx);
          auto* first = request->add_data();
          auto* second = request->add_data();
          auto* state = derived_first ? second : first;
          auto* derived = derived_first ? first : second;
          state->mutable_battle()->set_ready(false);
          derived->mutable_battle()->mutable_matching_parameter()->mutable_parameter()->set_search_start_time(987654);
          RPC_RETURN_CODE(
              RPC_AWAIT_CODE_RESULT(fixture.team->update_member_shared_data(ctx, *request->mutable_data())));
        }));
    CASE_EXPECT_EQ(before + 1, fixture.room.send_message_reqs.size());
    if (fixture.room.send_message_reqs.size() != before + 1) {
      continue;
    }
    const auto request = fixture.room.send_message_reqs.back();
    team_test::expect_send_message_envelope(request, fixture.team_id, fixture.player->get_user_id());
    const auto& update = request.action().member_update();
    CASE_EXPECT_EQ(2, update.shared_member_data_size());
    CASE_EXPECT_EQ(1, update.condition_size());
    if (update.condition_size() == 1) {
      CASE_EXPECT_EQ(1, update.condition(0).shared_team_data_size());
      if (update.condition(0).shared_team_data_size() == 1) {
        CASE_EXPECT_EQ(kStateKey, update.condition(0).shared_team_data(0).key());
        PROJECT_NAMESPACE_ID::DTeamSharedDataModule condition;
        CASE_EXPECT_TRUE(update.condition(0).shared_team_data(0).value().UnpackTo(&condition));
        CASE_EXPECT_TRUE(condition.has_battle());
        CASE_EXPECT_FALSE(condition.battle().matching());
      }
    }
    const auto* state = find_entry(update.shared_member_data(), kStateKey);
    const auto* derived = find_entry(update.shared_member_data(), kDerivedKey);
    if (nullptr == state || nullptr == derived) {
      continue;
    }
    team_test::expect_packed_member_ready_entry(*state, false);
    team_test::expect_packed_member_matching_parameter_entry(*derived);
    PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule decoded;
    CASE_EXPECT_TRUE(derived->value().data().UnpackTo(&decoded));
    if (decoded.battle().matching_parameter().ByteSizeLong() != 0) {
      continue;
    }
    CASE_EXPECT_EQ(initial_pushes + delivered,
                   team_test::collect_dirty_sync_pushes(fixture.runtime, fixture.client.session_id()).size());
    auto committed = request.action();
    committed.mutable_member_update()->clear_condition();
    CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.runtime, fixture.channel, committed));
    ++delivered;
    CASE_EXPECT_EQ(initial_pushes + delivered,
                   team_test::collect_dirty_sync_pushes(fixture.runtime, fixture.client.session_id()).size());
    const auto view = team_test::collect_team_dirty(fixture.runtime, fixture.client.session_id(), fixture.team_id);
    CASE_EXPECT_EQ(initial_view.snapshots.size(), view.snapshots.size());
    CASE_EXPECT_EQ(initial_view.actions.size() + delivered, view.actions.size());
    if (!view.actions.empty()) {
      CASE_EXPECT_EQ(2, view.actions.back().shared_member_data_size());
      CASE_EXPECT_EQ(0, view.actions.back().action().member_update().shared_member_data_size());
      CASE_EXPECT_FALSE(view.actions.back().action().member_update().has_user_channel());
      CASE_EXPECT_EQ(0u, view.actions.back().action().member_update().user_router_server_id());
      size_t ready_entries = 0;
      size_t parameter_entries = 0;
      for (const auto& module : view.actions.back().shared_member_data()) {
        CASE_EXPECT_TRUE(module.has_battle());
        if (module.battle().data_type_case() == PROJECT_NAMESPACE_ID::DTeamMemberSharedDataTypeBattle::kReady) {
          ++ready_entries;
        }
        if (module.battle().has_matching_parameter()) {
          ++parameter_entries;
        }
        CASE_EXPECT_EQ(0u, module.battle().matching_parameter().ByteSizeLong());
        CASE_EXPECT_FALSE(module.battle().ready());
      }
      CASE_EXPECT_EQ(1u, ready_entries);
      CASE_EXPECT_EQ(1u, parameter_entries);
    }
    auto member = user_team_battle_library_function::find_member(
        *fixture.team, team_test::make_user_key(fixture.player->get_user_id()));
    CASE_EXPECT_TRUE(!!member);
    if (member) {
      CASE_EXPECT_FALSE(user_team_battle_library_function::is_ready(*member));
      CASE_EXPECT_EQ(0u, user_team_battle_library_function::get_matching_team_parameter(*member).ByteSizeLong());
    }
  }
  CASE_EXPECT_EQ(2u, delivered);
  CASE_EXPECT_EQ(0, fixture.runtime.stop());
}

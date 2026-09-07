// Copyright 2026 atframework

#include <string>

#include "app/handle_cs_rpc_lobbysvrclientservice.atfw.gen.h"
#include "lobbysvr_test_runtime_helper.h"    // NOLINT: build/include_subdir
#include "lobbysvr_test_user_team_common.h"  // NOLINT: build/include_subdir

namespace {
struct dirty_fixture {
  atfw::testing::runtime test;
  team_test::team_room_ss_capture room;
  user::ptr_t player;
  atfw::testing::mock_client client;
  team_test::channel_event_chain personal;
  team_test::channel_event_chain channel;
  int64_t team_id = 0;
  uint64_t user_id = 0;

  bool start(uint64_t identifier, bool subscribe = true) {
    user_id = identifier;
    team_id = static_cast<int64_t>(identifier);
    if (!team_test::start_team_runtime(test) || !team_test::setup_team_room_node(test) ||
        !team_test::setup_team_room_ss_capture(test, room)) {
      return false;
    }
    CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
    std::string subscriber;
    if (!team_test::setup_team_user(test, user_id, player, subscriber, personal.channel_key) ||
        !team_test::bind_client_session(test, player, identifier + 0x60000, client, subscribe) ||
        !team_test::join_team_via_notification(test, player, personal, team_id)) {
      return false;
    }
    channel.channel_key = team_test::make_team_channel_key(team_id);
    auto storage = make_storage();
    return team_test::apply_team_snapshot(test, team_id, storage) &&
           team_test::pump_until(test, [this] { return team() && team()->is_member(); });
  }

  atfw::team::DTeamStorage make_storage() const {
    auto storage = team_test::make_team_storage(team_id);
    team_test::add_storage_member(storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(
        storage, user_id,
        team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
            .set_shared_member_data({team_test::pack_member_module(team_test::make_member_ready_module(true))}));
    return storage;
  }

  user_team::ptr_t team() const {
    return player->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(team_id));
  }

  void flush() {
    CASE_EXPECT_TRUE(
        team_test::run_sync_task(test, "team.flush_dirty", [this](rpc::context& ctx) -> rpc::result_code_type {
          player->send_all_syn_msg(ctx);
          RPC_RETURN_CODE(0);
        }));
  }

  team_test::team_dirty_view view() { return team_test::collect_team_dirty(test, client.session_id(), team_id); }

  void expect_one_remove() {
    auto pushes = team_test::collect_dirty_sync_pushes(test, client.session_id());
    CASE_EXPECT_EQ(1, pushes.size());
    auto result = view();
    CASE_EXPECT_EQ(1, result.removals.size());
    CASE_EXPECT_TRUE(result.snapshots.empty());
    CASE_EXPECT_TRUE(result.actions.empty());
    if (!result.removals.empty()) {
      CASE_EXPECT_EQ(team_test::kZoneId, result.removals.front().zone_id());
      CASE_EXPECT_EQ(team_id, result.removals.front().team_id());
    }
  }

  ~dirty_fixture() {
    if (test.is_running()) {
      CASE_EXPECT_EQ(0, test.stop());
    }
  }
};
}  // namespace

CASE_TEST(lobbysvr_user_team, dirty_snapshot_increment_and_empty_action) {
  dirty_fixture fixture;
  CASE_EXPECT_TRUE(fixture.start(73001));
  if (!fixture.player || !fixture.team()) {
    return;
  }
  CASE_EXPECT_EQ(1, fixture.view().snapshots.size());
  fixture.test.cs().clear_history();

  atfw::team::DTeamAction update;
  *update.mutable_member_update()->mutable_user_key() = team_test::make_user_key(fixture.user_id);
  update.mutable_member_update()->set_client_version("dirty-v2");
  CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.test, fixture.channel, update));
  auto changes = fixture.view();
  CASE_EXPECT_EQ(1, team_test::collect_dirty_sync_pushes(fixture.test, fixture.client.session_id()).size());
  CASE_EXPECT_EQ(1, changes.actions.size());
  CASE_EXPECT_TRUE(changes.snapshots.empty());
  if (!changes.actions.empty()) {
    CASE_EXPECT_EQ(std::string("dirty-v2"), changes.actions.front().action().member_update().client_version());
  }
  fixture.test.cs().clear_history();

  atfw::team::DTeamAction empty;
  CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.test, fixture.channel, empty));
  CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(fixture.test, fixture.client.session_id()).empty());
  fixture.flush();
  CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(fixture.test, fixture.client.session_id()).empty());

  // A real forced snapshot changes data while self remains a member.
  auto replacement = fixture.make_storage();
  replacement.mutable_member(1)->set_client_version("repaired-v3");
  replacement.set_saved_action_sequence(fixture.channel.sequence);
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      fixture.test,
      team_test::make_snapshot_event(fixture.channel.channel_key, 1, fixture.channel.sequence, &replacement, 10)));
  changes = fixture.view();
  CASE_EXPECT_EQ(1, changes.snapshots.size());
  CASE_EXPECT_TRUE(changes.actions.empty());
  if (!changes.snapshots.empty()) {
    const auto* member = team_test::find_snapshot_member(changes.snapshots.front(), fixture.user_id);
    CASE_EXPECT_TRUE(member != nullptr);
    if (member != nullptr) {
      CASE_EXPECT_EQ(std::string("repaired-v3"), member->client_version());
    }
  }
  fixture.flush();
  CASE_EXPECT_EQ(1, team_test::collect_dirty_sync_pushes(fixture.test, fixture.client.session_id()).size());
}

CASE_TEST(lobbysvr_user_team, dirty_member_shared_data_deletion_preserves_key) {
  dirty_fixture fixture;
  CASE_EXPECT_TRUE(fixture.start(73002));
  if (!fixture.player || !fixture.team()) {
    return;
  }
  fixture.test.cs().clear_history();
  atfw::team::DTeamAction action;
  auto* update = action.mutable_member_update();
  *update->mutable_user_key() = team_test::make_user_key(fixture.user_id);
  // Wire contract: key with an empty Any deletes the module; false ready is a distinct stored value.
  update->add_shared_member_data()->set_key(4294967297LL);
  CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.test, fixture.channel, action));
  auto changes = fixture.view();
  CASE_EXPECT_EQ(1, changes.actions.size());
  CASE_EXPECT_TRUE(changes.snapshots.empty());
  if (!changes.actions.empty()) {
    const auto& change = changes.actions.front();
    CASE_EXPECT_EQ(0, change.shared_member_data_size());
    const auto& deleted = change.action().member_update().shared_member_data();
    CASE_EXPECT_EQ(1, deleted.size());
    if (!deleted.empty()) {
      CASE_EXPECT_EQ(4294967297LL, deleted.Get(0).key());
      CASE_EXPECT_TRUE(deleted.Get(0).value().data().type_url().empty());
    }
  }
  PROJECT_NAMESPACE_ID::SCUserGetInfoRsp response;
  CASE_EXPECT_TRUE(team_test::pull_team_data(fixture.test, fixture.player, response));
  CASE_EXPECT_EQ(1, response.user_team().team_size());
  if (response.user_team().team_size() == 1) {
    CASE_EXPECT_TRUE(nullptr == team_test::find_unpacked_member(response.user_team().team(0), fixture.user_id));
  }
}

CASE_TEST(lobbysvr_user_team, dirty_remove_sources_and_late_cleanup_are_idempotent) {
  // Same client-visible terminal state reached through each independent authoritative source.
  for (int source = 0; source < 6; ++source) {
    dirty_fixture fixture;
    CASE_EXPECT_TRUE(fixture.start(static_cast<uint64_t>(73010 + source)));
    CASE_MSG_INFO() << "dirty_remove source=" << source << " team_id=" << fixture.team_id << "\n";
    if (!fixture.player || !fixture.team()) {
      return;
    }
    fixture.test.cs().clear_history();
    if (source == 0) {
      atfw::team::DTeamMemberAction action;
      *action.mutable_remove_member()->mutable_team_key() = team_test::make_team_key(fixture.team_id);
      *action.mutable_remove_member()->mutable_user_key() = team_test::make_user_key(fixture.user_id);
      action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
      CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.test, fixture.personal, action));
      lobbysvr_test::flush_pending_chat_messages(fixture.test);
    } else if (source == 1) {
      atfw::team::DTeamAction action;
      *action.mutable_remove_member()->mutable_team_key() = team_test::make_team_key(fixture.team_id);
      *action.mutable_remove_member()->mutable_user_key() = team_test::make_user_key(fixture.user_id);
      action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
      CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.test, fixture.channel, action));
    } else if (source == 2) {
      atfw::team::DTeamAction action;
      *action.mutable_destroy_team() = team_test::make_team_key(fixture.team_id);
      CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.test, fixture.channel, action));
    } else if (source == 3) {
      atfw::dtmq::DChannelMessage message;
      message.mutable_detail()->mutable_destroy();
      CASE_EXPECT_TRUE(team_test::inject_log_message(fixture.test, fixture.channel, message));
    } else if (source == 4) {
      CASE_EXPECT_TRUE(team_test::receive_channel_event(
          fixture.test, team_test::make_channel_destroyed_event(fixture.channel.channel_key, 1, 1, 0)));
    } else {
      auto storage = fixture.make_storage();
      storage.mutable_member()->RemoveLast();
      CASE_EXPECT_TRUE(team_test::receive_channel_event(
          fixture.test, team_test::make_snapshot_event(fixture.channel.channel_key, 1, 0, &storage, 10)));
    }
    fixture.expect_one_remove();

    // Subsequent collection or a late personal notification must not repeat the remove.
    fixture.test.cs().clear_history();
    atfw::team::DTeamMemberAction late;
    *late.mutable_remove_member()->mutable_team_key() = team_test::make_team_key(fixture.team_id);
    *late.mutable_remove_member()->mutable_user_key() = team_test::make_user_key(fixture.user_id);
    late.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(fixture.test, fixture.personal, late));
    CASE_EXPECT_TRUE(team_test::run_sync_task(
        fixture.test, "team.cleanup_after_remove", [&fixture](rpc::context& ctx) -> rpc::result_code_type {
          fixture.player->get_user_team_manager().refresh_feature_limit_minute(ctx);
          fixture.player->send_all_syn_msg(ctx);
          RPC_RETURN_CODE(0);
        }));
    CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(fixture.test, fixture.client.session_id()).empty());
  }
}

CASE_TEST(lobbysvr_user_team, dirty_cs_exit_sends_remove_and_retry_cannot_extend_timeout) {
  team_test::now_offset_guard time;
  dirty_fixture fixture;
  CASE_EXPECT_TRUE(fixture.start(73020));
  if (!fixture.player || !fixture.team()) {
    return;
  }
  fixture.test.cs().clear_history();
  PROJECT_NAMESPACE_ID::CSTeamExitReq request;
  *request.mutable_team_key() = team_test::make_team_key(fixture.team_id);
  PROJECT_NAMESPACE_ID::SCTeamExitRsp response;
  auto rpc_name = rpc::lobbysvrclientservice::packer::get_full_name_of_team_exit();
  CASE_EXPECT_TRUE(team_test::post_cs_request(fixture.test, fixture.client,
                                              team_test::pack_cs_request(rpc_name, request), rpc_name, response));
  fixture.expect_one_remove();
  fixture.test.cs().clear_history();
  // Deliver no acknowledgement. Several retries cross the original exit deadline.
  auto elapsed = std::chrono::system_clock::duration::zero();
  auto step = team_test::get_exit_retry_interval() + std::chrono::seconds(1);
  while (elapsed <= team_test::get_exit_timeout()) {
    team_test::now_offset_guard::advance(step);
    elapsed += step;
    CASE_EXPECT_TRUE(team_test::run_sync_task(
        fixture.test, "team.retry_exit", [&fixture](rpc::context& ctx) -> rpc::result_code_type {
          fixture.player->get_user_team_manager().refresh_feature_limit_minute(ctx);
          fixture.player->send_all_syn_msg(ctx);
          RPC_RETURN_CODE(0);
        }));
  }
  CASE_EXPECT_TRUE(!fixture.team());
  CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(fixture.test, fixture.client.session_id()).empty());
}

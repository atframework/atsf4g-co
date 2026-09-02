// Copyright 2026 atframework

// Offline regression tests for lobbysvr user_team_manager / user_team (see
// src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md):
// - rejoining a team that is still in the pending-exit queue must restore it as the current team (the previous
//   current team moves into the pending-exit queue) instead of leaking it in the exit queue forever;
// - the per-minute cleanup walks team_group_ with a range-for, so a current team that never became a member must
//   be removed only after the iteration completes (remove_team may erase the group being iterated);
// - a member restored from a channel snapshot must never be treated as wait-to-be-member timeout;
// - member_set_role / team_update refresh the cached role/configure that feed the access-layer permission checks;
// - user_team::dump exports the full cached state with internal routing fields stripped and shared data unpacked;
// - channel incremental actions maintain the caches and project increase dirty pushes to the client;
// - personal-channel receipt events drive the manager-level pending invitation/join-request lists.

#include <string>

#include "lobbysvr_test_user_team_common.h"  // NOLINT: build/include_subdir
namespace {
constexpr int64_t kFirstTeamId = 101;
constexpr int64_t kSecondTeamId = 102;
}  // namespace

// Rejoining a team that is still in the pending-exit queue (fast A->B switch with the exit request in flight, then
// a kJoinedTeam event for A arrives) must restore A as the current team. Leaving A in the exit queue leaks it:
// retry_send_exit_team_request and can_be_removed both early-return once the exit timepoint is reset to zero.
// A restored team must also not retry its stale exit request when the minute refresh runs again.
CASE_TEST(lobbysvr_user_team, rejoin_pending_exit_team_restores_current) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30001;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  auto current_team_id = [&user_inst]() -> int64_t {
    auto current = user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    return current ? current->get_team_key().team_id() : 0;
  };

  // join A, then switch to B: A moves into the pending-exit queue while its exit request is in flight.
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kFirstTeamId));
  CASE_EXPECT_EQ(kFirstTeamId, current_team_id());

  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kSecondTeamId));
  CASE_EXPECT_EQ(kSecondTeamId, current_team_id());
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kFirstTeamId)));

  // The switch must have emitted exactly one exit request for A carrying IN_ANOTHER_TEAM.
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kFirstTeamId, kUserId) >= 1;
  }));
  {
    size_t a_exit_count = team_test::count_remove_member_requests(ss_capture, kFirstTeamId, kUserId);
    CASE_EXPECT_EQ(1, static_cast<int>(a_exit_count));
    for (const auto& req : ss_capture.send_message_reqs) {
      if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == kFirstTeamId) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_IN_ANOTHER_TEAM,
                       req.action().remove_member().remove_member_reason());
      }
    }
  }

  // A is rejoined while its exit request is still in flight: A must become current again and B must move into the
  // pending-exit queue, mirroring the plain switch flow.
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kFirstTeamId));
  CASE_EXPECT_EQ(kFirstTeamId, current_team_id());

  // A is current again; B is still tracked (it moved into the pending-exit queue) and is exiting.
  auto restored = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kFirstTeamId));
  CASE_EXPECT_TRUE(!!restored);
  if (restored) {
    CASE_EXPECT_FALSE(restored->is_exiting());
  }
  auto displaced = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kSecondTeamId));
  CASE_EXPECT_TRUE(!!displaced);
  if (displaced) {
    CASE_EXPECT_TRUE(displaced->is_exiting());
  }

  // The restored team must become a confirmed member (channel snapshot), otherwise the time advance below would
  // legitimately trigger the wait_add_member_timeout cleanup.
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kFirstTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kFirstTeamId, team_storage));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == restored->get_cached_permission_role();
    }));
  }

  // After the exit retry interval the minute refresh retries B's exit, but must never resend A's stale exit.
  {
    team_test::now_offset_guard time_guard;
    team_test::now_offset_guard::advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
    user::ptr_t user_ptr = user_inst;
    CASE_EXPECT_TRUE(
        team_test::run_sync_task(test, "team.refresh_minute", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
          user_ptr->get_user_team_manager().refresh_feature_limit_minute(ctx);
          RPC_RETURN_CODE(0);
        }));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return team_test::count_remove_member_requests(ss_capture, kSecondTeamId, kUserId) >= 1;
    }));
    CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kFirstTeamId, kUserId)));
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
// the per-minute cleanup even past wait_add_member_timeout (configured 5s default), and must not be reported as
// wait-to-be-member timeout. The table round-trip must keep the team.
CASE_TEST(lobbysvr_user_team, minute_refresh_keeps_member_current_team) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30002;
  constexpr int64_t kTeamId = 201;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(team_test::restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  // Team channel snapshot with the user in the member list: the subscriber becomes ready and is_member_ is set.
  // Observable condition: the snapshot resets the cached role from the join-data NORMAL to the storage role.
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));

  // Fast-forward past the configured wait_add_member_timeout: a member is never reported as timeout.
  {
    team_test::now_offset_guard time_guard;
    team_test::now_offset_guard::advance(team_test::get_wait_add_member_timeout() + std::chrono::seconds{1});
    {
      rpc::context ctx{rpc::context::create_without_task()};
      CASE_EXPECT_FALSE(current->wait_to_be_member_but_timeout(ctx));
    }

    user::ptr_t user_ptr = user_inst;
    CASE_EXPECT_TRUE(
        team_test::run_sync_task(test, "team.refresh_minute", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
          user_ptr->get_user_team_manager().refresh_feature_limit_minute(ctx);
          RPC_RETURN_CODE(0);
        }));
  }

  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));

  // Table round-trip keeps the current team and its type.
  PROJECT_NAMESPACE_ID::table_user dumped_table;
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(0, user_inst->get_user_team_manager().dump(ctx, dumped_table));
  }
  CASE_EXPECT_EQ(1, dumped_table.team_data().group_size());
  if (dumped_table.team_data().group_size() > 0) {
    CASE_EXPECT_TRUE(dumped_table.team_data().group(0).has_current());
    CASE_EXPECT_EQ(kTeamId, dumped_table.team_data().group(0).current().team_key().team_id());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL, dumped_table.team_data().group(0).team_type());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// A current team whose channel never reports the user as a member must be removed by the per-minute cleanup once
// the configured wait_add_member_timeout elapses. remove_team may erase the group from team_group_, which the
// cleanup is iterating with a range-for, so the removal must be deferred until after the iteration completes.
// The cleanup-driven removal sends the exit request with reason EXPIRED.
CASE_TEST(lobbysvr_user_team, minute_refresh_removes_never_member_current_team) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30003;
  constexpr int64_t kTeamId = 202;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(team_test::restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  // Team channel snapshot without the user in the member list: ready, but never a member. Observable condition:
  // the snapshot reset the cached role back to GUEST.
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_GUEST == current->get_cached_permission_role();
  }));

  // Fast-forward past the configured wait_add_member_timeout.
  {
    team_test::now_offset_guard time_guard;
    team_test::now_offset_guard::advance(team_test::get_wait_add_member_timeout() + std::chrono::seconds{1});
    {
      rpc::context ctx{rpc::context::create_without_task()};
      CASE_EXPECT_TRUE(current->wait_to_be_member_but_timeout(ctx));
    }

    user::ptr_t user_ptr = user_inst;
    CASE_EXPECT_TRUE(
        team_test::run_sync_task(test, "team.refresh_minute", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
          user_ptr->get_user_team_manager().refresh_feature_limit_minute(ctx);
          RPC_RETURN_CODE(0);
        }));
  }

  // The timeout-driven exit must reach teamsvr-room with reason EXPIRED (channel was ready, not a member, so the
  // manager removes the team immediately; the exit request is still emitted first).
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 1;
  }));
  for (const auto& req : ss_capture.send_message_reqs) {
    if (req.action().has_remove_member()) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXPIRED, req.action().remove_member().remove_member_reason());
    }
  }

  CASE_EXPECT_TRUE(nullptr ==
                   user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(nullptr ==
                   user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));

  // Table no longer carries the team.
  PROJECT_NAMESPACE_ID::table_user dumped_table;
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(0, user_inst->get_user_team_manager().dump(ctx, dumped_table));
  }
  CASE_EXPECT_EQ(0, dumped_table.team_data().group_size());

  CASE_EXPECT_EQ(0, test.stop());
}

// A member_set_role event on the team channel must refresh the subscribing user's cached permission role (only
// when it targets the user), and a team_update event must refresh the cached configure; both feed the access-layer
// permission pre-checks (task_action_team_update_member_role). Unknown members must never gain a ghost cache.
CASE_TEST(lobbysvr_user_team, member_set_role_updates_cached_data) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30004;
  constexpr int64_t kTeamId = 203;
  constexpr uint64_t kUnknownUserId = 91999;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(team_test::restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  // Team channel snapshot with the user as a NORMAL member.
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));
  CASE_EXPECT_FALSE(current->check_permission(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // member_set_role targeting the user: cached role and permission checks are refreshed.
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    protobuf_copy_message(*set_role->mutable_user_key(), team_test::make_user_key(kUserId));
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN == current->get_cached_permission_role();
    }));
    CASE_EXPECT_TRUE(current->check_permission(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));
    CASE_EXPECT_FALSE(current->check_permission(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  }

  // member_set_role targeting another member: cached role is unchanged, the member cache tracks the new role.
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    protobuf_copy_message(*set_role->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      rpc::context ctx{rpc::context::create_without_task()};
      snapshot.Clear();
      current->dump(ctx, snapshot);
      const auto* captain = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
      return nullptr != captain && atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN == captain->role();
    }));
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, current->get_cached_permission_role());
  }

  // member_set_role targeting an unknown user: no ghost member cache is created.
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    protobuf_copy_message(*set_role->mutable_user_key(), team_test::make_user_key(kUnknownUserId));
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
    team_test::pump_rounds(test, 2);
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    current->dump(ctx, snapshot);
    CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kUnknownUserId));
  }

  // team_update with configure: cached configure is refreshed as a whole.
  {
    atfw::team::DTeamAction team_action;
    team_action.mutable_team_update()->mutable_configure()->set_set_member_role_role(
        atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return atfw::team::EN_TEAM_MEMBER_ROLE_OWNER == current->get_configure().set_member_role_role();
    }));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// user_team::dump 导出的快照必须反映频道快照建立的全部缓存状态:
// - 成员列表保留全部业务字段(joined_timepoint/client_version/role/team_source)，按 key 索引的共享成员数据经
//   unpacked_member_data 解包下发，且不下发 user_channel/user_router_server_id/acknowledge_* 等内部字段;
// - 待处理邀请/加入请求中已过期或 key 无效的条目不下发，未过期条目按过期时间升序导出;
// - 队伍共享数据以解包后的模块数据写到 DUserTeamSnapshot.shared_team_data，snapshot.shared_team_data 的
//   原始打包数据不导出;
// - configure 整体随快照导出。
CASE_TEST(lobbysvr_user_team, dump_snapshot_exports_cached_state) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30005;
  constexpr int64_t kTeamId = 204;
  constexpr uint64_t kValidInviteeId = 91001;
  constexpr uint64_t kExpiredInviteeId = 91002;
  constexpr uint64_t kJoinRequesterId = 91003;
  constexpr uint64_t kShortInviteeId = 91004;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(team_test::restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  auto now = std::chrono::system_clock::now();
  const auto captain_joined = now - std::chrono::seconds{3600};

  // 队伍共享数据: 战斗模块 matching=true
  const auto matching_module = team_test::make_team_matching_module(true);
  // 成员共享数据: 队长的 ready=true
  const auto ready_module = team_test::make_member_ready_module(true);

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_storage.mutable_configure()->set_set_member_role_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
  team_storage.mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  {
    // 队长: 全业务字段 + 成员共享数据 + 内部字段污染(导出时必须剥掉内部字段、共享数据经
    // unpacked_member_data 解包下发)
    team_test::storage_member_options captain_options;
    captain_options.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER;
    captain_options.joined_timepoint = captain_joined;
    captain_options.client_version = "captain-client-1.0";
    captain_options.team_source_type = atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND;
    captain_options.shared_member_data.push_back(team_test::pack_member_module(ready_module));
    captain_options.pollute_internal_fields = true;
    auto* captain = team_test::add_storage_member(team_storage, team_test::kCaptainUserId, captain_options);
    // 非模块类型与空数据的条目不进入共享数据缓存，导出时被过滤
    auto* foreign_data = captain->add_shared_member_data();
    foreign_data->set_key(user_team_algorithm::make_team_member_shared_data_key(ready_module) + 1);
    CASE_EXPECT_TRUE(foreign_data->mutable_value()->mutable_data()->PackFrom(team_test::make_team_key(kTeamId + 1)));
    captain->add_shared_member_data();
  }
  {
    team_test::storage_member_options self_options;
    self_options.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
    self_options.joined_timepoint = now - std::chrono::seconds{60};
    self_options.client_version = "self-client-2.0";
    team_test::add_storage_member(team_storage, kUserId, self_options);
  }
  *team_storage.add_shared_team_data() = team_test::pack_team_module(matching_module);
  {
    // 未过期的邀请(携带内部路由字段，导出时必须剥掉)
    team_test::add_storage_invitation(team_storage, kValidInviteeId, now + std::chrono::seconds(300), true);
    // 有效期更短的邀请: 导出时必须排在前面
    team_test::add_storage_invitation(team_storage, kShortInviteeId, now + std::chrono::seconds(120), false);
    // 已过期的邀请: 快照加载时即被过滤，不会出现在导出中
    team_test::add_storage_invitation(team_storage, kExpiredInviteeId, now - std::chrono::seconds(60), false);
    // key 无效的邀请: 快照加载时即被过滤
    auto* invalid = team_storage.add_pending_invitation();
    protobuf_copy_message(*invalid->mutable_team_key(), team_storage.team_key());
    *invalid->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
  }
  {
    // 未过期的加入请求(携带内部路由字段，导出时必须剥掉)
    team_test::add_storage_join_request(team_storage, kJoinRequesterId, now + std::chrono::seconds(300), true);
    // key 无效的加入请求: 快照加载时即被过滤
    auto* invalid = team_storage.add_pending_join_request();
    protobuf_copy_message(*invalid->mutable_team_key(), team_storage.team_key());
    *invalid->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
  }

  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));

  PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
  {
    rpc::context ctx{rpc::context::create_without_task()};
    current->dump(ctx, snapshot);
  }

  CASE_EXPECT_EQ(kTeamId, snapshot.snapshot().team_key().team_id());
  CASE_EXPECT_EQ(team_test::kCaptainUserId, snapshot.snapshot().captain_user_key().user_id());
  // configure 整体导出
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, snapshot.snapshot().configure().set_member_role_role());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, snapshot.snapshot().configure().invite_role());

  // 成员: 队长 + 自己。内部字段必须被剥掉，原始打包的共享成员数据不回填，
  // 解包后的模块数据经 unpacked_member_data 下发
  CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
  for (const auto& member : snapshot.snapshot().member()) {
    team_test::expect_member_projection_clean(member);
  }
  const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
  CASE_EXPECT_TRUE(nullptr != captain_member);
  if (nullptr != captain_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
    CASE_EXPECT_EQ("captain-client-1.0", captain_member->client_version());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, captain_member->team_source_type());
    CASE_EXPECT_EQ(captain_joined, protobuf_to_system_clock(captain_member->joined_timepoint()));
  }
  const auto* self_member = team_test::find_snapshot_member(snapshot, kUserId);
  CASE_EXPECT_TRUE(nullptr != self_member);
  if (nullptr != self_member) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, self_member->role());
    CASE_EXPECT_EQ("self-client-2.0", self_member->client_version());
  }
  const auto* unpacked_captain = team_test::find_unpacked_member(snapshot, team_test::kCaptainUserId);
  CASE_EXPECT_TRUE(nullptr != unpacked_captain);
  if (nullptr != unpacked_captain) {
    CASE_EXPECT_EQ(1, unpacked_captain->shared_member_data_size());
    if (unpacked_captain->shared_member_data_size() > 0) {
      CASE_EXPECT_TRUE(unpacked_captain->shared_member_data(0).battle().ready());
    }
  }
  // 自己没有共享数据: 不产生 unpacked_member_data 条目
  CASE_EXPECT_TRUE(nullptr == team_test::find_unpacked_member(snapshot, kUserId));

  // 邀请: 只剩未过期的两个，按过期时间升序，且剥掉了内部通知频道
  CASE_EXPECT_EQ(2, snapshot.snapshot().pending_invitation_size());
  if (snapshot.snapshot().pending_invitation_size() >= 2) {
    CASE_EXPECT_EQ(kShortInviteeId, snapshot.snapshot().pending_invitation(0).invitee().user_id());
    CASE_EXPECT_EQ(kValidInviteeId, snapshot.snapshot().pending_invitation(1).invitee().user_id());
    for (const auto& invitation : snapshot.snapshot().pending_invitation()) {
      CASE_EXPECT_TRUE(invitation.invitee_private_channel().channel_id().empty());
    }
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

// 频道增量事件必须持续维护本地缓存并通过 SCUserDirtyChgSync 推送增量脏数据给客户端:
// - add_join_request/team_update/add_member/member_update/member_set_role/election_captain 各自更新缓存，
//   缓存内容可通过 user_team::dump 观察;
// - election_captain 同步新旧队长的成员角色与自己的 cached_permission_role_;
// - 过期准入数据由 cleanup_expired_admissions 清理且 dump 不下发;
// - 快照加载后客户端收到 snapshot 脏数据推送; 之后的每个增量动作触发 increase 推送(允许合批，断言语义为
//   动作集合与顺序，不锁死 push 条数)，内部路由字段被剥掉、成员共享数据(含 add_join_request 的
//   member_admission_data)被解包到 OneAction.shared_member_data。
CASE_TEST(lobbysvr_user_team, incremental_actions_update_cache_and_dirty_push) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
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
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  // 绑定 mock 客户端会话，让脏数据推送真正下发到 CS 通道
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));
  if (!client) {
    test.stop();
    return;
  }

  CASE_EXPECT_TRUE(team_test::restore_team_from_table(test, user_inst, kTeamId));
  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  // 快照加载后客户端应收到一条携带完整快照的脏数据推送
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_cached_permission_role());
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    if (!view.snapshots.empty()) {
      CASE_EXPECT_EQ(2, view.snapshots[0].snapshot().member_size());
    }
    CASE_EXPECT_TRUE(view.actions.empty());
  }
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  auto now = std::chrono::system_clock::now();

  // 成员共享数据: ready=true(打包进 DTeamAnyDataWithKey)
  const auto ready_module = team_test::make_member_ready_module(true);

  // 1. add_join_request: 进入待处理加入请求缓存(携带内部字段验证剥除)，
  //    申请人的共享成员数据经 member_admission_data 携带、解包后随动作下发
  {
    atfw::team::DTeamAction team_action;
    auto* join_request = team_action.mutable_add_join_request();
    protobuf_copy_message(*join_request->mutable_requester(), team_test::make_user_key(kJoinRequesterId));
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
    join_request->mutable_requester_private_channel()->set_channel_id("polluted-requester-channel");
    join_request->set_user_router_server_id(0x2222);
    *join_request->add_member_admission_data() = team_test::pack_member_module(ready_module);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 1b. 有效期更短的加入请求: 排序后必须排在更长有效期的条目之前
  {
    atfw::team::DTeamAction team_action;
    auto* join_request = team_action.mutable_add_join_request();
    protobuf_copy_message(*join_request->mutable_requester(), team_test::make_user_key(kShortJoinRequesterId));
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(120));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 1c. 两个不同有效期的邀请
  {
    atfw::team::DTeamAction team_action;
    auto* invitation = team_action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_invitee(), team_test::make_user_key(kLongInviteeId));
    *invitation->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(600));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  {
    atfw::team::DTeamAction team_action;
    auto* invitation = team_action.mutable_add_invitation();
    protobuf_copy_message(*invitation->mutable_invitee(), team_test::make_user_key(kShortInviteeId));
    *invitation->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(60));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 2. team_update: 队伍共享数据 matching=true
  const auto matching_module = team_test::make_team_matching_module(true);
  {
    atfw::team::DTeamAction team_action;
    *team_action.mutable_team_update()->add_shared_team_data() = team_test::pack_team_module(matching_module);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 3. add_member: 新成员携带打包的 ready 共享数据与内部字段
  {
    atfw::team::DTeamAction team_action;
    auto* member = team_action.mutable_add_member();
    protobuf_copy_message(*member->mutable_user_key(), team_test::make_user_key(kMemberUserId));
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(now);
    *member->add_shared_member_data() = team_test::pack_member_module(ready_module);
    member->mutable_user_channel()->set_channel_id("polluted-member-channel");
    member->set_user_router_server_id(0x4444);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 3b. member_update: 队长上报打包的 ready 共享数据，合并进缓存后随快照解包导出
  {
    atfw::team::DTeamAction team_action;
    auto* member_update = team_action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    member_update->set_client_version("test-client-1.0");
    *member_update->add_shared_member_data() = team_test::pack_member_module(ready_module);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 4. member_set_role: 新成员提升为 ADMIN
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    protobuf_copy_message(*set_role->mutable_user_key(), team_test::make_user_key(kMemberUserId));
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 5. election_captain 选自己再选回原队长: 自己的角色 OWNER -> NORMAL，对方 NORMAL -> OWNER
  {
    atfw::team::DTeamAction team_action;
    auto* election = team_action.mutable_election_captain();
    protobuf_copy_message(*election->mutable_user_key(), team_test::make_user_key(kUserId));
    election->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  {
    atfw::team::DTeamAction team_action;
    auto* election = team_action.mutable_election_captain();
    protobuf_copy_message(*election->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    election->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  // 6. 同 key 同过期时间的重复 add_join_request: 原位覆盖(内容更新但不新增条目、不改变顺序)，
  //    重发的加入请求同样携带申请人的准入数据
  {
    atfw::team::DTeamAction team_action;
    auto* join_request = team_action.mutable_add_join_request();
    protobuf_copy_message(*join_request->mutable_requester(), team_test::make_user_key(kJoinRequesterId));
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(now + std::chrono::seconds(300));
    join_request->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    *join_request->add_member_admission_data() = team_test::pack_member_module(ready_module);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 全部 11 个增量动作都到达客户端后统一断言(允许合批，不锁死 push 条数)
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::collect_team_dirty(test, kSessionId, kTeamId).actions.size() >= 11;
  }));

  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_cached_permission_role());
  CASE_EXPECT_EQ(team_test::kCaptainUserId, current->get_cached_captain_user_key().user_id());

  // 每个增量动作都应触发 increase 推送; 内部字段被剥掉，成员共享数据解包后随动作下发
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.snapshots.empty());
    CASE_EXPECT_EQ(11, static_cast<int>(view.actions.size()));
    CASE_EXPECT_EQ(3, static_cast<int>(team_test::count_actions_of_case(
                          view, atfw::team::DTeamAction::kAddJoinRequest)));
    CASE_EXPECT_EQ(
        2, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddInvitation)));
    CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kTeamUpdate)));
    CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddMember)));
    CASE_EXPECT_EQ(1,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate)));
    CASE_EXPECT_EQ(1,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kMemberSetRole)));
    CASE_EXPECT_EQ(
        2, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kElectionCaptain)));

    bool checked_add_member = false;
    bool checked_member_update = false;
    bool checked_add_join_request_shared_data = false;
    for (const auto& one_action : view.actions) {
      const auto& action = one_action.action();
      if (action.has_add_member()) {
        checked_add_member = true;
        CASE_EXPECT_TRUE(action.add_member().user_channel().channel_id().empty());
        CASE_EXPECT_EQ(0, action.add_member().user_router_server_id());
        CASE_EXPECT_EQ(0, action.add_member().acknowledge_action_sequence());
        // 原始打包的成员共享数据不随动作下发(只下发解包后的模块数据)
        CASE_EXPECT_EQ(0, action.add_member().shared_member_data_size());
        CASE_EXPECT_EQ(kMemberUserId, action.add_member().user_key().user_id());
        // 打包的成员共享数据解包后随动作下发
        CASE_EXPECT_EQ(1, one_action.shared_member_data_size());
        if (one_action.shared_member_data_size() > 0) {
          CASE_EXPECT_TRUE(one_action.shared_member_data(0).battle().ready());
        }
      }
      if (action.has_member_update()) {
        checked_member_update = true;
        CASE_EXPECT_EQ(0, action.member_update().shared_member_data_size());
        CASE_EXPECT_EQ(team_test::kCaptainUserId, action.member_update().user_key().user_id());
        CASE_EXPECT_EQ("test-client-1.0", action.member_update().client_version());
        CASE_EXPECT_EQ(1, one_action.shared_member_data_size());
        if (one_action.shared_member_data_size() > 0) {
          CASE_EXPECT_TRUE(one_action.shared_member_data(0).battle().ready());
        }
      }
      if (action.has_add_join_request() &&
          action.add_join_request().requester().user_id() == kJoinRequesterId &&
          action.add_join_request().member_admission_data_size() == 0) {
        // 原始打包的准入数据不随动作下发，申请人的共享成员数据解包后随动作下发
        CASE_EXPECT_TRUE(action.add_join_request().requester_private_channel().channel_id().empty());
        CASE_EXPECT_EQ(0, action.add_join_request().user_router_server_id());
        if (one_action.shared_member_data_size() > 0) {
          checked_add_join_request_shared_data = true;
          CASE_EXPECT_EQ(1, one_action.shared_member_data_size());
          CASE_EXPECT_TRUE(one_action.shared_member_data(0).battle().ready());
        }
      }
      if (action.has_add_invitation()) {
        CASE_EXPECT_TRUE(action.add_invitation().invitee_private_channel().channel_id().empty());
      }
    }
    CASE_EXPECT_TRUE(checked_add_member);
    CASE_EXPECT_TRUE(checked_member_update);
    CASE_EXPECT_TRUE(checked_add_join_request_shared_data);
  }

  // 缓存状态经 dump 可观察: matching、成员角色与待处理加入请求
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    current->dump(ctx, snapshot);

    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    const auto* new_member = team_test::find_snapshot_member(snapshot, kMemberUserId);
    CASE_EXPECT_TRUE(nullptr != new_member);
    if (nullptr != new_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, new_member->role());
      // 快照不再回填原始打包的成员共享数据，解包后的模块数据经 unpacked_member_data 下发
      CASE_EXPECT_EQ(0, new_member->shared_member_data_size());
    }
    const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != captain_member);
    if (nullptr != captain_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
      CASE_EXPECT_EQ("test-client-1.0", captain_member->client_version());
    }
    const auto* unpacked_member = team_test::find_unpacked_member(snapshot, kMemberUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked_member);
    if (nullptr != unpacked_member && unpacked_member->shared_member_data_size() > 0) {
      CASE_EXPECT_TRUE(unpacked_member->shared_member_data(0).battle().ready());
    }
    const auto* unpacked_captain = team_test::find_unpacked_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked_captain);
    if (nullptr != unpacked_captain && unpacked_captain->shared_member_data_size() > 0) {
      CASE_EXPECT_TRUE(unpacked_captain->shared_member_data(0).battle().ready());
    }

    // 准入数据按过期时间升序下发: 短有效期的加入请求/邀请排在前面
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_join_request_size());
    if (snapshot.snapshot().pending_join_request_size() >= 2) {
      CASE_EXPECT_EQ(kShortJoinRequesterId, snapshot.snapshot().pending_join_request(0).requester().user_id());
      CASE_EXPECT_EQ(kJoinRequesterId, snapshot.snapshot().pending_join_request(1).requester().user_id());
      // 同 key 同过期时间的重复 add_join_request 原位覆盖: 内容已更新为 FRIEND
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND,
                     snapshot.snapshot().pending_join_request(1).team_source_type());
      // 快照的待处理加入请求保留打包的准入数据(增量路径才解包到 OneAction.shared_member_data)
      CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request(1).member_admission_data_size());
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
  {
    team_test::now_offset_guard time_guard;
    team_test::now_offset_guard::advance(std::chrono::seconds(150));
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

    // 继续快进到 300s 有效期的加入请求也过期: 只剩最长有效期的邀请
    team_test::now_offset_guard::advance(std::chrono::seconds(250));
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
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// The personal-channel receipt events drive the manager-level pending admission lists:
// invited/apply_join_request populate them with the full room payload (expired entries filtered, events for
// other users ignored), reject_invitation/reject_join_request remove them. apply_join_request is sent by
// teamsvr-room apply_add_join_request as the requester's receipt.
CASE_TEST(lobbysvr_user_team, member_events_manage_pending_admissions) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 31001;
  constexpr uint64_t kOtherUserId = 31002;
  constexpr int64_t kInviteTeamId = 8101;
  constexpr int64_t kJoinTeamId = 8102;
  constexpr int64_t kExpiredTeamId = 8103;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  auto now = std::chrono::system_clock::now();
  auto valid_expiry = [&now]() { return now + std::chrono::seconds{60}; };

  // invited -> 待处理邀请入列，完整字段(含 room 填充的 PUBLIC admission 数据)逐项保存
  const auto ready_module = team_test::make_member_ready_module(true);
  {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(kInviteTeamId));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kUserId));
    protobuf_copy_message(*invited->mutable_invitee_private_channel(), private_channel_key);
    *invited->mutable_start_timepoint() = protobuf_from_system_clock(now);
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(valid_expiry());
    invited->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    // room 在 invited 通知中重填全部 PUBLIC admission 数据(见 USER_TEAM_TEST_PLAN §2.2)
    *invited->add_team_admission_data() = team_test::pack_team_module(team_test::make_team_matching_module(false));
    auto* member_admission = invited->add_member_admission_data();
    protobuf_copy_message(*member_admission->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    *member_admission->add_member_admission_data() = team_test::pack_member_module(ready_module);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kInviteTeamId));
  }));
  auto invitation = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kInviteTeamId));
  CASE_EXPECT_TRUE(!!invitation);
  if (invitation) {
    CASE_EXPECT_EQ(kUserId, invitation->invitee().user_id());
    CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation->inviter().user_id());
    CASE_EXPECT_EQ(kInviteTeamId, invitation->team_key().team_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), invitation->invitee_private_channel().channel_id());
    CASE_EXPECT_EQ(protobuf_to_system_clock(invitation->start_timepoint()), now);
    CASE_EXPECT_EQ(protobuf_to_system_clock(invitation->expired_timepoint()), valid_expiry());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, invitation->team_source_type());
    CASE_EXPECT_EQ(1, invitation->team_admission_data_size());
    CASE_EXPECT_EQ(1, invitation->member_admission_data_size());
    if (invitation->member_admission_data_size() > 0) {
      CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation->member_admission_data(0).user_key().user_id());
      CASE_EXPECT_EQ(1, invitation->member_admission_data(0).member_admission_data_size());
    }
  }

  // 非本人 invitee 的通知直接忽略
  {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(kInviteTeamId + 100));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kOtherUserId));
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(valid_expiry());
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
    // 非本人事件必须被忽略: 以私有频道已处理序号确认事件已处理完毕, 再断言未入列
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
             private_chain.sequence;
    }));
  }
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kInviteTeamId + 100)));

  // apply_join_request -> 待处理加入请求入列(房间受理回执)，完整字段逐项保存
  {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), team_test::make_team_key(kJoinTeamId));
    protobuf_copy_message(*applied->mutable_requester(), team_test::make_user_key(kUserId));
    protobuf_copy_message(*applied->mutable_requester_private_channel(), private_channel_key);
    *applied->mutable_expired_timepoint() = protobuf_from_system_clock(valid_expiry());
    applied->set_client_version("joiner-client-3.0");
    applied->set_user_router_server_id(0x11000001);
    applied->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    *applied->add_member_admission_data() = team_test::pack_member_module(ready_module);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kJoinTeamId));
  }));
  auto join_request =
      user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kJoinTeamId));
  CASE_EXPECT_TRUE(!!join_request);
  if (join_request) {
    CASE_EXPECT_EQ(kUserId, join_request->requester().user_id());
    CASE_EXPECT_EQ(kJoinTeamId, join_request->team_key().team_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), join_request->requester_private_channel().channel_id());
    CASE_EXPECT_EQ(protobuf_to_system_clock(join_request->expired_timepoint()), valid_expiry());
    CASE_EXPECT_EQ("joiner-client-3.0", join_request->client_version());
    CASE_EXPECT_EQ(0x11000001, static_cast<int>(join_request->user_router_server_id()));
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, join_request->team_source_type());
    CASE_EXPECT_EQ(1, join_request->member_admission_data_size());
  }

  // 非本人 requester 的回执直接忽略
  {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), team_test::make_team_key(kJoinTeamId + 100));
    protobuf_copy_message(*applied->mutable_requester(), team_test::make_user_key(kOtherUserId));
    *applied->mutable_expired_timepoint() = protobuf_from_system_clock(valid_expiry());
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
             private_chain.sequence;
    }));
  }
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kJoinTeamId + 100)));

  // 已过期的申请回执直接过滤，不入列
  {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), team_test::make_team_key(kExpiredTeamId));
    protobuf_copy_message(*applied->mutable_requester(), team_test::make_user_key(kUserId));
    *applied->mutable_expired_timepoint() =
        protobuf_from_system_clock(std::chrono::system_clock::now() - std::chrono::seconds{1});
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
             private_chain.sequence;
    }));
  }
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kExpiredTeamId)));

  // reject_invitation -> 待处理邀请出列
  {
    atfw::team::DTeamMemberAction action;
    auto* rejected = action.mutable_reject_invitation();
    protobuf_copy_message(*rejected->mutable_team_key(), team_test::make_team_key(kInviteTeamId));
    protobuf_copy_message(*rejected->mutable_invitee(), team_test::make_user_key(kUserId));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kInviteTeamId));
  }));

  // reject_join_request -> 待处理加入请求出列
  {
    atfw::team::DTeamMemberAction action;
    auto* rejected = action.mutable_reject_join_request();
    protobuf_copy_message(*rejected->mutable_team_key(), team_test::make_team_key(kJoinTeamId));
    protobuf_copy_message(*rejected->mutable_requester(), team_test::make_user_key(kUserId));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kJoinTeamId));
  }));

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-09(§2.2/§6.6): table 持久化自己的 pending 邀请/加入请求并跨重启(dump/init)恢复;
// 恢复时过滤已过期和无效 team_id 的条目, 当前队伍同 team_key 的 pending 由 add_team 恢复时清理。
CASE_TEST(lobbysvr_user_team, table_roundtrip_restores_pending_admissions) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30041;
  constexpr uint64_t kRestoredUserId = 30042;
  constexpr int64_t kCurrentTeamId = 900;
  constexpr int64_t kInviteTeamA = 901;
  constexpr int64_t kInviteTeamB = 902;
  constexpr int64_t kJoinTeamId = 903;
  constexpr int64_t kExpiringTeamId = 904;
  constexpr uint64_t kInviterA = 91021;
  constexpr uint64_t kInviterB = 91022;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  const auto now = std::chrono::system_clock::now();
  const auto long_expiry = now + std::chrono::seconds(300);
  const auto short_expiry = now + std::chrono::seconds(10);

  // 建立 pending: 两条长期邀请 + 一条加入请求 + 当前队伍的邀请 + 一条即将过期的邀请
  auto inject_invited = [&](int64_t team_id, uint64_t inviter_id, std::chrono::system_clock::time_point expiry) {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(inviter_id));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kUserId));
    *invited->mutable_start_timepoint() = protobuf_from_system_clock(now);
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  };
  inject_invited(kInviteTeamA, kInviterA, long_expiry);
  inject_invited(kInviteTeamB, kInviterB, long_expiry);
  inject_invited(kCurrentTeamId, kInviterA, long_expiry);
  inject_invited(kExpiringTeamId, kInviterA, short_expiry);
  {
    atfw::team::DTeamMemberAction action;
    auto* join_request = action.mutable_apply_join_request();
    protobuf_copy_message(*join_request->mutable_team_key(), team_test::make_team_key(kJoinTeamId));
    protobuf_copy_message(*join_request->mutable_requester(), team_test::make_user_key(kUserId));
    *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(long_expiry);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto& mgr = user_inst->get_user_team_manager();
    return !!mgr.get_pending_invitation(team_test::make_team_key(kInviteTeamA)) &&
           !!mgr.get_pending_invitation(team_test::make_team_key(kInviteTeamB)) &&
           !!mgr.get_pending_invitation(team_test::make_team_key(kCurrentTeamId)) &&
           !!mgr.get_pending_invitation(team_test::make_team_key(kExpiringTeamId)) &&
           !!mgr.get_pending_join_request(team_test::make_team_key(kJoinTeamId));
  }));

  // dump 到 table, 并模拟: 当前队伍成员关系 + 一条无效 team_id 的邀请(历史脏数据)
  PROJECT_NAMESPACE_ID::table_user table;
  int32_t dump_ret = -1;
  CASE_EXPECT_TRUE(team_test::run_sync_task(test, "team.dump_table", [&](rpc::context& ctx) -> rpc::result_code_type {
    dump_ret = user_inst->get_user_team_manager().dump(ctx, table);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_EQ(0, dump_ret);
  auto* group = table.mutable_team_data()->add_group();
  CASE_EXPECT_TRUE(nullptr != group);
  group->set_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
  protobuf_copy_message(*group->mutable_current(), team_test::make_join_data(kUserId, kCurrentTeamId));
  auto* bogus = table.mutable_team_data()->add_pending_invitation();
  CASE_EXPECT_TRUE(nullptr != bogus);
  bogus->mutable_team_key()->set_zone_id(team_test::kZoneId);
  bogus->mutable_team_key()->set_team_id(0);
  *bogus->mutable_expired_timepoint() = protobuf_from_system_clock(long_expiry);

  // 快进到短有效期邀请过期之后
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds(20));
  atfw::util::time::time_utility::update();

  // 恢复到全新用户(模拟重启/迁移)
  auto restored_user = user::create(kRestoredUserId, team_test::kZoneId, "team-test-user-restored");
  CASE_EXPECT_TRUE(!!restored_user);
  if (restored_user) {
    CASE_EXPECT_TRUE(team_test::run_sync_task(
        test, "team.restore_table", [&](rpc::context& ctx) -> rpc::result_code_type {
      restored_user->get_user_team_manager().init_from_table_data(ctx, table);
      RPC_RETURN_CODE(0);
    }));

    auto& restored_mgr = restored_user->get_user_team_manager();
    // 长期邀请/加入请求完整恢复(全字段逐项断言)
    auto invitation_a = restored_mgr.get_pending_invitation(team_test::make_team_key(kInviteTeamA));
    CASE_EXPECT_TRUE(!!invitation_a);
    if (invitation_a) {
      CASE_EXPECT_EQ(team_test::kZoneId, invitation_a->team_key().zone_id());
      CASE_EXPECT_EQ(kInviteTeamA, invitation_a->team_key().team_id());
      CASE_EXPECT_EQ(kInviterA, invitation_a->inviter().user_id());
      CASE_EXPECT_EQ(team_test::kZoneId, invitation_a->inviter().zone_id());
      CASE_EXPECT_EQ(kUserId, invitation_a->invitee().user_id());
      CASE_EXPECT_EQ(protobuf_from_system_clock(long_expiry).seconds(), invitation_a->expired_timepoint().seconds());
    }
    CASE_EXPECT_TRUE(!!restored_mgr.get_pending_invitation(team_test::make_team_key(kInviteTeamB)));
    auto join_request = restored_mgr.get_pending_join_request(team_test::make_team_key(kJoinTeamId));
    CASE_EXPECT_TRUE(!!join_request);
    if (join_request) {
      CASE_EXPECT_EQ(kUserId, join_request->requester().user_id());
      CASE_EXPECT_EQ(protobuf_from_system_clock(long_expiry).seconds(), join_request->expired_timepoint().seconds());
    }

    // 已过期/无效 team_id 的条目被过滤; 当前队伍同 team_key 的 pending 被 add_team 清理
    CASE_EXPECT_FALSE(!!restored_mgr.get_pending_invitation(team_test::make_team_key(kExpiringTeamId)));
    CASE_EXPECT_FALSE(!!restored_mgr.get_pending_invitation(team_test::make_team_key(0)));
    CASE_EXPECT_FALSE(!!restored_mgr.get_pending_invitation(team_test::make_team_key(kCurrentTeamId)));
    CASE_EXPECT_TRUE(!!restored_mgr.get_team_by_team_key(team_test::make_team_key(kCurrentTeamId)));
  }

  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  CASE_EXPECT_EQ(0, test.stop());
}

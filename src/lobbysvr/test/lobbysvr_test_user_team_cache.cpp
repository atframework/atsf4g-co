// Copyright 2026 atframework
//
// Offline regression tests for lobbysvr user_team channel-event cache semantics
// (plan: src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md §5.1).
//
// Covered here:
// - ROLE-02: election_captain role resolution must mirror teamsvr-room change_captain.
// - MEM-02: member_update full-field cache update and dirty projection (plan §2.3 member_update row).
// - MEM-04: member_update rejection paths must create neither ghost cache nor dirty push.
// - ROLE-01: member_set_role full-payload cache update and dirty projection (standalone case).
// - TEAM-01: team_update configure replace / shared-data key merge+removal and snapshot admission filtering.

#include <string>

#include "lobbysvr_test_user_team_common.h" // NOLINT: build/include_subdir

namespace {

// Dump the cached member list of the given team for role assertions (business-key lookup, order independent).
bool dump_team_snapshot(user_team& team, PROJECT_NAMESPACE_ID::DUserTeamSnapshot& output) {
  rpc::context ctx{rpc::context::create_without_task()};
  team.dump(ctx, output);
  return true;
}

// Find a pending invitation/join request in a dumped snapshot by business key (expired-time ascending list).
const atfw::team::DTeamInvitation* find_pending_invitation(const PROJECT_NAMESPACE_ID::DUserTeamSnapshot& snapshot,
                                                           uint64_t invitee_id) {
  for (const auto& invitation : snapshot.snapshot().pending_invitation()) {
    if (invitation.invitee().user_id() == invitee_id) {
      return &invitation;
    }
  }
  return nullptr;
}

const atfw::team::DTeamJoinRequest* find_pending_join_request(
    const PROJECT_NAMESPACE_ID::DUserTeamSnapshot& snapshot, uint64_t requester_id) {
  for (const auto& join_request : snapshot.snapshot().pending_join_request()) {
    if (join_request.requester().user_id() == requester_id) {
      return &join_request;
    }
  }
  return nullptr;
}

}  // namespace

// MEM-02: member_update 后成员缓存与 dirty 投影的完整契约:
// - 已存在成员的非空 client_version 覆盖、空版本不回退; role/joined_timepoint/team_source_type 等业务字段不被
//   member_update 改动; 共享成员数据按 key 覆盖(battle.ready false -> true), 未携带的模块 key 保留;
// - dirty increase 的 member_update 只保留 user_key(zone_id/user_id)/client_version, 剥离内部路由字段与原始
//   打包共享数据, 解包后的模块数据随 OneAction.shared_member_data 下发;
// - 队伍级 pending invitation/join request 缓存(含 join request 的 member_admission_data)不受 member_update
//   影响, 且 admission 不随 member_update 的 dirty payload 下发(快照投影中私有频道/router 已裁剪)。
CASE_TEST(lobbysvr_user_team, member_update_full_fields_and_dirty_projection) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30021;
  constexpr int64_t kTeamId = 302;
  constexpr uint64_t kSessionId = 0x10021;
  constexpr uint64_t kThirdMemberId = 91021;
  constexpr uint64_t kInviteeId = 91022;
  constexpr uint64_t kJoinRequesterId = 91023;

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
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  const auto now = std::chrono::system_clock::now();
  const auto third_joined_timepoint = now - std::chrono::seconds(60);

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(
      team_storage, team_test::kCaptainUserId,
      team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER)
          .set_client_version("capt-v1")
          .set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)
          .set_shared_member_data({team_test::pack_member_module(
              team_test::make_member_ready_module(false))}));
  team_test::add_storage_member(team_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("self-v1"));
  team_test::add_storage_member(
      team_storage, kThirdMemberId,
      team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
          .set_joined_timepoint(third_joined_timepoint)
          .set_client_version("third-v1")
          .set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH)
          .set_shared_member_data({team_test::pack_member_module(
              team_test::make_member_ready_module(false))}));
  team_test::add_storage_invitation(team_storage, kInviteeId, now + std::chrono::seconds(300), true);
  auto* storage_join_request =
      team_test::add_storage_join_request(team_storage, kJoinRequesterId, now + std::chrono::seconds(300), true);
  *storage_join_request->add_member_admission_data() =
      team_test::pack_member_module(team_test::make_member_ready_module(true));

  // 先绑定会话再应用快照, 快照脏推送才会被捕获
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));
  if (!client) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kThirdMemberId) &&
           1 == snapshot.snapshot().pending_invitation_size() &&
           1 == snapshot.snapshot().pending_join_request_size() &&
           !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));

  // 快照投影: pending invitation/join request 的私有频道与 router 已裁剪, join request 保留打包的 admission 数据
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    if (!view.snapshots.empty()) {
      const auto& pushed = view.snapshots[0];
      const auto* invitation = find_pending_invitation(pushed, kInviteeId);
      CASE_EXPECT_TRUE(nullptr != invitation);
      if (nullptr != invitation) {
        CASE_EXPECT_TRUE(invitation->invitee_private_channel().channel_id().empty());
        CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation->inviter().user_id());
      }
      const auto* pending_join = find_pending_join_request(pushed, kJoinRequesterId);
      CASE_EXPECT_TRUE(nullptr != pending_join);
      if (nullptr != pending_join) {
        CASE_EXPECT_TRUE(pending_join->requester_private_channel().channel_id().empty());
        CASE_EXPECT_EQ(0, pending_join->user_router_server_id());
        CASE_EXPECT_EQ(1, pending_join->member_admission_data_size());
      }
    }
  }
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. third 成员: 非空 client_version 覆盖 + 共享数据同 key 覆盖(ready false -> true); 内部路由字段被剥离
  {
    atfw::team::DTeamAction team_action;
    auto* member_update = team_action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kThirdMemberId));
    member_update->set_client_version("third-v2");
    *member_update->add_shared_member_data() =
        team_test::pack_member_module(team_test::make_member_ready_module(true));
    member_update->mutable_user_channel()->set_channel_id("polluted-update-channel");
    member_update->set_user_router_server_id(0x6666);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  // 2. third 成员: 空 client_version 不回退, 不携带共享数据时已有 key 保留
  {
    atfw::team::DTeamAction team_action;
    auto* member_update = team_action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kThirdMemberId));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  // 3. captain: 只更新 client_version, 其已有共享数据(ready=false)保留
  {
    atfw::team::DTeamAction team_action;
    auto* member_update = team_action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    member_update->set_client_version("capt-v2");
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }

  // 三条 member_update 增量都到达客户端后统一断言(允许合批, 不锁死 push 条数)
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::collect_team_dirty(test, kSessionId, kTeamId).actions.size() >= 3;
  }));

  // 缓存终态: member_data 业务字段逐项断言
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());

    const auto* third_member = team_test::find_snapshot_member(snapshot, kThirdMemberId);
    CASE_EXPECT_TRUE(nullptr != third_member);
    if (nullptr != third_member) {
      CASE_EXPECT_EQ(team_test::kZoneId, third_member->user_key().zone_id());
      CASE_EXPECT_EQ(kThirdMemberId, third_member->user_key().user_id());
      CASE_EXPECT_EQ("third-v2", third_member->client_version());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, third_member->role());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, third_member->team_source_type());
      // member_update 不得改动入队时间
      CASE_EXPECT_TRUE(third_joined_timepoint == protobuf_to_system_clock(third_member->joined_timepoint()));
      team_test::expect_member_projection_clean(*third_member);
    }
    const auto* unpacked_third = team_test::find_unpacked_member(snapshot, kThirdMemberId);
    CASE_EXPECT_TRUE(nullptr != unpacked_third);
    if (nullptr != unpacked_third) {
      CASE_EXPECT_EQ(1, unpacked_third->shared_member_data_size());
      if (unpacked_third->shared_member_data_size() > 0) {
        CASE_EXPECT_TRUE(unpacked_third->shared_member_data(0).battle().ready());
      }
    }

    const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != captain_member);
    if (nullptr != captain_member) {
      CASE_EXPECT_EQ("capt-v2", captain_member->client_version());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, captain_member->team_source_type());
    }
    const auto* unpacked_captain = team_test::find_unpacked_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked_captain);
    if (nullptr != unpacked_captain) {
      CASE_EXPECT_EQ(1, unpacked_captain->shared_member_data_size());
      if (unpacked_captain->shared_member_data_size() > 0) {
        // 未携带共享数据的 member_update 不影响已有 key
        CASE_EXPECT_FALSE(unpacked_captain->shared_member_data(0).battle().ready());
      }
    }

    const auto* self_member = team_test::find_snapshot_member(snapshot, kUserId);
    CASE_EXPECT_TRUE(nullptr != self_member);
    if (nullptr != self_member) {
      CASE_EXPECT_EQ("self-v1", self_member->client_version());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, self_member->role());
    }

    // member_update 不影响队伍级 pending invitation/join request 缓存(含 join request 的 admission 数据)
    const auto* invitation = find_pending_invitation(snapshot, kInviteeId);
    CASE_EXPECT_TRUE(nullptr != invitation);
    if (nullptr != invitation) {
      CASE_EXPECT_EQ(protobuf_from_system_clock(now + std::chrono::seconds(300)).seconds(),
                     invitation->expired_timepoint().seconds());
      CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation->inviter().user_id());
    }
    const auto* pending_join = find_pending_join_request(snapshot, kJoinRequesterId);
    CASE_EXPECT_TRUE(nullptr != pending_join);
    if (nullptr != pending_join) {
      CASE_EXPECT_EQ(1, pending_join->member_admission_data_size());
      CASE_EXPECT_EQ(protobuf_from_system_clock(now + std::chrono::seconds(300)).seconds(),
                     pending_join->expired_timepoint().seconds());
    }
  }

  // dirty increase: 恰好三条 member_update, 无 admission 动作, 无快照重发; admission 对客户端不可见
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.snapshots.empty());
    CASE_EXPECT_EQ(3, static_cast<int>(view.actions.size()));
    CASE_EXPECT_EQ(3, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate)));
    CASE_EXPECT_EQ(0,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddInvitation)));
    CASE_EXPECT_EQ(
        0, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kApproveInvitation)));
    CASE_EXPECT_EQ(
        0, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kRejectInvitation)));
    CASE_EXPECT_EQ(
        0, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddJoinRequest)));
    CASE_EXPECT_EQ(
        0, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kApproveJoinRequest)));
    CASE_EXPECT_EQ(
        0, static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kRejectJoinRequest)));

    const auto updates = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate);
    CASE_EXPECT_EQ(3, static_cast<int>(updates.size()));
    if (updates.size() >= 3) {
      // #1: user_key/client_version 保留, 内部路由字段剥离, 原始打包共享数据不下发,
      //     解包后的模块数据随 OneAction.shared_member_data 下发
      const auto& first = updates[0]->action().member_update();
      CASE_EXPECT_EQ(team_test::kZoneId, first.user_key().zone_id());
      CASE_EXPECT_EQ(kThirdMemberId, first.user_key().user_id());
      CASE_EXPECT_EQ("third-v2", first.client_version());
      CASE_EXPECT_TRUE(first.user_channel().channel_id().empty());
      CASE_EXPECT_EQ(0, first.user_router_server_id());
      CASE_EXPECT_EQ(0, first.shared_member_data_size());
      CASE_EXPECT_EQ(1, updates[0]->shared_member_data_size());
      if (updates[0]->shared_member_data_size() > 0) {
        CASE_EXPECT_TRUE(updates[0]->shared_member_data(0).battle().ready());
      }
      // #2: 空版本原样透传, 无共享数据
      const auto& second = updates[1]->action().member_update();
      CASE_EXPECT_EQ(kThirdMemberId, second.user_key().user_id());
      CASE_EXPECT_TRUE(second.client_version().empty());
      CASE_EXPECT_EQ(0, second.shared_member_data_size());
      CASE_EXPECT_EQ(0, updates[1]->shared_member_data_size());
      // #3: captain 版本更新
      const auto& third = updates[2]->action().member_update();
      CASE_EXPECT_EQ(team_test::kZoneId, third.user_key().zone_id());
      CASE_EXPECT_EQ(team_test::kCaptainUserId, third.user_key().user_id());
      CASE_EXPECT_EQ("capt-v2", third.client_version());
      CASE_EXPECT_EQ(0, updates[2]->shared_member_data_size());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// MEM-04: member_update 的拒绝路径: 频道 member_update 事件指向不存在(或已移除)的成员时, 不得建立幽灵成员
// 缓存, 也不得产生任何 dirty 推送(客户端不知道的成员不应收到其更新; 与 plan §2.3 "未知成员不创建幽灵缓存"
// 及客户端投影一致性要求一致)。
CASE_TEST(lobbysvr_user_team, member_update_rejected_without_cache_or_dirty_push) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30022;
  constexpr int64_t kTeamId = 303;
  constexpr uint64_t kSessionId = 0x10022;
  constexpr uint64_t kOtherMemberId = 91031;
  constexpr uint64_t kUnknownUserId = 91032;

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
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

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
  team_test::add_storage_member(team_storage, kOtherMemberId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("other-v1"));

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));
  if (!client) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 3 == snapshot.snapshot().member_size() &&
           !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // (a) 不存在的成员: 携带 client_version 与共享数据的 member_update 不得建立缓存、不得产生 dirty 推送
  {
    atfw::team::DTeamAction team_action;
    auto* member_update = team_action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kUnknownUserId));
    member_update->set_client_version("ghost-v1");
    *member_update->add_shared_member_data() =
        team_test::pack_member_module(team_test::make_member_ready_module(true));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  // 纯稳定性等待: 事件已经同步入口处理, 多 pump 只为耗尽潜在的异步尾巴
  team_test::pump_rounds(test, 4);
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kUnknownUserId));
    CASE_EXPECT_EQ(0, snapshot.unpacked_member_data_size());
  }
  CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(test, kSessionId).empty());
  CASE_EXPECT_EQ(0, static_cast<int>(test.cs().call_count()));

  // (b) 先移除成员, 迟到的 member_update 不得重建幽灵缓存、不得产生 dirty 推送
  {
    atfw::team::DTeamAction team_action;
    auto* remove_data = team_action.mutable_remove_member();
    protobuf_copy_message(*remove_data->mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*remove_data->mutable_user_key(), team_test::make_user_key(kOtherMemberId));
    remove_data->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  // remove_member 的 increase 是契约要求(可观察条件), 到达后再注入迟到更新
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_actions_of_case(team_test::collect_team_dirty(test, kSessionId, kTeamId),
                                            atfw::team::DTeamAction::kRemoveMember) >= 1;
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
  }
  test.cs().clear_history();

  {
    atfw::team::DTeamAction team_action;
    auto* member_update = team_action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kOtherMemberId));
    member_update->set_client_version("late-v2");
    *member_update->add_shared_member_data() =
        team_test::pack_member_module(team_test::make_member_ready_module(true));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  team_test::pump_rounds(test, 4);
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kOtherMemberId));
    // 其他成员的缓存不被污染
    const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != captain_member);
    if (nullptr != captain_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
    }
    const auto* self_member = team_test::find_snapshot_member(snapshot, kUserId);
    CASE_EXPECT_TRUE(nullptr != self_member);
  }
  CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(test, kSessionId).empty());
  CASE_EXPECT_EQ(0, static_cast<int>(test.cs().call_count()));

  CASE_EXPECT_EQ(0, test.stop());
}

// ROLE-01: member_set_role 更新缓存(独立完整 payload 用例, 不依赖 create/election 的附带断言):
// - 更新他人: cached_members_ 中目标成员 role 更新, 自己的 cached_permission_role_ 与队长不变;
// - 更新自己: cached_permission_role_ 同步, 权限判断随事件改变;
// - dirty increase 的 member_set_role action 完整字段(user_key 的 zone_id/user_id、role)逐项断言;
// - 队长 user_key(zone_id/user_id)与队长成员 role 全程不变。
CASE_TEST(lobbysvr_user_team, member_set_role_full_payload_update_cache) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30023;
  constexpr int64_t kTeamId = 304;
  constexpr uint64_t kSessionId = 0x10023;
  constexpr uint64_t kThirdMemberId = 91041;

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
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER)
                                    .set_client_version("capt-v1"));
  team_test::add_storage_member(team_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("self-v1"));
  team_test::add_storage_member(team_storage, kThirdMemberId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("third-v1"));

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));
  if (!client) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role() &&
           !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. 更新他人: third NORMAL -> ADMIN
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    protobuf_copy_message(*set_role->mutable_user_key(), team_test::make_user_key(kThirdMemberId));
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* third_member = team_test::find_snapshot_member(snapshot, kThirdMemberId);
    return nullptr != third_member && atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN == third_member->role();
  }));
  // 自己的权限角色与队长不受影响
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_cached_permission_role());
  CASE_EXPECT_EQ(team_test::kZoneId, current->get_cached_captain_user_key().zone_id());
  CASE_EXPECT_EQ(team_test::kCaptainUserId, current->get_cached_captain_user_key().user_id());
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(team_test::kZoneId, snapshot.snapshot().captain_user_key().zone_id());
    CASE_EXPECT_EQ(team_test::kCaptainUserId, snapshot.snapshot().captain_user_key().user_id());
    const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != captain_member);
    if (nullptr != captain_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
    }
  }

  // 2. 更新自己: self NORMAL -> ADMIN, cached_permission_role_ 与权限判断同步
  {
    atfw::team::DTeamAction team_action;
    auto* set_role = team_action.mutable_member_set_role();
    protobuf_copy_message(*set_role->mutable_user_key(), team_test::make_user_key(kUserId));
    set_role->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN == current->get_cached_permission_role();
  }));
  CASE_EXPECT_TRUE(current->check_permission(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));
  CASE_EXPECT_FALSE(current->check_permission(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* self_member = team_test::find_snapshot_member(snapshot, kUserId);
    CASE_EXPECT_TRUE(nullptr != self_member);
    if (nullptr != self_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, self_member->role());
      // 其他业务字段不被 member_set_role 改动
      CASE_EXPECT_EQ("self-v1", self_member->client_version());
    }
    const auto* third_member = team_test::find_snapshot_member(snapshot, kThirdMemberId);
    CASE_EXPECT_TRUE(nullptr != third_member);
    if (nullptr != third_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, third_member->role());
    }
    const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != captain_member);
    if (nullptr != captain_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
    }
  }

  // dirty increase: 恰好两条 member_set_role, 按到达顺序完整字段断言
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_actions_of_case(team_test::collect_team_dirty(test, kSessionId, kTeamId),
                                            atfw::team::DTeamAction::kMemberSetRole) >= 2;
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.snapshots.empty());
    CASE_EXPECT_EQ(2, static_cast<int>(view.actions.size()));
    const auto role_actions = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kMemberSetRole);
    CASE_EXPECT_EQ(2, static_cast<int>(role_actions.size()));
    if (role_actions.size() >= 2) {
      const auto& first = role_actions[0]->action().member_set_role();
      CASE_EXPECT_EQ(team_test::kZoneId, first.user_key().zone_id());
      CASE_EXPECT_EQ(kThirdMemberId, first.user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, first.role());
      CASE_EXPECT_EQ(0, role_actions[0]->shared_member_data_size());

      const auto& second = role_actions[1]->action().member_set_role();
      CASE_EXPECT_EQ(team_test::kZoneId, second.user_key().zone_id());
      CASE_EXPECT_EQ(kUserId, second.user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, second.role());
      CASE_EXPECT_EQ(0, role_actions[1]->shared_member_data_size());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// TEAM-01: team_update 更新共享数据(独立用例):
// - 快照加载过滤已过期/无效 key 的 admission, dirty 快照 payload 内断言其缺席, 有效项完整下发;
// - team_update 的 configure 整体覆盖(仅携带时), 未携带时保持不变; 完整七字段逐项断言;
// - shared_team_data 按 key 合并覆盖; 空/错误 Any 不进入解包缓存、不误删其他 key;
// - 空 Any 删除标记(与 teamsvr-room apply_team_update 的 GAP-09 语义一致)移除对应 key;
// - dirty increase 的 team_update action 完整下发(configure + 原始 Any 形式的 shared_team_data)。
CASE_TEST(lobbysvr_user_team, team_update_shared_data_and_snapshot_admission_filter) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30024;
  constexpr int64_t kTeamId = 305;
  constexpr uint64_t kSessionId = 0x10024;
  constexpr uint64_t kInviteeId = 91051;
  constexpr uint64_t kJoinRequesterId = 91052;
  constexpr uint64_t kExpiredInviteeId = 91053;
  constexpr uint64_t kExpiredJoinRequesterId = 91054;

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
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  const auto now = std::chrono::system_clock::now();

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_storage.mutable_configure()->set_manage_member_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  team_storage.mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  *team_storage.add_shared_team_data() =
      team_test::pack_team_module(team_test::make_team_matching_module(false));
  // 有效 admission(携带内部字段验证投影裁剪)
  team_test::add_storage_invitation(team_storage, kInviteeId, now + std::chrono::seconds(300), true);
  auto* storage_join_request =
      team_test::add_storage_join_request(team_storage, kJoinRequesterId, now + std::chrono::seconds(300), true);
  *storage_join_request->add_member_admission_data() =
      team_test::pack_member_module(team_test::make_member_ready_module(true));
  // 已过期的 admission: 快照加载时不得进入缓存, 也不得出现在 dirty payload
  team_test::add_storage_invitation(team_storage, kExpiredInviteeId, now - std::chrono::seconds(60));
  team_test::add_storage_join_request(team_storage, kExpiredJoinRequesterId, now - std::chrono::seconds(60));
  // 无效 key 的 admission: 同样不得进入缓存
  team_storage.add_pending_invitation();
  team_storage.add_pending_join_request();

  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));
  if (!client) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 2 == snapshot.snapshot().member_size() &&
           !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));

  // 快照(缓存与 dirty payload 两侧): team_key/captain/configure 完整, 过期与无效 admission 缺席,
  // 有效 admission 完整下发且内部字段已裁剪, 队伍共享数据以解包模块形式下发
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot dumped;
    dump_team_snapshot(*current, dumped);
    if (!view.snapshots.empty()) {
      for (const auto* pushed : {view.snapshots.data(), &dumped}) {
        CASE_EXPECT_EQ(team_test::kZoneId, pushed->snapshot().team_key().zone_id());
        CASE_EXPECT_EQ(kTeamId, pushed->snapshot().team_key().team_id());
        CASE_EXPECT_EQ(team_test::kZoneId, pushed->snapshot().captain_user_key().zone_id());
        CASE_EXPECT_EQ(team_test::kCaptainUserId, pushed->snapshot().captain_user_key().user_id());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, pushed->snapshot().configure().manage_member_role());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, pushed->snapshot().configure().invite_role());

        CASE_EXPECT_EQ(1, pushed->snapshot().pending_invitation_size());
        CASE_EXPECT_TRUE(nullptr == find_pending_invitation(*pushed, kExpiredInviteeId));
        const auto* invitation = find_pending_invitation(*pushed, kInviteeId);
        CASE_EXPECT_TRUE(nullptr != invitation);
        if (nullptr != invitation) {
          CASE_EXPECT_TRUE(invitation->invitee_private_channel().channel_id().empty());
          CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation->inviter().user_id());
          CASE_EXPECT_EQ(protobuf_from_system_clock(now + std::chrono::seconds(300)).seconds(),
                         invitation->expired_timepoint().seconds());
          CASE_EXPECT_EQ(kTeamId, invitation->team_key().team_id());
        }

        CASE_EXPECT_EQ(1, pushed->snapshot().pending_join_request_size());
        CASE_EXPECT_TRUE(nullptr == find_pending_join_request(*pushed, kExpiredJoinRequesterId));
        const auto* pending_join = find_pending_join_request(*pushed, kJoinRequesterId);
        CASE_EXPECT_TRUE(nullptr != pending_join);
        if (nullptr != pending_join) {
          CASE_EXPECT_TRUE(pending_join->requester_private_channel().channel_id().empty());
          CASE_EXPECT_EQ(0, pending_join->user_router_server_id());
          CASE_EXPECT_EQ(1, pending_join->member_admission_data_size());
          CASE_EXPECT_EQ(kTeamId, pending_join->team_key().team_id());
        }

        // 原始打包数据不下发, 解包后的模块数据随 shared_team_data 下发
        CASE_EXPECT_EQ(0, pushed->snapshot().shared_team_data_size());
        CASE_EXPECT_EQ(1, pushed->shared_team_data_size());
        if (pushed->shared_team_data_size() > 0) {
          CASE_EXPECT_FALSE(pushed->shared_team_data(0).battle().matching());
        }
      }
    }
  }
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. team_update: configure 整体覆盖(七字段全部非默认值) + shared data 同 key 覆盖(matching true)
  //    + 空 Any/错误类型 Any 不进入解包缓存、不误删合法 key
  const int64_t battle_module_key =
      user_team_algorithm::make_team_shared_data_key(team_test::make_team_matching_module(true));
  {
    atfw::team::DTeamAction team_action;
    auto* team_update = team_action.mutable_team_update();
    auto* configure = team_update->mutable_configure();
    configure->set_manage_member_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    configure->set_approve_join_request_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    configure->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    configure->set_update_team_data_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    configure->set_reject_invitation_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    configure->set_disable_join_request(true);
    configure->set_set_member_role_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *team_update->add_shared_team_data() =
        team_test::pack_team_module(team_test::make_team_matching_module(true));
    // 携带 key 但 value 为空(非删除目标 key): 不进入解包缓存, 也不误删合法 key
    auto* empty_entry = team_update->add_shared_team_data();
    empty_entry->set_key(777001);
    empty_entry->mutable_value()->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    // 错误类型 Any: 不进入解包缓存
    auto* foreign_entry = team_update->add_shared_team_data();
    foreign_entry->set_key(888001);
    foreign_entry->mutable_value()->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    CASE_EXPECT_TRUE(foreign_entry->mutable_value()->mutable_data()->PackFrom(team_test::make_team_key(kTeamId)));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_configure().manage_member_role();
  }));
  {
    // configure 整体覆盖: 快照中的 manage_member_role=OWNER/invite_role=ADMIN 被新值替换
    const auto& configure = current->get_configure();
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, configure.manage_member_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, configure.approve_join_request_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, configure.invite_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, configure.update_team_data_role());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, configure.reject_invitation_role());
    CASE_EXPECT_TRUE(configure.disable_join_request());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, configure.set_member_role_role());

    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
    if (snapshot.shared_team_data_size() > 0) {
      CASE_EXPECT_TRUE(snapshot.shared_team_data(0).battle().matching());
    }
    // matching=true 的 do_update 处理器已驱动派生状态
    CASE_EXPECT_TRUE(current->is_matching());
  }

  // 2. team_update: 空 Any 删除标记移除 battle 模块 key(与 teamsvr-room apply_team_update 的删除标记语义一致);
  //    删除同样触发处理器的 delete 回调, 派生状态 is_matching 必须随之复位; 未携带 configure 时缓存配置保持不变
  {
    atfw::team::DTeamAction team_action;
    auto* delete_marker = team_action.mutable_team_update()->add_shared_team_data();
    delete_marker->set_key(battle_module_key);
    delete_marker->mutable_value()->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 0 == snapshot.shared_team_data_size() && !current->is_matching();
  }));
  CASE_EXPECT_FALSE(current->is_matching());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_configure().manage_member_role());
  CASE_EXPECT_TRUE(current->get_configure().disable_join_request());

  // 3. team_update: 删除后同 key 重新合并(matching false)且仍不携带 configure
  {
    atfw::team::DTeamAction team_action;
    *team_action.mutable_team_update()->add_shared_team_data() =
        team_test::pack_team_module(team_test::make_team_matching_module(false));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 1 == snapshot.shared_team_data_size() && !snapshot.shared_team_data(0).battle().matching();
  }));
  CASE_EXPECT_FALSE(current->is_matching());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_configure().manage_member_role());

  // team_update 不影响队伍级 pending admission 缓存
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
  }

  // dirty increase: 三条 team_update action 完整下发(configure 与原始 Any 形式的 shared_team_data 保留)
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_actions_of_case(team_test::collect_team_dirty(test, kSessionId, kTeamId),
                                            atfw::team::DTeamAction::kTeamUpdate) >= 3;
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.snapshots.empty());
    CASE_EXPECT_EQ(3, static_cast<int>(view.actions.size()));
    const auto update_actions = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kTeamUpdate);
    CASE_EXPECT_EQ(3, static_cast<int>(update_actions.size()));
    if (update_actions.size() >= 3) {
      const auto& first = update_actions[0]->action().team_update();
      CASE_EXPECT_TRUE(first.has_configure());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, first.configure().manage_member_role());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, first.configure().approve_join_request_role());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, first.configure().invite_role());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, first.configure().update_team_data_role());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, first.configure().reject_invitation_role());
      CASE_EXPECT_TRUE(first.configure().disable_join_request());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, first.configure().set_member_role_role());
      // 队伍共享数据仍在 action 中以 Any 形式完整下发(含未被缓存接受的条目)
      CASE_EXPECT_EQ(3, first.shared_team_data_size());
      if (first.shared_team_data_size() >= 3) {
        PROJECT_NAMESPACE_ID::DTeamSharedDataModule unpacked;
        CASE_EXPECT_TRUE(first.shared_team_data(0).value().data().UnpackTo(&unpacked));
        CASE_EXPECT_TRUE(unpacked.battle().matching());
        CASE_EXPECT_EQ(777001, first.shared_team_data(1).key());
        CASE_EXPECT_EQ(888001, first.shared_team_data(2).key());
      }
      CASE_EXPECT_EQ(0, update_actions[0]->shared_member_data_size());

      const auto& second = update_actions[1]->action().team_update();
      CASE_EXPECT_FALSE(second.has_configure());
      CASE_EXPECT_EQ(1, second.shared_team_data_size());
      if (second.shared_team_data_size() > 0) {
        CASE_EXPECT_EQ(battle_module_key, second.shared_team_data(0).key());
        CASE_EXPECT_TRUE(second.shared_team_data(0).value().data().type_url().empty());
      }

      const auto& third = update_actions[2]->action().team_update();
      CASE_EXPECT_FALSE(third.has_configure());
      CASE_EXPECT_EQ(1, third.shared_team_data_size());
      if (third.shared_team_data_size() > 0) {
        PROJECT_NAMESPACE_ID::DTeamSharedDataModule unpacked;
        CASE_EXPECT_TRUE(third.shared_team_data(0).value().data().UnpackTo(&unpacked));
        CASE_EXPECT_FALSE(unpacked.battle().matching());
      }
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ROLE-02: election_captain 的角色解析必须与 teamsvr-room change_captain 一致:
// - 事件显式 role(>GUEST) 直接使用;
// - 事件缺省 role 时继承原队长当前缓存角色; 原队长缺失或无有效角色时回退 OWNER;
// - 原队长(仍是成员且不是新队长)降为 NORMAL;
// - 同一人重复当选不自我降级;
// - 目标不是成员时忽略事件。
CASE_TEST(lobbysvr_user_team, election_captain_role_semantics) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30011;
  constexpr int64_t kTeamId = 301;
  constexpr uint64_t kThirdMemberId = 91011;
  constexpr uint64_t kUnknownUserId = 99999;

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
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }

  // 快照: 原队长此前被降为 ADMIN(队长身份与成员角色是两个维度), 自己是普通成员, 另有第三成员
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  team_test::add_storage_member(team_storage, kThirdMemberId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    return nullptr != captain_member && atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN == captain_member->role();
  }));

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. 缺省 role: 新队长继承原队长当前缓存角色(ADMIN), 原队长降为 NORMAL
  {
    atfw::team::DTeamAction team_action;
    protobuf_copy_message(*team_action.mutable_election_captain()->mutable_user_key(),
                          team_test::make_user_key(kUserId));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_EQ(kUserId, current->get_cached_captain_user_key().user_id());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, current->get_cached_permission_role());
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* self_member = team_test::find_snapshot_member(snapshot, kUserId);
    CASE_EXPECT_TRUE(nullptr != self_member);
    if (nullptr != self_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, self_member->role());
    }
    const auto* old_captain = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != old_captain);
    if (nullptr != old_captain) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, old_captain->role());
    }
  }

  // 2. 同一人缺省重复当选: 继承自己的当前角色, 不自我降级
  {
    atfw::team::DTeamAction team_action;
    protobuf_copy_message(*team_action.mutable_election_captain()->mutable_user_key(),
                          team_test::make_user_key(kUserId));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_EQ(kUserId, current->get_cached_captain_user_key().user_id());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, current->get_cached_permission_role());

  // 3. 目标不是成员: 事件被忽略, 队长与角色不变
  {
    atfw::team::DTeamAction team_action;
    auto* election = team_action.mutable_election_captain();
    protobuf_copy_message(*election->mutable_user_key(), team_test::make_user_key(kUnknownUserId));
    election->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_EQ(kUserId, current->get_cached_captain_user_key().user_id());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, current->get_cached_permission_role());

  // 4. 显式 role 直接使用: 原队长被重新指定为 OWNER, 自己(旧队长)降为 NORMAL
  {
    atfw::team::DTeamAction team_action;
    auto* election = team_action.mutable_election_captain();
    protobuf_copy_message(*election->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    election->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_EQ(team_test::kCaptainUserId, current->get_cached_captain_user_key().user_id());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, current->get_cached_permission_role());
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* captain_member = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    CASE_EXPECT_TRUE(nullptr != captain_member);
    if (nullptr != captain_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain_member->role());
    }
  }

  // 5. 原队长缺失(已被移出成员缓存)时缺省 role 回退 OWNER
  {
    atfw::team::DTeamAction team_action;
    auto* remove_data = team_action.mutable_remove_member();
    protobuf_copy_message(*remove_data->mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*remove_data->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    remove_data->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  {
    atfw::team::DTeamAction team_action;
    protobuf_copy_message(*team_action.mutable_election_captain()->mutable_user_key(),
                          team_test::make_user_key(kThirdMemberId));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_EQ(kThirdMemberId, current->get_cached_captain_user_key().user_id());
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* third_member = team_test::find_snapshot_member(snapshot, kThirdMemberId);
    CASE_EXPECT_TRUE(nullptr != third_member);
    if (nullptr != third_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, third_member->role());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

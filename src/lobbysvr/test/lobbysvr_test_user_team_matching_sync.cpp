// Copyright 2026 atframework
// Offline regression tests for the lobbysvr team <-> matching glue in
// src/lobbysvr/service/logic/team/user_team_battle_library_function.cpp (see the adjacent README.md):
//   - start_matching_check / start_matching_finish / matching_finish glue entries (captain gate + uplink payload);
//   - channel battle.matching / matching_team_view reactions (captain repair, pending-start cancel, member
//   subscription);
//   - auto_check_and_correct_team_data immediate (election) and periodic (minute refresh) repairs;
//   - async_update_team_shared_data / async_update_member_shared_data batch contracts (normalize + conditions).
// All glue uplinks go through the unified batch update (user_team::update_team_shared_data /
// update_member_shared_data): matching=true carries a normalized matching_team_view and the all-member-ready
// condition, matching=false carries the cleared view and the team-is-matching condition (so a no-op reset
// cannot clobber matching_team_view), ready=true carries a normalized matching_parameter and the
// team-not-matching condition.
// Entries are the real production seams: user_matching_manager::start_matching / callback_start_matching inside a
// runtime task, and team channel events injected through global_receive_channel_event. Uplink assertions use the
// typed team_room_ss_capture; the matchsvr heartbeat is captured with the generated typed mock.
// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/match_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lobbysvr_test_runtime_helper.h"                  // NOLINT: build/include_subdir
#include "lobbysvr_test_user_team_common.h"                // NOLINT: build/include_subdir
#include "logic/matching/user_matching_manager.h"          // NOLINT: build/include_subdir
#include "logic/team/user_team_battle_library_function.h"  // NOLINT: build/include_subdir
#include "rpc/matching/matchsvrservice.atfw.gen.h"         // NOLINT: build/include_subdir

namespace {
// Seed the user's matching manager with an active matching-unit view (SEARCHING) the way the login recovery
// flow does, so is_in_matching() reports true without any matchsvr round trip.
bool seed_in_matching_state(atfw::testing::runtime& test, const user::ptr_t& user_inst, uint64_t unit_id,
                            uint64_t matchsvr_id) {
  return team_test::run_sync_task(test, "team.seed_matching",
                                  [user_inst, unit_id, matchsvr_id](rpc::context& ctx) -> rpc::result_code_type {
                                    PROJECT_NAMESPACE_ID::table_user table;
                                    auto* matching_data = table.mutable_matching_data();
                                    matching_data->set_matchsvr_server_id(matchsvr_id);
                                    auto* view = matching_data->mutable_view();
                                    view->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
                                    view->mutable_unit()->set_unit_id(unit_id);
                                    user_inst->get_user_matching_manager().init_from_table_data(ctx, table);
                                    RPC_RETURN_CODE(0);
                                  });
}

// Inject one team_update action carrying the given packed team shared-data entries on the team channel.
bool inject_team_update(atfw::testing::runtime& test, team_test::channel_event_chain& team_chain,
                        const atfw::team::DTeamAnyDataWithKey& entry) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_team_update()->add_shared_team_data(), entry);
  return team_test::inject_event_message(test, team_chain, action);
}

// Assert the team_update carries exactly the all-member-ready condition appended by the matching=true update
// (user_team.cpp append_condition_all_member_ready): one rule, one all-members group, one ready=true checked entry.
void expect_all_member_ready_condition(const atfw::team::DTeamUpdateData& team_update) {
  CASE_EXPECT_EQ(1, team_update.condition_size());
  if (1 == team_update.condition_size()) {
    const auto& rule = team_update.condition(0);
    CASE_EXPECT_EQ(1, rule.member_condition_group_size());
    if (1 == rule.member_condition_group_size()) {
      const auto& group = rule.member_condition_group(0);
      CASE_EXPECT_TRUE(group.all_members());
      CASE_EXPECT_EQ(1, group.member_condition().shared_member_data_size());
      if (1 == group.member_condition().shared_member_data_size()) {
        CASE_EXPECT_EQ(team_test::member_ready_data_key(), group.member_condition().shared_member_data(0).key());
      }
    }
  }
}

// Assert the team_update carries exactly the team-is-matching condition appended by the matching=false update
// (user_team.cpp append_condition_team_is_matching): one rule, one matching=true checked team-data entry.
void expect_team_is_matching_condition(const atfw::team::DTeamUpdateData& team_update) {
  CASE_EXPECT_EQ(1, team_update.condition_size());
  if (1 == team_update.condition_size()) {
    const auto& rule = team_update.condition(0);
    CASE_EXPECT_EQ(1, rule.shared_team_data_size());
    if (1 == rule.shared_team_data_size()) {
      CASE_EXPECT_EQ(team_test::team_matching_data_key(), rule.shared_team_data(0).key());
      PROJECT_NAMESPACE_ID::DTeamSharedDataModule checked;
      CASE_EXPECT_TRUE(rule.shared_team_data(0).value().UnpackTo(&checked));
      CASE_EXPECT_TRUE(checked.has_battle());
      if (checked.has_battle()) {
        CASE_EXPECT_TRUE(checked.battle().matching());
      }
    }
  }
}

// Assert one packed entry carries the matching_team_view fetched from the local matching manager
// (fetch_team_sync_matching_view: unit_id + subscriber_server_id), as opposed to an empty cleared view.
void expect_packed_team_view_entry(const atfw::team::DTeamAnyDataWithKey& entry, uint64_t unit_id,
                                   uint64_t subscriber_server_id) {
  CASE_EXPECT_EQ(team_test::team_matching_team_view_data_key(), entry.key());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER, entry.value().permission());
  PROJECT_NAMESPACE_ID::DTeamSharedDataModule unpacked;
  CASE_EXPECT_TRUE(entry.value().data().UnpackTo(&unpacked));
  CASE_EXPECT_TRUE(unpacked.has_battle());
  if (unpacked.has_battle()) {
    CASE_EXPECT_TRUE(unpacked.battle().has_matching_team_view());
    if (unpacked.battle().has_matching_team_view()) {
      CASE_EXPECT_EQ(unit_id, unpacked.battle().matching_team_view().unit_id());
      CASE_EXPECT_EQ(subscriber_server_id, unpacked.battle().matching_team_view().subscriber_server_id());
    }
  }
}
}  // namespace

// MTS-01: start_matching_check glue — 队内玩家经 user_matching_manager::start_matching 发起匹配时,
// register_start_matching_check_function 的 glue 回调只做队长校验: 队长上行一条 battle.matching=true 的
// team_update(经 async_update_team_shared_data 走统一批量更新, normalize 追加空 matching_team_view,
// 并附加全员 ready 更新条件); 非队长校验拒绝并以 EN_ERR_TEAM_PERMISSION_DENY 取消本地待匹配状态
// (is_in_matching_start() 复位), 零上行。队长立即返回成功且 is_in_matching_start() 置位(等 room 广播驱动后续流程)。
CASE_TEST(lobbysvr_user_team, matching_sync_01_start_check_captain_gate) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kCaptainUserId = 30071;
  constexpr uint64_t kMemberUserId = 30072;
  constexpr int64_t kCaptainTeamId = 571;
  constexpr int64_t kMemberTeamId = 572;
  team_test::now_offset_guard time_guard;

  user::ptr_t captain_inst;
  std::string captain_subscriber_key;
  atframework::dtmq::DChannelIdKey captain_private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kCaptainUserId, captain_inst, captain_subscriber_key,
                                              captain_private_channel_key));
  user::ptr_t member_inst;
  std::string member_subscriber_key;
  atframework::dtmq::DChannelIdKey member_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kMemberUserId, member_inst, member_subscriber_key, member_private_channel_key));
  if (!captain_inst || !member_inst) {
    test.stop();
    return;
  }

  team_test::channel_event_chain captain_private_chain;
  captain_private_chain.channel_key = captain_private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, captain_inst, captain_private_chain, kCaptainTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, true, nullptr, {}));
  team_test::channel_event_chain member_private_chain;
  member_private_chain.channel_key = member_private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, member_inst, member_private_chain, kMemberTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));

  // 队长: start_matching 经队内检查分支返回成功, glue 上行 matching=true 的 team_update,
  // normalize 追加空 matching_team_view 并附加全员 ready 条件
  CASE_EXPECT_TRUE(team_test::run_sync_task(
      test, "team.mts01_start_captain", [captain_inst](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(captain_inst->get_user_matching_manager().start_matching(ctx)));
      }));
  CASE_EXPECT_TRUE(captain_inst->get_user_matching_manager().is_in_matching_start());
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 1; }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (1 == ss_capture.send_message_reqs.size()) {
    const auto& action_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(action_req, kCaptainTeamId, kCaptainUserId);
    const auto& team_update = action_req.action().team_update();
    CASE_EXPECT_EQ(2, team_update.shared_team_data_size());
    if (2 == team_update.shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(team_update.shared_team_data(0), true);
      team_test::expect_packed_team_matching_team_view_entry(team_update.shared_team_data(1));
    }
    expect_all_member_ready_condition(team_update);
  }

  // 非队长: 响应仍成功, glue 队长校验拒绝并以 EN_ERR_TEAM_PERMISSION_DENY 取消本地待匹配状态, 零上行
  CASE_EXPECT_TRUE(team_test::run_sync_task(
      test, "team.mts01_start_member", [member_inst](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(member_inst->get_user_matching_manager().start_matching(ctx)));
      }));
  CASE_EXPECT_FALSE(member_inst->get_user_matching_manager().is_in_matching_start());
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-02: callback_start_matching(true) 驱动真实匹配发起流程 — 队长经 start_matching_inner_ 实际发起匹配请求;
// 测试环境缺少匹配配置, 请求在 fill_matching_scope 失败, 经 clear_matching_state 清理本地匹配状态并由
// matching_finish 上行一条 battle.matching=false 复位队伍(normalize 追加清空的 matching_team_view,
// 附加队伍匹配中条件)。非队长入口直接返回: 不发起匹配流程也不改本地状态(切换队长后由新队长发起), 零上行。
CASE_TEST(lobbysvr_user_team, matching_sync_02_callback_captain_request_failure_notifies_team) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kCaptainUserId = 30073;
  constexpr uint64_t kMemberUserId = 30074;
  constexpr int64_t kCaptainTeamId = 573;
  constexpr int64_t kMemberTeamId = 574;
  team_test::now_offset_guard time_guard;

  user::ptr_t captain_inst;
  std::string captain_subscriber_key;
  atframework::dtmq::DChannelIdKey captain_private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kCaptainUserId, captain_inst, captain_subscriber_key,
                                              captain_private_channel_key));
  user::ptr_t member_inst;
  std::string member_subscriber_key;
  atframework::dtmq::DChannelIdKey member_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kMemberUserId, member_inst, member_subscriber_key, member_private_channel_key));
  if (!captain_inst || !member_inst) {
    test.stop();
    return;
  }

  team_test::channel_event_chain captain_private_chain;
  captain_private_chain.channel_key = captain_private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, captain_inst, captain_private_chain, kCaptainTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, true, nullptr, {}));
  team_test::channel_event_chain member_private_chain;
  member_private_chain.channel_key = member_private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, member_inst, member_private_chain, kMemberTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));

  // 队长: callback_start_matching(true) 经 start_matching_inner_ 真实发起匹配请求; 测试环境缺少匹配配置,
  // 请求在 fill_matching_scope 失败, 经 clear_matching_state 清理本地匹配状态并由 matching_finish 上行一条
  // matching=false 复位队伍(normalize 追加清空的 matching_team_view, 附加队伍匹配中条件)
  CASE_EXPECT_TRUE(team_test::run_sync_task(
      test, "team.mts02_callback_captain", [captain_inst](rpc::context& ctx) -> rpc::result_code_type {
        captain_inst->get_user_matching_manager().callback_start_matching(ctx, true, 0);
        RPC_RETURN_CODE(0);
      }));
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 1; }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (1 == ss_capture.send_message_reqs.size()) {
    const auto& finish_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(finish_req, kCaptainTeamId, kCaptainUserId);
    // matching=false 的 normalize 追加清空后的 matching_team_view, 一并下发; 附加队伍匹配中条件
    CASE_EXPECT_EQ(2, finish_req.action().team_update().shared_team_data_size());
    if (2 == finish_req.action().team_update().shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(finish_req.action().team_update().shared_team_data(0), false);
      team_test::expect_packed_team_matching_team_view_entry(finish_req.action().team_update().shared_team_data(1));
    }
    expect_team_is_matching_condition(finish_req.action().team_update());
  }
  // 终态: 本地匹配状态已复位, 队伍派生状态从未被上行流程直接改写(等 room 权威广播)
  CASE_EXPECT_FALSE(captain_inst->get_user_matching_manager().is_in_matching());
  auto captain_team =
      captain_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kCaptainTeamId));
  CASE_EXPECT_TRUE(!!captain_team);
  if (captain_team) {
    CASE_EXPECT_FALSE(captain_team->is_matching());
  }

  // 非队长: callback_start_matching 直接返回, 不发起匹配流程也不改本地状态(切换队长后由新队长发起),
  // 因此零上行且匹配态保持复位
  CASE_EXPECT_TRUE(team_test::run_sync_task(
      test, "team.mts02_callback_member", [member_inst](rpc::context& ctx) -> rpc::result_code_type {
        member_inst->get_user_matching_manager().callback_start_matching(ctx, true, 0);
        RPC_RETURN_CODE(0);
      }));
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_FALSE(member_inst->get_user_matching_manager().is_in_matching());
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-03: 频道 battle.matching=true 的本地行为 — 队长收到权威 matching=true 但本地已不在匹配流程时
// (状态已失效), glue_layer_event_on_team_action_update_matching 立即上行一条 matching=false 修复;
// 派生标志保持 true 直到 room 广播复位; 非队长只更新派生标志, 不上行修复(修复仅队长能发起)。
CASE_TEST(lobbysvr_user_team, matching_sync_03_channel_matching_true_captain_repair) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kCaptainUserId = 30075;
  constexpr uint64_t kMemberUserId = 30076;
  constexpr int64_t kCaptainTeamId = 575;
  constexpr int64_t kMemberTeamId = 576;
  team_test::now_offset_guard time_guard;

  user::ptr_t captain_inst;
  std::string captain_subscriber_key;
  atframework::dtmq::DChannelIdKey captain_private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kCaptainUserId, captain_inst, captain_subscriber_key,
                                              captain_private_channel_key));
  user::ptr_t member_inst;
  std::string member_subscriber_key;
  atframework::dtmq::DChannelIdKey member_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kMemberUserId, member_inst, member_subscriber_key, member_private_channel_key));
  if (!captain_inst || !member_inst) {
    test.stop();
    return;
  }

  team_test::channel_event_chain captain_private_chain;
  captain_private_chain.channel_key = captain_private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, captain_inst, captain_private_chain, kCaptainTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, true, nullptr, {}));
  team_test::channel_event_chain member_private_chain;
  member_private_chain.channel_key = member_private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, member_inst, member_private_chain, kMemberTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));

  auto captain_team =
      captain_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kCaptainTeamId));
  auto member_team = member_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kMemberTeamId));
  CASE_EXPECT_TRUE(!!captain_team && !!member_team);
  if (!captain_team || !member_team) {
    test.stop();
    return;
  }

  // 队长: 注入 matching=true -> 标志置位 + 立即修复上行 matching=false
  team_test::channel_event_chain captain_team_chain;
  captain_team_chain.channel_key = team_test::make_team_channel_key(kCaptainTeamId);
  CASE_EXPECT_TRUE(inject_team_update(test, captain_team_chain,
                                      team_test::pack_team_module(team_test::make_team_matching_module(true))));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return captain_team->is_matching(); }));
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 1; }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (1 == ss_capture.send_message_reqs.size()) {
    const auto& action_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(action_req, kCaptainTeamId, kCaptainUserId);
    const auto& team_update = action_req.action().team_update();
    // 修复上行同样经过 normalize: matching=false 追加清空后的 matching_team_view, 附加队伍匹配中条件
    CASE_EXPECT_EQ(2, team_update.shared_team_data_size());
    if (2 == team_update.shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(team_update.shared_team_data(0), false);
      team_test::expect_packed_team_matching_team_view_entry(team_update.shared_team_data(1));
    }
    expect_team_is_matching_condition(team_update);
  }
  // 修复上行不改写本地派生标志: 仍保持 true, 直到 room 广播 matching=false 才复位且不再补发
  CASE_EXPECT_TRUE(captain_team->is_matching());
  CASE_EXPECT_TRUE(inject_team_update(test, captain_team_chain,
                                      team_test::pack_team_module(team_test::make_team_matching_module(false))));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return !captain_team->is_matching(); }));
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));

  // 非队长: 同样的权威事件只更新派生标志, 零修复上行
  team_test::channel_event_chain member_team_chain;
  member_team_chain.channel_key = team_test::make_team_channel_key(kMemberTeamId);
  CASE_EXPECT_TRUE(inject_team_update(test, member_team_chain,
                                      team_test::pack_team_module(team_test::make_team_matching_module(true))));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return member_team->is_matching(); }));
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  CASE_EXPECT_TRUE(inject_team_update(test, member_team_chain,
                                      team_test::pack_team_module(team_test::make_team_matching_module(false))));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return !member_team->is_matching(); }));

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-04: 队长变化后立即执行一次数据修复 — 普通成员收到权威 matching=true 时只置位派生标志(非队长零上行);
// election_captain 让自己成为队长后, auto_check_and_correct_team_data 立即发现"队伍匹配中但本地不在匹配流程"
// 并上行一条 matching=false 修复。
CASE_TEST(lobbysvr_user_team, matching_sync_04_election_triggers_immediate_repair) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30077;
  constexpr int64_t kTeamId = 577;
  team_test::now_offset_guard time_guard;

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
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));
  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team_ptr);
  if (!team_ptr) {
    test.stop();
    return;
  }

  // 普通成员: 注入 matching=true, 只置位派生标志, 零上行
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  CASE_EXPECT_TRUE(
      inject_team_update(test, team_chain, team_test::pack_team_module(team_test::make_team_matching_module(true))));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team_ptr->is_matching(); }));
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));

  // 自己当选队长(缺省 role 继承原队长 OWNER): 立即触发一次数据修复, 上行一条 matching=false
  {
    atfw::team::DTeamAction election_action;
    auto* election_captain = election_action.mutable_election_captain();
    protobuf_copy_message(*election_captain->mutable_user_key(), team_test::make_user_key(kUserId));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, election_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team_ptr->is_captain(); }));
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, team_ptr->get_cached_permission_role());
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 1; }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (1 == ss_capture.send_message_reqs.size()) {
    const auto& action_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& team_update = action_req.action().team_update();
    // 修复上行同样经过 normalize: matching=false 追加清空后的 matching_team_view, 附加队伍匹配中条件
    CASE_EXPECT_EQ(2, team_update.shared_team_data_size());
    if (2 == team_update.shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(team_update.shared_team_data(0), false);
      team_test::expect_packed_team_matching_team_view_entry(team_update.shared_team_data(1));
    }
    expect_team_is_matching_condition(team_update);
  }
  // 修复由 auto_check 驱动, 不直接改写本地派生标志
  CASE_EXPECT_TRUE(team_ptr->is_matching());

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-05: auto_check_and_correct_team_data 的周期修复 — 队伍派生标志为未匹配但本地实际在匹配流程中时
// (登录恢复/重启后的典型偏差), minute refresh 触发的定期检查由队长上行一条 matching=true 反向修复;
// 修复按 kCheckAndCorrectInterval(user_team.cpp 内 30s 常量, 无配置项)节流, 间隔内不重复上行,
// room 一直不确认时下一个周期间隔到达后再次重发。
CASE_TEST(lobbysvr_user_team, matching_sync_05_minute_refresh_reverse_repair_and_throttle) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30078;
  constexpr int64_t kTeamId = 578;
  constexpr uint64_t kMatchingUnitId = 91078;
  // user_team.cpp 内 kCheckAndCorrectInterval=30s, 无配置项; 按其实际值推进逻辑时间
  constexpr auto kCheckAndCorrectInterval = std::chrono::seconds(30);
  team_test::now_offset_guard time_guard;

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
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, true, nullptr, {}));
  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team_ptr);
  if (!team_ptr) {
    test.stop();
    return;
  }

  // seed 本地匹配中(单元视图 SEARCHING), 队伍派生标志保持未匹配
  CASE_EXPECT_TRUE(seed_in_matching_state(test, user_inst, kMatchingUnitId, 0));
  CASE_EXPECT_TRUE(user_inst->get_user_matching_manager().is_in_matching());
  CASE_EXPECT_FALSE(team_ptr->is_matching());

  auto minute_refresh = [&]() {
    return team_test::run_sync_task(test, "team.mts05_minute_refresh",
                                    [user_inst](rpc::context& ctx) -> rpc::result_code_type {
                                      user_inst->get_user_team_manager().refresh_feature_limit_minute(ctx);
                                      RPC_RETURN_CODE(0);
                                    });
  };

  // 到达检查间隔后 minute refresh: 队长上行 matching=true 反向修复, normalize 追加从本地匹配状态取出的
  // matching_team_view(种子单元视图)并附加全员 ready 条件
  team_test::now_offset_guard::advance(kCheckAndCorrectInterval + std::chrono::seconds(1));
  CASE_EXPECT_TRUE(minute_refresh());
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 1; }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (1 == ss_capture.send_message_reqs.size()) {
    const auto& action_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& team_update = action_req.action().team_update();
    CASE_EXPECT_EQ(2, team_update.shared_team_data_size());
    if (2 == team_update.shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(team_update.shared_team_data(0), true);
      expect_packed_team_view_entry(team_update.shared_team_data(1), kMatchingUnitId, 0);
    }
    expect_all_member_ready_condition(team_update);
  }

  // 间隔内再次 minute refresh 不重复上行
  CASE_EXPECT_TRUE(minute_refresh());
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));

  // room 一直不确认(派生标志仍为 false)时, 下一个间隔到达后再次重发修复(同样的条目录与条件)
  CASE_EXPECT_FALSE(team_ptr->is_matching());
  team_test::now_offset_guard::advance(kCheckAndCorrectInterval + std::chrono::seconds(1));
  CASE_EXPECT_TRUE(minute_refresh());
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 2; }));
  CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (2 == ss_capture.send_message_reqs.size()) {
    const auto& resend_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(resend_req, kTeamId, kUserId);
    const auto& team_update = resend_req.action().team_update();
    CASE_EXPECT_EQ(2, team_update.shared_team_data_size());
    if (2 == team_update.shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(team_update.shared_team_data(0), true);
      expect_packed_team_view_entry(team_update.shared_team_data(1), kMatchingUnitId, 0);
    }
    expect_all_member_ready_condition(team_update);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-06: 频道 matching_team_view 的本地行为 — 成员收到权威 team_view{unit_id, subscriber_server_id} 时,
// glue_layer_event_on_team_action_update_matching_team_view 经 subscribe_matching_unit 向 matchsvr 发起一次
// 心跳查询以订阅该匹配单元(捕获的 SSMatchingCheckReq.unit_id 精确等于视图值), 视图同时按 key 合并进队伍缓存;
// 不是成员的玩家跳过订阅(同样的本地匹配状态下也不产生心跳), 缓存合并不受门槛影响。
CASE_TEST(lobbysvr_user_team, matching_sync_06_team_view_subscription_member_gate) {
  constexpr uint64_t kMatchsvrId = 0x1E0011;
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  // matchsvr 发现节点与心跳 mock(与 lobbysvr_test_user_matching_manager.cpp 同一接入方式)
  {
    atfw::testing::mock_node node;
    node.set_id(kMatchsvrId)
        .set_name("unit-test-matchsvr-team-sync")
        .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr))
        .set_type_name("matchsvr")
        .set_zone_id(team_test::kZoneId)
        .add_label("hpa_scaling_ready", "1");
    CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
    if (nullptr != logic_server_last_common_module()) {
      logic_server_last_common_module()->reload();
    }
  }
  auto captured_heartbeats = std::make_shared<std::vector<PROJECT_NAMESPACE_ID::SSMatchingCheckReq>>();
  rpc::unit_test::ss_mock_rule_options heartbeat_rule_options;
  heartbeat_rule_options.match_node_id = kMatchsvrId;
  auto heartbeat_rule = rpc::matching::mock::matching_heart_bear(
      [captured_heartbeats](rpc::context&, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
                            PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        captured_heartbeats->push_back(request);
        response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        response.set_matching_id("team-sync-view-subscription");
        response.mutable_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
        response.mutable_snapshot()->mutable_unit()->set_unit_id(request.unit_id());
        RPC_RETURN_CODE(0);
      },
      heartbeat_rule_options);
  CASE_EXPECT_TRUE(!!heartbeat_rule);
  if (!heartbeat_rule) {
    test.stop();
    return;
  }

  constexpr uint64_t kMemberUserId = 30081;
  constexpr uint64_t kNonMemberUserId = 30082;
  constexpr int64_t kMemberTeamId = 581;
  constexpr int64_t kNonMemberTeamId = 582;
  constexpr uint64_t kMemberUnitId = 91081;
  constexpr uint64_t kNonMemberUnitId = 91082;
  team_test::now_offset_guard time_guard;

  user::ptr_t member_inst;
  std::string member_subscriber_key;
  atframework::dtmq::DChannelIdKey member_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kMemberUserId, member_inst, member_subscriber_key, member_private_channel_key));
  user::ptr_t non_member_inst;
  std::string non_member_subscriber_key;
  atframework::dtmq::DChannelIdKey non_member_private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kNonMemberUserId, non_member_inst, non_member_subscriber_key,
                                              non_member_private_channel_key));
  if (!member_inst || !non_member_inst) {
    test.stop();
    return;
  }

  // 成员: 普通成员入队(队长为另一用户)
  team_test::channel_event_chain member_private_chain;
  member_private_chain.channel_key = member_private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, member_inst, member_private_chain, kMemberTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));
  // 非成员: 经 joined_team 注册队伍但快照不含自己(等待成为成员的窗口期)
  team_test::channel_event_chain non_member_private_chain;
  non_member_private_chain.channel_key = non_member_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::join_team_via_notification(test, non_member_inst, non_member_private_chain, kNonMemberTeamId));
  {
    auto storage = team_test::make_team_storage(kNonMemberTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    team_test::add_storage_member(storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kNonMemberTeamId, storage));
  }
  auto member_team = member_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kMemberTeamId));
  auto non_member_team =
      non_member_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kNonMemberTeamId));
  CASE_EXPECT_TRUE(!!member_team && !!non_member_team);
  if (!member_team || !non_member_team) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(member_team->is_member());
  CASE_EXPECT_FALSE(non_member_team->is_member());

  // 两个玩家都处于真实的本地匹配状态, 排除"订阅门槛被心跳守卫掩盖"的可能
  CASE_EXPECT_TRUE(seed_in_matching_state(test, member_inst, kMemberUnitId, kMatchsvrId));
  CASE_EXPECT_TRUE(seed_in_matching_state(test, non_member_inst, kNonMemberUnitId, kMatchsvrId));

  // 成员: 注入 team_view -> 缓存按键合并 + 向 matchsvr 发起一次心跳查询
  team_test::channel_event_chain member_team_chain;
  member_team_chain.channel_key = team_test::make_team_channel_key(kMemberTeamId);
  {
    PROJECT_NAMESPACE_ID::DTeamSharedDataModule view_module;
    auto* team_view = view_module.mutable_battle()->mutable_matching_team_view();
    team_view->set_unit_id(kMemberUnitId);
    team_view->set_subscriber_server_id(kMatchsvrId);
    CASE_EXPECT_TRUE(inject_team_update(test, member_team_chain, team_test::pack_team_module(view_module)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return !captured_heartbeats->empty(); }));
  CASE_EXPECT_EQ(1, static_cast<int>(captured_heartbeats->size()));
  if (1 == captured_heartbeats->size()) {
    CASE_EXPECT_EQ(kMemberUnitId, captured_heartbeats->front().unit_id());
  }
  CASE_EXPECT_EQ(kMemberUnitId, user_team_battle_library_function::get_matching_team_sync_view(*member_team).unit_id());

  // 非成员: 同样的权威视图只合并缓存, 订阅被 is_member 门槛拒绝, 零心跳
  team_test::channel_event_chain non_member_team_chain;
  non_member_team_chain.channel_key = team_test::make_team_channel_key(kNonMemberTeamId);
  {
    PROJECT_NAMESPACE_ID::DTeamSharedDataModule view_module;
    auto* team_view = view_module.mutable_battle()->mutable_matching_team_view();
    team_view->set_unit_id(kNonMemberUnitId);
    team_view->set_subscriber_server_id(kMatchsvrId);
    CASE_EXPECT_TRUE(inject_team_update(test, non_member_team_chain, team_test::pack_team_module(view_module)));
  }
  CASE_EXPECT_EQ(kNonMemberUnitId,
                 user_team_battle_library_function::get_matching_team_sync_view(*non_member_team).unit_id());
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(1, static_cast<int>(captured_heartbeats->size()));

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-07: 成员侧只读接口 — user_team_battle_library_function 的 find_member / foreach_member / is_ready /
// get_matching_team_parameter 从成员共享数据缓存读回快照种子内容, 缺失数据回退默认值; 队伍级
// get_matching_team_sync_view / get_matching_start_data 无缓存时回退默认实例。
CASE_TEST(lobbysvr_user_team, matching_sync_07_member_read_accessors) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30083;
  constexpr uint64_t kOtherUserId = 30084;
  constexpr uint64_t kUnknownUserId = 30089;
  constexpr int64_t kTeamId = 583;
  constexpr int64_t kSearchStartTime = 91083;
  team_test::now_offset_guard time_guard;

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
  {
    auto storage = team_test::make_team_storage(kTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    team_test::add_storage_member(storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule param_module;
    param_module.mutable_battle()->mutable_matching_parameter()->mutable_parameter()->set_search_start_time(
        kSearchStartTime);
    team_test::add_storage_member(
        storage, kUserId,
        team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
            .set_shared_member_data({team_test::pack_member_module(team_test::make_member_ready_module(true)),
                                     team_test::pack_member_module(param_module)}));
    team_test::add_storage_member(storage, kOtherUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, storage));
  }
  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team_ptr);
  if (!team_ptr) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team_ptr->is_member(); }));

  CASE_EXPECT_TRUE(team_test::run_sync_task(test, "team.mts07_read", [&](rpc::context& ctx) -> rpc::result_code_type {
    // 自己: 读回种子数据
    auto self_member = user_team_battle_library_function::find_member(*team_ptr, team_test::make_user_key(kUserId));
    CASE_EXPECT_TRUE(!!self_member);
    if (self_member) {
      CASE_EXPECT_TRUE(user_team_battle_library_function::is_ready(*self_member));
      CASE_EXPECT_EQ(
          kSearchStartTime,
          user_team_battle_library_function::get_matching_team_parameter(*self_member).parameter().search_start_time());
    }
    // 另一成员: 未种子共享数据时回退默认值
    auto other_member =
        user_team_battle_library_function::find_member(*team_ptr, team_test::make_user_key(kOtherUserId));
    CASE_EXPECT_TRUE(!!other_member);
    if (other_member) {
      CASE_EXPECT_FALSE(user_team_battle_library_function::is_ready(*other_member));
      CASE_EXPECT_EQ(0, user_team_battle_library_function::get_matching_team_parameter(*other_member)
                            .parameter()
                            .search_start_time());
    }
    // 未知成员不创建幽灵缓存
    auto unknown_member =
        user_team_battle_library_function::find_member(*team_ptr, team_test::make_user_key(kUnknownUserId));
    CASE_EXPECT_FALSE(!!unknown_member);
    // 遍历恰好覆盖种子快照的全部成员(队长 + 自己 + 另一成员, 共三人), 其中仅自己 ready
    int visited_count = 0;
    int ready_count = 0;
    CASE_EXPECT_TRUE(user_team_battle_library_function::foreach_member(
        ctx, *team_ptr, [&visited_count, &ready_count](rpc::context&, const user_team_member_cache& member) {
          ++visited_count;
          if (user_team_battle_library_function::is_ready(member)) {
            ++ready_count;
          }
          return true;
        }));
    CASE_EXPECT_EQ(3, visited_count);
    CASE_EXPECT_EQ(1, ready_count);
    // 队伍级只读接口无缓存时回退默认实例
    CASE_EXPECT_EQ(0, user_team_battle_library_function::get_matching_team_sync_view(*team_ptr).unit_id());
    CASE_EXPECT_TRUE(user_team_battle_library_function::get_matching_start_data(*team_ptr).SerializeAsString().empty());
    RPC_RETURN_CODE(0);
  }));

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-08: 频道 battle.matching=false 取消本地待匹配 — 快照种子的 matching=true 在加载时先触发一次
// auto_check 反向修复(队长未在匹配流程), 之后 start_matching 进入待匹配状态并上行 matching=true;
// room 权威广播 matching=false 到达时, glue_layer_event_on_team_action_update_matching 发现本地仍在
// 待匹配窗口, 以 EN_ERR_TEAM_MEMBER_NOT_READY 取消待匹配状态(is_in_matching_start() 复位)且不再上行。
CASE_TEST(lobbysvr_user_team, matching_sync_08_channel_matching_false_cancels_pending_start) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30086;
  constexpr int64_t kTeamId = 584;
  team_test::now_offset_guard time_guard;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  // 自己是队长(OWNER)入队, 快照队伍共享数据种子 matching=true(登录恢复时队伍仍在匹配的权威状态)
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  {
    auto storage = team_test::make_team_storage(kTeamId);
    protobuf_copy_message(*storage.mutable_captain_user_key(), team_test::make_user_key(kUserId));
    team_test::add_storage_member(storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    protobuf_copy_message(*storage.add_shared_team_data(),
                          team_test::pack_team_module(team_test::make_team_matching_module(true)));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, storage));
  }
  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team_ptr);
  if (!team_ptr) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team_ptr->is_captain() && team_ptr->is_matching(); }));

  // 快照加载后的 auto_check 发现队伍匹配中但本地不在匹配流程: 队长上行一条 matching=false 修复
  // (附加队伍匹配中条件), 派生标志保持 true 等 room 权威广播
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 1; }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (1 == ss_capture.send_message_reqs.size()) {
    const auto& repair_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(repair_req, kTeamId, kUserId);
    const auto& team_update = repair_req.action().team_update();
    CASE_EXPECT_EQ(2, team_update.shared_team_data_size());
    if (2 == team_update.shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(team_update.shared_team_data(0), false);
      team_test::expect_packed_team_matching_team_view_entry(team_update.shared_team_data(1));
    }
    expect_team_is_matching_condition(team_update);
  }

  // start_matching 进入待匹配状态: 队长校验通过, 上行 matching=true(含全员 ready 条件)
  CASE_EXPECT_TRUE(
      team_test::run_sync_task(test, "team.mts08_start", [user_inst](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(user_inst->get_user_matching_manager().start_matching(ctx)));
      }));
  CASE_EXPECT_TRUE(user_inst->get_user_matching_manager().is_in_matching_start());
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kTeamUpdate) >= 2; }));
  CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (2 == ss_capture.send_message_reqs.size()) {
    const auto& start_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(start_req, kTeamId, kUserId);
    const auto& team_update = start_req.action().team_update();
    CASE_EXPECT_EQ(2, team_update.shared_team_data_size());
    if (2 == team_update.shared_team_data_size()) {
      team_test::expect_packed_team_matching_entry(team_update.shared_team_data(0), true);
      team_test::expect_packed_team_matching_team_view_entry(team_update.shared_team_data(1));
    }
    expect_all_member_ready_condition(team_update);
  }

  // room 权威广播 matching=false: 本地仍在待匹配窗口, glue 以 EN_ERR_TEAM_MEMBER_NOT_READY 取消待匹配,
  // 派生标志复位, 不再上行任何消息
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  CASE_EXPECT_TRUE(
      inject_team_update(test, team_chain, team_test::pack_team_module(team_test::make_team_matching_module(false))));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return !team_ptr->is_matching(); }));
  CASE_EXPECT_FALSE(user_inst->get_user_matching_manager().is_in_matching_start());
  CASE_EXPECT_FALSE(user_inst->get_user_matching_manager().is_in_matching());
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(2, static_cast<int>(ss_capture.send_message_reqs.size()));

  CASE_EXPECT_EQ(0, test.stop());
}

// MTS-09: async_update_member_shared_data 契约 — 空数组被两个 async_update_* 入口显式拒绝(返回 false,
// 零上行); 成员经 async_update_member_shared_data 提交 ready=true 时走统一批量更新: normalize 追加
// matching_parameter 自动补全条目(证明携带标准化数据), 并附加 "队伍不在匹配中" 更新条件。
CASE_TEST(lobbysvr_user_team, matching_sync_09_async_update_member_shared_data_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30087;
  constexpr int64_t kTeamId = 585;
  team_test::now_offset_guard time_guard;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  // 普通成员身份(成员共享数据是本人数据, 无队长门槛)
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));
  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team_ptr);
  if (!team_ptr) {
    test.stop();
    return;
  }

  // 空数组: 两个 async_update_* 入口都显式拒绝
  CASE_EXPECT_TRUE(
      team_test::run_sync_task(test, "team.mts09_empty", [&team_ptr](rpc::context& ctx) -> rpc::result_code_type {
        auto empty_team_data = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DTeamSharedDataModuleArray>(ctx);
        CASE_EXPECT_FALSE(team_ptr->async_update_team_shared_data(ctx, std::move(empty_team_data)));
        auto empty_member_data = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModuleArray>(ctx);
        CASE_EXPECT_FALSE(team_ptr->async_update_member_shared_data(ctx, std::move(empty_member_data)));
        RPC_RETURN_CODE(0);
      }));
  team_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.send_message_reqs.size()));

  // ready=true: 上行 member_update 携带 ready 条目与 normalize 追加的 matching_parameter 自动补全条目,
  // 附加 "队伍不在匹配中" 条件
  CASE_EXPECT_TRUE(
      team_test::run_sync_task(test, "team.mts09_ready", [&team_ptr](rpc::context& ctx) -> rpc::result_code_type {
        auto member_data = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModuleArray>(ctx);
        member_data->add_element()->mutable_battle()->set_ready(true);
        CASE_EXPECT_TRUE(team_ptr->async_update_member_shared_data(ctx, std::move(member_data)));
        RPC_RETURN_CODE(0);
      }));
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return ss_capture.send_message_action_count(atfw::team::DTeamAction::kMemberUpdate) >= 1; }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
  if (1 == ss_capture.send_message_reqs.size()) {
    const auto& action_req = ss_capture.send_message_reqs.back();
    team_test::expect_send_message_envelope(action_req, kTeamId, kUserId);
    const auto& member_update = action_req.action().member_update();
    CASE_EXPECT_EQ(kUserId, member_update.user_key().user_id());
    CASE_EXPECT_EQ(2, member_update.shared_member_data_size());
    if (2 == member_update.shared_member_data_size()) {
      team_test::expect_packed_member_ready_entry(member_update.shared_member_data(0), true);
      const auto& autocomplete_entry = member_update.shared_member_data(1);
      CASE_EXPECT_EQ(team_test::member_matching_parameter_data_key(), autocomplete_entry.key());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER, autocomplete_entry.value().permission());
      PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule autocomplete_unpacked;
      CASE_EXPECT_TRUE(autocomplete_entry.value().data().UnpackTo(&autocomplete_unpacked));
      CASE_EXPECT_TRUE(autocomplete_unpacked.has_battle());
      if (autocomplete_unpacked.has_battle()) {
        CASE_EXPECT_TRUE(autocomplete_unpacked.battle().has_matching_parameter());
        if (autocomplete_unpacked.battle().has_matching_parameter()) {
          CASE_EXPECT_GT(autocomplete_unpacked.battle().matching_parameter().parameter().search_start_time(), 0);
        }
      }
    }
    CASE_EXPECT_EQ(1, member_update.condition_size());
    if (1 == member_update.condition_size()) {
      const auto& rule = member_update.condition(0);
      CASE_EXPECT_EQ(1, rule.shared_team_data_size());
      if (1 == rule.shared_team_data_size()) {
        CASE_EXPECT_EQ(team_test::team_matching_data_key(), rule.shared_team_data(0).key());
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

// MTS-10: 取消匹配按操作时的实时队长身份鉴权。相同 Unit 下，普通成员取消时不得向 matchsvr 发包；
// 该成员当选队长后无需重建 Unit，即可发送取消请求。
CASE_TEST(lobbysvr_user_team, matching_sync_10_cancel_uses_current_captain) {
  constexpr uint64_t kMatchsvrId = 0x1E0012;
  constexpr uint64_t kUserId = 30088;
  constexpr uint64_t kUnitId = 91088;
  constexpr int64_t kTeamId = 586;

  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));

  atfw::testing::mock_node node;
  node.set_id(kMatchsvrId)
      .set_name("unit-test-matchsvr-captain-cancel")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr))
      .set_type_name("matchsvr")
      .set_zone_id(team_test::kZoneId)
      .add_label("hpa_scaling_ready", "1");
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }

  auto captured_cancels = std::make_shared<std::vector<PROJECT_NAMESPACE_ID::SSMatchingCancelReq>>();
  rpc::unit_test::ss_mock_rule_options cancel_rule_options;
  cancel_rule_options.match_node_id = kMatchsvrId;
  cancel_rule_options.times = 1;
  auto cancel_rule = rpc::matching::mock::cancel_matching(
      [captured_cancels](rpc::context&, const PROJECT_NAMESPACE_ID::SSMatchingCancelReq& request,
                         PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        captured_cancels->push_back(request);
        response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        response.set_matching_id("current-captain-cancel");
        response.mutable_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED);
        response.mutable_snapshot()->mutable_unit()->set_unit_id(request.unit_id());
        RPC_RETURN_CODE(0);
      },
      cancel_rule_options);
  CASE_EXPECT_TRUE(!!cancel_rule);
  if (!cancel_rule) {
    test.stop();
    return;
  }

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
  CASE_EXPECT_TRUE(team_test::join_team_with_snapshot(test, user_inst, private_chain, kTeamId,
                                                      atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, false, nullptr, {}));
  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team_ptr);
  if (!team_ptr) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(seed_in_matching_state(test, user_inst, kUnitId, kMatchsvrId));

  auto member_cancel_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  CASE_EXPECT_TRUE(team_test::run_sync_task(
      test, "team.mts10_member_cancel", [user_inst, member_cancel_result](rpc::context& ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::CSMatchingCancelReq request;
        PROJECT_NAMESPACE_ID::SCMatchingCancelRsp response;
        request.set_unit_id(kUnitId);
        *member_cancel_result =
            RPC_AWAIT_CODE_RESULT(user_inst->get_user_matching_manager().cancel_matching(ctx, request, response));
        RPC_RETURN_CODE(0);
      }));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_NOT_TEAM_CAPTAIN, *member_cancel_result);
  CASE_EXPECT_TRUE(captured_cancels->empty());

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  atfw::team::DTeamAction election_action;
  protobuf_copy_message(*election_action.mutable_election_captain()->mutable_user_key(),
                        team_test::make_user_key(kUserId));
  CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, election_action));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team_ptr->is_captain(); }));

  auto captain_cancel_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  CASE_EXPECT_TRUE(team_test::run_sync_task(
      test, "team.mts10_captain_cancel",
      [user_inst, captain_cancel_result](rpc::context& ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::CSMatchingCancelReq request;
        PROJECT_NAMESPACE_ID::SCMatchingCancelRsp response;
        request.set_unit_id(kUnitId);
        *captain_cancel_result =
            RPC_AWAIT_CODE_RESULT(user_inst->get_user_matching_manager().cancel_matching(ctx, request, response));
        RPC_RETURN_CODE(0);
      }));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SUCCESS, *captain_cancel_result);
  CASE_EXPECT_EQ(1, static_cast<int>(captured_cancels->size()));
  if (1 == captured_cancels->size()) {
    CASE_EXPECT_EQ(kUnitId, captured_cancels->front().unit_id());
    CASE_EXPECT_EQ(kUserId, captured_cancels->front().operator_user().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, captured_cancels->front().operator_user().zone_id());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

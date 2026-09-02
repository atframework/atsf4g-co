// Copyright 2026 atframework
// Offline regression tests for lobbysvr user_team/user_team_manager:
// - JOIN/CREATE lifecycle, sequence/hash guards (SEQ), heartbeat (HB), exit retry (EXIT)
//   and table dump/init round trips (DUMP). See src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md §3.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <string>
#include <utility>
#include <vector>

#include "lobbysvr_test_user_team_common.h"  // NOLINT: build/include_subdir

namespace {

// 逻辑当前时间(真实时间 + 全局 now offset): 过期时间必须基于它计算, 否则会被此前用例
// 通过 now_offset_guard 累积的 floor 污染而意外过期/不过期
inline std::chrono::system_clock::time_point logical_system_now() {
  return std::chrono::system_clock::now() + atfw::util::time::time_utility::get_global_now_offset();
}

// 注入一条完整字段的 joined_team 个人通知(team_key/user_key/team_channel/captain_user_key/user_role 五字段)
bool inject_joined_team_event(atfw::testing::runtime& test, team_test::channel_event_chain& private_chain,
                              const atfw::team::DTeamMemberJoinData& join_data) {
  atfw::team::DTeamMemberAction action;
  protobuf_copy_message(*action.mutable_joined_team(), join_data);
  return team_test::inject_event_message(test, private_chain, action);
}

// 注入一条本人的 invited 个人通知(最小字段集, room 侧会重填 admission 数据, 这里只关心 pending 生命周期)
bool inject_self_invited_event(atfw::testing::runtime& test, team_test::channel_event_chain& private_chain,
                               uint64_t user_id, int64_t team_id,
                               std::chrono::system_clock::time_point expired_timepoint) {
  atfw::team::DTeamMemberAction action;
  auto* invited = action.mutable_invited();
  protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
  protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(user_id));
  *invited->mutable_start_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  *invited->mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  return team_test::inject_event_message(test, private_chain, action);
}

// 注入一条本人的 apply_join_request 个人通知(room 归一化后的回执)
bool inject_self_apply_join_request_event(atfw::testing::runtime& test,
                                          team_test::channel_event_chain& private_chain, uint64_t user_id,
                                          int64_t team_id, std::chrono::system_clock::time_point expired_timepoint) {
  atfw::team::DTeamMemberAction action;
  auto* join_request = action.mutable_apply_join_request();
  protobuf_copy_message(*join_request->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*join_request->mutable_requester(), team_test::make_user_key(user_id));
  *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  return team_test::inject_event_message(test, private_chain, action);
}

// 注入一条本人的 reject_invitation 个人回执(room 删除自己的邀请缓存)
bool inject_reject_invitation_event(atfw::testing::runtime& test, team_test::channel_event_chain& private_chain,
                                    uint64_t user_id, int64_t team_id) {
  atfw::team::DTeamMemberAction action;
  auto* reject_invitation = action.mutable_reject_invitation();
  protobuf_copy_message(*reject_invitation->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*reject_invitation->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
  protobuf_copy_message(*reject_invitation->mutable_invitee(), team_test::make_user_key(user_id));
  return team_test::inject_event_message(test, private_chain, action);
}

// 注入一条本人的 reject_join_request 个人回执
bool inject_reject_join_request_event(atfw::testing::runtime& test, team_test::channel_event_chain& private_chain,
                                      uint64_t user_id, int64_t team_id) {
  atfw::team::DTeamMemberAction action;
  auto* reject_join_request = action.mutable_reject_join_request();
  protobuf_copy_message(*reject_join_request->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*reject_join_request->mutable_requester(), team_test::make_user_key(user_id));
  return team_test::inject_event_message(test, private_chain, action);
}

// 注入一条本人的个人 remove_member 通知(主动退出回环/被踢/解散)
bool inject_personal_remove_event(atfw::testing::runtime& test, team_test::channel_event_chain& private_chain,
                                  uint64_t user_id, int64_t team_id, atfw::team::EnTeamExitReason reason) {
  atfw::team::DTeamMemberAction action;
  auto* remove_member = action.mutable_remove_member();
  protobuf_copy_message(*remove_member->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*remove_member->mutable_user_key(), team_test::make_user_key(user_id));
  remove_member->set_remove_member_reason(reason);
  return team_test::inject_event_message(test, private_chain, action);
}

// 注入一条队伍频道 remove_member WAL 事件
bool inject_channel_remove_event(atfw::testing::runtime& test, team_test::channel_event_chain& team_chain,
                                 int64_t team_id, uint64_t user_id, atfw::team::EnTeamExitReason reason) {
  atfw::team::DTeamAction action;
  auto* remove_member = action.mutable_remove_member();
  protobuf_copy_message(*remove_member->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*remove_member->mutable_user_key(), team_test::make_user_key(user_id));
  remove_member->set_remove_member_reason(reason);
  return team_test::inject_event_message(test, team_chain, action);
}

// 携带 WAL 日志的快照事件: channel_snapshot.messages 会整体替换订阅端日志缓存,
// user_team::load_snapshot 随后从缓存重放 saved_action_sequence 之后的日志
atframework::dtmq::SSChannelEventSync make_snapshot_event_with_messages(
    const atframework::dtmq::DChannelIdKey& channel_key, int64_t create_sequence, int64_t last_sequence,
    const atfw::team::DTeamStorage& storage, int64_t custom_data_sequence, uint64_t last_hash_code,
    std::vector<atframework::dtmq::DChannelMessage> messages) {
  auto event_sync = team_test::make_snapshot_event(channel_key, create_sequence, last_sequence, &storage,
                                                   custom_data_sequence, last_hash_code);
  for (auto& msg : messages) {
    *event_sync.mutable_channel_snapshot()->add_messages() = std::move(msg);
  }
  return event_sync;
}

// 在真实 runtime task 内驱动 second refresh(过期准入清理 + 成员心跳)
bool run_second_refresh(atfw::testing::runtime& test, const user::ptr_t& user_ptr) {
  return team_test::run_sync_task(test, "team.refresh_second", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
    user_ptr->get_user_team_manager().refresh_feature_limit_second(ctx);
    RPC_RETURN_CODE(0);
  });
}

// 在真实 runtime task 内驱动 minute refresh(退出重试 + 防泄露收编)
bool run_minute_refresh(atfw::testing::runtime& test, const user::ptr_t& user_ptr) {
  return team_test::run_sync_task(test, "team.refresh_minute", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
    user_ptr->get_user_team_manager().refresh_feature_limit_minute(ctx);
    RPC_RETURN_CODE(0);
  });
}

// 在真实 runtime task 内直接调用 user_team::maybe_send_heartbeat(单元入口, 覆盖非 current 队伍的状态分支)
bool run_team_heartbeat(atfw::testing::runtime& test, const user_team::ptr_t& team) {
  return team_test::run_sync_task(test, "team.maybe_heartbeat", [&team](rpc::context& ctx) -> rpc::result_code_type {
    team->maybe_send_heartbeat(ctx);
    RPC_RETURN_CODE(0);
  });
}

// 在真实 runtime task 内调用 manager 的 create_team 协程并返回其业务结果码
int32_t run_create_team(atfw::testing::runtime& test, const user::ptr_t& user_ptr,
                        PROJECT_NAMESPACE_ID::EnTeamType team_type, atfw::team::DTeamKey& output_team_key) {
  int32_t ret = 0;
  bool ran = team_test::run_sync_task(test, "team.create_team", [&](rpc::context& ctx) -> rpc::result_code_type {
    ret = RPC_AWAIT_CODE_RESULT(user_ptr->get_user_team_manager().create_team(ctx, team_type, output_team_key));
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_TRUE(ran);
  return ret;
}

// 在真实 runtime task 内触发 manager 的主动退出(发送退出请求并转入 pending-to-exit)
bool run_exit_team(atfw::testing::runtime& test, const user::ptr_t& user_ptr, int64_t team_id,
                   atfw::team::EnTeamExitReason reason) {
  return team_test::run_sync_task(test, "team.exit_team", [&](rpc::context& ctx) -> rpc::result_code_type {
    user_ptr->get_user_team_manager().remove_team(ctx, team_test::make_team_key(team_id), reason);
    RPC_RETURN_CODE(0);
  });
}

// 在真实 runtime task 内执行 table init(登录恢复入口)
bool run_init_from_table(atfw::testing::runtime& test, const user::ptr_t& user_ptr,
                         const PROJECT_NAMESPACE_ID::table_user& table) {
  return team_test::run_sync_task(test, "team.init_from_table", [&](rpc::context& ctx) -> rpc::result_code_type {
    user_ptr->get_user_team_manager().init_from_table_data(ctx, table);
    RPC_RETURN_CODE(0);
  });
}

PROJECT_NAMESPACE_ID::table_user dump_team_table(user& user_inst) {
  PROJECT_NAMESPACE_ID::table_user dumped_table;
  rpc::context ctx{rpc::context::create_without_task()};
  CASE_EXPECT_EQ(0, user_inst.get_user_team_manager().dump(ctx, dumped_table));
  return dumped_table;
}

// table dump 中该队伍不得出现(group 可能整体消失或没有 current)
void expect_team_absent_in_table(user& user_inst, int64_t team_id) {
  auto dumped_table = dump_team_table(user_inst);
  for (const auto& group : dumped_table.team_data().group()) {
    if (group.has_current()) {
      CASE_EXPECT_TRUE(team_id != group.current().team_key().team_id());
    }
  }
}

size_t count_running_teams(const user::ptr_t& user_ptr) {
  size_t ret = 0;
  user_ptr->get_user_team_manager().foreach_running_team(
      [&ret](uint32_t, const atfw::util::nostd::nonnull<user_team::ptr_t>&) { ++ret; });
  return ret;
}

}  // namespace

// JOIN-01: joined_team 的五个字段完整进入新队; 订阅严格使用通知里的 team_channel; 非本人通知忽略;
// snapshot ready 后才判定成员(成员身份/角色以快照为准, 不以通知里的 user_role 判定成员资格)。
CASE_TEST(lobbysvr_user_team, joined_team_notification_registers_team) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30101;
  constexpr int64_t kTeamId = 611;
  constexpr uint64_t kOtherUserId = 91011;
  constexpr uint64_t kJoinedCaptainId = 91012;

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

  // 非本人 joined_team 通知必须忽略
  {
    auto join_data = team_test::make_join_data(kOtherUserId, kTeamId);
    CASE_EXPECT_TRUE(inject_joined_team_event(test, private_chain, join_data));
    team_test::pump_rounds(test, 3);
    CASE_EXPECT_TRUE(
        !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  }

  // 本人 joined_team: 使用与标准派生不同的 team_channel, 证明订阅严格使用通知里的频道
  atframework::dtmq::DChannelIdKey custom_channel;
  custom_channel.set_channel_type(atfw::team::EN_TEAM_CHANNEL_TYPE_TEAM_ROOM);
  custom_channel.set_channel_id(team_test::make_team_channel_key(kTeamId).channel_id() + ".join01");
  {
    atfw::team::DTeamMemberJoinData join_data;
    protobuf_copy_message(*join_data.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*join_data.mutable_user_key(), team_test::make_user_key(kUserId));
    protobuf_copy_message(*join_data.mutable_team_channel(), custom_channel);
    join_data.set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    protobuf_copy_message(*join_data.mutable_captain_user_key(), team_test::make_user_key(kJoinedCaptainId));
    CASE_EXPECT_TRUE(inject_joined_team_event(test, private_chain, join_data));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }

  // 五字段落地: team_key/team_channel/captain/user_role(user_key 即所属玩家, 由 manager 归属体现)
  CASE_EXPECT_EQ(kTeamId, team->get_team_key().team_id());
  CASE_EXPECT_EQ(custom_channel.channel_id(), team->get_channel_key().channel_id());
  CASE_EXPECT_EQ(custom_channel.channel_type(), team->get_channel_key().channel_type());
  CASE_EXPECT_EQ(kJoinedCaptainId, team->get_cached_captain_user_key().user_id());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, team->get_cached_permission_role());

  // snapshot ready 前不判定成员: 缓存无成员, 且 refresh 不发心跳(频道未 ready)
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_EQ(0, snapshot.snapshot().member_size());
  }
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_TRUE(ss_capture.heartbeat_reqs.empty());

  // 快照经自定义频道驱动(订阅确实落在 team_channel 上); 成员身份与角色以快照为准(ADMIN -> NORMAL)
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
    team_test::add_storage_member(team_storage, kJoinedCaptainId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(
        team_test::receive_channel_event(test, team_test::make_snapshot_event(custom_channel, 1, 0, &team_storage)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kUserId));
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kJoinedCaptainId));
  }

  // ready 且确认是成员后 refresh 才发心跳
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return !ss_capture.heartbeat_reqs.empty(); }));

  CASE_EXPECT_EQ(0, test.stop());
}

// JOIN-02: 同组 A->B 切队: A 转 pending-to-exit 并发出 reason=IN_ANOTHER_TEAM 的退出请求;
// table 只保存 B/current 和 team_type; A 的缓存随对象保留(退出中)。
CASE_TEST(lobbysvr_user_team, switch_team_moves_previous_to_pending_exit) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30102;
  constexpr int64_t kFirstTeamId = 621;
  constexpr int64_t kSecondTeamId = 622;

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

  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kFirstTeamId));
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kFirstTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kFirstTeamId, team_storage));
  }
  auto first_team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kFirstTeamId));
  CASE_EXPECT_TRUE(!!first_team);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == first_team->get_cached_permission_role();
  }));

  // 切换到 B
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kSecondTeamId));
  {
    auto current =
        user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    CASE_EXPECT_TRUE(!!current);
    if (current) {
      CASE_EXPECT_EQ(kSecondTeamId, current->get_team_key().team_id());
    }
  }
  // A 仍在索引中且处于退出流程
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kFirstTeamId)));
  CASE_EXPECT_TRUE(first_team->is_exiting());

  // 切队必须发出恰好一条 reason=IN_ANOTHER_TEAM 的 A 退出请求, payload 完整
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kFirstTeamId, kUserId) >= 1;
  }));
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kFirstTeamId, kUserId)));
  for (const auto& req : ss_capture.send_message_reqs) {
    if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == kFirstTeamId) {
      CASE_EXPECT_EQ(team_test::kZoneId, req.action().remove_member().team_key().zone_id());
      CASE_EXPECT_EQ(kUserId, req.action().remove_member().user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_IN_ANOTHER_TEAM,
                     req.action().remove_member().remove_member_reason());
    }
  }

  // 退出中 A 的成员缓存仍随对象保留
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    first_team->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kUserId));
  }

  // table 只保存 B/current 和 team_type, A( pending-to-exit )不落地
  {
    auto dumped_table = dump_team_table(*user_inst);
    CASE_EXPECT_EQ(1, dumped_table.team_data().group_size());
    if (dumped_table.team_data().group_size() > 0) {
      const auto& group = dumped_table.team_data().group(0);
      CASE_EXPECT_EQ(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL), group.team_type());
      CASE_EXPECT_TRUE(group.has_current());
      if (group.has_current()) {
        CASE_EXPECT_EQ(kSecondTeamId, group.current().team_key().team_id());
        CASE_EXPECT_EQ(kUserId, group.current().user_key().user_id());
        CASE_EXPECT_EQ(team_test::make_team_channel_key(kSecondTeamId).channel_id(),
                       group.current().team_channel().channel_id());
        CASE_EXPECT_EQ(team_test::kCaptainUserId, group.current().captain_user_key().user_id());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, group.current().user_role());
      }
    }
    expect_team_absent_in_table(*user_inst, kFirstTeamId);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// JOIN-03: pending-to-exit 的 A 再收到 joined: A 恢复 current(通知里的新 captain/role 生效), B 转退出;
// 旧 A 不泄漏(table 只有 A), 恢复后 minute refresh 不重试 A 的旧退出请求, B 的退出按 retry 边界重试。
CASE_TEST(lobbysvr_user_team, rejoin_pending_exit_team_swaps_back) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30103;
  constexpr int64_t kFirstTeamId = 631;
  constexpr int64_t kSecondTeamId = 632;
  constexpr uint64_t kNewCaptainId = 91033;

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

  // A 先成为成员(快照确认): 否则 minute refresh 的防泄漏分支会对"非成员 current 队伍"补发退出请求
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kFirstTeamId));
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kFirstTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kFirstTeamId, team_storage));
  }
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kSecondTeamId));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kFirstTeamId, kUserId) >= 1;
  }));
  auto first_team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kFirstTeamId));
  CASE_EXPECT_TRUE(!!first_team);
  if (!first_team) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(first_team->is_exiting());

  // A 重新加入(退出请求仍在飞行中): 恢复为 current, 通知里的新 captain/role 生效
  {
    auto join_data = team_test::make_join_data(kUserId, kFirstTeamId);
    join_data.set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
    protobuf_copy_message(*join_data.mutable_captain_user_key(), team_test::make_user_key(kNewCaptainId));
    CASE_EXPECT_TRUE(inject_joined_team_event(test, private_chain, join_data));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current =
        user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    return current && current->get_team_key().team_id() == kFirstTeamId;
  }));
  CASE_EXPECT_TRUE(!first_team->is_exiting());
  CASE_EXPECT_EQ(kNewCaptainId, first_team->get_cached_captain_user_key().user_id());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, first_team->get_cached_permission_role());

  // B 转入退出队列并发出 IN_ANOTHER_TEAM 退出请求
  auto second_team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kSecondTeamId));
  CASE_EXPECT_TRUE(!!second_team);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kSecondTeamId, kUserId) >= 1;
  }));
  if (second_team) {
    CASE_EXPECT_TRUE(second_team->is_exiting());
  }

  // 旧 A 不泄漏: table 只有 A/current
  {
    auto dumped_table = dump_team_table(*user_inst);
    CASE_EXPECT_EQ(1, dumped_table.team_data().group_size());
    expect_team_absent_in_table(*user_inst, kSecondTeamId);
  }

  // 推进超过 retry 边界: minute refresh 只重试 B 的退出, A 的旧退出请求不再重试
  {
    team_test::now_offset_guard time_guard;
    team_test::now_offset_guard::advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
    CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kSecondTeamId, kUserId) >= 2;
  }));
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kFirstTeamId, kUserId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// CREATE-01: create 上行 payload 完整(team_id=0/zone、sender/私有频道、client_version/router、configure 默认空、
// 两类初始 shared data); 响应成功后无需 joined_team 即注册 OWNER/captain/self, 输出 key 精确等于响应;
// 注册时清理同 team_key 的自己的 invitation/join request。
CASE_TEST(lobbysvr_user_team, create_team_registers_owner_and_clears_pending) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30104;
  constexpr int64_t kCreatedTeamId = 700641;
  ss_capture.next_allocated_team_id = kCreatedTeamId;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  // 上报可区分的客户端版本
  {
    PROJECT_NAMESPACE_ID::DClientDeviceInfo client_info;
    client_info.set_client_version("create-01-client-v1");
    CASE_EXPECT_TRUE(team_test::run_sync_task(
        test, "team.set_client_info", [&user_inst, &client_info](rpc::context&) -> rpc::result_code_type {
          user_inst->set_client_info(client_info);
          RPC_RETURN_CODE(0);
        }));
  }

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  auto valid_expiry = logical_system_now() + std::chrono::seconds(600);

  // 预置同 team_key 的陈旧邀请/加入请求(例如他人重复邀请或此前申请未收到回执)
  CASE_EXPECT_TRUE(inject_self_invited_event(test, private_chain, kUserId, kCreatedTeamId, valid_expiry));
  CASE_EXPECT_TRUE(inject_self_apply_join_request_event(test, private_chain, kUserId, kCreatedTeamId, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto& mgr = user_inst->get_user_team_manager();
    return !!mgr.get_pending_invitation(team_test::make_team_key(kCreatedTeamId)) &&
           !!mgr.get_pending_join_request(team_test::make_team_key(kCreatedTeamId));
  }));

  atfw::team::DTeamKey output_team_key;
  int32_t ret = run_create_team(test, user_inst, PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL, output_team_key);
  CASE_EXPECT_EQ(0, ret);

  // 输出 key 精确等于响应(zone_id + 分配的 team_id)
  CASE_EXPECT_EQ(kCreatedTeamId, output_team_key.team_id());
  CASE_EXPECT_EQ(team_test::kZoneId, output_team_key.zone_id());

  // 上行 payload 完整
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.create_reqs.size()));
  if (!ss_capture.create_reqs.empty()) {
    const auto& req = ss_capture.create_reqs.front();
    // team_id 置 0 由 room 分配, zone_id 参与一致性哈希路由
    CASE_EXPECT_EQ(0, req.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, req.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, req.sender_user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, req.sender_user_key().zone_id());
    // 创建者私有频道
    CASE_EXPECT_EQ(private_channel_key.channel_type(), req.sender_user_channel().channel_type());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), req.sender_user_channel().channel_id());
    // 客户端版本与成员通知路由
    CASE_EXPECT_EQ(std::string("create-01-client-v1"), req.client_version());
    CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), req.user_router_server_id());
    // configure 保持默认空值(由 room 修订默认门槛)
    CASE_EXPECT_TRUE(!req.has_configure());
    // 两类初始 shared data: 队伍 battle.matching=false + 成员 battle.ready=false
    CASE_EXPECT_EQ(1, req.shared_team_data_size());
    if (req.shared_team_data_size() > 0) {
      const auto& team_data = req.shared_team_data(0);
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER, team_data.value().permission());
      PROJECT_NAMESPACE_ID::DTeamSharedDataModule module;
      CASE_EXPECT_TRUE(team_data.value().data().UnpackTo(&module));
      CASE_EXPECT_TRUE(module.has_battle());
      CASE_EXPECT_TRUE(!module.battle().matching());
      CASE_EXPECT_EQ(user_team_algorithm::make_team_shared_data_key(module), team_data.key());
    }
    CASE_EXPECT_EQ(1, req.shared_member_data_size());
    if (req.shared_member_data_size() > 0) {
      const auto& member_data = req.shared_member_data(0);
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER, member_data.value().permission());
      PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule module;
      CASE_EXPECT_TRUE(member_data.value().data().UnpackTo(&module));
      CASE_EXPECT_TRUE(module.has_battle());
      CASE_EXPECT_TRUE(!module.battle().ready());
      CASE_EXPECT_EQ(user_team_algorithm::make_team_member_shared_data_key(module), member_data.key());
    }
  }

  // 无需 joined_team 即注册: 创建者即队长(OWNER), 频道使用响应里的 room_channel
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kCreatedTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  {
    auto current =
        user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    CASE_EXPECT_TRUE(!!current);
    if (current) {
      CASE_EXPECT_EQ(kCreatedTeamId, current->get_team_key().team_id());
    }
    CASE_EXPECT_EQ(kUserId, team->get_cached_captain_user_key().user_id());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, team->get_cached_permission_role());
    CASE_EXPECT_EQ(team_test::make_team_channel_key(kCreatedTeamId).channel_id(), team->get_channel_key().channel_id());
  }

  // 注册后同 team_key 的自己的 invitation/join request 必须清理
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kCreatedTeamId)));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kCreatedTeamId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// CREATE-02: client_result 业务失败不注册、不修改 output/index, 错误码精确透传;
// 成功后频道 ready 前无缓存/无心跳; 首个快照携带压缩点后的 WAL 日志时,
// user_team::load_snapshot 重放日志恢复缓存(不丢事件), 回放期间的变更被快照吞并(只下发最终 snapshot),
// 之后的实时事件正常下发 increase。
CASE_TEST(lobbysvr_user_team, create_team_failure_passthrough_and_wal_replay) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30105;
  constexpr uint64_t kSessionId = 0x10105;
  constexpr int64_t kCreatedTeamId = 700651;
  constexpr uint64_t kReplayedMemberId = 91051;
  ss_capture.next_allocated_team_id = kCreatedTeamId;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));

  // 1. 业务失败: 错误码精确透传, 不注册队伍、不修改 output
  ss_capture.create_responder = [](const atfw::team::SSTeamRoomCreateReq&, atfw::team::SSTeamRoomCreateRsp&) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM;
  };
  atfw::team::DTeamKey output_team_key;
  int32_t ret = run_create_team(test, user_inst, PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL, output_team_key);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM, ret);
  CASE_EXPECT_EQ(0, output_team_key.team_id());
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kCreatedTeamId)));
  ss_capture.create_responder = nullptr;

  // 2. 成功: 注册后频道 ready 前无缓存、无快照脏数据、refresh 不发心跳
  ret = run_create_team(test, user_inst, PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL, output_team_key);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(kCreatedTeamId, output_team_key.team_id());
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kCreatedTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_EQ(0, snapshot.snapshot().member_size());
  }
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_TRUE(ss_capture.heartbeat_reqs.empty());
  CASE_EXPECT_TRUE(team_test::collect_team_dirty(test, kSessionId, kCreatedTeamId).snapshots.empty());

  // 3. 首个快照携带 WAL 日志: 压缩点(saved_action_sequence=0)之后的日志全部重放, 缓存与事件流一致
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kCreatedTeamId, 0);
  protobuf_copy_message(*team_storage.mutable_captain_user_key(), team_test::make_user_key(kUserId));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));

  std::vector<atframework::dtmq::DChannelMessage> replay_msgs;
  {
    atfw::team::DTeamAction action;
    auto* member = action.mutable_add_member();
    protobuf_copy_message(*member->mutable_user_key(), team_test::make_user_key(kReplayedMemberId));
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
    replay_msgs.push_back(team_test::make_event_message(1, action));
  }
  {
    atfw::team::DTeamAction action;
    *action.mutable_team_update()->add_shared_team_data() =
        team_test::pack_team_module(team_test::make_team_matching_module(true));
    replay_msgs.push_back(team_test::make_event_message(2, action));
  }
  uint64_t replayed_hash = team_test::chain_message_hashes(replay_msgs, 0);

  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, make_snapshot_event_with_messages(team_test::make_team_channel_key(kCreatedTeamId), 1, 2, team_storage, 1,
                                              replayed_hash, replay_msgs)));
  // 重放不丢事件: add_member 与 team_update 的效果都进入缓存
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kReplayedMemberId);
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kUserId));
    CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
    if (snapshot.shared_team_data_size() > 0) {
      CASE_EXPECT_TRUE(snapshot.shared_team_data(0).has_battle());
      CASE_EXPECT_TRUE(snapshot.shared_team_data(0).battle().matching());
    }
  }
  // 回放期间的变更被快照吞并: 客户端只收到一条 snapshot, 没有 increase
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kCreatedTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    CASE_EXPECT_TRUE(view.actions.empty());
  }

  // 4. 之后的实时事件正常下发 increase
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kCreatedTeamId);
  team_chain.sequence = 2;
  team_chain.hash_code = replayed_hash;
  {
    atfw::team::DTeamAction action;
    auto* member_update = action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kUserId));
    member_update->set_client_version("create-02-v1");
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kCreatedTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate).empty();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kCreatedTeamId);
    auto updates = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate);
    CASE_EXPECT_EQ(1, static_cast<int>(updates.size()));
    if (!updates.empty()) {
      CASE_EXPECT_EQ(kUserId, updates.front()->action().member_update().user_key().user_id());
      CASE_EXPECT_EQ(std::string("create-02-v1"), updates.front()->action().member_update().client_version());
    }
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// CREATE-03: 非法 team_type(未设置/配置表中不存在)直接拒绝: 不上行 create、不注册本地队伍、不修改输出 key
CASE_TEST(lobbysvr_user_team, create_team_rejects_invalid_team_type) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));
  ss_capture.next_allocated_team_id = 730101;

  constexpr uint64_t kUserId = 73001;
  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  constexpr int64_t kPreservedTeamId = 730999;
  atfw::team::DTeamKey output_team_key;
  output_team_key.set_team_id(kPreservedTeamId);
  // 未设置(0=INVALID)
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVALID_TEAM_TYPE,
                 run_create_team(test, user_inst, PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_INVALID, output_team_key));
  // 配置表中不存在
  // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVALID_TEAM_TYPE,
                 run_create_team(test, user_inst, static_cast<PROJECT_NAMESPACE_ID::EnTeamType>(999),
                                 output_team_key));
  // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)

  // 零上行、零注册、输出 key 未修改
  CASE_EXPECT_EQ(0, static_cast<int>(ss_capture.create_reqs.size()));
  CASE_EXPECT_EQ(kPreservedTeamId, output_team_key.team_id());
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_INVALID));

  CASE_EXPECT_EQ(0, test.stop());
}

// SEQ-01: 快照 saved_action_sequence=N 时, 重放阶段 <=N 的日志不应用(其效果由快照覆盖), >N 按 hash chain 应用;
// 重复/旧日志不重复应用也不重复下发 dirty; hash 不匹配的事件不应用、不推进已确认序号, 由后续快照恢复。
CASE_TEST(lobbysvr_user_team, snapshot_saved_sequence_guards_incremental_replay) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));

  constexpr uint64_t kUserId = 30106;
  constexpr uint64_t kSessionId = 0x10106;
  constexpr int64_t kTeamId = 661;
  constexpr uint64_t kSkippedMemberId1 = 93011;
  constexpr uint64_t kSkippedMemberId2 = 93012;
  constexpr uint64_t kAppliedMemberId = 93013;
  constexpr uint64_t kMismatchMemberId = 93014;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }

  auto add_member_message = [](int64_t sequence, uint64_t user_id) {
    atfw::team::DTeamAction action;
    auto* member = action.mutable_add_member();
    protobuf_copy_message(*member->mutable_user_key(), team_test::make_user_key(user_id));
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
    return team_test::make_event_message(sequence, action);
  };

  // 快照 saved_action_sequence=2, WAL 缓存携带日志 1..3
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId, 2);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  std::vector<atframework::dtmq::DChannelMessage> replay_msgs;
  replay_msgs.push_back(add_member_message(1, kSkippedMemberId1));
  replay_msgs.push_back(add_member_message(2, kSkippedMemberId2));
  replay_msgs.push_back(add_member_message(3, kAppliedMemberId));
  uint64_t chained_hash = team_test::chain_message_hashes(replay_msgs, 0);
  const auto applied_msg = replay_msgs.back();
  const auto skipped_msg = replay_msgs[1];

  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, make_snapshot_event_with_messages(team_test::make_team_channel_key(kTeamId), 1, 3, team_storage, 1,
                                              chained_hash, replay_msgs)));
  // <=saved 的日志不应用, >saved 的日志应用
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kAppliedMemberId);
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kSkippedMemberId1));
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kSkippedMemberId2));
  }
  // 重放被快照吞并: 只有一条 snapshot, 没有 increase
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    CASE_EXPECT_TRUE(view.actions.empty());
  }

  // 重复日志(同 sequence/hash 重投): 不重复应用, 不重复下发
  {
    std::vector<atframework::dtmq::DChannelMessage> dup_msgs{applied_msg};
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_incremental_event(team_test::make_team_channel_key(kTeamId), 3, dup_msgs)));
  }
  // 旧日志(sequence 不高于已保存序号): 忽略
  {
    std::vector<atframework::dtmq::DChannelMessage> old_msgs{skipped_msg};
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_incremental_event(team_test::make_team_channel_key(kTeamId), 3, old_msgs)));
  }
  team_test::pump_rounds(test, 3);
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    CASE_EXPECT_TRUE(view.actions.empty());
  }

  // hash 不匹配: 事件不应用、不推进已确认序号
  auto mismatch_msg = add_member_message(4, kMismatchMemberId);
  uint64_t real_hash = 0;
  {
    std::vector<atframework::dtmq::DChannelMessage> real_msgs{mismatch_msg};
    real_hash = team_test::chain_message_hashes(real_msgs, chained_hash);
    mismatch_msg.set_hash_code(real_hash ^ 0x9E3779B97F4A7C15ULL);
  }
  {
    std::vector<atframework::dtmq::DChannelMessage> bad_msgs{mismatch_msg};
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_incremental_event(team_test::make_team_channel_key(kTeamId), 4, bad_msgs)));
  }
  team_test::pump_rounds(test, 3);
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kMismatchMemberId));
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
  }

  // hash 不匹配后由后续快照恢复权威状态(新快照覆盖, 重放仍遵守 saved 序号)
  atfw::team::DTeamStorage recovery_storage = team_test::make_team_storage(kTeamId, 4);
  team_test::add_storage_member(recovery_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(recovery_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  team_test::add_storage_member(recovery_storage, kAppliedMemberId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  team_test::add_storage_member(recovery_storage, kMismatchMemberId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  {
    std::vector<atframework::dtmq::DChannelMessage> recovered_msgs{add_member_message(4, kMismatchMemberId)};
    team_test::chain_message_hashes(recovered_msgs, chained_hash);
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, make_snapshot_event_with_messages(team_test::make_team_channel_key(kTeamId), 1, 4, recovery_storage, 2,
                                                real_hash, recovered_msgs)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kMismatchMemberId);
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_EQ(4, snapshot.snapshot().member_size());
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(2, static_cast<int>(view.snapshots.size()));
    CASE_EXPECT_TRUE(view.actions.empty());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// SEQ-02: 压缩快照覆盖旧缓存(旧成员/旧共享 key 不泄漏); 快照重放期间的 action 被 snapshot 吞并,
// 只下发最终 snapshot; 实时 action 下发 increase。
CASE_TEST(lobbysvr_user_team, compacted_snapshot_overrides_cache_and_swallows_replay) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));

  constexpr uint64_t kUserId = 30107;
  constexpr uint64_t kSessionId = 0x10107;
  constexpr int64_t kTeamId = 662;
  constexpr uint64_t kStaleMemberId = 93101;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }

  // 初始快照: 含一个随后会被压缩快照淘汰的旧成员
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId, 0);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    team_test::add_storage_member(team_storage, kStaleMemberId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 实时事件: member_update(self, version + ready) / team_update(matching)
  {
    atfw::team::DTeamAction action;
    auto* member_update = action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kUserId));
    member_update->set_client_version("seq02-v1");
    *member_update->add_shared_member_data() =
        team_test::pack_member_module(team_test::make_member_ready_module(true));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  {
    atfw::team::DTeamAction action;
    *action.mutable_team_update()->add_shared_team_data() =
        team_test::pack_team_module(team_test::make_team_matching_module(true));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kTeamUpdate).empty();
  }));

  // 压缩快照: saved_action_sequence=2 覆盖上述两条日志, 成员表淘汰旧成员, 携带最终版本/共享数据;
  // 快照不携带 WAL 日志(已被压缩), custom_data_sequence 递增才会被订阅端接受
  {
    atfw::team::DTeamStorage compacted_storage = team_test::make_team_storage(kTeamId, 2);
    team_test::add_storage_member(compacted_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(
        compacted_storage, kUserId,
        team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
            .set_client_version("seq02-v1")
            .set_shared_member_data({team_test::pack_member_module(
                team_test::make_member_ready_module(true))}));
    *compacted_storage.add_shared_team_data() =
        team_test::pack_team_module(team_test::make_team_matching_module(true));
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 2, &compacted_storage, 2,
                                             team_chain.hash_code)));
  }

  // 快照覆盖旧缓存: 旧成员被清理, 版本/共享数据与压缩快照逐项一致
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.size() >= 2;
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kStaleMemberId));
    const auto* self_member = team_test::find_snapshot_member(snapshot, kUserId);
    CASE_EXPECT_TRUE(nullptr != self_member);
    if (nullptr != self_member) {
      CASE_EXPECT_EQ(std::string("seq02-v1"), self_member->client_version());
    }
    const auto* unpacked = team_test::find_unpacked_member(snapshot, kUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked);
    if (nullptr != unpacked) {
      CASE_EXPECT_EQ(1, unpacked->shared_member_data_size());
      if (unpacked->shared_member_data_size() > 0) {
        CASE_EXPECT_TRUE(unpacked->shared_member_data(0).battle().ready());
      }
    }
    CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
    if (snapshot.shared_team_data_size() > 0) {
      CASE_EXPECT_TRUE(snapshot.shared_team_data(0).battle().matching());
    }
  }
  // 回放吞并: 没有为重放期间的事件补发 increase(成员更新/队伍更新各只出现一次, 来自实时阶段)
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(2, static_cast<int>(view.snapshots.size()));
    CASE_EXPECT_EQ(1, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate)));
    CASE_EXPECT_EQ(1,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kTeamUpdate)));
  }

  // 压缩点之后的实时事件正常下发 increase
  {
    atfw::team::DTeamAction action;
    auto* member_update = action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kUserId));
    member_update->set_client_version("seq02-v2");
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return team_test::count_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate) >= 2;
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto updates = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kMemberUpdate);
    CASE_EXPECT_EQ(2, static_cast<int>(updates.size()));
    if (updates.size() >= 2) {
      CASE_EXPECT_EQ(std::string("seq02-v2"), updates[1]->action().member_update().client_version());
    }
    CASE_EXPECT_EQ(2, static_cast<int>(view.snapshots.size()));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// SEQ-03: 旧代际销毁后重新创建(高 create sequence 新代际快照): 乱序到达的旧代际 destroy 日志(重投)与
// metadata-only destroy 事件都不删除新代际; 真正的频道销毁只有 WAL destroy 日志(或带快照的 destroy 元数据)
// 才生效, 且新代际 create sequence 之后的 destroy 正常收编。
CASE_TEST(lobbysvr_user_team, stale_destroy_does_not_remove_new_generation) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));

  constexpr uint64_t kUserId = 30108;
  constexpr uint64_t kSessionId = 0x10108;
  constexpr int64_t kTeamId = 663;
  constexpr uint64_t kOldMemberId = 93201;
  constexpr uint64_t kNewMemberId = 93202;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  const auto team_channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. 旧代际: create_sequence=1 的快照 + WAL destroy 日志销毁
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  {
    atfw::team::DTeamStorage gen1_storage = team_test::make_team_storage(kTeamId, 0);
    team_test::add_storage_member(gen1_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(gen1_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    team_test::add_storage_member(gen1_storage, kOldMemberId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(
        team_test::receive_channel_event(test, team_test::make_snapshot_event(team_channel_key, 1, 0, &gen1_storage)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));

  team_test::channel_event_chain gen1_chain;
  gen1_chain.channel_key = team_channel_key;
  CASE_EXPECT_TRUE(team_test::inject_log_message(test, gen1_chain, team_test::make_destroy_log_message(0)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam)));
  }

  // 2. 重新创建: 再次加入 + 高 create sequence(10) 的新代际快照
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  {
    atfw::team::DTeamStorage gen2_storage = team_test::make_team_storage(kTeamId, 0);
    team_test::add_storage_member(gen2_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(gen2_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    team_test::add_storage_member(gen2_storage, kNewMemberId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_channel_key, 10, 10, &gen2_storage, 2)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kNewMemberId);
  }));
  {
    // 新代际缓存权威重建, 旧代际成员不泄漏
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kOldMemberId));
    CASE_EXPECT_TRUE(!team->is_destroyed());
  }

  // 3a. 旧代际 destroy 日志重投(与旧代际销毁同一条日志, 不高于订阅端已处理序号): 忽略, 新代际不受影响
  {
    std::vector<atframework::dtmq::DChannelMessage> stale_msgs{team_test::make_destroy_log_message(1)};
    team_test::chain_message_hashes(stale_msgs, 0);
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_incremental_event(team_channel_key, 10, stale_msgs)));
  }
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(!team->is_destroyed());

  // 3b. metadata-only 的 destroy 事件(destroy_sequence 低于新代际 create_sequence): 不销毁
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_channel_destroyed_event(team_channel_key, 10, 8, 10)));
  // 3c. metadata-only 的 destroy 事件(destroy_sequence 高于 create_sequence): 同样不销毁,
  //     频道销毁只经 WAL destroy 日志或快照元数据生效
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_channel_destroyed_event(team_channel_key, 10, 11, 10)));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(!team->is_destroyed());
  {
    // 上述乱序事件不产生新的解散通知, 缓存仍是新代际
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kNewMemberId));
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam)));
  }

  // 4. 对照: 新代际 create sequence 之后的 WAL destroy 日志正常收编
  team_test::channel_event_chain gen2_chain;
  gen2_chain.channel_key = team_channel_key;
  gen2_chain.sequence = 11;
  CASE_EXPECT_TRUE(team_test::inject_log_message(test, gen2_chain, team_test::make_destroy_log_message(0)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(2,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam)));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// HB-01: 成员且 ready 时心跳上报 team/user/router 和最新已确认 sequence/hash; 间隔内节流, 达到边界再发。
// 同时锁定配置事实: heartbeat_interval 注解默认 120s(min 5s), wait_add_member_timeout 注解默认 5s。
CASE_TEST(lobbysvr_user_team, heartbeat_reports_watermark_and_throttles) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  // 真实配置值(lobbysvr_config.proto 注解默认), 不写死旧的代码 fallback
  CASE_EXPECT_EQ(120, static_cast<int>(
                          std::chrono::duration_cast<std::chrono::seconds>(team_test::get_heartbeat_interval())
                              .count()));
  CASE_EXPECT_EQ(5, static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                         team_test::get_wait_add_member_timeout())
                                         .count()));

  constexpr uint64_t kUserId = 30109;
  constexpr int64_t kTeamId = 664;

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
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));

  // 应用两条频道事件, 让已确认序号非零
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  {
    atfw::team::DTeamAction action;
    auto* member_update = action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(kUserId));
    member_update->set_client_version("hb-01-v1");
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  {
    atfw::team::DTeamAction action;
    *action.mutable_team_update()->add_shared_team_data() =
        team_test::pack_team_module(team_test::make_team_matching_module(true));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }

  // 守卫构造把逻辑时间抬到进程 floor: 之后的首次心跳时间戳与时间快进共享同一基准,
  // 否则“间隔内节流”断言会被此前用例累积的 floor 污染
  team_test::now_offset_guard time_guard;

  // 首次心跳: 上报完整身份与最新 sequence/hash
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return !ss_capture.heartbeat_reqs.empty(); }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.heartbeat_reqs.size()));
  {
    const auto& req = ss_capture.heartbeat_reqs.front();
    CASE_EXPECT_EQ(kTeamId, req.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, req.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, req.user_key().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, req.user_key().zone_id());
    CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), req.user_router_server_id());
    CASE_EXPECT_EQ(2, static_cast<int>(req.sequence()));
    CASE_EXPECT_TRUE(team_chain.hash_code == req.hash_code());
  }

  // 间隔内节流: 不重复发送
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.heartbeat_reqs.size()));

  // 未达到边界仍节流; 达到边界后再发, 上报的已确认序号不变(只增不减)
  team_test::now_offset_guard::advance(team_test::get_heartbeat_interval() - std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.heartbeat_reqs.size()));
  team_test::now_offset_guard::advance(std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return ss_capture.heartbeat_reqs.size() >= 2; }));
  {
    const auto& req = ss_capture.heartbeat_reqs.back();
    CASE_EXPECT_EQ(kTeamId, req.team_key().team_id());
    CASE_EXPECT_EQ(2, static_cast<int>(req.sequence()));
    CASE_EXPECT_TRUE(team_chain.hash_code == req.hash_code());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// HB-02: 未 ready、ready 但非成员、退出中、频道已销毁均不发心跳; 成员且 ready 时发送(对照)。
CASE_TEST(lobbysvr_user_team, heartbeat_suppressed_outside_running_member_state) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30110;
  constexpr int64_t kTeamId = 665;

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
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }

  // 1. 频道未 ready: 不发
  CASE_EXPECT_TRUE(run_team_heartbeat(test, team));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_TRUE(ss_capture.heartbeat_reqs.empty());

  // 2. ready 但非成员(快照不含自己): 不发
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    return 1 == snapshot.snapshot().member_size();
  }));
  CASE_EXPECT_TRUE(run_team_heartbeat(test, team));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_TRUE(ss_capture.heartbeat_reqs.empty());

  // 3. 成员且 ready(对照): 发送
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 0, &team_storage, 2)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return !ss_capture.heartbeat_reqs.empty(); }));
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.heartbeat_reqs.size()));

  // 4. 退出中: 不发
  CASE_EXPECT_TRUE(run_exit_team(test, user_inst, kTeamId, atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  CASE_EXPECT_TRUE(team->is_exiting());
  CASE_EXPECT_TRUE(run_team_heartbeat(test, team));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.heartbeat_reqs.size()));

  // 5. 频道已销毁: 不发
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  CASE_EXPECT_TRUE(team_test::inject_log_message(test, team_chain, team_test::make_destroy_log_message(0)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  CASE_EXPECT_TRUE(team->is_destroyed());
  CASE_EXPECT_TRUE(run_team_heartbeat(test, team));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.heartbeat_reqs.size()));

  CASE_EXPECT_EQ(0, test.stop());
}

// EXIT-01: 主动退出上行 remove self 的完整 payload 和 reason(EXIT_TEAM); 仍为成员时进入 pending-to-exit,
// 期间队伍不作为 running team 导出、不发心跳, 非解散频道事件不收敛退出(缓存仍随事件维护, 代码事实);
// 回环 remove_member(self) 后 minute refresh 收编; 迟到重复事件幂等。
CASE_TEST(lobbysvr_user_team, exit_team_request_then_channel_remove_converges) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30111;
  constexpr uint64_t kSessionId = 0x10111;
  constexpr int64_t kTeamId = 667;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 1. 主动退出: 上行 remove self, reason=EXIT_TEAM
  CASE_EXPECT_TRUE(run_exit_team(test, user_inst, kTeamId, atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 1;
  }));
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));
  {
    const auto& req = ss_capture.send_message_reqs.front();
    CASE_EXPECT_TRUE(req.action().has_remove_member());
    CASE_EXPECT_EQ(kTeamId, req.team_key().team_id());
    CASE_EXPECT_EQ(kUserId, req.sender_user_key().user_id());
    const auto& remove_data = req.action().remove_member();
    CASE_EXPECT_EQ(kTeamId, remove_data.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, remove_data.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, remove_data.user_key().user_id());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM, remove_data.remove_member_reason());
  }

  // 仍为成员: 转入 pending-to-exit, 不再是 current, 但仍保留在索引中
  CASE_EXPECT_TRUE(team->is_exiting());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM, team->get_last_exit_reason());
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));

  // 2. 退出期间: 非解散频道事件不收敛退出(代码事实: 缓存仍随事件维护), 队伍不作为 running 导出, 不发心跳
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  {
    atfw::team::DTeamAction action;
    auto* member_update = action.mutable_member_update();
    protobuf_copy_message(*member_update->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    member_update->set_client_version("exit-01-v1");
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team->dump(ctx, snapshot);
    const auto* captain = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
    return nullptr != captain && captain->client_version() == "exit-01-v1";
  }));
  CASE_EXPECT_TRUE(team->is_exiting());
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM, team->get_last_exit_reason());
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(user_inst)));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_TRUE(ss_capture.heartbeat_reqs.empty());

  // 3. 回环 remove_member(self): 客户端收到带真实 reason 的 increase, self 缓存删除
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember).empty();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto removes = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember);
    CASE_EXPECT_EQ(1, static_cast<int>(removes.size()));
    if (!removes.empty()) {
      const auto& remove_data = removes.front()->action().remove_member();
      CASE_EXPECT_EQ(kTeamId, remove_data.team_key().team_id());
      CASE_EXPECT_EQ(kUserId, remove_data.user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM, remove_data.remove_member_reason());
    }
  }
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST, team->get_cached_permission_role());

  // 4. minute refresh 收编(频道 ready 且已确认非成员)
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  expect_team_absent_in_table(*user_inst, kTeamId);
  team = nullptr;

  // 5. 迟到重复事件: 不重建队伍、不重复下发、不再发退出请求
  test.cs().clear_history();
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// EXIT-02: exit retry interval 边界前不重试、达到边界补发; exit timeout 后清理 manager/索引/table
// 且清理后停止重试; 频道 destroyed 可不等超时直接移除; 对象释放后 dirty handle 注销,
// 重新加入同 team_id 时脏数据重新正常下发。锁定配置事实: exit_retry_interval=10s, exit_timeout=35s。
CASE_TEST(lobbysvr_user_team, exit_retry_timeout_cleanup_and_channel_destroy) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  CASE_EXPECT_EQ(10,
                 static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                      team_test::get_exit_retry_interval())
                                      .count()));
  CASE_EXPECT_EQ(35, static_cast<int>(
                         std::chrono::duration_cast<std::chrono::seconds>(team_test::get_exit_timeout()).count()));

  constexpr uint64_t kUserId = 30112;
  constexpr uint64_t kSessionId = 0x10112;
  constexpr int64_t kDestroyedTeamId = 669;
  constexpr int64_t kTimeoutTeamId = 668;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client));

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  auto make_self_member_storage = [](int64_t team_id) {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(team_id);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    return team_storage;
  };

  // 1. 频道 destroyed 可直接移除(不等 exit timeout)
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kDestroyedTeamId));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kDestroyedTeamId, make_self_member_storage(kDestroyedTeamId)));
  CASE_EXPECT_TRUE(
      run_exit_team(test, user_inst, kDestroyedTeamId, atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kDestroyedTeamId, kUserId) >= 1;
  }));
  {
    team_test::channel_event_chain destroyed_chain;
    destroyed_chain.channel_key = team_test::make_team_channel_key(kDestroyedTeamId);
    CASE_EXPECT_TRUE(team_test::inject_log_message(test, destroyed_chain, team_test::make_destroy_log_message(0)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kDestroyedTeamId));
  }));
  // 销毁路径合成解散通知且只下发一次, 不再补发退出请求
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kDestroyedTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam).empty();
  }));
  {
    // 重复投递第二条 destroy 日志: 频道已销毁且队伍已移除, 不产生第二次解散通知
    team_test::channel_event_chain destroyed_chain2;
    destroyed_chain2.channel_key = team_test::make_team_channel_key(kDestroyedTeamId);
    destroyed_chain2.sequence = 1;
    std::vector<atframework::dtmq::DChannelMessage> first{team_test::make_destroy_log_message(1)};
    destroyed_chain2.hash_code = team_test::chain_message_hashes(first, 0);
    CASE_EXPECT_TRUE(team_test::inject_log_message(test, destroyed_chain2, team_test::make_destroy_log_message(0)));
    team_test::pump_rounds(test, 3);
    auto view = team_test::collect_team_dirty(test, kSessionId, kDestroyedTeamId);
    CASE_EXPECT_EQ(1,
                   static_cast<int>(team_test::count_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam)));
  }
  // 销毁不再产生额外的 DESTROY_TEAM 退出请求(频道销毁即 room 已不存在, kDestroy 分支 send_exit=false;
  // 之后的 on_destroyed 回调因索引已空幂等返回, 重复 destroy 日志被订阅端忽略)
  {
    std::vector<atfw::team::EnTeamExitReason> destroyed_team_reasons;
    for (const auto& req : ss_capture.send_message_reqs) {
      if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == kDestroyedTeamId &&
          req.action().remove_member().user_key().user_id() == kUserId) {
        destroyed_team_reasons.push_back(req.action().remove_member().remove_member_reason());
      }
    }
    CASE_EXPECT_EQ(1, static_cast<int>(destroyed_team_reasons.size()));
    if (!destroyed_team_reasons.empty()) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM, destroyed_team_reasons[0]);
    }
  }
  expect_team_absent_in_table(*user_inst, kDestroyedTeamId);

  // 2. exit retry 边界与 exit timeout 清理
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTimeoutTeamId));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTimeoutTeamId, make_self_member_storage(kTimeoutTeamId)));
  // 守卫构造把逻辑时间抬到进程 floor: 退出请求时间戳与后续快进共享同一基准,
  // 否则“边界前不重试”断言会被此前用例累积的 floor 污染
  team_test::now_offset_guard time_guard;
  CASE_EXPECT_TRUE(run_exit_team(test, user_inst, kTimeoutTeamId, atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTimeoutTeamId, kUserId) >= 1;
  }));
  auto timeout_team =
      user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTimeoutTeamId));
  CASE_EXPECT_TRUE(!!timeout_team);
  if (!timeout_team) {
    test.stop();
    return;
  }

  // retry 边界前: 不重试
  team_test::now_offset_guard::advance(team_test::get_exit_retry_interval() - std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTimeoutTeamId, kUserId)));

  // 越过 retry 边界: 补发, reason 保持 EXIT_TEAM
  team_test::now_offset_guard::advance(std::chrono::seconds{2});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTimeoutTeamId, kUserId) >= 2;
  }));
  for (const auto& req : ss_capture.send_message_reqs) {
    if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == kTimeoutTeamId) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM, req.action().remove_member().remove_member_reason());
    }
  }

  // 越过 exit timeout(自最近一次退出请求起算, 期间不能再跑 minute refresh 以免重试刷新计时):
  // 个人回环 remove 到达时直接收编
  team_test::now_offset_guard::advance(team_test::get_exit_timeout() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTimeoutTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_EXIT_TEAM));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTimeoutTeamId));
  }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  expect_team_absent_in_table(*user_inst, kTimeoutTeamId);

  timeout_team = nullptr;
  // 3. 清理后停止重试
  {
    const size_t sent_requests = ss_capture.send_message_reqs.size();
    team_test::now_offset_guard cleanup_guard;
    team_test::now_offset_guard::advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
    CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
    team_test::pump_rounds(test, 2);
    CASE_EXPECT_EQ(static_cast<int>(sent_requests), static_cast<int>(ss_capture.send_message_reqs.size()));
  }

  // 4. 对象释放后 dirty handle 已注销: 重新加入同 team_id, 新快照事件的脏数据重新正常下发,
  //    且缓存经快照完整重建(不依赖旧对象残留状态)
  test.cs().clear_history();
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTimeoutTeamId));
  auto rejoined = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTimeoutTeamId));
  CASE_EXPECT_TRUE(!!rejoined);
  if (!rejoined) {
    test.stop();
    return;
  }
  {
    atfw::team::DTeamStorage rejoin_storage = make_self_member_storage(kTimeoutTeamId);
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTimeoutTeamId), 1, 0,
                                             &rejoin_storage, 2)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTimeoutTeamId).snapshots.empty();
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    rejoined->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kUserId));
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// DUMP-01: manager table dump/init 往返恢复 team_type/current(含 channel/captain/role)、自己的 pending
// invitation/join-request 与个人频道已处理序号; pending-to-exit 不落地; 非法 team type/空 channel/已过期
// pending 不恢复; 恢复后旧个人事件(<=已处理序号)幂等去重不重复落地, >已处理序号事件正常处理;
// 恢复的队伍经快照重建 user_team 缓存; last_exit 信息不落地(恢复的队伍不在退出流程)。
CASE_TEST(lobbysvr_user_team, table_dump_init_round_trip_restores_watermark_team_and_pendings) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kFirstUserId = 30113;
  constexpr uint64_t kSecondUserId = 30213;
  constexpr int64_t kAbandonedTeamId = 671;
  constexpr int64_t kCurrentTeamId = 672;
  constexpr int64_t kInvitedTeamId = 673;
  constexpr int64_t kJoinRequestTeamId = 674;
  constexpr int64_t kExpiredInvitationTeamId = 675;
  constexpr int64_t kInvalidTypeTeamId = 676;
  constexpr int64_t kEmptyChannelTeamId = 677;
  constexpr int64_t kLateInviteTeamId = 678;

  user::ptr_t first_user;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kFirstUserId, first_user, subscriber_key, private_channel_key));
  if (!first_user) {
    test.stop();
    return;
  }

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  auto valid_expiry = logical_system_now() + std::chrono::seconds(600);

  // 先加入 671 再切到 672: 671 进入 pending-to-exit(不应落地), 672 为 current
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, first_user, private_chain, kAbandonedTeamId));
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, first_user, private_chain, kCurrentTeamId));
  // 自己的 pending 邀请/加入请求(个人频道已处理序号随之推进到 4)
  CASE_EXPECT_TRUE(inject_self_invited_event(test, private_chain, kFirstUserId, kInvitedTeamId, valid_expiry));
  CASE_EXPECT_TRUE(
      inject_self_apply_join_request_event(test, private_chain, kFirstUserId, kJoinRequestTeamId, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 4 == first_user->get_user_team_manager().get_processed_private_chat_channel_sequence();
  }));

  PROJECT_NAMESPACE_ID::table_user dumped_table = dump_team_table(*first_user);
  {
    const auto& team_data = dumped_table.team_data();
    CASE_EXPECT_EQ(4, static_cast<int>(team_data.processed_private_chat_channel_sequence()));
    // 只有 672/current 落地, 671(pending-to-exit) 不落地
    CASE_EXPECT_EQ(1, team_data.group_size());
    if (team_data.group_size() > 0) {
      const auto& group = team_data.group(0);
      CASE_EXPECT_EQ(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL), group.team_type());
      CASE_EXPECT_TRUE(group.has_current());
      if (group.has_current()) {
        CASE_EXPECT_EQ(kCurrentTeamId, group.current().team_key().team_id());
        // current join data 的 team_type 随 dump 落地(恢复时据此校验与分组)
        CASE_EXPECT_EQ(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL), group.current().team_type());
        CASE_EXPECT_EQ(kFirstUserId, group.current().user_key().user_id());
        CASE_EXPECT_EQ(team_test::make_team_channel_key(kCurrentTeamId).channel_id(),
                       group.current().team_channel().channel_id());
        CASE_EXPECT_EQ(team_test::kCaptainUserId, group.current().captain_user_key().user_id());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, group.current().user_role());
      }
    }
    // 自己的 pending 落地(恢复后不被已处理序号跳过也不丢失)
    CASE_EXPECT_EQ(1, team_data.pending_invitation_size());
    if (team_data.pending_invitation_size() > 0) {
      CASE_EXPECT_EQ(kInvitedTeamId, team_data.pending_invitation(0).team_key().team_id());
    }
    CASE_EXPECT_EQ(1, team_data.pending_join_request_size());
    if (team_data.pending_join_request_size() > 0) {
      CASE_EXPECT_EQ(kJoinRequestTeamId, team_data.pending_join_request(0).team_key().team_id());
    }
  }
  expect_team_absent_in_table(*first_user, kAbandonedTeamId);

  // 构造迁移到另一个 server 实例的同一玩家(新 user 对象, 身份字段改写), 并混入非法条目
  {
    auto* group = dumped_table.mutable_team_data()->mutable_group(0);
    protobuf_copy_message(*group->mutable_current()->mutable_user_key(), team_test::make_user_key(kSecondUserId));
    protobuf_copy_message(*dumped_table.mutable_team_data()->mutable_pending_invitation(0)->mutable_invitee(),
                          team_test::make_user_key(kSecondUserId));
    protobuf_copy_message(*dumped_table.mutable_team_data()->mutable_pending_join_request(0)->mutable_requester(),
                          team_test::make_user_key(kSecondUserId));

    // 非法 team type 的分组不恢复(恢复校验以持久化 join data 的 team_type 为准)
    auto* invalid_group = dumped_table.mutable_team_data()->add_group();
    invalid_group->set_team_type(999);
    auto invalid_join = team_test::make_join_data(kSecondUserId, kInvalidTypeTeamId);
    invalid_join.set_team_type(999);
    protobuf_copy_message(*invalid_group->mutable_current(), invalid_join);
    // 空 channel 的分组不恢复
    auto* empty_channel_group = dumped_table.mutable_team_data()->add_group();
    empty_channel_group->set_team_type(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
    auto empty_channel_join = team_test::make_join_data(kSecondUserId, kEmptyChannelTeamId);
    empty_channel_join.clear_team_channel();
    protobuf_copy_message(*empty_channel_group->mutable_current(), empty_channel_join);
    // 已过期的 pending 不恢复
    auto* expired_invitation = dumped_table.mutable_team_data()->add_pending_invitation();
    protobuf_copy_message(*expired_invitation->mutable_team_key(),
                          team_test::make_team_key(kExpiredInvitationTeamId));
    protobuf_copy_message(*expired_invitation->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*expired_invitation->mutable_invitee(), team_test::make_user_key(kSecondUserId));
    *expired_invitation->mutable_start_timepoint() =
        protobuf_from_system_clock(logical_system_now() - std::chrono::seconds(100));
    *expired_invitation->mutable_expired_timepoint() =
        protobuf_from_system_clock(logical_system_now() - std::chrono::seconds(10));
  }

  user::ptr_t second_user;
  std::string second_subscriber_key;
  atframework::dtmq::DChannelIdKey second_private_channel_key;
  CASE_EXPECT_TRUE(
      team_test::setup_team_user(test, kSecondUserId, second_user, second_subscriber_key,
                                 second_private_channel_key));
  if (!second_user) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(run_init_from_table(test, second_user, dumped_table));

  // 恢复结果: 个人频道已处理序号、current 队伍、pending; 非法/过期条目不恢复; 恢复的队伍不在退出流程(last_exit 不落地)
  CASE_EXPECT_EQ(4, static_cast<int>(
                        second_user->get_user_team_manager().get_processed_private_chat_channel_sequence()));
  auto restored_team =
      second_user->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kCurrentTeamId));
  CASE_EXPECT_TRUE(!!restored_team);
  if (!restored_team) {
    test.stop();
    return;
  }
  {
    auto current =
        second_user->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    CASE_EXPECT_TRUE(!!current);
    if (current) {
      CASE_EXPECT_EQ(kCurrentTeamId, current->get_team_key().team_id());
    }
    CASE_EXPECT_TRUE(!restored_team->is_exiting());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_DEFAULT, restored_team->get_last_exit_reason());
    CASE_EXPECT_TRUE(!restored_team->is_destroyed());
    CASE_EXPECT_EQ(team_test::kCaptainUserId, restored_team->get_cached_captain_user_key().user_id());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, restored_team->get_cached_permission_role());
  }
  CASE_EXPECT_TRUE(
      !second_user->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kAbandonedTeamId)));
  CASE_EXPECT_TRUE(
      !second_user->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kInvalidTypeTeamId)));
  CASE_EXPECT_TRUE(
      !second_user->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kEmptyChannelTeamId)));
  CASE_EXPECT_TRUE(
      !!second_user->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kInvitedTeamId)));
  CASE_EXPECT_TRUE(
      !!second_user->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kJoinRequestTeamId)));
  CASE_EXPECT_TRUE(
      !second_user->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kExpiredInvitationTeamId)));
  {
    auto pending =
        second_user->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kInvitedTeamId));
    CASE_EXPECT_TRUE(!!pending);
    if (pending) {
      CASE_EXPECT_EQ(kSecondUserId, pending->invitee().user_id());
      CASE_EXPECT_EQ(team_test::kCaptainUserId, pending->inviter().user_id());
    }
  }

  // user_team 缓存重建: 恢复的队伍经快照事件重建完整缓存
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kCurrentTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
    team_test::add_storage_member(team_storage, kSecondUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
    team_storage.mutable_configure()->set_disable_join_request(true);
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kCurrentTeamId, team_storage));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    restored_team->dump(ctx, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kSecondUserId);
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    restored_team->dump(ctx, snapshot);
    CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(snapshot.snapshot().configure().disable_join_request());
  }

  // 幂等重放: <=已处理序号的旧个人事件不重复落地, >已处理序号事件正常处理
  team_test::channel_event_chain second_private_chain;
  second_private_chain.channel_key = second_private_channel_key;
  // seq1: 旧 reject_invitation(673) -> 跳过, pending 保留
  CASE_EXPECT_TRUE(inject_reject_invitation_event(test, second_private_chain, kSecondUserId, kInvitedTeamId));
  // seq2: 旧 reject_join_request(674) -> 跳过, pending 保留
  CASE_EXPECT_TRUE(inject_reject_join_request_event(test, second_private_chain, kSecondUserId, kJoinRequestTeamId));
  // seq3: 旧 remove_member(672) -> 跳过, 队伍保留
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, second_private_chain, kSecondUserId, kCurrentTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  // seq4: 旧 invited(678) -> 跳过, 不落地
  CASE_EXPECT_TRUE(
      inject_self_invited_event(test, second_private_chain, kSecondUserId, kLateInviteTeamId, valid_expiry));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(
      !!second_user->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kInvitedTeamId)));
  CASE_EXPECT_TRUE(
      !!second_user->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kJoinRequestTeamId)));
  CASE_EXPECT_TRUE(
      !!second_user->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kCurrentTeamId)));
  CASE_EXPECT_TRUE(
      !second_user->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kLateInviteTeamId)));
  CASE_EXPECT_EQ(4, static_cast<int>(
                        second_user->get_user_team_manager().get_processed_private_chat_channel_sequence()));

  // seq5: 同一事件的更新版本(>已处理序号)正常落地一次, 已处理序号推进
  CASE_EXPECT_TRUE(
      inject_self_invited_event(test, second_private_chain, kSecondUserId, kLateInviteTeamId, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!second_user->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kLateInviteTeamId));
  }));
  CASE_EXPECT_EQ(5, static_cast<int>(
                        second_user->get_user_team_manager().get_processed_private_chat_channel_sequence()));

  // 往返稳定: 新实例的 dump 保持已处理序号/current/pending
  {
    auto redumped = dump_team_table(*second_user);
    CASE_EXPECT_EQ(5, static_cast<int>(redumped.team_data().processed_private_chat_channel_sequence()));
    CASE_EXPECT_EQ(1, redumped.team_data().group_size());
    if (redumped.team_data().group_size() > 0) {
      CASE_EXPECT_EQ(kCurrentTeamId, redumped.team_data().group(0).current().team_key().team_id());
      CASE_EXPECT_EQ(kSecondUserId, redumped.team_data().group(0).current().user_key().user_id());
    }
    CASE_EXPECT_EQ(2, redumped.team_data().pending_invitation_size());
    CASE_EXPECT_EQ(1, redumped.team_data().pending_join_request_size());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// DUMP-02: user_get_info(need_user_team) 只导出 current 且非 exiting/destroyed 的队伍,
// 导出使用与 dirty snapshot 相同的字段裁剪契约(内部频道/router/ack 剥除, 成员共享数据解包下发,
// pending 私有频道剥除)。
CASE_TEST(lobbysvr_user_team, user_get_info_exports_only_running_team_with_trimming) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));

  constexpr uint64_t kUserId = 30114;
  constexpr int64_t kRunningTeamId = 681;
  constexpr int64_t kExitingTeamId = 682;
  constexpr uint64_t kPendingInviteeId = 93301;

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

  // 681: 富缓存(污染的内部字段 + 打包共享数据 + configure + 队伍级 pending)
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kRunningTeamId));
  {
    atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kRunningTeamId);
    team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                  team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER)
                                      .set_pollute_internal_fields(true));
    team_test::add_storage_member(
        team_storage, kUserId,
        team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
            .set_client_version("dump-02-v1")
            .set_shared_member_data({team_test::pack_member_module(team_test::make_member_ready_module(true))})
            .set_pollute_internal_fields(true));
    team_storage.mutable_configure()->set_disable_join_request(true);
    team_test::add_storage_invitation(team_storage, kPendingInviteeId,
                                      logical_system_now() + std::chrono::seconds(600), true);
    *team_storage.add_shared_team_data() =
        team_test::pack_team_module(team_test::make_team_matching_module(true));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kRunningTeamId, team_storage));
  }
  auto running_team =
      user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kRunningTeamId));
  CASE_EXPECT_TRUE(!!running_team);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == running_team->get_cached_permission_role();
  }));

  // 切到 682 再切回 681: 682 处于 pending-to-exit(导出时排除), 681 保持 current 且缓存完整
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kExitingTeamId));
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kRunningTeamId));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current =
        user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
    return current && current->get_team_key().team_id() == kRunningTeamId && !current->is_exiting();
  }));
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kExitingTeamId)));

  // 经生产注册的 need_user_team get_info 句柄导出
  PROJECT_NAMESPACE_ID::SCUserGetInfoRsp get_info_rsp;
  CASE_EXPECT_TRUE(team_test::run_sync_task(
      test, "team.user_get_info", [&user_inst, &get_info_rsp](rpc::context& ctx) -> rpc::result_code_type {
        const auto* need_user_team_field =
            PROJECT_NAMESPACE_ID::CSUserGetInfoReq::descriptor()->FindFieldByNumber(
                PROJECT_NAMESPACE_ID::CSUserGetInfoReq::kNeedUserTeamFieldNumber);
        for (const auto& handle : user::get_get_info_handle()) {
          if (handle.first == need_user_team_field && nullptr != handle.second) {
            handle.second(ctx, get_info_rsp, *user_inst);
          }
        }
        RPC_RETURN_CODE(0);
      }));

  // 只导出 running 的 681
  CASE_EXPECT_EQ(1, get_info_rsp.user_team_size());
  if (1 != get_info_rsp.user_team_size()) {
    test.stop();
    return;
  }
  const auto& exported = get_info_rsp.user_team(0);
  CASE_EXPECT_EQ(kRunningTeamId, exported.snapshot().team_key().team_id());
  CASE_EXPECT_TRUE(exported.snapshot().configure().disable_join_request());

  // 成员视图裁剪契约: 内部字段剥除, 原始打包共享数据不回填, 解包数据随 unpacked_member_data 下发
  CASE_EXPECT_EQ(2, exported.snapshot().member_size());
  for (const auto& member : exported.snapshot().member()) {
    team_test::expect_member_projection_clean(member);
  }
  CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(exported, team_test::kCaptainUserId));
  {
    const auto* self_member = team_test::find_snapshot_member(exported, kUserId);
    CASE_EXPECT_TRUE(nullptr != self_member);
    if (nullptr != self_member) {
      CASE_EXPECT_EQ(std::string("dump-02-v1"), self_member->client_version());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, self_member->role());
    }
    const auto* unpacked = team_test::find_unpacked_member(exported, kUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked);
    if (nullptr != unpacked) {
      CASE_EXPECT_EQ(1, unpacked->shared_member_data_size());
      if (unpacked->shared_member_data_size() > 0) {
        CASE_EXPECT_TRUE(unpacked->shared_member_data(0).battle().ready());
      }
    }
  }

  // 队伍共享数据: 原始打包数据不下发, 解包后的模块数据随 snapshot 下发
  CASE_EXPECT_EQ(0, exported.snapshot().shared_team_data_size());
  CASE_EXPECT_EQ(1, exported.shared_team_data_size());
  if (exported.shared_team_data_size() > 0) {
    CASE_EXPECT_TRUE(exported.shared_team_data(0).battle().matching());
  }

  // 队伍级 pending: 导出且剥除私有频道
  CASE_EXPECT_EQ(1, exported.snapshot().pending_invitation_size());
  if (exported.snapshot().pending_invitation_size() > 0) {
    CASE_EXPECT_EQ(kPendingInviteeId, exported.snapshot().pending_invitation(0).invitee().user_id());
    CASE_EXPECT_TRUE(exported.snapshot().pending_invitation(0).invitee_private_channel().channel_id().empty());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

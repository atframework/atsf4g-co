// Copyright 2026 atframework
//
// Offline robustness tests for lobbysvr user_team/user_team_manager
// (plan: src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md §3, §5.6).
//
// Covered here:
// - ROBUST-01: 重复事件等幂(同一 WAL 消息重投去重, 缓存/dirty/SS 调用数不重复叠加);
// - ROBUST-02: 乱序事件最终一致(update 先于 add 不建幽灵缓存, admit 乱序收敛到正序终态);
// - ROBUST-03: 频道销毁(WAL destroy 日志)后迟到的 action/个人事件不改变终态;
// - ROBUST-04: 快照重建清理旧 pending/成员/共享数据, manager 索引与 table 数据一致, 脏推送 handle 随对象释放注销。
// 文件末尾四例对应计划 §5.6 P2 健壮性矩阵: 个人频道错误 Any、队伍快照错误 Any、共享数据错误 Any、夹具卫生。

#include <string>
#include <vector>

#include "lobbysvr_test_user_team_common.h"  // NOLINT: build/include_subdir

namespace {

// 构造一条合法 hash chain 的事件消息批次并推进 chain; 返回的批次可原样重复投递(验证 WAL 层对重复消息去重)
std::vector<atframework::dtmq::DChannelMessage> make_chain_event_batch(team_test::channel_event_chain& chain,
                                                                       const google::protobuf::Message& event) {
  std::vector<atframework::dtmq::DChannelMessage> msgs{team_test::make_event_message(chain.sequence + 1, event)};
  chain.hash_code = team_test::chain_message_hashes(msgs, chain.hash_code);
  chain.sequence = msgs.back().sequence();
  return msgs;
}

// 投递一个已构造的消息批次(不推进 chain): 首次投递或原样重复投递同一批 WAL 消息
bool deliver_chain_batch(atfw::testing::runtime& test, const team_test::channel_event_chain& chain,
                         const std::vector<atframework::dtmq::DChannelMessage>& msgs) {
  return team_test::receive_channel_event(
      test, team_test::make_incremental_event(chain.channel_key, msgs.back().sequence(), msgs));
}

// dump 队伍缓存快照(业务 key 查找, 与哈希表输出顺序无关)
void dump_team_snapshot(user_team& team, PROJECT_NAMESPACE_ID::DUserTeamSnapshot& output) {
  rpc::context ctx{rpc::context::create_without_task()};
  team.dump(ctx, output);
}

// 在 dump 快照的 pending 列表中按业务 key 查找
const atfw::team::DTeamInvitation* find_pending_invitation(
    const PROJECT_NAMESPACE_ID::DUserTeamSnapshot& snapshot, uint64_t invitee_id) {
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

// 断言 table dump 中不存在该队伍(group 可能整体消失或没有 current)
void expect_team_absent_in_table(user& user_inst, int64_t team_id) {
  PROJECT_NAMESPACE_ID::table_user dumped_table;
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(0, user_inst.get_user_team_manager().dump(ctx, dumped_table));
  }
  for (const auto& group : dumped_table.team_data().group()) {
    if (group.has_current()) {
      CASE_EXPECT_TRUE(team_id != group.current().team_key().team_id());
    }
  }
}

size_t count_running_teams(user& user_inst) {
  size_t ret = 0;
  user_inst.get_user_team_manager().foreach_running_team(
      [&ret](uint32_t, const atfw::util::nostd::nonnull<user_team::ptr_t>&) { ++ret; });
  return ret;
}

// ---- 事件构造器 -----------------------------------------------------------------------

// 队伍频道 add_member 事件(完整业务字段 + 解包用共享数据)
atfw::team::DTeamAction make_add_member_action(uint64_t user_id, std::chrono::system_clock::time_point joined,
                                               atfw::team::EnTeamPermissionRole role,
                                               const std::string& client_version,
                                               atfw::team::EnTeamSourceType source_type, bool member_ready) {
  atfw::team::DTeamAction action;
  auto* member = action.mutable_add_member();
  protobuf_copy_message(*member->mutable_user_key(), team_test::make_user_key(user_id));
  *member->mutable_joined_timepoint() = protobuf_from_system_clock(joined);
  member->set_role(role);
  member->set_team_source_type(source_type);
  member->set_client_version(client_version);
  protobuf_copy_message(*member->add_shared_member_data(),
                        team_test::pack_member_module(team_test::make_member_ready_module(member_ready)));
  return action;
}

// 队伍频道 member_update 事件(非空版本 + 可选共享数据)
atfw::team::DTeamAction make_member_update_action(uint64_t user_id, const std::string& client_version,
                                                  bool with_ready_module, bool ready) {
  atfw::team::DTeamAction action;
  auto* update = action.mutable_member_update();
  protobuf_copy_message(*update->mutable_user_key(), team_test::make_user_key(user_id));
  update->set_client_version(client_version);
  if (with_ready_module) {
    protobuf_copy_message(*update->add_shared_member_data(),
                          team_test::pack_member_module(team_test::make_member_ready_module(ready)));
  }
  return action;
}

// 填充邀请公共字段(开始/过期时间确定; 队伍频道事件携带被邀请人私有频道, dump 视图必须裁剪)
void fill_invitation(atfw::team::DTeamInvitation& invitation, int64_t team_id, uint64_t invitee_id,
                     std::chrono::system_clock::time_point expired_timepoint, bool pollute_channel = true) {
  protobuf_copy_message(*invitation.mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*invitation.mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
  protobuf_copy_message(*invitation.mutable_invitee(), team_test::make_user_key(invitee_id));
  *invitation.mutable_start_timepoint() =
      protobuf_from_system_clock(expired_timepoint - std::chrono::hours(1));
  *invitation.mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  if (pollute_channel) {
    invitation.mutable_invitee_private_channel()->set_channel_id("invitee-channel-" +
                                                                 std::to_string(invitee_id));
  }
}

// 填充加入请求公共字段(私有频道/router 属于内部路由字段, dump 视图必须裁剪)
void fill_join_request(atfw::team::DTeamJoinRequest& join_request, int64_t team_id, uint64_t requester_id,
                       std::chrono::system_clock::time_point expired_timepoint, bool pollute_channel = true) {
  protobuf_copy_message(*join_request.mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*join_request.mutable_requester(), team_test::make_user_key(requester_id));
  *join_request.mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  if (pollute_channel) {
    join_request.mutable_requester_private_channel()->set_channel_id("requester-channel-" +
                                                                     std::to_string(requester_id));
    join_request.set_user_router_server_id(0x6000 + static_cast<uint32_t>(requester_id % 0x1000));
  }
}

// 队伍频道邀请/加入请求结论事件(approve/reject 按 invitee/requester 删除 pending)
atfw::team::DTeamAction make_invitation_result_action(bool approve, int64_t team_id, uint64_t invitee_id) {
  atfw::team::DTeamAction action;
  auto& invitation = approve ? *action.mutable_approve_invitation() : *action.mutable_reject_invitation();
  fill_invitation(invitation, team_id, invitee_id, std::chrono::system_clock::from_time_t(0), false);
  invitation.clear_start_timepoint();
  invitation.clear_expired_timepoint();
  return action;
}

atfw::team::DTeamAction make_join_request_result_action(bool approve, int64_t team_id, uint64_t requester_id) {
  atfw::team::DTeamAction action;
  auto& join_request =
      approve ? *action.mutable_approve_join_request() : *action.mutable_reject_join_request();
  fill_join_request(join_request, team_id, requester_id, std::chrono::system_clock::from_time_t(0), false);
  join_request.clear_expired_timepoint();
  return action;
}

// 个人频道 invited 通知
atfw::team::DTeamMemberAction make_invited_action(int64_t team_id, uint64_t invitee_id,
                                                  std::chrono::system_clock::time_point expired_timepoint) {
  atfw::team::DTeamMemberAction action;
  fill_invitation(*action.mutable_invited(), team_id, invitee_id, expired_timepoint);
  return action;
}

// 个人频道 apply_join_request 通知(room 归一化后的完整字段)
atfw::team::DTeamMemberAction make_apply_join_request_action(int64_t team_id, uint64_t requester_id,
                                                             std::chrono::system_clock::time_point expired_timepoint,
                                                             const std::string& client_version,
                                                             uint64_t router_id) {
  atfw::team::DTeamMemberAction action;
  auto* join_request = action.mutable_apply_join_request();
  fill_join_request(*join_request, team_id, requester_id, expired_timepoint, false);
  protobuf_copy_message(*join_request->mutable_requester_private_channel(),
                        team_test::make_private_channel_key(requester_id));
  join_request->set_client_version(client_version);
  join_request->set_user_router_server_id(router_id);
  return action;
}

// 个人频道 remove_member 通知
atfw::team::DTeamMemberAction make_personal_remove_action(int64_t team_id, uint64_t user_id,
                                                          atfw::team::EnTeamExitReason reason) {
  atfw::team::DTeamMemberAction action;
  auto* remove_member = action.mutable_remove_member();
  protobuf_copy_message(*remove_member->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*remove_member->mutable_user_key(), team_test::make_user_key(user_id));
  remove_member->set_remove_member_reason(reason);
  return action;
}

}  // namespace

// ROBUST-01: 重复事件等幂。
// - 同一 add_member WAL 消息(sequence/hash 相同)重投: 成员缓存不重复叠加, 保留同一份业务字段,
//   dirty increase 数不增加;
// - 同一 add_member(self) 重投: self 首次加入只触发一次本端 shared data flush(SS send_message 数不增加);
// - 同一 add_invitation 重投: 队伍级 pending 只有一条, dirty 不重复;
// - 同一 invited 个人通知重投: manager 自己的 pending 不变, 已处理序号不前进, 不重复置脏;
// - 重复消息不腐蚀 WAL 链: 后续更高 sequence 的合法事件仍正常处理。
CASE_TEST(lobbysvr_user_team, robust_duplicate_events_idempotent) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));
  team_test::now_offset_guard time_guard;

  constexpr uint64_t kUserId = 30051;
  constexpr uint64_t kSessionId = 0x10051;
  constexpr int64_t kTeamId = 511;
  constexpr int64_t kInvitedTeamId = 512;
  constexpr int64_t kInvitedTeamId2 = 513;
  constexpr uint64_t kMemberUserId = 91051;
  constexpr uint64_t kInviteeUserId = 92051;

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

  // 初始快照: 仅队长(自己尚未被 add_member 确认, is_member_ 仍为 false)
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));

  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 1 == snapshot.snapshot().member_size();
  }));
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST, current->get_cached_permission_role());
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. 同一 add_member(其他成员) WAL 消息投递两次
  const auto joined_member = team_test::now_offset_guard::logical_now() - std::chrono::seconds(10);
  const auto joined_member_ts = protobuf_from_system_clock(joined_member);
  auto add_member_batch = make_chain_event_batch(
      team_chain, make_add_member_action(kMemberUserId, joined_member, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL,
                                         "1.0.1-robust01", atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, false));
  CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, add_member_batch));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kMemberUserId);
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
    const auto* member = team_test::find_snapshot_member(snapshot, kMemberUserId);
    CASE_EXPECT_TRUE(nullptr != member);
    if (nullptr != member) {
      CASE_EXPECT_EQ(team_test::kZoneId, member->user_key().zone_id());
      CASE_EXPECT_EQ(kMemberUserId, member->user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, member->role());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, member->team_source_type());
      CASE_EXPECT_EQ(std::string("1.0.1-robust01"), member->client_version());
      CASE_EXPECT_EQ(joined_member_ts.seconds(), member->joined_timepoint().seconds());
      CASE_EXPECT_EQ(joined_member_ts.nanos(), member->joined_timepoint().nanos());
      team_test::expect_member_projection_clean(*member);
    }
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddMember)));
    CASE_EXPECT_TRUE(ss_capture.send_message_reqs.empty());
  }

  // 重复投递同一条消息: 第二次为无操作(WAL 层按 sequence/hash 去重, 事件不再下发)
  CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, add_member_batch));
  team_test::pump_rounds(test, 2);
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(2, snapshot.snapshot().member_size());
    const auto* member = team_test::find_snapshot_member(snapshot, kMemberUserId);
    CASE_EXPECT_TRUE(nullptr != member);
    if (nullptr != member) {
      CASE_EXPECT_EQ(std::string("1.0.1-robust01"), member->client_version());
      CASE_EXPECT_EQ(joined_member_ts.seconds(), member->joined_timepoint().seconds());
    }
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddMember)));
    CASE_EXPECT_EQ(1, static_cast<int>(view.actions.size()));
    CASE_EXPECT_TRUE(ss_capture.send_message_reqs.empty());
  }

  // 2. 同一 add_member(self) 投递两次: self 首次加入只 flush 一次本端 shared data
  auto add_self_batch = make_chain_event_batch(
      team_chain, make_add_member_action(kUserId, team_test::now_offset_guard::logical_now(),
                                         atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, "1.0.1-self",
                                         atfw::team::EN_TEAM_SOURCE_TYPE_NONE, false));
  CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, add_self_batch));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return 1 == ss_capture.send_message_reqs.size(); }));
  {
    const auto& flush_req = ss_capture.send_message_reqs.front();
    CASE_EXPECT_TRUE(flush_req.action().has_member_update());
    CASE_EXPECT_EQ(kTeamId, flush_req.team_key().team_id());
    CASE_EXPECT_EQ(kUserId, flush_req.action().member_update().user_key().user_id());
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(2, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddMember)));
  }
  CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, add_self_batch));
  team_test::pump_rounds(test, 2);
  {
    // SS flush 调用数不重复增加, dirty 不重复, 成员缓存不重复叠加
    CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.send_message_reqs.size()));
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(2, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddMember)));
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
  }

  // 3. 同一 add_invitation 投递两次: 队伍级 pending 只有一条
  const auto invitation_expired = team_test::now_offset_guard::logical_now() + std::chrono::hours(1);
  const auto invitation_expired_ts = protobuf_from_system_clock(invitation_expired);
  {
    atfw::team::DTeamAction action;
    fill_invitation(*action.mutable_add_invitation(), kTeamId, kInviteeUserId, invitation_expired);
    auto invitation_batch = make_chain_event_batch(team_chain, action);
    CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, invitation_batch));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
      dump_team_snapshot(*current, snapshot);
      return nullptr != find_pending_invitation(snapshot, kInviteeUserId);
    }));
    {
      auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
      CASE_EXPECT_EQ(1, static_cast<int>(
                            team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddInvitation)));
    }
    // 重复投递: pending 不重复叠加, dirty 不重复
    CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, invitation_batch));
    team_test::pump_rounds(test, 2);
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    const auto* invitation = find_pending_invitation(snapshot, kInviteeUserId);
    CASE_EXPECT_TRUE(nullptr != invitation);
    if (nullptr != invitation) {
      CASE_EXPECT_EQ(invitation_expired_ts.seconds(), invitation->expired_timepoint().seconds());
      // dump 视图裁剪内部路由字段
      CASE_EXPECT_TRUE(invitation->invitee_private_channel().channel_id().empty());
    }
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddInvitation)));
  }

  // 4. 重复消息没有腐蚀 WAL 链: 后续更高 sequence 的合法 action 仍正常处理
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, team_chain, make_member_update_action(kMemberUserId, "1.0.2-robust01", false, false)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* member = team_test::find_snapshot_member(snapshot, kMemberUserId);
    return nullptr != member && "1.0.2-robust01" == member->client_version();
  }));

  // 5. 同一 invited 个人通知投递两次: manager 自己的 pending 不变, 已处理序号不前进, 不重复置脏
  const auto invited_expired = team_test::now_offset_guard::logical_now() + std::chrono::hours(1);
  const auto invited_expired_ts = protobuf_from_system_clock(invited_expired);
  auto invited_batch =
      make_chain_event_batch(private_chain, make_invited_action(kInvitedTeamId, kUserId, invited_expired));
  CASE_EXPECT_TRUE(deliver_chain_batch(test, private_chain, invited_batch));
  auto& team_mgr = user_inst->get_user_team_manager();
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!team_mgr.get_pending_invitation(team_test::make_team_key(kInvitedTeamId));
  }));
  const int64_t watermark_after_first = team_mgr.get_processed_private_chat_channel_sequence();
  CASE_EXPECT_EQ(private_chain.sequence, watermark_after_first);
  team_mgr.clear_dirty();

  CASE_EXPECT_TRUE(deliver_chain_batch(test, private_chain, invited_batch));
  team_test::pump_rounds(test, 2);
  {
    auto pending = team_mgr.get_pending_invitation(team_test::make_team_key(kInvitedTeamId));
    CASE_EXPECT_TRUE(!!pending);
    if (pending) {
      CASE_EXPECT_EQ(kInvitedTeamId, pending->team_key().team_id());
      CASE_EXPECT_EQ(kUserId, pending->invitee().user_id());
      CASE_EXPECT_EQ(invited_expired_ts.seconds(), pending->expired_timepoint().seconds());
    }
    CASE_EXPECT_EQ(watermark_after_first, team_mgr.get_processed_private_chat_channel_sequence());
    CASE_EXPECT_TRUE(!team_mgr.is_dirty());
  }

  // 个人频道后续合法事件仍正常处理
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, private_chain, make_invited_action(kInvitedTeamId2, kUserId, invited_expired)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!team_mgr.get_pending_invitation(team_test::make_team_key(kInvitedTeamId2));
  }));

  CASE_EXPECT_EQ(0, test.stop());
}

// ROBUST-02: 乱序事件最终一致。
// - update 先于 add(同一 WAL 批次内保证应用顺序): member_update 不会为未知成员创建幽灵缓存,
//   随后的 add_member 以其自身业务字段落地(成员字段逐项断言);
// - 过期/乱序的 WAL 同步重投被已保存序号拒绝(kIgnore), 不改变已落地状态;
// - 权威快照重建后终态与正序一致(快照后 WAL 日志存储被清空, 后续增量按空存储重新起 hash 链);
// - admit 乱序: approve 先于 add 到达为幂等无操作, add_member 落地后迟到的 add_invitation/add_join_request
//   被成员身份抑制, 终态与正序(add -> approve)一致: 成员在队、两类 pending 均无残留。
CASE_TEST(lobbysvr_user_team, robust_out_of_order_events_converge) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  team_test::now_offset_guard time_guard;

  constexpr uint64_t kUserId = 30052;
  constexpr uint64_t kSessionId = 0x10052;
  constexpr int64_t kTeamId = 521;
  constexpr uint64_t kReorderedUserId = 91052;   // update 先于 add 的成员
  constexpr uint64_t kAdmitUserId = 92052;       // admit 乱序的成员
  constexpr uint64_t kInOrderUserId = 93052;     // 正序对照(始终不是成员)

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

  // 初始快照: 队长 + 自己(直接是成员, 避免 wait_add_member_timeout 语义干扰)
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("2.0.0-self"));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));

  auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // --- A. member_update 先于 add_member(同一 WAL 批次内, 应用顺序即投递顺序) ------------
  // A1. 同一批次 [update(X), add(X)]: update 不为未知成员创建幽灵缓存, add 以自身业务字段落地
  const auto joined_reordered = team_test::now_offset_guard::logical_now() - std::chrono::seconds(30);
  const auto joined_reordered_ts = protobuf_from_system_clock(joined_reordered);
  atfw::team::DTeamAction update_action = make_member_update_action(kReorderedUserId, "9.9.9-pre", true, true);
  atfw::team::DTeamAction add_action =
      make_add_member_action(kReorderedUserId, joined_reordered, atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL,
                             "1.0.0-add", atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, false);
  std::vector<atframework::dtmq::DChannelMessage> update_then_add_batch;
  update_then_add_batch.push_back(team_test::make_event_message(team_chain.sequence + 1, update_action));
  update_then_add_batch.push_back(team_test::make_event_message(team_chain.sequence + 2, add_action));
  team_chain.hash_code = team_test::chain_message_hashes(update_then_add_batch, team_chain.hash_code);
  team_chain.sequence = update_then_add_batch.back().sequence();
  CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, update_then_add_batch));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kReorderedUserId);
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    // 成员只来自 add_member: 先到的 update 不创建幽灵缓存也不覆盖 add 的字段
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    const auto* member = team_test::find_snapshot_member(snapshot, kReorderedUserId);
    CASE_EXPECT_TRUE(nullptr != member);
    if (nullptr != member) {
      CASE_EXPECT_EQ(team_test::kZoneId, member->user_key().zone_id());
      CASE_EXPECT_EQ(kReorderedUserId, member->user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, member->role());
      CASE_EXPECT_EQ(std::string("1.0.0-add"), member->client_version());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, member->team_source_type());
      CASE_EXPECT_EQ(joined_reordered_ts.seconds(), member->joined_timepoint().seconds());
      CASE_EXPECT_EQ(joined_reordered_ts.nanos(), member->joined_timepoint().nanos());
    }
    // 共享数据以 add 携带的 ready=false 为准(先到的 update 的 ready=true 不生效)
    const auto* unpacked = team_test::find_unpacked_member(snapshot, kReorderedUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked);
    if (nullptr != unpacked && !unpacked->shared_member_data().empty()) {
      CASE_EXPECT_TRUE(unpacked->shared_member_data().begin()->has_battle());
      CASE_EXPECT_TRUE(!unpacked->shared_member_data().begin()->battle().ready());
    }
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddMember)));
  }

  // A2. 过期/乱序的 WAL 同步重投(同一批次再次下发): 按已保存序号整条忽略, 不改变已落地状态
  CASE_EXPECT_TRUE(deliver_chain_batch(test, team_chain, update_then_add_batch));
  team_test::pump_rounds(test, 2);
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    const auto* member = team_test::find_snapshot_member(snapshot, kReorderedUserId);
    CASE_EXPECT_TRUE(nullptr != member);
    if (nullptr != member) {
      CASE_EXPECT_EQ(std::string("1.0.0-add"), member->client_version());
      CASE_EXPECT_EQ(joined_reordered_ts.seconds(), member->joined_timepoint().seconds());
    }
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kAddMember)));
  }

  // A3. 权威快照(saved 覆盖已收日志)重建: 终态与正序处理同一 room 日志流一致(逐项断言)
  atfw::team::DTeamStorage rebuilt_storage = team_test::make_team_storage(kTeamId, team_chain.sequence);
  team_test::add_storage_member(rebuilt_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(rebuilt_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("2.0.0-self"));
  team_test::add_storage_member(
      rebuilt_storage, kReorderedUserId,
      team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
          .set_joined_timepoint(joined_reordered)
          .set_client_version("9.9.9-final")
          .set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH)
          .set_shared_member_data({team_test::pack_member_module(team_test::make_member_ready_module(true))}));
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), /*create_sequence=*/1,
                                           team_chain.sequence, &rebuilt_storage, /*custom_data_sequence=*/2,
                                           team_chain.hash_code)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* member = team_test::find_snapshot_member(snapshot, kReorderedUserId);
    return nullptr != member && "9.9.9-final" == member->client_version();
  }));
  // 快照不携带消息日志, WAL 日志存储被清空; 后续增量按空存储重新起 hash 链(首条消息跳过校验并重写 hash)
  team_chain.hash_code = 0;
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    const auto* member = team_test::find_snapshot_member(snapshot, kReorderedUserId);
    CASE_EXPECT_TRUE(nullptr != member);
    if (nullptr != member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, member->role());
      CASE_EXPECT_EQ(std::string("9.9.9-final"), member->client_version());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, member->team_source_type());
      CASE_EXPECT_EQ(joined_reordered_ts.seconds(), member->joined_timepoint().seconds());
      CASE_EXPECT_EQ(joined_reordered_ts.nanos(), member->joined_timepoint().nanos());
      team_test::expect_member_projection_clean(*member);
    }
    const auto* unpacked = team_test::find_unpacked_member(snapshot, kReorderedUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked);
    if (nullptr != unpacked && !unpacked->shared_member_data().empty()) {
      CASE_EXPECT_TRUE(unpacked->shared_member_data().begin()->battle().ready());
    }
    const auto* self_member = team_test::find_snapshot_member(snapshot, kUserId);
    CASE_EXPECT_TRUE(nullptr != self_member);
    if (nullptr != self_member) {
      CASE_EXPECT_EQ(std::string("2.0.0-self"), self_member->client_version());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, self_member->role());
    }
  }

  // --- B. admit 乱序 -------------------------------------------------------------------
  // B1. approve 先于任何 add 到达: 幂等无操作
  CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain,
                                                   make_invitation_result_action(true, kTeamId, kAdmitUserId)));
  CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain,
                                                   make_join_request_result_action(true, kTeamId, kAdmitUserId)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kApproveJoinRequest).empty();
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_join_request_size());
  }

  // B2. admit(add_member) 落地
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, team_chain,
      make_add_member_action(kAdmitUserId, team_test::now_offset_guard::logical_now(),
                             atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, "2.1.0-admit",
                             atfw::team::EN_TEAM_SOURCE_TYPE_SNS_PLATFORM, false)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kAdmitUserId);
  }));

  // B3. 迟到的 add_invitation/add_join_request: 目标已是成员, 被抑制, 不进入 pending
  const auto late_expired = team_test::now_offset_guard::logical_now() + std::chrono::hours(1);
  {
    atfw::team::DTeamAction action;
    fill_invitation(*action.mutable_add_invitation(), kTeamId, kAdmitUserId, late_expired);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  {
    atfw::team::DTeamAction action;
    fill_join_request(*action.mutable_add_join_request(), kTeamId, kAdmitUserId, late_expired);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    for (const auto* one_action : team_test::find_actions_of_case(view, atfw::team::DTeamAction::kAddJoinRequest)) {
      if (one_action->action().add_join_request().requester().user_id() == kAdmitUserId) {
        return true;
      }
    }
    return false;
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kAdmitUserId));
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, kAdmitUserId));
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kAdmitUserId));
  }

  // B4. 正序对照(W): add -> approve 收敛到同样的终态(无 pending 残留, 非成员)
  {
    atfw::team::DTeamAction action;
    fill_invitation(*action.mutable_add_invitation(), kTeamId, kInOrderUserId, late_expired);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != find_pending_invitation(snapshot, kInOrderUserId);
  }));
  CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain,
                                                   make_invitation_result_action(true, kTeamId, kInOrderUserId)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr == find_pending_invitation(snapshot, kInOrderUserId);
  }));
  {
    atfw::team::DTeamAction action;
    fill_join_request(*action.mutable_add_join_request(), kTeamId, kInOrderUserId, late_expired);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != find_pending_join_request(snapshot, kInOrderUserId);
  }));
  CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain,
                                                   make_join_request_result_action(true, kTeamId, kInOrderUserId)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 0 == snapshot.snapshot().pending_join_request_size();
  }));
  {
    // 终态一致: 乱序 admit 的 Z 是正序结果的成员且无 pending; 正序对照的 W 非成员且无 pending
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kAdmitUserId));
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kInOrderUserId));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ROBUST-03: 频道销毁期间迟到 action 不改变终态。
// 频道经 WAL destroy 日志销毁、manager 收编后, 迟到的队伍 action/重复 destroy 日志/迟到个人通知
// 均不改变终态: manager 索引/分组为空、table 无残留、无新增 dirty 推送、无新增 SS 调用。
CASE_TEST(lobbysvr_user_team, robust_late_events_after_channel_destroy_ignored) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));
  team_test::now_offset_guard time_guard;

  constexpr uint64_t kUserId = 30053;
  constexpr uint64_t kSessionId = 0x10053;
  constexpr int64_t kTeamId = 531;
  constexpr uint64_t kMemberUserId = 91053;
  constexpr uint64_t kInviteeUserId = 92053;
  constexpr uint64_t kRequesterUserId = 93053;
  constexpr uint64_t kLateUserId = 94053;

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

  // 丰富缓存: 队长 + 自己 + 另一成员 + 两类 pending + 队伍共享数据
  const auto admission_expired = team_test::now_offset_guard::logical_now() + std::chrono::hours(1);
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  team_test::add_storage_member(
      team_storage, kMemberUserId,
      team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
          .set_client_version("3.1.0-m")
          .set_shared_member_data({team_test::pack_member_module(team_test::make_member_ready_module(true))}));
  team_test::add_storage_invitation(team_storage, kInviteeUserId, admission_expired, true);
  team_test::add_storage_join_request(team_storage, kRequesterUserId, admission_expired, true);
  protobuf_copy_message(*team_storage.add_shared_team_data(),
                        team_test::pack_team_module(team_test::make_team_matching_module(true)));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));

  auto& team_mgr = user_inst->get_user_team_manager();
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    if (!current || atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL != current->get_cached_permission_role()) {
      return false;
    }
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 3 == snapshot.snapshot().member_size() && 1 == snapshot.snapshot().pending_invitation_size() &&
           1 == snapshot.snapshot().pending_join_request_size();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 销毁前的正常流量: member_update 生效
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, team_chain, make_member_update_action(kMemberUserId, "3.1.1-m", false, false)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    if (!current) {
      return false;
    }
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* member = team_test::find_snapshot_member(snapshot, kMemberUserId);
    return nullptr != member && "3.1.1-m" == member->client_version();
  }));

  // 频道销毁(WAL destroy 日志): 客户端先收到 destroy increase, manager 随后清空
  CASE_EXPECT_TRUE(
      team_test::inject_log_message(test, team_chain, team_test::make_destroy_log_message(/*sequence=*/0)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam).empty();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto destroys = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam);
    CASE_EXPECT_EQ(1, static_cast<int>(destroys.size()));
    if (!destroys.empty()) {
      CASE_EXPECT_EQ(kTeamId, destroys.front()->action().destroy_team().team_id());
    }
  }
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return !team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId)); }));
  CASE_EXPECT_TRUE(!team_mgr.get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
  expect_team_absent_in_table(*user_inst, kTeamId);
  // 频道销毁时 room 已不存在: kDestroy 分支 send_exit=false, 不得产生任何 remove_member 退出上行。
  // settle 后记录上行请求数; 迟到事件不得使其增加
  team_test::pump_rounds(test, 4);
  const size_t send_message_after_destroy = ss_capture.send_message_reqs.size();
  CASE_EXPECT_EQ(0, static_cast<int>(send_message_after_destroy));

  // 迟到事件: 不改变终态
  test.cs().clear_history();
  // 迟到的队伍频道 action(频道已销毁, 订阅端整条忽略)
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, team_chain,
      make_add_member_action(kLateUserId, team_test::now_offset_guard::logical_now(),
                             atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, "3.9.9-late",
                             atfw::team::EN_TEAM_SOURCE_TYPE_BATTLE, false)));
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, team_chain, make_member_update_action(kMemberUserId, "3.9.9-late-update", false, false)));
  {
    atfw::team::DTeamAction action;
    fill_invitation(*action.mutable_add_invitation(), kTeamId, kLateUserId, admission_expired);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  // 重复的 destroy 日志
  CASE_EXPECT_TRUE(
      team_test::inject_log_message(test, team_chain, team_test::make_destroy_log_message(/*sequence=*/0)));
  // 迟到的个人 remove_member 通知
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, private_chain,
      make_personal_remove_action(kTeamId, kUserId, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM)));
  team_test::pump_rounds(test, 3);

  // 终态断言: manager 空、table 无残留、无新增 dirty、无新增 SS 调用
  CASE_EXPECT_TRUE(!team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(!team_mgr.get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
  expect_team_absent_in_table(*user_inst, kTeamId);
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }
  // 迟到事件不产生任何新上行(self 来自快照无 flush; 退出上行随销毁收编停止)
  CASE_EXPECT_EQ(static_cast<int>(send_message_after_destroy),
                 static_cast<int>(ss_capture.send_message_reqs.size()));
  CASE_EXPECT_TRUE(ss_capture.heartbeat_reqs.empty());

  CASE_EXPECT_EQ(0, test.stop());
}

// ROBUST-04: 快照重建时 pending/索引清理。
// - joined_team 注册队伍时清理同 team_key 的自己旧 pending 邀请/加入请求(add_team 语义);
// - 权威重建快照整体替换队伍级缓存: 旧成员/旧两类 pending/旧共享数据/旧 configure 全部清理,
//   仅存新快照内容(逐项断言); 客户端收到的新 snapshot 同样无旧条目且内部字段被裁剪;
// - manager 索引指向同一队伍对象(不重建), 其他队伍的自己 pending 不受重建影响;
// - table dump 与 manager 索引一致; 对象随频道销毁释放后 dirty handle 注销, 不再产生迟到推送。
CASE_TEST(lobbysvr_user_team, robust_snapshot_rebuild_cleans_pendings_and_indexes) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  team_test::now_offset_guard time_guard;

  constexpr uint64_t kUserId = 30054;
  constexpr uint64_t kSessionId = 0x10054;
  constexpr int64_t kTeamId = 541;
  constexpr int64_t kSelfInvitedTeamId = 542;
  constexpr int64_t kSelfRequestedTeamId = 543;
  constexpr uint64_t kOldMemberUserId = 91054;
  constexpr uint64_t kNewMemberUserId = 91055;
  constexpr uint64_t kOldInviteeId = 92054;
  constexpr uint64_t kExtraInviteeId = 92055;
  constexpr uint64_t kNewInviteeId = 92056;
  constexpr uint64_t kOldRequesterId = 93054;
  constexpr uint64_t kExtraRequesterId = 93055;
  constexpr uint64_t kNewRequesterId = 93056;

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

  // 1. 自己的 pending: 541 的陈旧邀请(将被 joined 清理), 542 邀请 + 543 加入请求(重建后必须保留)
  const auto pending_expired = team_test::now_offset_guard::logical_now() + std::chrono::hours(1);
  const auto pending_expired_ts = protobuf_from_system_clock(pending_expired);
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, private_chain, make_invited_action(kTeamId, kUserId, pending_expired)));
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, private_chain, make_invited_action(kSelfInvitedTeamId, kUserId, pending_expired)));
  CASE_EXPECT_TRUE(team_test::inject_event_message(
      test, private_chain,
      make_apply_join_request_action(kSelfRequestedTeamId, kUserId, pending_expired, "4.0.0-self", 0x1234)));
  auto& team_mgr = user_inst->get_user_team_manager();
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!team_mgr.get_pending_invitation(team_test::make_team_key(kTeamId)) &&
           !!team_mgr.get_pending_invitation(team_test::make_team_key(kSelfInvitedTeamId)) &&
           !!team_mgr.get_pending_join_request(team_test::make_team_key(kSelfRequestedTeamId));
  }));

  // 2. joined_team(541): add_team 清理同 team_key 的自己 pending, 其他队伍的不受影响
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));
  CASE_EXPECT_TRUE(!team_mgr.get_pending_invitation(team_test::make_team_key(kTeamId)));
  {
    auto pending = team_mgr.get_pending_invitation(team_test::make_team_key(kSelfInvitedTeamId));
    CASE_EXPECT_TRUE(!!pending);
    if (pending) {
      CASE_EXPECT_EQ(kSelfInvitedTeamId, pending->team_key().team_id());
      CASE_EXPECT_EQ(kUserId, pending->invitee().user_id());
      CASE_EXPECT_EQ(team_test::kCaptainUserId, pending->inviter().user_id());
      CASE_EXPECT_EQ(pending_expired_ts.seconds(), pending->expired_timepoint().seconds());
    }
    auto join_request = team_mgr.get_pending_join_request(team_test::make_team_key(kSelfRequestedTeamId));
    CASE_EXPECT_TRUE(!!join_request);
    if (join_request) {
      CASE_EXPECT_EQ(kSelfRequestedTeamId, join_request->team_key().team_id());
      CASE_EXPECT_EQ(kUserId, join_request->requester().user_id());
      CASE_EXPECT_EQ(std::string("4.0.0-self"), join_request->client_version());
      CASE_EXPECT_EQ(0x1234, static_cast<int>(join_request->user_router_server_id()));
    }
  }

  // 3. 快照 S1(旧缓存): M1 + P1 + R1 + configure + 队伍共享数据
  atfw::team::DTeamStorage old_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(old_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(old_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("4.1.0-self"));
  team_test::add_storage_member(
      old_storage, kOldMemberUserId,
      team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
          .set_client_version("4.1.0-m1")
          .set_shared_member_data({team_test::pack_member_module(team_test::make_member_ready_module(false))}));
  team_test::add_storage_invitation(old_storage, kOldInviteeId, pending_expired, true);
  team_test::add_storage_join_request(old_storage, kOldRequesterId, pending_expired, true);
  old_storage.mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
  protobuf_copy_message(*old_storage.add_shared_team_data(),
                        team_test::pack_team_module(team_test::make_team_matching_module(false)));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, old_storage));

  auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    if (atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL != current->get_cached_permission_role()) {
      return false;
    }
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 3 == snapshot.snapshot().member_size();
  }));
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, kOldMemberUserId));
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kOldInviteeId));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kOldRequesterId));
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, snapshot.snapshot().configure().invite_role());
  }
  // 客户端快照视图: 内部字段裁剪, 成员共享数据解包下发
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return 1 == view.snapshots.size();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    const auto& first_snapshot = view.snapshots.front();
    CASE_EXPECT_EQ(kTeamId, first_snapshot.snapshot().team_key().team_id());
    for (const auto& member : first_snapshot.snapshot().member()) {
      team_test::expect_member_projection_clean(member);
    }
    CASE_EXPECT_EQ(0, first_snapshot.snapshot().shared_team_data_size());
    for (const auto& invitation : first_snapshot.snapshot().pending_invitation()) {
      CASE_EXPECT_TRUE(invitation.invitee_private_channel().channel_id().empty());
    }
    const auto* unpacked = team_test::find_unpacked_member(first_snapshot, kOldMemberUserId);
    CASE_EXPECT_TRUE(nullptr != unpacked);
    if (nullptr != unpacked && !unpacked->shared_member_data().empty()) {
      CASE_EXPECT_TRUE(unpacked->shared_member_data().begin()->has_battle());
      CASE_EXPECT_TRUE(!unpacked->shared_member_data().begin()->battle().ready());
    }
  }

  // 4. 增量追加第二组 pending(将被重建快照整体替换)
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  {
    atfw::team::DTeamAction action;
    fill_invitation(*action.mutable_add_invitation(), kTeamId, kExtraInviteeId,
                    pending_expired + std::chrono::seconds(100));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 2 == snapshot.snapshot().pending_invitation_size();
  }));
  {
    atfw::team::DTeamAction action;
    fill_join_request(*action.mutable_add_join_request(), kTeamId, kExtraRequesterId,
                      pending_expired + std::chrono::seconds(100));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return 2 == snapshot.snapshot().pending_join_request_size();
  }));

  // 5. 权威重建快照 S2(saved 覆盖 seq1-2): 旧 pending/成员/共享数据/configure 全部清理
  atfw::team::DTeamStorage rebuilt_storage = team_test::make_team_storage(kTeamId, team_chain.sequence);
  team_test::add_storage_member(rebuilt_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(rebuilt_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("4.1.0-self"));
  team_test::add_storage_member(
      rebuilt_storage, kNewMemberUserId,
      team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN)
          .set_client_version("4.1.0-m3")
          .set_shared_member_data({team_test::pack_member_module(team_test::make_member_ready_module(true))}));
  team_test::add_storage_invitation(rebuilt_storage, kNewInviteeId,
                                    pending_expired + std::chrono::seconds(200), true);
  team_test::add_storage_join_request(rebuilt_storage, kNewRequesterId,
                                      pending_expired + std::chrono::seconds(200), true);
  rebuilt_storage.mutable_configure()->set_invite_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  protobuf_copy_message(*rebuilt_storage.add_shared_team_data(),
                        team_test::pack_team_module(team_test::make_team_matching_module(true)));
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), /*create_sequence=*/1,
                                           team_chain.sequence, &rebuilt_storage, /*custom_data_sequence=*/2,
                                           team_chain.hash_code)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kNewMemberUserId);
  }));
  // 快照不携带消息日志, WAL 日志存储被清空; 后续增量(destroy 日志)按空存储重新起 hash 链
  team_chain.hash_code = 0;
  {
    // 缓存逐项断言: 旧成员/旧 pending/旧共享数据全部清理, 仅存新快照内容
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kOldMemberUserId));
    CASE_EXPECT_TRUE(nullptr == team_test::find_unpacked_member(snapshot, kOldMemberUserId));
    const auto* new_member = team_test::find_snapshot_member(snapshot, kNewMemberUserId);
    CASE_EXPECT_TRUE(nullptr != new_member);
    if (nullptr != new_member) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN, new_member->role());
      CASE_EXPECT_EQ(std::string("4.1.0-m3"), new_member->client_version());
    }
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kOldInviteeId));
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kExtraInviteeId));
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kNewInviteeId));
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, kOldRequesterId));
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, kExtraRequesterId));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kNewRequesterId));
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, snapshot.snapshot().configure().invite_role());
    CASE_EXPECT_EQ(1, static_cast<int>(snapshot.shared_team_data().size()));
    if (!snapshot.shared_team_data().empty()) {
      CASE_EXPECT_TRUE(snapshot.shared_team_data().begin()->has_battle());
      CASE_EXPECT_TRUE(snapshot.shared_team_data().begin()->battle().matching());
    }
  }
  {
    // 客户端收到第二条 snapshot, 无旧条目且内部字段被裁剪
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(2, static_cast<int>(view.snapshots.size()));
    const auto& rebuilt_snapshot = view.snapshots.back();
    CASE_EXPECT_EQ(3, rebuilt_snapshot.snapshot().member_size());
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(rebuilt_snapshot, kOldMemberUserId));
    CASE_EXPECT_EQ(1, rebuilt_snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(1, rebuilt_snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(rebuilt_snapshot, kNewInviteeId));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(rebuilt_snapshot, kNewRequesterId));
    for (const auto& invitation : rebuilt_snapshot.snapshot().pending_invitation()) {
      CASE_EXPECT_TRUE(invitation.invitee_private_channel().channel_id().empty());
    }
    for (const auto& join_request : rebuilt_snapshot.snapshot().pending_join_request()) {
      CASE_EXPECT_TRUE(join_request.requester_private_channel().channel_id().empty());
      CASE_EXPECT_EQ(0, static_cast<int>(join_request.user_router_server_id()));
    }
    CASE_EXPECT_EQ(0, rebuilt_snapshot.snapshot().shared_team_data_size());
    CASE_EXPECT_EQ(1, static_cast<int>(rebuilt_snapshot.shared_team_data().size()));
    if (!rebuilt_snapshot.shared_team_data().empty()) {
      CASE_EXPECT_TRUE(rebuilt_snapshot.shared_team_data().begin()->battle().matching());
    }
  }
  {
    // manager 索引断言: 同一队伍对象(不重建), 其他队伍的自己 pending 不受重建影响
    CASE_EXPECT_TRUE(current == team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId)));
    CASE_EXPECT_TRUE(current == team_mgr.get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
    CASE_EXPECT_TRUE(!!team_mgr.get_pending_invitation(team_test::make_team_key(kSelfInvitedTeamId)));
    CASE_EXPECT_TRUE(!!team_mgr.get_pending_join_request(team_test::make_team_key(kSelfRequestedTeamId)));
  }
  {
    // table dump 与 manager 索引一致
    PROJECT_NAMESPACE_ID::table_user dumped_table;
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(0, team_mgr.dump(ctx, dumped_table));
    CASE_EXPECT_EQ(1, dumped_table.team_data().group_size());
    if (1 == dumped_table.team_data().group_size()) {
      const auto& group = dumped_table.team_data().group(0);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL,
                     static_cast<PROJECT_NAMESPACE_ID::EnTeamType>(group.team_type()));
      CASE_EXPECT_TRUE(group.has_current());
      if (group.has_current()) {
        CASE_EXPECT_EQ(kTeamId, group.current().team_key().team_id());
        CASE_EXPECT_EQ(team_test::kZoneId, group.current().team_key().zone_id());
        CASE_EXPECT_EQ(kUserId, group.current().user_key().user_id());
        CASE_EXPECT_EQ(team_test::kCaptainUserId, group.current().captain_user_key().user_id());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, group.current().user_role());
      }
    }
  }

  // 6. 脏推送 handle 清理: 对象随频道销毁释放后不再产生任何迟到推送
  CASE_EXPECT_TRUE(
      team_test::inject_log_message(test, team_chain, team_test::make_destroy_log_message(/*sequence=*/0)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam).empty();
  }));
  CASE_EXPECT_TRUE(team_test::pump_until(
      test, [&] { return !team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId)); }));
  current = nullptr;

  test.cs().clear_history();
  team_test::pump_rounds(test, 3);
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }
  expect_team_absent_in_table(*user_inst, kTeamId);

  CASE_EXPECT_EQ(0, test.stop());
}

// =============================================================================
// 以下四例对应计划 §5.6 P2 健壮性矩阵(Any 解包错误与夹具卫生);
// 上文 ROBUST-01..04 四例对应 §3 消息缺失/乱序叙事的等幂与终态契约。
// =============================================================================

// §5.6 ROBUST-01: 个人频道错误 Any(类型不符/无法解包)的 action 不修改任一缓存;
// 已处理序号仍推进(事件已消费), 后续更高 sequence 的合法 action 正常处理。
CASE_TEST(lobbysvr_user_team, robust_bad_any_personal_event_ignored) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30055;
  constexpr uint64_t kSessionId = 0x10055;
  constexpr int64_t kTeamId = 551;

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
  team_test::now_offset_guard time_guard;

  // Leg A: type_url 正确但 value 损坏 -> 业务 dispatch 触发、UnpackTo 失败被忽略, 已处理序号推进;
  {
    atframework::dtmq::DChannelMessage bad_msg;
    atfw::team::DTeamMemberAction shell;
    shell.mutable_invited();
    CASE_EXPECT_TRUE(bad_msg.mutable_detail()->mutable_event()->PackFrom(shell));
    bad_msg.mutable_detail()->mutable_event()->set_value("corrupted-payload-bytes");
    *bad_msg.mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
    CASE_EXPECT_TRUE(team_test::inject_log_message(test, private_chain, bad_msg));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  // Leg B: type_url 不匹配 -> 订阅层按消息类型过滤, 业务回调不触发; 只允许 no-op
  {
    atframework::dtmq::DChannelMessage bad_msg;
    atfw::team::DTeamStorage wrong_typed;
    protobuf_copy_message(*wrong_typed.mutable_team_key(), team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(bad_msg.mutable_detail()->mutable_event()->PackFrom(wrong_typed));
    *bad_msg.mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
    CASE_EXPECT_TRUE(team_test::inject_log_message(test, private_chain, bad_msg));
  }
  test.cs().clear_history();
  team_test::pump_rounds(test, 3);
  {
    auto& mgr = user_inst->get_user_team_manager();
    CASE_EXPECT_TRUE(!mgr.get_pending_invitation(team_test::make_team_key(kTeamId)));
    CASE_EXPECT_TRUE(!mgr.get_pending_join_request(team_test::make_team_key(kTeamId)));
    CASE_EXPECT_TRUE(!mgr.get_team_by_team_key(team_test::make_team_key(kTeamId)));
    CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
    CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(test, kSessionId).empty());
  }
  // 注: 后续的合法事件正常入列同时证明两条坏消息已被 WAL 接收(hash chain 连续), 而非静默丢失。

  // 后续更高 sequence 的合法 invited 正常入列(全字段)
  const auto expiry = team_test::now_offset_guard::logical_now() + std::chrono::seconds(300);
  {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kUserId));
    *invited->mutable_start_timepoint() = protobuf_from_system_clock(team_test::now_offset_guard::logical_now());
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamId));
  }));
  {
    auto pending = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!pending);
    if (pending) {
      CASE_EXPECT_EQ(kTeamId, pending->team_key().team_id());
      CASE_EXPECT_EQ(kUserId, pending->invitee().user_id());
      CASE_EXPECT_EQ(team_test::kCaptainUserId, pending->inviter().user_id());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// §5.6 ROBUST-02: 队伍快照 custom_data 为错误 Any 时不清空旧缓存、不产生伪 snapshot 推送;
// 后续合法快照(更高 custom_data_sequence)正常恢复并下发。
CASE_TEST(lobbysvr_user_team, robust_bad_any_team_snapshot_preserves_cache) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30056;
  constexpr uint64_t kSessionId = 0x10056;
  constexpr int64_t kTeamId = 552;
  constexpr uint64_t kMemberUserId = 91056;

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

  // 建立富缓存基线: 两成员 + 队伍共享数据
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  team_test::add_storage_member(team_storage, kMemberUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("4.0.0-base"));
  protobuf_copy_message(*team_storage.add_shared_team_data(),
                        team_test::pack_team_module(team_test::make_team_matching_module(true)));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  auto& team_mgr = user_inst->get_user_team_manager();
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    return current && nullptr != team_test::find_snapshot_member([&] {
      PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
      dump_team_snapshot(*current, snapshot);
      return snapshot;
    }(), kMemberUserId);
  }));
  test.cs().clear_history();

  // 错误 Any 快照: custom_data 打包成 DTeamInvitation(期望 DTeamStorage), custom_data_sequence 必须更高
  {
    atfw::team::DTeamInvitation wrong_typed;
    protobuf_copy_message(*wrong_typed.mutable_team_key(), team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(team_test::receive_channel_event(
        test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), /*create_sequence=*/1,
                                             /*last_sequence=*/0, &wrong_typed, /*custom_data_sequence=*/2)));
  }
  // no-op 稳定性: 旧缓存逐项不变, 且不产生新的客户端推送
  team_test::pump_rounds(test, 4);
  {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!current);
    if (current) {
      PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
      dump_team_snapshot(*current, snapshot);
      CASE_EXPECT_EQ(3, snapshot.snapshot().member_size());
      const auto* member = team_test::find_snapshot_member(snapshot, kMemberUserId);
      CASE_EXPECT_TRUE(nullptr != member);
      if (nullptr != member) {
        CASE_EXPECT_EQ(std::string("4.0.0-base"), member->client_version());
      }
      // dump 级解包模块: matching=true
      CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
      if (snapshot.shared_team_data_size() > 0) {
        CASE_EXPECT_TRUE(snapshot.shared_team_data(0).battle().matching());
      }
      CASE_EXPECT_TRUE(current->is_matching());
    }
    CASE_EXPECT_TRUE(team_test::collect_dirty_sync_pushes(test, kSessionId).empty());
  }

  // 合法快照(更高 custom_data_sequence)恢复: 缓存重建且客户端收到 snapshot
  atfw::team::DTeamStorage healed_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(healed_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(healed_storage, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  team_test::add_storage_member(healed_storage, kMemberUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL)
                                    .set_client_version("4.0.1-healed"));
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), /*create_sequence=*/1,
                                           /*last_sequence=*/0, &healed_storage, /*custom_data_sequence=*/3)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    if (!current) {
      return false;
    }
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    const auto* member = team_test::find_snapshot_member(snapshot, kMemberUserId);
    return nullptr != member && "4.0.1-healed" == member->client_version();
  }));
  // matching 模块未出现在修复快照中: 快照为全量覆盖, 旧 key 不得残留
  {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!current);
    if (current) {
      PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
      dump_team_snapshot(*current, snapshot);
      // 快照为全量覆盖: 修复快照未携带 matching key, 旧模块不得残留; 派生状态也必须复位
      CASE_EXPECT_EQ(0, snapshot.shared_team_data_size());
      CASE_EXPECT_FALSE(current->is_matching());
    }
    CASE_EXPECT_TRUE(!team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// §5.6 ROBUST-03: 队伍 action 共享数据中的错误 Any 只跳过该 key, 同批合法 key 正常合并;
// 缓存与 dump 输出不产生错误的解包结果。
CASE_TEST(lobbysvr_user_team, robust_bad_shared_data_any_skips_only_that_key) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30057;
  constexpr uint64_t kSessionId = 0x10057;
  constexpr int64_t kTeamId = 553;

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

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  protobuf_copy_message(*team_storage.add_shared_team_data(),
                        team_test::pack_team_module(team_test::make_team_matching_module(true)));
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  auto& team_mgr = user_inst->get_user_team_manager();
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    if (!current) {
      return false;
    }
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return nullptr != team_test::find_snapshot_member(snapshot, kUserId);
  }));
  test.cs().clear_history();

  // team_update: 同批携带一个错误 Any(非空但类型不符, 不是 GAP-09 删除标记)和一个合法 matching=false 模块
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  {
    atfw::team::DTeamAction action;
    auto* update = action.mutable_team_update();
    auto* bad = update->add_shared_team_data();
    bad->set_key(424242);
    bad->mutable_value()->mutable_data()->set_type_url("type.googleapis.com/atframework.team.DTeamInvitation");
    bad->mutable_value()->mutable_data()->set_value("not-a-valid-module-payload");
    protobuf_copy_message(*update->add_shared_team_data(),
                          team_test::pack_team_module(team_test::make_team_matching_module(false)));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, action));
  }
  // 合法 key 生效: matching 翻转为 false; 错误 key 不进缓存
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    if (!current) {
      return false;
    }
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    dump_team_snapshot(*current, snapshot);
    return snapshot.shared_team_data_size() > 0 && !snapshot.shared_team_data(0).battle().matching();
  }));
  {
    auto current = team_mgr.get_team_by_team_key(team_test::make_team_key(kTeamId));
    CASE_EXPECT_TRUE(!!current);
    if (current) {
      PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
      dump_team_snapshot(*current, snapshot);
      // dump 只导出合法解包的模块(battle.matching=false), 错误 Any 不产生输出
      CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
      if (snapshot.shared_team_data_size() > 0) {
        CASE_EXPECT_TRUE(snapshot.shared_team_data(0).has_battle());
        CASE_EXPECT_FALSE(snapshot.shared_team_data(0).battle().matching());
      }
    }
    // increase 视图按原始 action 透传(客户端契约), 缓存侧已验证只应用合法 key
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(
        !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kTeamUpdate).empty());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// §5.6 ROBUST-04: 夹具卫生——用例入口时全局时间 offset 无泄漏(组内前序用例 teardown 均已复位);
// 同一进程内两个 runtime 顺序跑同一流程(不同 id), 单例/dirty handle/订阅状态互不污染。
// 注: dtmq 订阅管理器为进程级单例且频道已消费序号不回退, 因此两遍必须使用不同频道 id(§4.3 增补约定)。
CASE_TEST(lobbysvr_user_team, robust_fixture_time_and_runtime_hygiene) {
  // 组内前序用例 teardown 若泄漏 offset, 此处立即暴露(standalone 时恒为 0)
  CASE_EXPECT_TRUE(atfw::util::time::time_utility::get_global_now_offset() == std::chrono::seconds::zero());

  auto run_once = [](uint64_t user_id, int64_t team_id) {
    atfw::testing::runtime test;
    CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
    if (!test.is_running()) {
      return;
    }
    user::ptr_t user_inst;
    std::string subscriber_key;
    atframework::dtmq::DChannelIdKey private_channel_key;
    CASE_EXPECT_TRUE(team_test::setup_team_user(test, user_id, user_inst, subscriber_key, private_channel_key));
    if (!user_inst) {
      test.stop();
      return;
    }
    team_test::channel_event_chain private_chain;
    private_chain.channel_key = private_channel_key;
    CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, team_id));
    CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, team_id, team_test::make_team_storage(team_id)));
    CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
      return !!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(team_id));
    }));
    CASE_EXPECT_EQ(0, test.stop());
  };

  run_once(30058, 554);
  // 第一遍 stop 后无 offset 泄漏, 第二遍在全新 runtime 上得到相同结果
  CASE_EXPECT_TRUE(atfw::util::time::time_utility::get_global_now_offset() == std::chrono::seconds::zero());
  run_once(30059, 555);
}

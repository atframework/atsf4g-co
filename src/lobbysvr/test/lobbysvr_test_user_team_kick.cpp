// Copyright 2026 atframework
//
// Offline regression tests for lobbysvr team-removal convergence and client notification
// (plan: src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md §3, §5.1).
//
// Covered here:
// - KICK-01: both personal-notification / team-channel orderings and the common final state (§3.1);
// - KICK-02: personal notification lost; the team-channel remove_member(self) action (and, as a separate
//   fallback variant, an authoritative snapshot whose member list excludes self) drives client notification
//   and the final manager/cache/index cleanup (§3.2, snapshot fallback per §3.3);
// - KICK-03: team-channel action and snapshot both lost; the personal remove_member fallback alone starts
//   exit convergence, retries keep the full team_key payload, and the WAL remove (channel destroy) closes
//   the subscription and finishes manager/index cleanup (§3.3 assignment variant);
// - KICK-04: only the personal remove arrived while the channel snapshot still contains self; the
//   pending-to-exit retry task keeps driving room convergence and the team is never exported as a
//   running team in the meantime (§5.1 KICK-04);
// - DEST-01: destroy_team action / channel destroy / personal destroy notification must deliver a destroy
//   increase to the client before the cached team object is dropped (§2.3, §6.7).

#include "lobbysvr_test_user_team_common.h"

#include "rpc/dtmq/dtmqproxysvrservice.atfw.gen.h"

namespace {

// 注入一条本人的个人 remove_member 通知(被踢/队伍解散)
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

// 注入一条队伍频道 destroy_team WAL 事件
bool inject_channel_destroy_action(atfw::testing::runtime& test, team_test::channel_event_chain& team_chain,
                                   int64_t team_id) {
  atfw::team::DTeamAction action;
  protobuf_copy_message(*action.mutable_destroy_team(), team_test::make_team_key(team_id));
  return team_test::inject_event_message(test, team_chain, action);
}

// 在真实 runtime task 内驱动 minute refresh
bool run_minute_refresh(atfw::testing::runtime& test, const user::ptr_t& user_ptr) {
  return team_test::run_sync_task(test, "team.refresh_minute", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
    user_ptr->get_user_team_manager().refresh_feature_limit_minute(ctx);
    RPC_RETURN_CODE(0);
  });
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

// 注入 ready 的 dtmq-proxy 节点并应答 subscribe/heartbeat(与 chat manager 测试一致), 使对象释放后的
// outbound unsubscribe 调用真实进入 mock SS 记录(KICK-03 的订阅自动关闭断言依赖它)
bool setup_dtmq_proxy_node(atfw::testing::runtime& test, atfw::testing::ss_rule_handle& out_subscribe_rule) {
  atfw::testing::mock_node node;
  node.set_id(team_test::kDtmqProxyNodeId)
      .set_name("unit-test-dtmq-proxy")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtMqProxySvr))
      .set_type_name("dtmq-proxysvr")
      .set_zone_id(team_test::kZoneId)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  if (!remote) {
    CASE_MSG_INFO() << "add dtmq proxy node failed\n";
    return false;
  }
  // Mock injection writes the global discovery set directly; the common-module discovery index only replays
  // existing nodes on reload.
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }

  out_subscribe_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_subscribe(),
      atframework::dtmq::SSChannelSubscribeReq::descriptor()->full_name(),
      atframework::dtmq::SSChannelSubscribeRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& view, google::protobuf::Message& rsp_msg) -> rpc::result_code_type {
        const auto& req = static_cast<const atframework::dtmq::SSChannelSubscribeReq&>(view.body);
        auto& rsp = static_cast<atframework::dtmq::SSChannelSubscribeRsp&>(rsp_msg);
        for (const auto& heartbeat : req.heartbeat()) {
          auto* subscribe_node = rsp.add_subscribe_node();
          if (nullptr != subscribe_node) {
            subscribe_node->mutable_channel_key()->CopyFrom(heartbeat.channel_key());
            subscribe_node->set_server_id(team_test::kDtmqProxyNodeId);
          }
        }
        RPC_RETURN_CODE(0);
      });
  if (!out_subscribe_rule) {
    CASE_MSG_INFO() << "mock dtmq subscribe failed: " << test.ss().get_diagnostic() << '\n';
    return false;
  }
  return true;
}

// 富字段初始快照(§3.2 要求): self + 队长 + 队伍共享数据 + 两类 pending, 用于验证 remove/snapshot 兜底
// 不误删其他缓存
atfw::team::DTeamStorage make_rich_kick_storage(int64_t team_id, uint64_t self_id, uint64_t invitee_id,
                                                uint64_t requester_id,
                                                std::chrono::system_clock::time_point admission_expiry) {
  atfw::team::DTeamStorage storage = team_test::make_team_storage(team_id);
  team_test::add_storage_member(storage, team_test::kCaptainUserId,
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(storage, self_id, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  protobuf_copy_message(*storage.add_shared_team_data(),
                        team_test::pack_team_module(team_test::make_team_matching_module(false)));
  team_test::add_storage_invitation(storage, invitee_id, admission_expiry);
  team_test::add_storage_join_request(storage, requester_id, admission_expiry);
  return storage;
}

// 完整校验一条捕获的退出/移除请求 payload(顶层 team_key/sender 与内嵌 action 的 zone_id/user_id/reason)
void expect_remove_request_payload(const atfw::team::SSTeamRoomSendMessageReq& req, int64_t team_id,
                                   uint64_t user_id, atfw::team::EnTeamExitReason reason) {
  CASE_EXPECT_EQ(team_test::kZoneId, req.team_key().zone_id());
  CASE_EXPECT_EQ(team_id, req.team_key().team_id());
  CASE_EXPECT_EQ(team_test::kZoneId, req.sender_user_key().zone_id());
  CASE_EXPECT_EQ(user_id, req.sender_user_key().user_id());
  CASE_EXPECT_TRUE(req.action().has_remove_member());
  if (req.action().has_remove_member()) {
    const auto& remove_data = req.action().remove_member();
    CASE_EXPECT_EQ(team_test::kZoneId, remove_data.team_key().zone_id());
    CASE_EXPECT_EQ(team_id, remove_data.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, remove_data.user_key().zone_id());
    CASE_EXPECT_EQ(user_id, remove_data.user_key().user_id());
    CASE_EXPECT_EQ(reason, remove_data.remove_member_reason());
  }
}

// 按 reason 统计捕获的 remove_member 请求(区分超期兜底 EXPIRED / 被踢 REMOVE_MEMBER 等移除路径)
size_t count_remove_requests_by_reason(const team_test::team_room_ss_capture& capture, int64_t team_id,
                                       uint64_t user_id, atfw::team::EnTeamExitReason reason) {
  size_t ret = 0;
  for (const auto& req : capture.send_message_reqs) {
    if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == team_id &&
        req.action().remove_member().user_key().user_id() == user_id &&
        req.action().remove_member().remove_member_reason() == reason) {
      ++ret;
    }
  }
  return ret;
}

// 取最后一条该 team/user 的捕获 remove_member 请求(最近一次退出/重试)
const atfw::team::SSTeamRoomSendMessageReq* find_last_remove_request(
    const team_test::team_room_ss_capture& capture, int64_t team_id, uint64_t user_id) {
  const atfw::team::SSTeamRoomSendMessageReq* ret = nullptr;
  for (const auto& req : capture.send_message_reqs) {
    if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == team_id &&
        req.action().remove_member().user_key().user_id() == user_id) {
      ret = &req;
    }
  }
  return ret;
}

// user_get_info 的导出源: current 且非 exiting/destroyed 的队伍计数
size_t count_running_teams(user& user_inst) {
  size_t ret = 0;
  user_inst.get_user_team_manager().foreach_running_team(
      [&ret](uint32_t, const atfw::util::nostd::nonnull<user_team::ptr_t>&) { ++ret; });
  return ret;
}

}  // namespace

// KICK-01 顺序一(§3.1.1): 队伍频道 remove_member(self) 先到。
// - self 成员缓存删除、role 回到 GUEST、客户端立即收到带真实 reason 的 increase;
// - 个人 remove_member 到达后 manager 移除该队伍;
// - 共同终态: 索引/分组/table 均无该队; 迟到的重复通知不重建队伍、不产生二次脏数据。
CASE_TEST(lobbysvr_user_team, kick_channel_event_first_then_personal) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30031;
  constexpr uint64_t kSessionId = 0x10031;
  constexpr int64_t kTeamId = 501;

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
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(team_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
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

  // 1. 队伍频道 remove_member(self, KICKED) 先到
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));

  // 客户端收到带 team_key/user_key/真实 reason 的 increase
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
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, remove_data.remove_member_reason());
    }
  }

  // self 成员缓存删除、role 回到 GUEST; 个人通知未到时 manager 仍保留该队伍
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST, current->get_cached_permission_role());
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    current->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kUserId));
    // 其他成员不受误删
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId));
  }
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));

  // 2. 个人 remove_member 到达: manager 移除该队伍
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  expect_team_absent_in_table(*user_inst, kTeamId);

  // 释放本地引用: 对象随 manager 移除而释放后 dirty handle 才会注销(生产无其他持有者)
  current = nullptr;

  // 3. 迟到的重复个人通知/队伍事件: 不重建队伍、不产生二次脏数据、不产生退出请求
  test.cs().clear_history();
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }
  CASE_EXPECT_EQ(0, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// KICK-01 顺序二(§3.1.2): 个人 remove_member 先到。
// - 当前队伍转入 pending-to-exit, 缓存暂时保留, reason 必须来自个人通知(不能写回 DEFAULT);
// - 推进到 exit retry 边界后 minute refresh 补发 remove 请求, reason 等于个人通知中的真实 reason;
// - 随后队伍频道 remove_member(self) 完成客户端 increase; minute refresh 收编队伍;
// - 共同终态同顺序一。
CASE_TEST(lobbysvr_user_team, kick_personal_event_first_then_channel) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30032;
  constexpr uint64_t kSessionId = 0x10032;
  constexpr int64_t kTeamId = 502;

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
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(team_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));

  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 1. 个人 remove_member(KICKED) 先到: 队伍转入 pending-to-exit, 缓存暂时保留
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team->is_exiting(); }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  // reason 必须来自个人通知, 不能写回 DEFAULT(退出重试复用该 reason)
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, team->get_last_exit_reason());

  // 2. 推进到 exit retry 边界: minute refresh 补发 remove 请求, reason 必须仍然是 KICKED
  {
    team_test::now_offset_guard time_guard;
    time_guard.advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
    CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 1;
  }));
  {
    bool reason_kept = false;
    for (const auto& req : ss_capture.send_message_reqs) {
      if (req.action().has_remove_member() && req.action().remove_member().team_key().team_id() == kTeamId &&
          req.action().remove_member().user_key().user_id() == kUserId) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, req.action().remove_member().remove_member_reason());
        reason_kept = true;
      }
    }
    CASE_EXPECT_TRUE(reason_kept);
  }

  // 3. 队伍频道 remove_member(self) 到达: 完成客户端 increase
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember).empty();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto removes = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember);
    CASE_EXPECT_EQ(1, static_cast<int>(removes.size()));
    if (!removes.empty()) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER,
                     removes.front()->action().remove_member().remove_member_reason());
    }
  }

  // 4. minute refresh 收编队伍(频道 ready 且已确认非成员)
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  expect_team_absent_in_table(*user_inst, kTeamId);

  // 释放本地引用: 对象随 manager 移除而释放后 dirty handle 才会注销(生产无其他持有者)
  team = nullptr;

  // 5. 迟到的重复事件: 不重建队伍、不再发退出请求、不产生二次脏数据
  test.cs().clear_history();
  const size_t sent_requests = ss_capture.send_message_reqs.size();
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_EQ(static_cast<int>(sent_requests), static_cast<int>(ss_capture.send_message_reqs.size()));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// DEST-01 路径一(§2.3/§6.7): 队伍频道 destroy_team action。
// 客户端必须先收到携带 destroy_team action 的 increase, 然后 manager 才允许移除对象;
// 当前实现会在 batch dirty flush 前移除对象, 导致客户端永远收不到解散通知。
CASE_TEST(lobbysvr_user_team, destroy_action_pushes_increase_before_cleanup) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30033;
  constexpr uint64_t kSessionId = 0x10033;
  constexpr int64_t kTeamId = 503;

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
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(team_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    return current && atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  CASE_EXPECT_TRUE(inject_channel_destroy_action(test, team_chain, kTeamId));

  // 客户端先收到 destroy increase, manager 随后清空
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
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  expect_team_absent_in_table(*user_inst, kTeamId);

  // 清理后不再有指向该队伍的迟到脏数据
  test.cs().clear_history();
  team_test::pump_rounds(test, 3);
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// DEST-01 路径二(§2.3/§6.7): 仅有频道 destroy(无 destroy_team action)时合成等价的解散通知。
// 真实的频道销毁经 WAL destroy 日志下发(subscriber set_destroyed -> on_destroyed);
// 旧代际 destroy/on_destroyed 乱序不删除新代际由 SEQ-03 覆盖(需要完整的代际快照机制)。
CASE_TEST(lobbysvr_user_team, channel_destroyed_event_synthesizes_destroy_notify) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30034;
  constexpr uint64_t kSessionId = 0x10034;
  constexpr int64_t kTeamId = 504;

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
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(team_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage, /*create_sequence=*/1));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    return current && atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 频道直接销毁(WAL destroy 日志, 无 destroy_team action): 客户端仍必须收到等价的解散通知
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
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
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  expect_team_absent_in_table(*user_inst, kTeamId);

  CASE_EXPECT_EQ(0, test.stop());
}

// DEST-01 路径三(§2.3/§6.7): 个人 remove_member(reason=DESTROY_TEAM) 通知。
// 客户端必须收到等价的 destroy increase; 队伍对象随后经频道销毁收编, manager 清空。
CASE_TEST(lobbysvr_user_team, personal_destroy_notification_pushes_increase) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30035;
  constexpr uint64_t kSessionId = 0x10035;
  constexpr int64_t kTeamId = 505;

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
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(team_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto current = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
    return current && atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == current->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 个人解散通知: 客户端必须收到 destroy increase(reason 必须透传为 DESTROY_TEAM 才能触发解散语义)
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam).empty();
  }));

  // 队伍解散后频道随之销毁(WAL destroy 日志): manager 最终清空, 且不重复产生第二条 destroy 通知
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  CASE_EXPECT_TRUE(
      team_test::inject_log_message(test, team_chain, team_test::make_destroy_log_message(/*sequence=*/0)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(
                          team_test::count_actions_of_case(view, atfw::team::DTeamAction::kDestroyTeam)));
  }
  expect_team_absent_in_table(*user_inst, kTeamId);

  CASE_EXPECT_EQ(0, test.stop());
}

// KICK-02 主路径(§3.2): 个人 remove_member 通知整体丢失, 仅队伍频道 remove_member(self, 带完整
// zone_id/user_id) 送达。
// - 客户端立即收到带 team_key/user_key/真实 reason 的 increase;
// - 对象尚待生命周期收编时 dump 已无 self、role 回到 GUEST, 其他成员/共享数据/两类 pending 不受污染;
// - 逻辑时钟推进超过 wait_add_member_timeout(配置 5s)后 minute refresh 收编: manager 索引/分组/table 清空;
// - 清理有且只有一条移除路径(超期兜底恰好一条 EXPIRED 退出请求, 无 REMOVE_MEMBER 路径);
// - 迟到的个人通知幂等, 不产生二次退出循环。
CASE_TEST(lobbysvr_user_team, kick_channel_action_fallback_without_personal_notify) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30036;
  constexpr uint64_t kSessionId = 0x10036;
  constexpr int64_t kTeamId = 561;
  constexpr uint64_t kInviteeId = 81001;
  constexpr uint64_t kRequesterId = 81002;

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

  team_test::now_offset_guard time_guard;
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

  // 初始快照: self + 队长 + 队伍共享数据 + 两类 pending(过期时间取逻辑时钟, 确定且远大于后续推进步长)
  atfw::team::DTeamStorage team_storage = make_rich_kick_storage(
      kTeamId, kUserId, kInviteeId, kRequesterId, time_guard.logical_now() + std::chrono::hours{1});
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

  // 1. 仅注入队伍频道 remove_member(self, KICKED), 个人通知丢失
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));

  // 客户端立即收到带完整 team_key(含 zone_id)/user_key(含 zone_id)/真实 reason 的 increase
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return !team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember).empty();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto removes = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember);
    CASE_EXPECT_EQ(1, static_cast<int>(removes.size()));
    CASE_EXPECT_TRUE(view.snapshots.empty());
    if (!removes.empty()) {
      const auto& remove_data = removes.front()->action().remove_member();
      CASE_EXPECT_EQ(team_test::kZoneId, remove_data.team_key().zone_id());
      CASE_EXPECT_EQ(kTeamId, remove_data.team_key().team_id());
      CASE_EXPECT_EQ(team_test::kZoneId, remove_data.user_key().zone_id());
      CASE_EXPECT_EQ(kUserId, remove_data.user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, remove_data.remove_member_reason());
    }
  }

  // 2. 对象尚待生命周期收编: dump 已无 self、role 回到 GUEST; 其他成员/共享数据/admission 不被误删
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST, current->get_cached_permission_role());
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    current->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kUserId));
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId));
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
    if (1 == snapshot.snapshot().pending_invitation_size()) {
      CASE_EXPECT_EQ(kInviteeId, snapshot.snapshot().pending_invitation(0).invitee().user_id());
    }
    if (1 == snapshot.snapshot().pending_join_request_size()) {
      CASE_EXPECT_EQ(kRequesterId, snapshot.snapshot().pending_join_request(0).requester().user_id());
    }
    CASE_EXPECT_EQ(1, snapshot.shared_team_data_size());
  }
  // 个人通知未到, manager 仍保留该队伍(等待生命周期收编)
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  // 此刻没有任何退出请求发出(频道 action 只更新缓存)
  CASE_EXPECT_EQ(0, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  // 3. 推进超过 wait_add_member_timeout(配置 5s, 不写死旧的 30s)后 minute refresh 收编
  time_guard.advance(team_test::get_wait_add_member_timeout() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  expect_team_absent_in_table(*user_inst, kTeamId);

  // 恰好一条移除路径: 超期兜底发出且仅发出一条 EXPIRED 退出请求, 个人通知路径(REMOVE_MEMBER)从未运行
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 1;
  }));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));
  CASE_EXPECT_EQ(1,
                 static_cast<int>(count_remove_requests_by_reason(ss_capture, kTeamId, kUserId,
                                                                  atfw::team::EN_TEAM_EXIT_REASON_EXPIRED)));
  CASE_EXPECT_EQ(0,
                 static_cast<int>(count_remove_requests_by_reason(ss_capture, kTeamId, kUserId,
                                                                  atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER)));
  const auto* exit_req = find_last_remove_request(ss_capture, kTeamId, kUserId);
  CASE_EXPECT_TRUE(nullptr != exit_req);
  if (nullptr != exit_req) {
    expect_remove_request_payload(*exit_req, kTeamId, kUserId, atfw::team::EN_TEAM_EXIT_REASON_EXPIRED);
  }

  // 释放本地引用: 对象随 manager 移除而释放(生产无其他持有者)
  current = nullptr;

  // 4. 迟到的个人通知: 幂等, 不重建队伍、不产生二次退出循环、不产生二次脏数据
  test.cs().clear_history();
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));
  // 清理后不再重试
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// KICK-02 快照兜底变体(§3.2 任务变体, 行为契约见 §3.3): 个人通知与 remove 日志都未消费, 只收到成员表
// 排除自己的权威快照。(代码事实: DTeamStorage/lobbysvr 缓存均无 last_exit_member 字段, 兜底信号即成员表
// 排除 self。)
// - team B: 已超过 wait_add_member_timeout 时新快照到达, 快照回调完成下发后立即移除队伍;
//   新快照先清空旧缓存再重建: 旧 self/旧共享 key/旧 admission 均不泄漏;
// - team C(对照): 新快照仍含 self 时保留队伍不误判超时; 排除 self 但未超时则保持对象直到 minute
//   refresh, 且不得恢复 self;
// - 两个队伍各自恰好一条移除路径(一条 EXPIRED 退出请求)。
CASE_TEST(lobbysvr_user_team, kick_snapshot_fallback_without_personal_notify) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30037;
  constexpr uint64_t kSessionId = 0x10037;
  constexpr int64_t kTeamIdB = 562;
  constexpr int64_t kTeamIdC = 563;
  constexpr uint64_t kInviteeB = 81011;
  constexpr uint64_t kRequesterB = 81012;
  constexpr uint64_t kRequesterB2 = 81013;
  constexpr uint64_t kInviteeC = 81014;

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

  team_test::now_offset_guard time_guard;
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  // ---- team B: 已超过 wait_add_member_timeout, 排除 self 的快照到达后立即收编 ----
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamIdB));
  atfw::team::DTeamStorage storage_b1 = make_rich_kick_storage(
      kTeamIdB, kUserId, kInviteeB, kRequesterB, time_guard.logical_now() + std::chrono::hours{1});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamIdB, storage_b1));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamIdB));
    return team && atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 推进超过 wait_add_member_timeout, 再注入排除 self 的新快照(更高 custom_data_sequence)
  time_guard.advance(team_test::get_wait_add_member_timeout() + std::chrono::seconds{1});
  atfw::team::DTeamStorage storage_b2 = team_test::make_team_storage(kTeamIdB);
  team_test::add_storage_member(storage_b2, team_test::kCaptainUserId,
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  // 旧 invitation/旧 join request/旧队伍共享 key 全部消失, 只保留一条新的 join request
  team_test::add_storage_join_request(storage_b2, kRequesterB2,
                                      time_guard.logical_now() + std::chrono::hours{1});
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamIdB), /*create_sequence=*/1,
                                           /*last_sequence=*/0, &storage_b2, /*custom_data_sequence=*/2)));

  // 客户端收到 snapshot: 成员表不含 self, 旧缓存不得泄漏
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamIdB).snapshots.empty();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamIdB);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    CASE_EXPECT_TRUE(view.actions.empty());
    if (!view.snapshots.empty()) {
      const auto& snapshot = view.snapshots.front();
      CASE_EXPECT_EQ(kTeamIdB, snapshot.snapshot().team_key().team_id());
      CASE_EXPECT_EQ(1, snapshot.snapshot().member_size());
      CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kUserId));
      const auto* captain = team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId);
      CASE_EXPECT_TRUE(nullptr != captain);
      if (nullptr != captain) {
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, captain->role());
      }
      // 旧共享 key/旧 invitation/旧 join request 不泄漏, 仅保留新快照的 join request
      CASE_EXPECT_EQ(0, snapshot.shared_team_data_size());
      CASE_EXPECT_EQ(0, snapshot.unpacked_member_data_size());
      CASE_EXPECT_EQ(0, snapshot.snapshot().pending_invitation_size());
      CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
      if (1 == snapshot.snapshot().pending_join_request_size()) {
        const auto& join_request = snapshot.snapshot().pending_join_request(0);
        CASE_EXPECT_EQ(kRequesterB2, join_request.requester().user_id());
        CASE_EXPECT_TRUE(join_request.requester_private_channel().channel_id().empty());
        CASE_EXPECT_EQ(0, join_request.user_router_server_id());
      }
    }
  }

  // 已超过 wait_add_member_timeout: 快照回调完成下发后立即移除队伍
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamIdB));
  }));
  expect_team_absent_in_table(*user_inst, kTeamIdB);
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamIdB, kUserId) >= 1;
  }));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamIdB, kUserId)));
  CASE_EXPECT_EQ(1,
                 static_cast<int>(count_remove_requests_by_reason(ss_capture, kTeamIdB, kUserId,
                                                                  atfw::team::EN_TEAM_EXIT_REASON_EXPIRED)));
  {
    const auto* exit_req = find_last_remove_request(ss_capture, kTeamIdB, kUserId);
    CASE_EXPECT_TRUE(nullptr != exit_req);
    if (nullptr != exit_req) {
      expect_remove_request_payload(*exit_req, kTeamIdB, kUserId, atfw::team::EN_TEAM_EXIT_REASON_EXPIRED);
    }
  }

  // ---- team C 对照组: 新快照仍含 self 时保留队伍, 不误判超时 ----
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamIdC));
  atfw::team::DTeamStorage storage_c1 = make_rich_kick_storage(
      kTeamIdC, kUserId, kInviteeC, kRequesterB, time_guard.logical_now() + std::chrono::hours{1});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamIdC, storage_c1));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamIdC));
    return team && atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 对照快照(更高 custom_data_sequence)仍包含 self: 队伍必须保留且不被误判超时
  atfw::team::DTeamStorage storage_c2 = team_test::make_team_storage(kTeamIdC);
  team_test::add_storage_member(storage_c2, team_test::kCaptainUserId,
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(storage_c2, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamIdC), /*create_sequence=*/1,
                                           /*last_sequence=*/0, &storage_c2, /*custom_data_sequence=*/2)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamIdC).snapshots.empty();
  }));
  {
    auto team_c = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamIdC));
    CASE_EXPECT_TRUE(!!team_c);
    CASE_EXPECT_TRUE(
        !!user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
    CASE_EXPECT_EQ(1, static_cast<int>(count_running_teams(*user_inst)));
    if (team_c) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL, team_c->get_cached_permission_role());
    }
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamIdC);
    if (!view.snapshots.empty()) {
      CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(view.snapshots.back(), kUserId));
    }
    CASE_EXPECT_EQ(0, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamIdC, kUserId)));
  }

  // 未超过 wait_add_member_timeout 时排除 self 的快照: 保持对象直到 minute refresh, 但不得恢复 self
  atfw::team::DTeamStorage storage_c3 = team_test::make_team_storage(kTeamIdC);
  team_test::add_storage_member(storage_c3, team_test::kCaptainUserId,
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamIdC), /*create_sequence=*/1,
                                           /*last_sequence=*/0, &storage_c3, /*custom_data_sequence=*/3)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::collect_team_dirty(test, kSessionId, kTeamIdC).snapshots.size() >= 2;
  }));
  auto team_c = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamIdC));
  CASE_EXPECT_TRUE(!!team_c);
  if (!team_c) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST, team_c->get_cached_permission_role());
  {
    PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
    rpc::context ctx{rpc::context::create_without_task()};
    team_c->dump(ctx, snapshot);
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(snapshot, kUserId));
    CASE_EXPECT_TRUE(nullptr != team_test::find_snapshot_member(snapshot, team_test::kCaptainUserId));
    // 旧 invitation/共享数据不得泄漏
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(0, snapshot.shared_team_data_size());
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamIdC);
    CASE_EXPECT_TRUE(nullptr == team_test::find_snapshot_member(view.snapshots.back(), kUserId));
  }
  // 未超时: minute refresh 之前对象保留, 也不发出退出请求
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_EQ(0, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamIdC, kUserId)));

  // 推进超过 wait_add_member_timeout 后 minute refresh 收编 team C
  time_guard.advance(team_test::get_wait_add_member_timeout() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamIdC));
  }));
  expect_team_absent_in_table(*user_inst, kTeamIdC);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamIdC, kUserId) >= 1;
  }));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamIdC, kUserId)));
  CASE_EXPECT_EQ(1,
                 static_cast<int>(count_remove_requests_by_reason(ss_capture, kTeamIdC, kUserId,
                                                                  atfw::team::EN_TEAM_EXIT_REASON_EXPIRED)));

  // 迟到的个人通知: 幂等, 不重建队伍、不产生二次脏数据
  team_c = nullptr;
  test.cs().clear_history();
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamIdC,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamIdC)));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamIdC);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamIdC, kUserId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// KICK-03(§3.3 任务变体): 频道 remove action 和快照都丢失, 仅个人 remove_member 通知到达。
// - 个人通知兜底驱动退出收敛: 队伍转入 pending-to-exit, 重试请求的 team_key payload 完整校验;
// - WAL remove(频道 destroy 日志)后订阅自动关闭: 对象释放, 迟到的频道事件触发 outbound unsubscribe;
// - manager/索引/table 清理全部断言; 收编后停止重试。
CASE_TEST(lobbysvr_user_team, kick_personal_notify_fallback_until_wal_remove) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));
  atfw::testing::ss_rule_handle subscribe_rule;
  CASE_EXPECT_TRUE(setup_dtmq_proxy_node(test, subscribe_rule));

  constexpr uint64_t kUserId = 30038;
  constexpr uint64_t kSessionId = 0x10038;
  constexpr int64_t kTeamId = 564;

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

  team_test::now_offset_guard time_guard;
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(team_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));

  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 1. 仅个人 remove_member(KICKED) 到达: 转入 pending-to-exit, manager 立即把它移出 current/running 导出
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team->is_exiting(); }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, team->get_last_exit_reason());
  // 代码事实: 个人通知只置退出标记, 不直接发退出请求; 客户端在此阶段也不收频道脏数据
  CASE_EXPECT_EQ(0, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));
  team_test::pump_rounds(test, 3);
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }

  // 2. 退出重试定时任务驱动收敛: 边界前不重发, 越过 retry interval 后由 minute refresh 补发
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(0, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  time_guard.advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 1;
  }));
  // team_key payload 完整校验(顶层 team_key/sender 与内嵌 action 的 zone_id/user_id/reason)
  {
    const auto* retry_req = find_last_remove_request(ss_capture, kTeamId, kUserId);
    CASE_EXPECT_TRUE(nullptr != retry_req);
    if (nullptr != retry_req) {
      expect_remove_request_payload(*retry_req, kTeamId, kUserId,
                                    atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    }
  }
  // 频道 action/快照始终未到达: 重试不会把队伍收编(is_member_ 仍为 true), 队伍保持 pending-to-exit
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));

  // 3. WAL remove(频道 destroy 日志): 客户端收到合成 destroy 通知, manager/索引/table 清理
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
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
    // 频道 remove action 从未到达, 客户端不会收到 remove_member increase
    CASE_EXPECT_TRUE(team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember).empty());
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
  expect_team_absent_in_table(*user_inst, kTeamId);

  // 频道销毁时 room 已不存在, WAL destroy 不再补发 DESTROY_TEAM 退出请求(kDestroy 分支 send_exit=false);
  // 随后 set_destroyed -> on_destroyed 再次 remove_team 时索引已空, 幂等返回。
  // 因此收编后恰好只有一条退出请求: 分钟刷新的重试(REMOVE_MEMBER)。
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));
  CASE_EXPECT_EQ(1,
                 static_cast<int>(count_remove_requests_by_reason(ss_capture, kTeamId, kUserId,
                                                                  atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER)));
  CASE_EXPECT_EQ(0,
                 static_cast<int>(count_remove_requests_by_reason(ss_capture, kTeamId, kUserId,
                                                                  atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM)));
  {
    const auto* last_exit_req = find_last_remove_request(ss_capture, kTeamId, kUserId);
    CASE_EXPECT_TRUE(nullptr != last_exit_req);
    if (nullptr != last_exit_req) {
      expect_remove_request_payload(*last_exit_req, kTeamId, kUserId,
                                    atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    }
  }

  // 4. 订阅自动关闭: 释放本地引用后对象随之销毁并注销频道订阅; 迟到的频道事件触发 outbound unsubscribe
  team = nullptr;
  test.cs().clear_history();
  const size_t unsubscribe_before = test.ss().calls(rpc::dtmq::packer::get_full_name_of_unsubscribe());
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return test.ss().calls(rpc::dtmq::packer::get_full_name_of_unsubscribe()) > unsubscribe_before;
  }));
  // 迟到事件不重建队伍、不产生二次脏数据、不再发退出请求
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));
  // 收编后停止重试
  time_guard.advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(1, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// KICK-04(§5.1): 只有个人 remove 到达、队伍快照仍暂时含 self。
// - 个人 remove 后 manager 立即把队伍移出 current/running 导出(转入 pending-to-exit);
// - 退出重试定时任务继续驱动 room 收敛(代码事实: retry interval(10s) < exit timeout(35s), 每次 minute
//   refresh 越过 retry 边界会先补发并重置退出时间点, 因此 exit timeout 分支在持续 refresh 下不可达,
//   收编由迟到的频道 remove action(!is_member_) 完成; 这里以重试 SS 调用及其 payload 为 oracle);
// - 迟到的旧快照仍含 self 时不恢复为 current/running, 也不中断重试;
// - 频道 remove action 迟到到达后 minute refresh 收编, 收编后停止重试。
CASE_TEST(lobbysvr_user_team, kick_exit_retry_converges_while_snapshot_still_has_self) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30039;
  constexpr uint64_t kSessionId = 0x10039;
  constexpr int64_t kTeamId = 565;

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

  team_test::now_offset_guard time_guard;
  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;
  CASE_EXPECT_TRUE(team_test::join_team_via_notification(test, user_inst, private_chain, kTeamId));

  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(team_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::apply_team_snapshot(test, kTeamId, team_storage));

  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!team);
  if (!team) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
  }));
  test.cs().clear_history();

  // 1. 个人 remove 到达, 频道 remove action/snapshot 迟到: manager 立即移出 current/running 导出
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] { return team->is_exiting(); }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
  CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, team->get_last_exit_reason());
  // 期间客户端不会收到该队伍的 remove increase(频道事件尚未到达)
  team_test::pump_rounds(test, 3);
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRemoveMember).empty());
  }

  // 2. 退出重试定时任务驱动收敛: 边界前不重发, 越过 retry interval 后补发且 reason 保持个人通知值
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(0, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  time_guard.advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 1;
  }));
  {
    const auto* retry_req = find_last_remove_request(ss_capture, kTeamId, kUserId);
    CASE_EXPECT_TRUE(nullptr != retry_req);
    if (nullptr != retry_req) {
      expect_remove_request_payload(*retry_req, kTeamId, kUserId,
                                    atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    }
  }

  time_guard.advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 2;
  }));

  // 3. 迟到的旧快照仍含 self: 不恢复为 current/running 导出, 也不中断退出重试
  atfw::team::DTeamStorage stale_storage = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(stale_storage, team_test::kCaptainUserId,
                                {.role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER});
  team_test::add_storage_member(stale_storage, kUserId, {.role = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL});
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), /*create_sequence=*/1,
                                           /*last_sequence=*/0, &stale_storage, /*custom_data_sequence=*/2)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  CASE_EXPECT_TRUE(team->is_exiting());
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));

  time_guard.advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId) >= 3;
  }));
  {
    const auto* retry_req = find_last_remove_request(ss_capture, kTeamId, kUserId);
    CASE_EXPECT_TRUE(nullptr != retry_req);
    if (nullptr != retry_req) {
      expect_remove_request_payload(*retry_req, kTeamId, kUserId,
                                    atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);
    }
  }

  // 4. 迟到的频道 remove action 到达: 客户端 increase 完成, minute refresh 收编队伍
  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
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
      CASE_EXPECT_EQ(team_test::kZoneId, remove_data.team_key().zone_id());
      CASE_EXPECT_EQ(kTeamId, remove_data.team_key().team_id());
      CASE_EXPECT_EQ(team_test::kZoneId, remove_data.user_key().zone_id());
      CASE_EXPECT_EQ(kUserId, remove_data.user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER, remove_data.remove_member_reason());
    }
  }
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId));
  }));
  CASE_EXPECT_TRUE(
      !user_inst->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL));
  CASE_EXPECT_EQ(0, static_cast<int>(count_running_teams(*user_inst)));
  expect_team_absent_in_table(*user_inst, kTeamId);

  // 5. 收编后停止重试; 迟到的重复个人通知/队伍事件幂等
  team = nullptr;
  time_guard.advance(team_test::get_exit_retry_interval() + std::chrono::seconds{1});
  CASE_EXPECT_TRUE(run_minute_refresh(test, user_inst));
  team_test::pump_rounds(test, 2);
  CASE_EXPECT_EQ(3, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  test.cs().clear_history();
  CASE_EXPECT_TRUE(inject_personal_remove_event(test, private_chain, kUserId, kTeamId,
                                                atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  CASE_EXPECT_TRUE(inject_channel_remove_event(test, team_chain, kTeamId, kUserId,
                                               atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_TRUE(!user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(kTeamId)));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_TRUE(view.actions.empty());
    CASE_EXPECT_TRUE(view.snapshots.empty());
  }
  CASE_EXPECT_EQ(3, static_cast<int>(team_test::count_remove_member_requests(ss_capture, kTeamId, kUserId)));

  CASE_EXPECT_EQ(0, test.stop());
}

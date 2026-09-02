// Copyright 2026 atframework
//
// Offline regression tests for lobbysvr user_team_manager self pending invitation/join-request cache
// (plan: src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md §5.3).
//
// Covered here:
// - ADM-TEAM-01..07 (§5.2): team-level pending invitation/join-request cache — snapshot load filters
//   expired/invalid entries and keeps full fields in expiry order, snapshot eviction/overwrite gated by the
//   custom_data_sequence watermark, add upsert (in-place overwrite vs reorder), member-joined ignore,
//   approve/reject removal from list+index with idempotent duplicates, add_member clearing both pendings of
//   the joined user, and the expiry boundary with staged prefix cleanup that never fabricates room cancel
//   events;
// - ADM-SELF-01/02/03/05/08/10 (§5.3): self pending invitation/join-request cache — full §2.2 payloads of
//   invited / apply_join_request (send_join_request success alone must not pre-insert the cache), same-key
//   overwrite and reorder, reject removal with idempotent duplicates, staged second-refresh expiry cleanup
//   plus the expired-record reapply pre-check through the real CS entry, and the private-channel sequence
//   watermark dedup with dirty landing;
// - ADM-SELF-04: invalid/expired invitation records must be rejected at insert (team_id validation gap);
// - ADM-SELF-06: approve/reject invitation uplink payload, success cleanup, ordinary failure retention and
//   room-not-found cleanup of the local pending record;
// - ADM-SELF-07: registering a team (joined_team / create / login restore) must clear the same-team pending
//   invitation and join request.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.team.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <string>
#include <vector>

#include "lobbysvr_test_user_team_common.h"  // NOLINT: build/include_subdir

// 生成的 CS dispatcher 注册入口(test runtime 每次启动后需显式注册, 否则 RPC 分发返回 EN_SYS_INIT)
#include "app/handle_cs_rpc_lobbysvrclientservice.atfw.gen.h"

namespace {

// 注入一条本人的 invited 个人通知(room 已在通知中重填 PUBLIC admission 数据, 这里只保留最小字段集)
bool inject_invited_event(atfw::testing::runtime& test, team_test::channel_event_chain& private_chain,
                          uint64_t user_id, int64_t team_id, std::chrono::system_clock::time_point expired_timepoint) {
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
bool inject_apply_join_request_event(atfw::testing::runtime& test, team_test::channel_event_chain& private_chain,
                                     uint64_t user_id, int64_t team_id,
                                     std::chrono::system_clock::time_point expired_timepoint) {
  atfw::team::DTeamMemberAction action;
  auto* join_request = action.mutable_apply_join_request();
  protobuf_copy_message(*join_request->mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*join_request->mutable_requester(), team_test::make_user_key(user_id));
  *join_request->mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  return team_test::inject_event_message(test, private_chain, action);
}

// 在真实 runtime task 内调用 manager 的 approve/reject invitation 协程并返回其业务结果码
template <class TFn>
int32_t run_manager_admission_call(atfw::testing::runtime& test, const char* name, TFn fn) {
  int32_t ret = 0;
  bool ran = team_test::run_sync_task(test, name, [&fn, &ret](rpc::context& ctx) -> rpc::result_code_type {
    ret = RPC_AWAIT_CODE_RESULT(fn(ctx));
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_TRUE(ran);
  return ret;
}

}  // namespace

// ADM-SELF-04: 无效 team_id / 已过期 / expiry==now 的邀请都不得进入自己的 pending 缓存;
// join-request 路径已有同样校验, 这里锁定两条路径行为一致。
CASE_TEST(lobbysvr_user_team, self_pending_admission_insert_validation) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  // user id 不得与其他用例文件重复: 进程级 dtmq subscriber/WAL 状态按 channel_id 共享,
  // 同 id 的前序用例会让本用例从 sequence 1 重放的个人频道事件被静默去重(原 30021~30023 与 cache 文件撞车)
  constexpr uint64_t kUserId = 30012;
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

  // 用逻辑时钟构造过期时间: 共享的订阅定时器 wheel 不会回退, 组内前序用例可能已推进全局 offset(§6.9)
  team_test::now_offset_guard time_guard;
  const auto now = team_test::now_offset_guard::logical_now();
  const auto valid_expiry = now + std::chrono::seconds(300);

  // 无效 team_id(0): 不得插入(邀请路径当前缺失该校验)
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, 0, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(0)));

  // join-request 路径对无效 team_id 已有校验, 锁定一致行为
  CASE_EXPECT_TRUE(inject_apply_join_request_event(test, private_chain, kUserId, 0, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(0)));

  // 已过期与 expiry==now: 均视为过期, 不得插入
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, 401, now - std::chrono::seconds(1)));
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, 402, now));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(401)));
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(402)));

  // 对照: 合法邀请正常插入且字段完整
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, 403, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(403));
  }));
  auto pending = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(403));
  CASE_EXPECT_TRUE(!!pending);
  if (pending) {
    CASE_EXPECT_EQ(403, pending->team_key().team_id());
    CASE_EXPECT_EQ(kUserId, pending->invitee().user_id());
    CASE_EXPECT_EQ(team_test::kCaptainUserId, pending->inviter().user_id());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-06: approve/reject invitation 的上行 payload 完整; 成功删除本地 pending; 普通业务失败保留以便重试;
// room 记录不存在(直接 INVITATION_NOT_FOUND 或 CHANNEL_NOT_FOUND 映射)时本地 pending 确定失效, 必须删除。
CASE_TEST(lobbysvr_user_team, approve_reject_invitation_result_contract) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30013;
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
  // 过期时间基于逻辑时钟(§6.9: 全局 offset 可能被组内前序用例推进)
  team_test::now_offset_guard time_guard;
  const auto valid_expiry = team_test::now_offset_guard::logical_now() + std::chrono::seconds(300);

  // 1. 成功: 上行 payload 完整, 本地 pending 删除
  constexpr int64_t kTeamSuccess = 411;
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamSuccess, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamSuccess));
  }));
  {
    int32_t ret = run_manager_admission_call(test, "team.approve_invitation.success",
                                             [&](rpc::context& ctx) -> rpc::result_code_type {
                                               auto pending = user_inst->get_user_team_manager()
                                                                  .get_pending_invitation(
                                                                      team_test::make_team_key(kTeamSuccess));
                                               RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                                                   user_inst->get_user_team_manager().approve_invitation(ctx,
                                                                                                         pending)));
                                             });
    CASE_EXPECT_EQ(0, ret);
  }
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.approve_invitation_reqs.size()));
  if (!ss_capture.approve_invitation_reqs.empty()) {
    const auto& req = ss_capture.approve_invitation_reqs.front();
    CASE_EXPECT_EQ(kTeamSuccess, req.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, req.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, req.sender_user_key().user_id());
    CASE_EXPECT_EQ(kUserId, req.invitee().user_id());
    CASE_EXPECT_EQ(user_inst->get_client_info().client_version(), req.client_version());
    CASE_EXPECT_TRUE(0 != req.user_router_server_id());
    CASE_EXPECT_TRUE(req.shared_member_data_size() > 0);
  }
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamSuccess)));

  // 2. room 记录不存在(INVITATION_NOT_FOUND): 本地 pending 确定失效, 必须删除
  constexpr int64_t kTeamNotFound = 412;
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamNotFound, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamNotFound));
  }));
  ss_capture.approve_invitation_responder =
      [](const atfw::team::SSTeamRoomApproveInvitationReq&, atfw::team::SSTeamRoomApproveInvitationRsp&) {
        return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND;
      };
  {
    int32_t ret = run_manager_admission_call(test, "team.approve_invitation.not_found",
                                             [&](rpc::context& ctx) -> rpc::result_code_type {
                                               auto pending = user_inst->get_user_team_manager()
                                                                  .get_pending_invitation(
                                                                      team_test::make_team_key(kTeamNotFound));
                                               RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                                                   user_inst->get_user_team_manager().approve_invitation(ctx,
                                                                                                         pending)));
                                             });
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND, ret);
  }
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamNotFound)));
  ss_capture.approve_invitation_responder = nullptr;

  // 3. 频道不存在(reject 遇 CHANNEL_NOT_FOUND): 映射为 INVITATION_NOT_FOUND 且删除本地 pending
  constexpr int64_t kTeamChannelGone = 413;
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamChannelGone, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamChannelGone));
  }));
  ss_capture.reject_invitation_responder =
      [](const atfw::team::SSTeamRoomRejectInvitationReq&, atfw::team::SSTeamRoomRejectInvitationRsp&) {
        return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND;
      };
  {
    int32_t ret = run_manager_admission_call(test, "team.reject_invitation.channel_gone",
                                             [&](rpc::context& ctx) -> rpc::result_code_type {
                                               auto pending = user_inst->get_user_team_manager()
                                                                  .get_pending_invitation(
                                                                      team_test::make_team_key(kTeamChannelGone));
                                               RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                                                   user_inst->get_user_team_manager().reject_invitation(ctx,
                                                                                                        pending)));
                                             });
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND, ret);
  }
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.reject_invitation_reqs.size()));
  if (!ss_capture.reject_invitation_reqs.empty()) {
    const auto& req = ss_capture.reject_invitation_reqs.front();
    CASE_EXPECT_EQ(kTeamChannelGone, req.team_key().team_id());
    CASE_EXPECT_EQ(kUserId, req.sender_user_key().user_id());
    CASE_EXPECT_EQ(kUserId, req.invitee().user_id());
  }
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamChannelGone)));
  ss_capture.reject_invitation_responder = nullptr;

  // 4. 普通业务失败: 错误码透传, 本地 pending 保留以便重试
  constexpr int64_t kTeamBusy = 414;
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamBusy, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamBusy));
  }));
  ss_capture.approve_invitation_responder =
      [](const atfw::team::SSTeamRoomApproveInvitationReq&, atfw::team::SSTeamRoomApproveInvitationRsp&) {
        return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
      };
  {
    int32_t ret = run_manager_admission_call(test, "team.approve_invitation.busy",
                                             [&](rpc::context& ctx) -> rpc::result_code_type {
                                               auto pending = user_inst->get_user_team_manager()
                                                                  .get_pending_invitation(
                                                                      team_test::make_team_key(kTeamBusy));
                                               RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                                                   user_inst->get_user_team_manager().approve_invitation(ctx,
                                                                                                         pending)));
                                             });
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, ret);
  }
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamBusy)));

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-07: joined_team/create/登录恢复注册 team 时, 必须删除同 team_key 的自己的 invitation 和 join request;
// 其他队伍的 pending 不受影响。
CASE_TEST(lobbysvr_user_team, join_team_clears_own_pending_admissions) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));

  constexpr uint64_t kUserId = 30014;
  constexpr int64_t kJoinedTeamId = 421;
  constexpr int64_t kOtherTeamId = 422;

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
  // 过期时间基于逻辑时钟(§6.9: 全局 offset 可能被组内前序用例推进)
  team_test::now_offset_guard time_guard;
  const auto valid_expiry = team_test::now_offset_guard::logical_now() + std::chrono::seconds(300);

  // 非 approve 路径留下的陈旧 pending: 同一队伍既有邀请(可能他人重复邀请)又有加入请求
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kJoinedTeamId, valid_expiry));
  CASE_EXPECT_TRUE(inject_apply_join_request_event(test, private_chain, kUserId, kJoinedTeamId, valid_expiry));
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kOtherTeamId, valid_expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto& mgr = user_inst->get_user_team_manager();
    return !!mgr.get_pending_invitation(team_test::make_team_key(kJoinedTeamId)) &&
           !!mgr.get_pending_join_request(team_test::make_team_key(kJoinedTeamId)) &&
           !!mgr.get_pending_invitation(team_test::make_team_key(kOtherTeamId));
  }));

  // 通过真实 joined_team 通知注册队伍
  CASE_EXPECT_TRUE(
      team_test::join_team_via_notification(test, user_inst, private_chain, kJoinedTeamId));

  // 同 team_key 的两类 pending 都必须清理
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kJoinedTeamId)));
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kJoinedTeamId)));
  // 其他队伍的 pending 不受影响
  CASE_EXPECT_TRUE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kOtherTeamId)));

  CASE_EXPECT_EQ(0, test.stop());
}

namespace {

// ---- ADM-TEAM/ADM-SELF 矩阵共享的本地构建器(文件内静态, 不改动公共夹具) ----

// 打包一个 PUBLIC 权限的队伍/成员 admission 条目(room 在通知与日志中填充的可见准入数据)
atfw::team::DTeamAnyDataWithKey pack_public_team_module(const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& module) {
  atfw::team::DTeamAnyDataWithKey entry;
  entry.set_key(user_team_algorithm::make_team_shared_data_key(module));
  entry.mutable_value()->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC);
  CASE_EXPECT_TRUE(entry.mutable_value()->mutable_data()->PackFrom(module));
  return entry;
}

atfw::team::DTeamAnyDataWithKey pack_public_member_module(
    const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& module) {
  atfw::team::DTeamAnyDataWithKey entry;
  entry.set_key(user_team_algorithm::make_team_member_shared_data_key(module));
  entry.mutable_value()->set_permission(atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC);
  CASE_EXPECT_TRUE(entry.mutable_value()->mutable_data()->PackFrom(module));
  return entry;
}

// 组装一条字段完整、内部通知频道被污染的队伍级邀请记录(快照/日志中的权威形态)
atfw::team::DTeamInvitation make_full_invitation(int64_t team_id, uint64_t inviter_id, uint64_t invitee_id,
                                                 std::chrono::system_clock::time_point start_timepoint,
                                                 std::chrono::system_clock::time_point expired_timepoint,
                                                 atfw::team::EnTeamSourceType source_type) {
  atfw::team::DTeamInvitation invitation;
  protobuf_copy_message(*invitation.mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*invitation.mutable_inviter(), team_test::make_user_key(inviter_id));
  protobuf_copy_message(*invitation.mutable_invitee(), team_test::make_user_key(invitee_id));
  invitation.mutable_invitee_private_channel()->set_channel_id("invitee-private-channel-" +
                                                               std::to_string(invitee_id));
  invitation.set_team_source_type(source_type);
  CASE_EXPECT_TRUE(
      invitation.mutable_team_source_data()->PackFrom(team_test::make_team_matching_module(false)));
  *invitation.mutable_start_timepoint() = protobuf_from_system_clock(start_timepoint);
  *invitation.mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  *invitation.add_team_admission_data() =
      pack_public_team_module(team_test::make_team_matching_module(false));
  auto* member_admission = invitation.add_member_admission_data();
  protobuf_copy_message(*member_admission->mutable_user_key(), team_test::make_user_key(inviter_id));
  *member_admission->add_member_admission_data() =
      pack_public_member_module(team_test::make_member_ready_module(true));
  return invitation;
}

// 组装一条字段完整、内部通知频道/路由被污染的队伍级加入请求记录
atfw::team::DTeamJoinRequest make_full_join_request(int64_t team_id, uint64_t requester_id,
                                                    std::chrono::system_clock::time_point expired_timepoint,
                                                    atfw::team::EnTeamSourceType source_type,
                                                    const std::string& client_version, uint64_t router_id) {
  atfw::team::DTeamJoinRequest join_request;
  protobuf_copy_message(*join_request.mutable_team_key(), team_test::make_team_key(team_id));
  protobuf_copy_message(*join_request.mutable_requester(), team_test::make_user_key(requester_id));
  join_request.mutable_requester_private_channel()->set_channel_id("requester-private-channel-" +
                                                                   std::to_string(requester_id));
  join_request.set_team_source_type(source_type);
  CASE_EXPECT_TRUE(
      join_request.mutable_team_source_data()->PackFrom(team_test::make_team_matching_module(false)));
  join_request.set_client_version(client_version);
  join_request.set_user_router_server_id(router_id);
  *join_request.mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  *join_request.add_member_admission_data() =
      pack_public_member_module(team_test::make_member_ready_module(true));
  return join_request;
}

// 队伍频道 action 注入(包装一层, 让 case 直接表达业务动作)
bool inject_team_invitation_action(atfw::testing::runtime& test, team_test::channel_event_chain& team_chain,
                                   atfw::team::DTeamAction::ActionCase action_case,
                                   const atfw::team::DTeamInvitation& invitation) {
  atfw::team::DTeamAction action;
  if (atfw::team::DTeamAction::kAddInvitation == action_case) {
    protobuf_copy_message(*action.mutable_add_invitation(), invitation);
  } else if (atfw::team::DTeamAction::kApproveInvitation == action_case) {
    protobuf_copy_message(*action.mutable_approve_invitation(), invitation);
  } else {
    protobuf_copy_message(*action.mutable_reject_invitation(), invitation);
  }
  return team_test::inject_event_message(test, team_chain, action);
}

bool inject_team_join_request_action(atfw::testing::runtime& test, team_test::channel_event_chain& team_chain,
                                     atfw::team::DTeamAction::ActionCase action_case,
                                     const atfw::team::DTeamJoinRequest& join_request) {
  atfw::team::DTeamAction action;
  if (atfw::team::DTeamAction::kAddJoinRequest == action_case) {
    protobuf_copy_message(*action.mutable_add_join_request(), join_request);
  } else if (atfw::team::DTeamAction::kApproveJoinRequest == action_case) {
    protobuf_copy_message(*action.mutable_approve_join_request(), join_request);
  } else {
    protobuf_copy_message(*action.mutable_reject_join_request(), join_request);
  }
  return team_test::inject_event_message(test, team_chain, action);
}

// 队伍级 pending 的可观察出口: user_team::dump 导出的快照(按过期时间升序, 剥内部路由字段)
PROJECT_NAMESPACE_ID::DUserTeamSnapshot dump_team_snapshot(const user_team::ptr_t& team) {
  PROJECT_NAMESPACE_ID::DUserTeamSnapshot snapshot;
  rpc::context ctx{rpc::context::create_without_task()};
  team->dump(ctx, snapshot);
  return snapshot;
}

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

// 在真实 runtime task 内驱动 second refresh(个人 pending 清理 + 队伍级 admission 清理 + 心跳)
bool run_second_refresh(atfw::testing::runtime& test, const user::ptr_t& user_ptr) {
  return team_test::run_sync_task(test, "team.refresh_second", [&user_ptr](rpc::context& ctx) -> rpc::result_code_type {
    user_ptr->get_user_team_manager().refresh_feature_limit_second(ctx);
    RPC_RETURN_CODE(0);
  });
}

// 统计某个 session 已收到的指定 RPC 的下游响应数(用于等待"本次 post 之后"的新响应, 避免匹配到同一 case
// 内早先同 RPC 的陈旧响应)
size_t count_admission_cs_responses(atfw::testing::runtime& test, uint64_t session_id,
                                    gsl::string_view rpc_full_name) {
  size_t ret = 0;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const auto* record = test.cs().call_at(i);
    if (nullptr == record || atfw::testing::cs_downstream_record::op_type::post != record->op ||
        record->session_id != session_id) {
      continue;
    }
    atframework::CSMsg cs_msg;
    if (!cs_msg.ParseFromString(record->message.body().post().content())) {
      continue;
    }
    if (cs_msg.head().has_rpc_response() && rpc_full_name == cs_msg.head().rpc_response().rpc_name()) {
      ++ret;
    }
  }
  return ret;
}

// 经真实 dispatcher post 一个 CS 请求, 并等到本次 post 之后产生的完整下游响应
template <class TRequest>
bool post_admission_cs_request(atfw::testing::runtime& test, const atfw::testing::mock_client& client,
                               gsl::string_view rpc_full_name, const TRequest& req_body,
                               atframework::CSMsg& out_rsp_msg) {
  const size_t responses_before = count_admission_cs_responses(test, client.session_id(), rpc_full_name);
  auto packed = team_test::pack_cs_request(rpc_full_name, req_body);
  if (0 != client.post(packed)) {
    CASE_MSG_INFO() << "post failed\n";
    return false;
  }
  if (!team_test::pump_until(test, [&] {
        return count_admission_cs_responses(test, client.session_id(), rpc_full_name) > responses_before;
      })) {
    CASE_MSG_INFO() << "no response for " << rpc_full_name.data() << '\n';
    return false;
  }
  return nullptr != team_test::find_downstream_response(test, client.session_id(), rpc_full_name, out_rsp_msg);
}

// 断言快照导出的邀请保留全部业务字段、剥掉内部通知频道
void expect_snapshot_invitation_full(const atfw::team::DTeamInvitation& invitation, int64_t team_id,
                                     uint64_t inviter_id, uint64_t invitee_id,
                                     std::chrono::system_clock::time_point start_timepoint,
                                     std::chrono::system_clock::time_point expired_timepoint,
                                     atfw::team::EnTeamSourceType source_type) {
  CASE_EXPECT_EQ(team_test::kZoneId, invitation.team_key().zone_id());
  CASE_EXPECT_EQ(team_id, invitation.team_key().team_id());
  CASE_EXPECT_EQ(inviter_id, invitation.inviter().user_id());
  CASE_EXPECT_EQ(team_test::kZoneId, invitation.inviter().zone_id());
  CASE_EXPECT_EQ(invitee_id, invitation.invitee().user_id());
  CASE_EXPECT_EQ(team_test::kZoneId, invitation.invitee().zone_id());
  // 内部通知频道不下发
  CASE_EXPECT_TRUE(invitation.invitee_private_channel().channel_id().empty());
  CASE_EXPECT_EQ(source_type, invitation.team_source_type());
  CASE_EXPECT_TRUE(invitation.has_team_source_data());
  CASE_EXPECT_EQ(start_timepoint, protobuf_to_system_clock(invitation.start_timepoint()));
  CASE_EXPECT_EQ(expired_timepoint, protobuf_to_system_clock(invitation.expired_timepoint()));
  CASE_EXPECT_EQ(1, invitation.team_admission_data_size());
  CASE_EXPECT_EQ(1, invitation.member_admission_data_size());
  if (invitation.member_admission_data_size() > 0) {
    CASE_EXPECT_EQ(inviter_id, invitation.member_admission_data(0).user_key().user_id());
    CASE_EXPECT_EQ(1, invitation.member_admission_data(0).member_admission_data_size());
  }
}

// 断言快照导出的加入请求保留全部业务字段、剥掉内部通知频道与路由
void expect_snapshot_join_request_full(const atfw::team::DTeamJoinRequest& join_request, int64_t team_id,
                                       uint64_t requester_id,
                                       std::chrono::system_clock::time_point expired_timepoint,
                                       atfw::team::EnTeamSourceType source_type,
                                       const std::string& client_version) {
  CASE_EXPECT_EQ(team_test::kZoneId, join_request.team_key().zone_id());
  CASE_EXPECT_EQ(team_id, join_request.team_key().team_id());
  CASE_EXPECT_EQ(requester_id, join_request.requester().user_id());
  CASE_EXPECT_EQ(team_test::kZoneId, join_request.requester().zone_id());
  // 内部通知频道与路由不下发
  CASE_EXPECT_TRUE(join_request.requester_private_channel().channel_id().empty());
  CASE_EXPECT_EQ(0, join_request.user_router_server_id());
  CASE_EXPECT_EQ(source_type, join_request.team_source_type());
  CASE_EXPECT_TRUE(join_request.has_team_source_data());
  CASE_EXPECT_EQ(client_version, join_request.client_version());
  CASE_EXPECT_EQ(expired_timepoint, protobuf_to_system_clock(join_request.expired_timepoint()));
  CASE_EXPECT_EQ(1, join_request.member_admission_data_size());
}

// 组建一支带 self 成员的队伍并使其频道快照就绪, 返回当前队伍对象
user_team::ptr_t setup_running_team(atfw::testing::runtime& test, const user::ptr_t& user_inst, int64_t team_id) {
  if (!team_test::restore_team_from_table(test, user_inst, team_id)) {
    return nullptr;
  }
  atfw::team::DTeamStorage team_storage = team_test::make_team_storage(team_id);
  team_test::add_storage_member(team_storage, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(team_storage, user_inst->get_user_id(),
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  if (!team_test::apply_team_snapshot(test, team_id, team_storage)) {
    return nullptr;
  }
  auto team = user_inst->get_user_team_manager().get_team_by_team_key(team_test::make_team_key(team_id));
  if (!team) {
    return nullptr;
  }
  if (!team_test::pump_until(test, [&] {
        return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL == team->get_cached_permission_role();
      })) {
    return nullptr;
  }
  return team;
}

}  // namespace

// ADM-TEAM-01: 快照加载多条有效/已过期/无效 key 的邀请与加入请求:
// - 只保留有效项, 完整字段不丢, dump 按过期时间升序导出并剥掉内部路由字段;
// - 更高 custom_data_sequence 的新快照权威重建: 已删除的条目被淘汰、同 key 条目内容被覆盖;
// - custom_data_sequence 低于已应用值的过期快照不得覆盖当前缓存。
CASE_TEST(lobbysvr_user_team, team_admission_snapshot_load_filters_and_orders) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30101;
  constexpr uint64_t kSessionId = 0x10101;
  constexpr int64_t kTeamId = 601;
  constexpr uint64_t kInviteeLong = 93101;
  constexpr uint64_t kInviteeShort = 93102;
  constexpr uint64_t kInviteeExpired = 93103;
  constexpr uint64_t kRequesterLong = 93111;
  constexpr uint64_t kRequesterShort = 93112;
  constexpr uint64_t kRequesterExpired = 93113;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto current = setup_running_team(test, user_inst, kTeamId);
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  // 初始快照: 有效(长/短)/已过期/无效 key 的邀请与加入请求混合
  atfw::team::DTeamStorage storage_v1 = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(storage_v1, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(storage_v1, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  {
    auto invitation_long = make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeLong,
                                                base - std::chrono::seconds(30), base + std::chrono::seconds(600),
                                                atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    protobuf_copy_message(*storage_v1.add_pending_invitation(), invitation_long);

    atfw::team::DTeamInvitation invitation_short;
    protobuf_copy_message(*invitation_short.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*invitation_short.mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invitation_short.mutable_invitee(), team_test::make_user_key(kInviteeShort));
    *invitation_short.mutable_start_timepoint() = protobuf_from_system_clock(base);
    *invitation_short.mutable_expired_timepoint() = protobuf_from_system_clock(base + std::chrono::seconds(120));
    protobuf_copy_message(*storage_v1.add_pending_invitation(), invitation_short);

    // 已过期: 加载时即被过滤
    protobuf_copy_message(*storage_v1.add_pending_invitation(),
                          make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeExpired,
                                               base - std::chrono::seconds(300), base - std::chrono::seconds(60),
                                               atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND));
    // 无效 key(无 invitee): 加载时即被过滤
    auto* invalid = storage_v1.add_pending_invitation();
    protobuf_copy_message(*invalid->mutable_team_key(), storage_v1.team_key());
    *invalid->mutable_expired_timepoint() = protobuf_from_system_clock(base + std::chrono::seconds(300));
  }
  {
    protobuf_copy_message(*storage_v1.add_pending_join_request(),
                          make_full_join_request(kTeamId, kRequesterLong, base + std::chrono::seconds(300),
                                                 atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-client-1.0", 0x7788));

    atfw::team::DTeamJoinRequest join_request_short;
    protobuf_copy_message(*join_request_short.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*join_request_short.mutable_requester(), team_test::make_user_key(kRequesterShort));
    *join_request_short.mutable_expired_timepoint() =
        protobuf_from_system_clock(base + std::chrono::seconds(90));
    protobuf_copy_message(*storage_v1.add_pending_join_request(), join_request_short);

    protobuf_copy_message(*storage_v1.add_pending_join_request(),
                          make_full_join_request(kTeamId, kRequesterExpired, base - std::chrono::seconds(30),
                                                 atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-client-old", 0x7799));
    auto* invalid = storage_v1.add_pending_join_request();
    protobuf_copy_message(*invalid->mutable_team_key(), storage_v1.team_key());
    *invalid->mutable_expired_timepoint() = protobuf_from_system_clock(base + std::chrono::seconds(300));
  }

  // setup_running_team 的初始快照已占用 custom_data_sequence=1, 携带 admission 的快照必须使用更大的序号
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 0, &storage_v1,
                                           /*custom_data_sequence=*/2)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 2 == dump_team_snapshot(current).snapshot().pending_invitation_size() &&
           2 == dump_team_snapshot(current).snapshot().pending_join_request_size();
  }));

  // 只保留有效项, 按过期时间升序导出, 完整字段不丢
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_invitation_size());
    if (snapshot.snapshot().pending_invitation_size() >= 2) {
      CASE_EXPECT_EQ(kInviteeShort, snapshot.snapshot().pending_invitation(0).invitee().user_id());
      CASE_EXPECT_EQ(kInviteeLong, snapshot.snapshot().pending_invitation(1).invitee().user_id());
      expect_snapshot_invitation_full(snapshot.snapshot().pending_invitation(1), kTeamId, team_test::kCaptainUserId,
                                      kInviteeLong, base - std::chrono::seconds(30),
                                      base + std::chrono::seconds(600), atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    }
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_join_request_size());
    if (snapshot.snapshot().pending_join_request_size() >= 2) {
      CASE_EXPECT_EQ(kRequesterShort, snapshot.snapshot().pending_join_request(0).requester().user_id());
      CASE_EXPECT_EQ(kRequesterLong, snapshot.snapshot().pending_join_request(1).requester().user_id());
      expect_snapshot_join_request_full(snapshot.snapshot().pending_join_request(1), kTeamId, kRequesterLong,
                                        base + std::chrono::seconds(300), atfw::team::EN_TEAM_SOURCE_TYPE_MATCH,
                                        "jr-client-1.0");
    }
  }
  // 客户端 snapshot 推送携带同样的 pending 内容
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    CASE_EXPECT_EQ(1, static_cast<int>(view.snapshots.size()));
    if (!view.snapshots.empty()) {
      CASE_EXPECT_EQ(2, view.snapshots[0].snapshot().pending_invitation_size());
      CASE_EXPECT_EQ(2, view.snapshots[0].snapshot().pending_join_request_size());
    }
  }

  // 更高 custom_data_sequence 的新快照: 短有效期条目被淘汰, 长有效期条目内容被覆盖
  atfw::team::DTeamStorage storage_v2 = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(storage_v2, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(storage_v2, kUserId, team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  protobuf_copy_message(*storage_v2.add_pending_invitation(),
                        make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeLong,
                                             base - std::chrono::seconds(30), base + std::chrono::seconds(600),
                                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH));
  protobuf_copy_message(*storage_v2.add_pending_join_request(),
                        make_full_join_request(kTeamId, kRequesterLong, base + std::chrono::seconds(300),
                                               atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, "jr-client-2.0", 0x7788));
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 0, &storage_v2,
                                           /*custom_data_sequence=*/3)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    return 1 == snapshot.snapshot().pending_invitation_size() &&
           atfw::team::EN_TEAM_SOURCE_TYPE_MATCH == snapshot.snapshot().pending_invitation(0).team_source_type();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    // 被淘汰的短有效期条目不再出现
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kInviteeShort));
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, kRequesterShort));
    const auto* invitation = find_pending_invitation(snapshot, kInviteeLong);
    CASE_EXPECT_TRUE(nullptr != invitation);
    if (nullptr != invitation) {
      expect_snapshot_invitation_full(*invitation, kTeamId, team_test::kCaptainUserId, kInviteeLong,
                                      base - std::chrono::seconds(30), base + std::chrono::seconds(600),
                                      atfw::team::EN_TEAM_SOURCE_TYPE_MATCH);
    }
    const auto* join_request = find_pending_join_request(snapshot, kRequesterLong);
    CASE_EXPECT_TRUE(nullptr != join_request);
    if (nullptr != join_request) {
      expect_snapshot_join_request_full(*join_request, kTeamId, kRequesterLong, base + std::chrono::seconds(300),
                                        atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, "jr-client-2.0");
    }
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.size() >= 2;
  }));
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    if (view.snapshots.size() >= 2) {
      CASE_EXPECT_EQ(1, view.snapshots[1].snapshot().pending_invitation_size());
      CASE_EXPECT_EQ(1, view.snapshots[1].snapshot().pending_join_request_size());
    }
  }

  // 序号低于已应用值(custom_data_sequence=2 < 当前 3)的过期快照: 不得覆盖当前缓存
  atfw::team::DTeamStorage storage_stale = team_test::make_team_storage(kTeamId);
  team_test::add_storage_member(storage_stale, team_test::kCaptainUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER));
  team_test::add_storage_member(storage_stale, kUserId,
                                team_test::role_options(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  protobuf_copy_message(*storage_stale.add_pending_invitation(),
                        make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeShort, base,
                                             base + std::chrono::seconds(120),
                                             atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND));
  CASE_EXPECT_TRUE(team_test::receive_channel_event(
      test, team_test::make_snapshot_event(team_test::make_team_channel_key(kTeamId), 1, 0, &storage_stale,
                                           /*custom_data_sequence=*/2)));
  team_test::pump_rounds(test, 3);
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kInviteeShort));
    const auto* invitation = find_pending_invitation(snapshot, kInviteeLong);
    CASE_EXPECT_TRUE(nullptr != invitation);
    if (nullptr != invitation) {
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, invitation->team_source_type());
    }
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-TEAM-02: add_invitation 同 key 同 expiry 原位覆盖、expiry 变化重排; invitee 已是成员时忽略;
// team/source/time/admission 全字段保存; 增量 dirty 剥掉私有通知频道但保留 admission 原始数据。
CASE_TEST(lobbysvr_user_team, team_add_invitation_upsert_and_projection) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30102;
  constexpr uint64_t kSessionId = 0x10102;
  constexpr int64_t kTeamId = 602;
  constexpr uint64_t kInvitee1 = 93121;
  constexpr uint64_t kInvitee2 = 93122;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto current = setup_running_team(test, user_inst, kTeamId);
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. 两条不同过期时间的邀请: I1(300s, 全字段), I2(120s)
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kInvitee1, base - std::chrono::seconds(30),
                           base + std::chrono::seconds(300), atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)));
  {
    atfw::team::DTeamInvitation invitation2;
    protobuf_copy_message(*invitation2.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*invitation2.mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invitation2.mutable_invitee(), team_test::make_user_key(kInvitee2));
    *invitation2.mutable_start_timepoint() = protobuf_from_system_clock(base);
    *invitation2.mutable_expired_timepoint() = protobuf_from_system_clock(base + std::chrono::seconds(120));
    CASE_EXPECT_TRUE(
        inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kAddInvitation, invitation2));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 2 == dump_team_snapshot(current).snapshot().pending_invitation_size();
  }));
  {
    // 按过期时间升序: I2 在前, I1 在后; I1 全字段保存
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(kInvitee2, snapshot.snapshot().pending_invitation(0).invitee().user_id());
    CASE_EXPECT_EQ(kInvitee1, snapshot.snapshot().pending_invitation(1).invitee().user_id());
    expect_snapshot_invitation_full(snapshot.snapshot().pending_invitation(1), kTeamId, team_test::kCaptainUserId,
                                    kInvitee1, base - std::chrono::seconds(30), base + std::chrono::seconds(300),
                                    atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
  }

  // 2. 同 key 同 expiry 重复邀请: 原位覆盖(顺序不变, 内容更新)
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kInvitee1, base - std::chrono::seconds(30),
                           base + std::chrono::seconds(300), atfw::team::EN_TEAM_SOURCE_TYPE_MATCH)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    const auto* invitation = find_pending_invitation(snapshot, kInvitee1);
    return nullptr != invitation && atfw::team::EN_TEAM_SOURCE_TYPE_MATCH == invitation->team_source_type();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_invitation_size());
    if (snapshot.snapshot().pending_invitation_size() >= 2) {
      // 顺序未被重排
      CASE_EXPECT_EQ(kInvitee2, snapshot.snapshot().pending_invitation(0).invitee().user_id());
      CASE_EXPECT_EQ(kInvitee1, snapshot.snapshot().pending_invitation(1).invitee().user_id());
      CASE_EXPECT_EQ(base + std::chrono::seconds(300),
                     protobuf_to_system_clock(snapshot.snapshot().pending_invitation(1).expired_timepoint()));
    }
  }

  // 3. 同 key expiry 变化(更短): 重排到头部
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kInvitee1, base - std::chrono::seconds(30),
                           base + std::chrono::seconds(60), atfw::team::EN_TEAM_SOURCE_TYPE_MATCH)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    return 2 == snapshot.snapshot().pending_invitation_size() &&
           kInvitee1 == snapshot.snapshot().pending_invitation(0).invitee().user_id();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_invitation_size());
    if (snapshot.snapshot().pending_invitation_size() >= 2) {
      CASE_EXPECT_EQ(kInvitee1, snapshot.snapshot().pending_invitation(0).invitee().user_id());
      CASE_EXPECT_EQ(base + std::chrono::seconds(60),
                     protobuf_to_system_clock(snapshot.snapshot().pending_invitation(0).expired_timepoint()));
      CASE_EXPECT_EQ(kInvitee2, snapshot.snapshot().pending_invitation(1).invitee().user_id());
    }
  }

  // 4. invitee 已是成员(自己): 动作照常下发, 但不进入队伍级 pending 缓存
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kUserId, base, base + std::chrono::seconds(300),
                           atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_actions_of_case(team_test::collect_team_dirty(test, kSessionId, kTeamId),
                                            atfw::team::DTeamAction::kAddInvitation) >= 4;
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kUserId));
  }

  // 5. dirty 视图: 私有通知频道被剥掉, admission 原始数据保留(快照/增量不同契约见 §2.3)
  //    (5 条注入: I1/I2/I1 原位覆盖/I1 重排/成员忽略, 成员忽略的动作也照常下发)
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto adds = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kAddInvitation);
    CASE_EXPECT_EQ(5, static_cast<int>(adds.size()));
    bool checked_full_payload = false;
    for (const auto* one_action : adds) {
      const auto& invitation = one_action->action().add_invitation();
      CASE_EXPECT_TRUE(invitation.invitee_private_channel().channel_id().empty());
      if (invitation.invitee().user_id() == kInvitee1 &&
          atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND == invitation.team_source_type()) {
        checked_full_payload = true;
        CASE_EXPECT_EQ(kTeamId, invitation.team_key().team_id());
        CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation.inviter().user_id());
        CASE_EXPECT_EQ(base - std::chrono::seconds(30), protobuf_to_system_clock(invitation.start_timepoint()));
        CASE_EXPECT_TRUE(invitation.has_team_source_data());
        CASE_EXPECT_EQ(1, invitation.team_admission_data_size());
        CASE_EXPECT_EQ(1, invitation.member_admission_data_size());
      }
    }
    CASE_EXPECT_TRUE(checked_full_payload);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-TEAM-03: approve_invitation/reject_invitation 同时删除 list 和索引; 重复结果幂等;
// 另一类 pending(同 invitee 的加入请求)不受影响; dirty 剥私有频道且保留完整业务字段。
CASE_TEST(lobbysvr_user_team, team_invitation_result_removes_pending) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30103;
  constexpr uint64_t kSessionId = 0x10103;
  constexpr int64_t kTeamId = 603;
  constexpr uint64_t kInvitee1 = 93131;
  constexpr uint64_t kInvitee2 = 93132;
  constexpr uint64_t kInvitee3 = 93133;
  constexpr uint64_t kInviteeUnknown = 93139;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto current = setup_running_team(test, user_inst, kTeamId);
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  auto make_invitation = [&](uint64_t invitee_id, int64_t expire_seconds) {
    return make_full_invitation(kTeamId, team_test::kCaptainUserId, invitee_id, base,
                                base + std::chrono::seconds(expire_seconds),
                                atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
  };

  // 三条邀请 + 同 invitee 的一条加入请求(验证批复只影响对应类型)
  CASE_EXPECT_TRUE(inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kAddInvitation,
                                                 make_invitation(kInvitee1, 300)));
  CASE_EXPECT_TRUE(inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kAddInvitation,
                                                 make_invitation(kInvitee2, 200)));
  CASE_EXPECT_TRUE(inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kAddInvitation,
                                                 make_invitation(kInvitee3, 400)));
  CASE_EXPECT_TRUE(inject_team_join_request_action(
      test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
      make_full_join_request(kTeamId, kInvitee1, base + std::chrono::seconds(300),
                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-of-invitee1", 0x7801)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    return 3 == snapshot.snapshot().pending_invitation_size() &&
           1 == snapshot.snapshot().pending_join_request_size();
  }));

  // 1. approve_invitation(kInvitee2): 从 list 和索引删除, 其余保留
  CASE_EXPECT_TRUE(inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kApproveInvitation,
                                                 make_invitation(kInvitee2, 200)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 2 == dump_team_snapshot(current).snapshot().pending_invitation_size();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kInvitee2));
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kInvitee1));
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kInvitee3));
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
  }

  // 2. reject_invitation(kInvitee1): 删除邀请, 但同 invitee 的加入请求不受影响
  CASE_EXPECT_TRUE(inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kRejectInvitation,
                                                 make_invitation(kInvitee1, 300)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 1 == dump_team_snapshot(current).snapshot().pending_invitation_size();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kInvitee1));
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kInvitee3));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kInvitee1));
  }

  // 3. 重复 approve / 未知 invitee 的 reject: 幂等, 不损坏剩余缓存
  CASE_EXPECT_TRUE(inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kApproveInvitation,
                                                 make_invitation(kInvitee2, 200)));
  CASE_EXPECT_TRUE(inject_team_invitation_action(test, team_chain, atfw::team::DTeamAction::kRejectInvitation,
                                                 make_invitation(kInviteeUnknown, 300)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return team_test::count_actions_of_case(view, atfw::team::DTeamAction::kRejectInvitation) >= 2;
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kInvitee3));
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
  }

  // 4. dirty 视图: approve/reject 剥掉私有频道, 保留完整业务字段(room 写入日志的是规范化后的完整记录)
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto approves = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kApproveInvitation);
    CASE_EXPECT_EQ(2, static_cast<int>(approves.size()));
    if (!approves.empty()) {
      const auto& invitation = approves.front()->action().approve_invitation();
      CASE_EXPECT_TRUE(invitation.invitee_private_channel().channel_id().empty());
      CASE_EXPECT_EQ(kTeamId, invitation.team_key().team_id());
      CASE_EXPECT_EQ(team_test::kZoneId, invitation.team_key().zone_id());
      CASE_EXPECT_EQ(kInvitee2, invitation.invitee().user_id());
      CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation.inviter().user_id());
      CASE_EXPECT_EQ(base, protobuf_to_system_clock(invitation.start_timepoint()));
      CASE_EXPECT_EQ(base + std::chrono::seconds(200), protobuf_to_system_clock(invitation.expired_timepoint()));
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, invitation.team_source_type());
      CASE_EXPECT_EQ(1, invitation.team_admission_data_size());
    }
    auto rejects = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRejectInvitation);
    CASE_EXPECT_EQ(2, static_cast<int>(rejects.size()));
    for (const auto* one_action : rejects) {
      CASE_EXPECT_TRUE(one_action->action().reject_invitation().invitee_private_channel().channel_id().empty());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-TEAM-04: add_join_request 同 key 覆盖/重排、成员忽略;
// requester/channel/source/version/router/expiry/admission 全字段保存;
// dirty 剥 channel/router, member_admission_data 解包到 OneAction.shared_member_data。
CASE_TEST(lobbysvr_user_team, team_add_join_request_upsert_and_projection) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30104;
  constexpr uint64_t kSessionId = 0x10104;
  constexpr int64_t kTeamId = 604;
  constexpr uint64_t kRequester1 = 93141;
  constexpr uint64_t kRequester2 = 93142;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto current = setup_running_team(test, user_inst, kTeamId);
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 1. 两条加入请求: R1(300s, 全字段), R2(120s)
  CASE_EXPECT_TRUE(inject_team_join_request_action(
      test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
      make_full_join_request(kTeamId, kRequester1, base + std::chrono::seconds(300),
                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-client-1.0", 0x7811)));
  {
    atfw::team::DTeamJoinRequest join_request2;
    protobuf_copy_message(*join_request2.mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*join_request2.mutable_requester(), team_test::make_user_key(kRequester2));
    *join_request2.mutable_expired_timepoint() = protobuf_from_system_clock(base + std::chrono::seconds(120));
    CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
                                                     join_request2));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 2 == dump_team_snapshot(current).snapshot().pending_join_request_size();
  }));
  {
    // 升序: R2 在前; R1 全字段保存(快照导出剥 channel/router, 保留打包的 admission 原始数据)
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(kRequester2, snapshot.snapshot().pending_join_request(0).requester().user_id());
    CASE_EXPECT_EQ(kRequester1, snapshot.snapshot().pending_join_request(1).requester().user_id());
    expect_snapshot_join_request_full(snapshot.snapshot().pending_join_request(1), kTeamId, kRequester1,
                                      base + std::chrono::seconds(300), atfw::team::EN_TEAM_SOURCE_TYPE_MATCH,
                                      "jr-client-1.0");
  }

  // 2. 同 key 同 expiry: 原位覆盖(顺序不变, version 更新)
  CASE_EXPECT_TRUE(inject_team_join_request_action(
      test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
      make_full_join_request(kTeamId, kRequester1, base + std::chrono::seconds(300),
                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-client-1.1", 0x7811)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    const auto* join_request = find_pending_join_request(snapshot, kRequester1);
    return nullptr != join_request && "jr-client-1.1" == join_request->client_version();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_join_request_size());
    if (snapshot.snapshot().pending_join_request_size() >= 2) {
      CASE_EXPECT_EQ(kRequester2, snapshot.snapshot().pending_join_request(0).requester().user_id());
      CASE_EXPECT_EQ(kRequester1, snapshot.snapshot().pending_join_request(1).requester().user_id());
    }
  }

  // 3. 同 key expiry 变化(更短): 重排到头部
  CASE_EXPECT_TRUE(inject_team_join_request_action(
      test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
      make_full_join_request(kTeamId, kRequester1, base + std::chrono::seconds(60),
                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-client-1.2", 0x7811)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    return 2 == snapshot.snapshot().pending_join_request_size() &&
           kRequester1 == snapshot.snapshot().pending_join_request(0).requester().user_id();
  }));

  // 4. requester 已是成员(队长): 动作照常下发, 但不进入队伍级 pending 缓存
  CASE_EXPECT_TRUE(inject_team_join_request_action(
      test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
      make_full_join_request(kTeamId, team_test::kCaptainUserId, base + std::chrono::seconds(300),
                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "captain-jr", 0x7812)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_actions_of_case(team_test::collect_team_dirty(test, kSessionId, kTeamId),
                                            atfw::team::DTeamAction::kAddJoinRequest) >= 4;
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, team_test::kCaptainUserId));
  }

  // 5. dirty 视图: 剥 channel/router, 原始 member_admission_data 不下发,
  //    解包后的成员共享数据随 OneAction.shared_member_data 下发
  //    (5 条注入: R1/R2/R1 原位覆盖/R1 重排/成员忽略, 成员忽略的动作也照常下发)
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto adds = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kAddJoinRequest);
    CASE_EXPECT_EQ(5, static_cast<int>(adds.size()));
    bool checked_full_payload = false;
    for (const auto* one_action : adds) {
      const auto& join_request = one_action->action().add_join_request();
      CASE_EXPECT_TRUE(join_request.requester_private_channel().channel_id().empty());
      CASE_EXPECT_EQ(0, join_request.user_router_server_id());
      CASE_EXPECT_EQ(0, join_request.member_admission_data_size());
      if (join_request.requester().user_id() == kRequester1 && "jr-client-1.0" == join_request.client_version()) {
        checked_full_payload = true;
        CASE_EXPECT_EQ(kTeamId, join_request.team_key().team_id());
        CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, join_request.team_source_type());
        CASE_EXPECT_TRUE(join_request.has_team_source_data());
        CASE_EXPECT_EQ(base + std::chrono::seconds(300), protobuf_to_system_clock(join_request.expired_timepoint()));
        CASE_EXPECT_EQ(1, one_action->shared_member_data_size());
        if (one_action->shared_member_data_size() > 0) {
          CASE_EXPECT_TRUE(one_action->shared_member_data(0).battle().ready());
        }
      }
    }
    CASE_EXPECT_TRUE(checked_full_payload);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-TEAM-05: approve_join_request/reject_join_request 删除 list 和索引; 重复结果幂等;
// 另一类 pending(同 requester 的邀请)不受影响; dirty 剥 channel/router 且保留完整业务字段。
CASE_TEST(lobbysvr_user_team, team_join_request_result_removes_pending) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30105;
  constexpr uint64_t kSessionId = 0x10105;
  constexpr int64_t kTeamId = 605;
  constexpr uint64_t kRequester1 = 93151;
  constexpr uint64_t kRequester2 = 93152;
  constexpr uint64_t kRequester3 = 93153;
  constexpr uint64_t kRequesterUnknown = 93159;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto current = setup_running_team(test, user_inst, kTeamId);
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  auto make_join_request = [&](uint64_t requester_id, int64_t expire_seconds, const char* version) {
    return make_full_join_request(kTeamId, requester_id, base + std::chrono::seconds(expire_seconds),
                                  atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, version, 0x7821);
  };

  // 三条加入请求 + 同 requester 的一条邀请(验证批复只影响对应类型)
  CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
                                                   make_join_request(kRequester1, 300, "jr-1")));
  CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
                                                   make_join_request(kRequester2, 200, "jr-2")));
  CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
                                                   make_join_request(kRequester3, 400, "jr-3")));
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kRequester1, base, base + std::chrono::seconds(300),
                           atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    return 3 == snapshot.snapshot().pending_join_request_size() &&
           1 == snapshot.snapshot().pending_invitation_size();
  }));

  // 1. approve_join_request(kRequester2): 从 list 和索引删除, 其余保留
  CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kApproveJoinRequest,
                                                   make_join_request(kRequester2, 200, "jr-2")));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 2 == dump_team_snapshot(current).snapshot().pending_join_request_size();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, kRequester2));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kRequester1));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kRequester3));
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
  }

  // 2. reject_join_request(kRequester1): 删除加入请求, 但同 requester 的邀请不受影响
  CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kRejectJoinRequest,
                                                   make_join_request(kRequester1, 300, "jr-1")));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 1 == dump_team_snapshot(current).snapshot().pending_join_request_size();
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, kRequester1));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kRequester3));
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kRequester1));
  }

  // 3. 重复 approve / 未知 requester 的 reject: 幂等
  CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kApproveJoinRequest,
                                                   make_join_request(kRequester2, 200, "jr-2")));
  CASE_EXPECT_TRUE(inject_team_join_request_action(test, team_chain, atfw::team::DTeamAction::kRejectJoinRequest,
                                                   make_join_request(kRequesterUnknown, 300, "jr-x")));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    return team_test::count_actions_of_case(view, atfw::team::DTeamAction::kRejectJoinRequest) >= 2;
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kRequester3));
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
  }

  // 4. dirty 视图: approve/reject 剥 channel/router, 保留完整业务字段(含 member_admission_data 原始数据)
  {
    auto view = team_test::collect_team_dirty(test, kSessionId, kTeamId);
    auto approves = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kApproveJoinRequest);
    CASE_EXPECT_EQ(2, static_cast<int>(approves.size()));
    if (!approves.empty()) {
      const auto& join_request = approves.front()->action().approve_join_request();
      CASE_EXPECT_TRUE(join_request.requester_private_channel().channel_id().empty());
      CASE_EXPECT_EQ(0, join_request.user_router_server_id());
      CASE_EXPECT_EQ(kTeamId, join_request.team_key().team_id());
      CASE_EXPECT_EQ(team_test::kZoneId, join_request.team_key().zone_id());
      CASE_EXPECT_EQ(kRequester2, join_request.requester().user_id());
      CASE_EXPECT_EQ("jr-2", join_request.client_version());
      CASE_EXPECT_EQ(base + std::chrono::seconds(200), protobuf_to_system_clock(join_request.expired_timepoint()));
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, join_request.team_source_type());
      CASE_EXPECT_EQ(1, join_request.member_admission_data_size());
    }
    auto rejects = team_test::find_actions_of_case(view, atfw::team::DTeamAction::kRejectJoinRequest);
    CASE_EXPECT_EQ(2, static_cast<int>(rejects.size()));
    for (const auto* one_action : rejects) {
      CASE_EXPECT_TRUE(one_action->action().reject_join_request().requester_private_channel().channel_id().empty());
      CASE_EXPECT_EQ(0, one_action->action().reject_join_request().user_router_server_id());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-TEAM-06: add_member 同时删除该 user 的队伍级 invitation 和 join request; 删除一个 key 不影响其他 key。
CASE_TEST(lobbysvr_user_team, team_add_member_clears_joined_user_pendings) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30106;
  constexpr uint64_t kSessionId = 0x10106;
  constexpr int64_t kTeamId = 606;
  constexpr uint64_t kMember1 = 93161;
  constexpr uint64_t kMember2 = 93162;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto current = setup_running_team(test, user_inst, kTeamId);
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // M1/M2 各自同时持有队伍级邀请与加入请求
  for (uint64_t member_id : {kMember1, kMember2}) {
    CASE_EXPECT_TRUE(inject_team_invitation_action(
        test, team_chain, atfw::team::DTeamAction::kAddInvitation,
        make_full_invitation(kTeamId, team_test::kCaptainUserId, member_id, base, base + std::chrono::seconds(300),
                             atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)));
    CASE_EXPECT_TRUE(inject_team_join_request_action(
        test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
        make_full_join_request(kTeamId, member_id, base + std::chrono::seconds(300),
                               atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-member", 0x7831)));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    return 2 == snapshot.snapshot().pending_invitation_size() &&
           2 == snapshot.snapshot().pending_join_request_size();
  }));

  // M1 入队: 其两类 pending 同时删除, M2 的不受影响
  {
    atfw::team::DTeamAction team_action;
    auto* member = team_action.mutable_add_member();
    protobuf_copy_message(*member->mutable_user_key(), team_test::make_user_key(kMember1));
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(base);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return nullptr != team_test::find_snapshot_member(dump_team_snapshot(current), kMember1);
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_TRUE(nullptr == find_pending_invitation(snapshot, kMember1));
    CASE_EXPECT_TRUE(nullptr == find_pending_join_request(snapshot, kMember1));
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kMember2));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kMember2));
  }

  // M2 入队: 两类 pending 全部清空
  {
    atfw::team::DTeamAction team_action;
    auto* member = team_action.mutable_add_member();
    protobuf_copy_message(*member->mutable_user_key(), team_test::make_user_key(kMember2));
    member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    *member->mutable_joined_timepoint() = protobuf_from_system_clock(base);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, team_chain, team_action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return nullptr != team_test::find_snapshot_member(dump_team_snapshot(current), kMember2);
  }));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_EQ(4, snapshot.snapshot().member_size());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-TEAM-07: expired_timepoint == now 视为过期(插入与清理两侧); 分两段推进时间只清理已过期前缀;
// refresh 后 list/index/dump 三者一致(同 key 可重新插入); 清理不伪造 room 取消事件。
CASE_TEST(lobbysvr_user_team, team_admission_expiry_boundary_and_cleanup) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30107;
  constexpr uint64_t kSessionId = 0x10107;
  constexpr int64_t kTeamId = 607;
  constexpr uint64_t kInviteeShort = 93171;
  constexpr uint64_t kInviteeLong = 93172;
  constexpr uint64_t kInviteeBoundary = 93173;
  constexpr uint64_t kRequesterShort = 93181;
  constexpr uint64_t kRequesterLong = 93182;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto current = setup_running_team(test, user_inst, kTeamId);
  CASE_EXPECT_TRUE(!!current);
  if (!current) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !team_test::collect_team_dirty(test, kSessionId, kTeamId).snapshots.empty();
  }));
  test.cs().clear_history();

  team_test::channel_event_chain team_chain;
  team_chain.channel_key = team_test::make_team_channel_key(kTeamId);

  // 短(100s)/长(600s)有效期的邀请与加入请求各一条
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeShort, base, base + std::chrono::seconds(100),
                           atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)));
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeLong, base, base + std::chrono::seconds(600),
                           atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)));
  CASE_EXPECT_TRUE(inject_team_join_request_action(
      test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
      make_full_join_request(kTeamId, kRequesterShort, base + std::chrono::seconds(100),
                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-short", 0x7841)));
  CASE_EXPECT_TRUE(inject_team_join_request_action(
      test, team_chain, atfw::team::DTeamAction::kAddJoinRequest,
      make_full_join_request(kTeamId, kRequesterLong, base + std::chrono::seconds(600),
                             atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, "jr-long", 0x7842)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto snapshot = dump_team_snapshot(current);
    return 2 == snapshot.snapshot().pending_invitation_size() &&
           2 == snapshot.snapshot().pending_join_request_size();
  }));

  // 阶段 0: 推进 50s, 未达任何过期边界: refresh 不清理任何条目
  team_test::now_offset_guard::advance(std::chrono::seconds(50));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(2, snapshot.snapshot().pending_join_request_size());
  }

  // expiry == logical now 的插入视为过期: 动作事件已处理(dirty 视图照常下发)但 upsert 拒绝, 不进入缓存
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeBoundary, base,
                           team_test::now_offset_guard::logical_now(), atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return team_test::count_actions_of_case(team_test::collect_team_dirty(test, kSessionId, kTeamId),
                                            atfw::team::DTeamAction::kAddInvitation) >= 3;
  }));
  CASE_EXPECT_TRUE(nullptr == find_pending_invitation(dump_team_snapshot(current), kInviteeBoundary));

  // 阶段 1: 推进到 100s 边界: 已过期条目在 refresh 前就不得从 dump 下发(dump 侧过滤),
  // 清理只移除过期前缀(两条 100s 条目), 长有效期保留
  team_test::now_offset_guard::advance(std::chrono::seconds(50));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
  }
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(2, static_cast<int>(current->cleanup_expired_admissions(ctx)));
  }
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_join_request_size());
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kInviteeLong));
    CASE_EXPECT_TRUE(nullptr != find_pending_join_request(snapshot, kRequesterLong));
  }

  // list/index 一致: 已清理的 key 可以重新插入(索引无残留), 且幂等清理不重复计数
  CASE_EXPECT_TRUE(inject_team_invitation_action(
      test, team_chain, atfw::team::DTeamAction::kAddInvitation,
      make_full_invitation(kTeamId, team_test::kCaptainUserId, kInviteeShort, base, base + std::chrono::seconds(900),
                           atfw::team::EN_TEAM_SOURCE_TYPE_MATCH)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return 2 == dump_team_snapshot(current).snapshot().pending_invitation_size();
  }));
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(0, static_cast<int>(current->cleanup_expired_admissions(ctx)));
  }

  // 清理过期准入数据是本地行为: 不得伪造 room 的 approve/reject 取消事件
  CASE_EXPECT_TRUE(ss_capture.approve_invitation_reqs.empty());
  CASE_EXPECT_TRUE(ss_capture.reject_invitation_reqs.empty());
  CASE_EXPECT_TRUE(ss_capture.approve_join_request_reqs.empty());
  CASE_EXPECT_TRUE(ss_capture.reject_join_request_reqs.empty());
  CASE_EXPECT_EQ(0, static_cast<int>(
                        ss_capture.send_message_action_count(atfw::team::DTeamAction::kApproveInvitation)));
  CASE_EXPECT_EQ(0, static_cast<int>(
                        ss_capture.send_message_action_count(atfw::team::DTeamAction::kRejectInvitation)));
  CASE_EXPECT_EQ(0, static_cast<int>(
                        ss_capture.send_message_action_count(atfw::team::DTeamAction::kApproveJoinRequest)));
  CASE_EXPECT_EQ(0, static_cast<int>(
                        ss_capture.send_message_action_count(atfw::team::DTeamAction::kRejectJoinRequest)));

  // 阶段 2: 推进到 600s 边界: 长有效期条目也被清理, 只剩重新插入的 900s 邀请
  team_test::now_offset_guard::advance(std::chrono::seconds(500));
  {
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(2, static_cast<int>(current->cleanup_expired_admissions(ctx)));
  }
  {
    auto snapshot = dump_team_snapshot(current);
    CASE_EXPECT_EQ(1, snapshot.snapshot().pending_invitation_size());
    CASE_EXPECT_TRUE(nullptr != find_pending_invitation(snapshot, kInviteeShort));
    CASE_EXPECT_EQ(0, snapshot.snapshot().pending_join_request_size());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-01: 按 §2.2 构造完整的 invited 个人通知; manager 逐字段保存 PUBLIC 队伍/全成员 admission 快照;
// 非本人 invitee 的通知忽略。
CASE_TEST(lobbysvr_user_team, self_invited_notification_full_payload) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30111;
  constexpr uint64_t kOtherUserId = 30119;
  constexpr uint64_t kOtherMemberId = 93201;
  constexpr int64_t kTeamId = 8311;
  constexpr int64_t kOtherTeamId = 8312;

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

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();
  const auto start = base - std::chrono::seconds(30);
  const auto expiry = base + std::chrono::seconds(300);

  // room 在 invited 通知中重填当前队伍所有 PUBLIC team_admission_data 与每个成员的 PUBLIC member_admission_data
  {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kUserId));
    protobuf_copy_message(*invited->mutable_invitee_private_channel(), private_channel_key);
    *invited->mutable_start_timepoint() = protobuf_from_system_clock(start);
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    invited->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    CASE_EXPECT_TRUE(
        invited->mutable_team_source_data()->PackFrom(team_test::make_team_matching_module(false)));
    *invited->add_team_admission_data() = pack_public_team_module(team_test::make_team_matching_module(false));
    // 两个成员的 PUBLIC 准入数据
    auto* captain_admission = invited->add_member_admission_data();
    protobuf_copy_message(*captain_admission->mutable_user_key(), team_test::make_user_key(team_test::kCaptainUserId));
    *captain_admission->add_member_admission_data() =
        pack_public_member_module(team_test::make_member_ready_module(true));
    auto* member_admission = invited->add_member_admission_data();
    protobuf_copy_message(*member_admission->mutable_user_key(), team_test::make_user_key(kOtherMemberId));
    *member_admission->add_member_admission_data() =
        pack_public_member_module(team_test::make_member_ready_module(false));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamId));
  }));

  // 逐字段保存: 完整邀请身份/私有频道/来源/开始与过期时间/PUBLIC admission 数据
  auto invitation = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!invitation);
  if (invitation) {
    CASE_EXPECT_EQ(team_test::kZoneId, invitation->team_key().zone_id());
    CASE_EXPECT_EQ(kTeamId, invitation->team_key().team_id());
    CASE_EXPECT_EQ(team_test::kCaptainUserId, invitation->inviter().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, invitation->inviter().zone_id());
    CASE_EXPECT_EQ(kUserId, invitation->invitee().user_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), invitation->invitee_private_channel().channel_id());
    CASE_EXPECT_EQ(start, protobuf_to_system_clock(invitation->start_timepoint()));
    CASE_EXPECT_EQ(expiry, protobuf_to_system_clock(invitation->expired_timepoint()));
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, invitation->team_source_type());
    CASE_EXPECT_TRUE(invitation->has_team_source_data());

    CASE_EXPECT_EQ(1, invitation->team_admission_data_size());
    if (invitation->team_admission_data_size() > 0) {
      const auto& team_data = invitation->team_admission_data(0);
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC, team_data.value().permission());
      PROJECT_NAMESPACE_ID::DTeamSharedDataModule module;
      CASE_EXPECT_TRUE(team_data.value().data().UnpackTo(&module));
      CASE_EXPECT_TRUE(module.has_battle());
      CASE_EXPECT_EQ(user_team_algorithm::make_team_shared_data_key(module), team_data.key());
    }

    CASE_EXPECT_EQ(2, invitation->member_admission_data_size());
    bool checked_captain_admission = false;
    bool checked_member_admission = false;
    for (const auto& member_data : invitation->member_admission_data()) {
      CASE_EXPECT_EQ(1, member_data.member_admission_data_size());
      if (member_data.member_admission_data_size() <= 0) {
        continue;
      }
      const auto& entry = member_data.member_admission_data(0);
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC, entry.value().permission());
      PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule module;
      CASE_EXPECT_TRUE(entry.value().data().UnpackTo(&module));
      if (member_data.user_key().user_id() == team_test::kCaptainUserId) {
        checked_captain_admission = true;
        CASE_EXPECT_TRUE(module.battle().ready());
      } else if (member_data.user_key().user_id() == kOtherMemberId) {
        checked_member_admission = true;
        CASE_EXPECT_FALSE(module.battle().ready());
      }
    }
    CASE_EXPECT_TRUE(checked_captain_admission);
    CASE_EXPECT_TRUE(checked_member_admission);
  }

  // 非本人 invitee 的通知直接忽略
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kOtherUserId, kOtherTeamId,
                                        base + std::chrono::seconds(300)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kOtherTeamId)));
  // 本人的通知不受影响
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-02: apply_join_request 保存 room 归一化后的全部字段(身份/频道/来源/版本/路由/过期/准入数据);
// 非本人 requester 忽略; send_join_request 的 SS 成功本身不提前插入缓存(缓存只由个人通知建立)。
CASE_TEST(lobbysvr_user_team, self_join_request_receipt_full_payload) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30112;
  constexpr uint64_t kOtherUserId = 30129;
  constexpr int64_t kTeamId = 8321;
  constexpr int64_t kOtherTeamId = 8322;

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

  team_test::now_offset_guard time_guard;
  const auto expiry = team_test::now_offset_guard::logical_now() + std::chrono::seconds(300);

  // 1. send_join_request 上行成功: payload 完整, 但本地 pending 不提前插入
  {
    ::google::protobuf::Any source_data;
    CASE_EXPECT_TRUE(source_data.PackFrom(team_test::make_team_matching_module(false)));
    int32_t ret = run_manager_admission_call(test, "team.send_join_request.success",
                                             [&](rpc::context& ctx) -> rpc::result_code_type {
                                               RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                                                   user_inst->get_user_team_manager().send_join_request(
                                                       ctx, team_test::make_team_key(kTeamId),
                                                       atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, source_data)));
                                             });
    CASE_EXPECT_EQ(0, ret);
  }
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.add_join_request_reqs.size()));
  if (!ss_capture.add_join_request_reqs.empty()) {
    const auto& join_request = ss_capture.add_join_request_reqs.front().join_request();
    CASE_EXPECT_EQ(kTeamId, join_request.team_key().team_id());
    CASE_EXPECT_EQ(team_test::kZoneId, join_request.team_key().zone_id());
    CASE_EXPECT_EQ(kUserId, join_request.requester().user_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), join_request.requester_private_channel().channel_id());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND, join_request.team_source_type());
    CASE_EXPECT_TRUE(join_request.has_team_source_data());
    CASE_EXPECT_EQ(user_inst->get_client_info().client_version(), join_request.client_version());
    CASE_EXPECT_TRUE(0 != join_request.user_router_server_id());
    CASE_EXPECT_TRUE(join_request.member_admission_data_size() > 0);
  }
  // SS 成功不等于 room 已受理: 本地 pending 仍为空
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamId)));

  // 2. room 归一化后的 apply_join_request 回执: 逐字段保存(CS 拉取/get_pending 返回完整字段)
  {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), team_test::make_team_key(kTeamId));
    protobuf_copy_message(*applied->mutable_requester(), team_test::make_user_key(kUserId));
    protobuf_copy_message(*applied->mutable_requester_private_channel(), private_channel_key);
    *applied->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    applied->set_client_version("room-normalized-9.9");
    applied->set_user_router_server_id(team_test::kTeamRoomNodeId);
    applied->set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH);
    CASE_EXPECT_TRUE(
        applied->mutable_team_source_data()->PackFrom(team_test::make_team_matching_module(true)));
    *applied->add_member_admission_data() =
        pack_public_member_module(team_test::make_member_ready_module(true));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamId));
  }));
  auto join_request = user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamId));
  CASE_EXPECT_TRUE(!!join_request);
  if (join_request) {
    CASE_EXPECT_EQ(team_test::kZoneId, join_request->team_key().zone_id());
    CASE_EXPECT_EQ(kTeamId, join_request->team_key().team_id());
    CASE_EXPECT_EQ(kUserId, join_request->requester().user_id());
    CASE_EXPECT_EQ(team_test::kZoneId, join_request->requester().zone_id());
    CASE_EXPECT_EQ(private_channel_key.channel_id(), join_request->requester_private_channel().channel_id());
    CASE_EXPECT_EQ(expiry, protobuf_to_system_clock(join_request->expired_timepoint()));
    CASE_EXPECT_EQ("room-normalized-9.9", join_request->client_version());
    CASE_EXPECT_EQ(team_test::kTeamRoomNodeId, join_request->user_router_server_id());
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, join_request->team_source_type());
    CASE_EXPECT_TRUE(join_request->has_team_source_data());
    CASE_EXPECT_EQ(1, join_request->member_admission_data_size());
  }

  // 3. 非本人 requester 的回执直接忽略
  {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), team_test::make_team_key(kOtherTeamId));
    protobuf_copy_message(*applied->mutable_requester(), team_test::make_user_key(kOtherUserId));
    *applied->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  }
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_FALSE(
      !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kOtherTeamId)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamId)));

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-03: 两类个人缓存的同 team_key 同 expiry 原位覆盖、不同 expiry 重排、不同 team_key 共存;
// 通过 get 返回的最新完整内容与分段 cleanup 终态证明 list/map 同步。
CASE_TEST(lobbysvr_user_team, self_pending_upsert_reorder_and_segmented_cleanup) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30113;
  constexpr int64_t kTeamA = 8331;
  constexpr int64_t kTeamB = 8332;
  constexpr int64_t kTeamC = 8333;

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

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  auto inject_invitation = [&](int64_t team_id, std::chrono::system_clock::time_point expiry,
                               atfw::team::EnTeamSourceType source_type) {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kUserId));
    *invited->mutable_start_timepoint() = protobuf_from_system_clock(base);
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    invited->set_team_source_type(source_type);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  };
  auto inject_join_request = [&](int64_t team_id, std::chrono::system_clock::time_point expiry,
                                 const char* client_version) {
    atfw::team::DTeamMemberAction action;
    auto* applied = action.mutable_apply_join_request();
    protobuf_copy_message(*applied->mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*applied->mutable_requester(), team_test::make_user_key(kUserId));
    *applied->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    applied->set_client_version(client_version);
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  };

  // 三类共存: 邀请 A(300s), 邀请 B(120s), 加入请求 C(300s)
  inject_invitation(kTeamA, base + std::chrono::seconds(300), atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
  inject_invitation(kTeamB, base + std::chrono::seconds(120), atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
  inject_join_request(kTeamC, base + std::chrono::seconds(300), "jr-v1");
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto& mgr = user_inst->get_user_team_manager();
    return !!mgr.get_pending_invitation(team_test::make_team_key(kTeamA)) &&
           !!mgr.get_pending_invitation(team_test::make_team_key(kTeamB)) &&
           !!mgr.get_pending_join_request(team_test::make_team_key(kTeamC));
  }));

  // 同 team_key 同 expiry: 原位覆盖, get 返回最新完整内容
  inject_invitation(kTeamA, base + std::chrono::seconds(300), atfw::team::EN_TEAM_SOURCE_TYPE_MATCH);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto pending = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA));
    return pending && atfw::team::EN_TEAM_SOURCE_TYPE_MATCH == pending->team_source_type();
  }));
  {
    auto pending = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA));
    CASE_EXPECT_TRUE(!!pending);
    if (pending) {
      CASE_EXPECT_EQ(base + std::chrono::seconds(300), protobuf_to_system_clock(pending->expired_timepoint()));
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_MATCH, pending->team_source_type());
    }
  }
  inject_join_request(kTeamC, base + std::chrono::seconds(300), "jr-v2");
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto pending = user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamC));
    return pending && "jr-v2" == pending->client_version();
  }));

  // 同 team_key 不同 expiry: 重排, get 返回更新后的过期时间
  inject_invitation(kTeamA, base + std::chrono::seconds(900), atfw::team::EN_TEAM_SOURCE_TYPE_BATTLE);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto pending = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA));
    return pending && atfw::team::EN_TEAM_SOURCE_TYPE_BATTLE == pending->team_source_type();
  }));
  {
    auto pending = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA));
    CASE_EXPECT_TRUE(!!pending);
    if (pending) {
      CASE_EXPECT_EQ(base + std::chrono::seconds(900), protobuf_to_system_clock(pending->expired_timepoint()));
    }
  }

  // 分段 cleanup 第一段: 推进 150s, 只有最短有效期的 B 被清理(前缀语义: A/C 均未过期即停止)
  team_test::now_offset_guard::advance(std::chrono::seconds(150));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamB)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamC)));

  // 分段 cleanup 第二段: 推进到 350s, C(300s)过期被清理; 重排后的 A(900s)必须存活 —
  // 若重排时只改内容未更新过期时间, A 会在此处被误清理
  team_test::now_offset_guard::advance(std::chrono::seconds(200));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamC)));
  {
    auto pending = user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA));
    CASE_EXPECT_TRUE(!!pending);
    if (pending) {
      CASE_EXPECT_EQ(base + std::chrono::seconds(900), protobuf_to_system_clock(pending->expired_timepoint()));
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_SOURCE_TYPE_BATTLE, pending->team_source_type());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-05: 个人 reject_invitation/reject_join_request 删除对应 list/index; 重复 reject 幂等;
// 另一类/另一 team 的 pending 不受影响; 非本人回执忽略。
CASE_TEST(lobbysvr_user_team, self_reject_notifications_remove_pending) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30114;
  constexpr uint64_t kOtherUserId = 30149;
  constexpr int64_t kTeamA = 8341;
  constexpr int64_t kTeamB = 8342;
  constexpr int64_t kTeamC = 8343;

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

  team_test::now_offset_guard time_guard;
  const auto expiry = team_test::now_offset_guard::logical_now() + std::chrono::seconds(300);

  // 建立: 邀请 A、加入请求 B、邀请 C
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamA, expiry));
  CASE_EXPECT_TRUE(inject_apply_join_request_event(test, private_chain, kUserId, kTeamB, expiry));
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamC, expiry));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto& mgr = user_inst->get_user_team_manager();
    return !!mgr.get_pending_invitation(team_test::make_team_key(kTeamA)) &&
           !!mgr.get_pending_join_request(team_test::make_team_key(kTeamB)) &&
           !!mgr.get_pending_invitation(team_test::make_team_key(kTeamC));
  }));

  auto inject_reject_invitation = [&](int64_t team_id, uint64_t invitee_id) {
    atfw::team::DTeamMemberAction action;
    auto* rejected = action.mutable_reject_invitation();
    protobuf_copy_message(*rejected->mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*rejected->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*rejected->mutable_invitee(), team_test::make_user_key(invitee_id));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  };
  auto inject_reject_join_request = [&](int64_t team_id, uint64_t requester_id) {
    atfw::team::DTeamMemberAction action;
    auto* rejected = action.mutable_reject_join_request();
    protobuf_copy_message(*rejected->mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*rejected->mutable_requester(), team_test::make_user_key(requester_id));
    CASE_EXPECT_TRUE(team_test::inject_event_message(test, private_chain, action));
  };

  // reject_invitation(A): 删除对应 pending; 重复 reject 幂等; 其他 team/类型不受影响
  inject_reject_invitation(kTeamA, kUserId);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA));
  }));
  inject_reject_invitation(kTeamA, kUserId);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamB)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamC)));

  // reject_join_request(B): 删除对应 pending; 重复 reject 幂等; 错类型的 reject(B 的邀请)不影响其他记录
  inject_reject_join_request(kTeamB, kUserId);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamB));
  }));
  inject_reject_join_request(kTeamB, kUserId);
  inject_reject_invitation(kTeamB, kUserId);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamB)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamC)));

  // 非本人回执忽略
  inject_reject_invitation(kTeamC, kOtherUserId);
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return user_inst->get_user_team_manager().get_processed_private_chat_channel_sequence() >=
           private_chain.sequence;
  }));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamC)));

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-08: second refresh 分段清理两类过期前缀, 清理后 get 返回空;
// 重复申请预检(真实 CS 入口)不能把已过期但尚未 refresh 的记录当作有效。
CASE_TEST(lobbysvr_user_team, self_pending_second_refresh_and_expired_reapply_precheck) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }
  // 注册生成的 CS dispatcher(per-runtime, 否则分发返回 EN_SYS_INIT)
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());
  CASE_EXPECT_TRUE(team_test::setup_team_room_node(test));
  team_test::team_room_ss_capture ss_capture;
  CASE_EXPECT_TRUE(team_test::setup_team_room_ss_capture(test, ss_capture));

  constexpr uint64_t kUserId = 30115;
  constexpr uint64_t kSessionId = 0x10115;
  constexpr int64_t kTeamA = 8351;
  constexpr int64_t kTeamB = 8352;
  constexpr int64_t kTeamC = 8353;
  constexpr int64_t kTeamD = 8354;
  constexpr int64_t kTeamE = 8355;

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
  if (!client) {
    test.stop();
    return;
  }

  team_test::channel_event_chain private_chain;
  private_chain.channel_key = private_channel_key;

  team_test::now_offset_guard time_guard;
  const auto base = team_test::now_offset_guard::logical_now();

  // 邀请 A(100s)、加入请求 B(100s)、邀请 C(300s)、加入请求 D(100s)
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamA, base + std::chrono::seconds(100)));
  CASE_EXPECT_TRUE(
      inject_apply_join_request_event(test, private_chain, kUserId, kTeamB, base + std::chrono::seconds(100)));
  CASE_EXPECT_TRUE(inject_invited_event(test, private_chain, kUserId, kTeamC, base + std::chrono::seconds(300)));
  CASE_EXPECT_TRUE(
      inject_apply_join_request_event(test, private_chain, kUserId, kTeamD, base + std::chrono::seconds(100)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    auto& mgr = user_inst->get_user_team_manager();
    return !!mgr.get_pending_invitation(team_test::make_team_key(kTeamA)) &&
           !!mgr.get_pending_join_request(team_test::make_team_key(kTeamB)) &&
           !!mgr.get_pending_invitation(team_test::make_team_key(kTeamC)) &&
           !!mgr.get_pending_join_request(team_test::make_team_key(kTeamD));
  }));

  // 第一段: 推进 50s, 未达边界, 两类缓存都保留
  team_test::now_offset_guard::advance(std::chrono::seconds(50));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamB)));

  // 第二段: 推进到 100s 边界, 两类过期前缀都被清理(长有效期的 C 保留), 清理后 get 返回空
  team_test::now_offset_guard::advance(std::chrono::seconds(50));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamA)));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamB)));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamD)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamC)));

  // 预检: E 的加入请求已过期但尚未 refresh, 真实 CS 入口不得返回 ALREADY_EXISTS, 必须重新上行
  CASE_EXPECT_TRUE(inject_apply_join_request_event(
      test, private_chain, kUserId, kTeamE, team_test::now_offset_guard::logical_now() + std::chrono::seconds(50)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamE));
  }));
  // 推进 60s: E 已过期但 second refresh 尚未运行
  team_test::now_offset_guard::advance(std::chrono::seconds(60));
  {
    atframework::shared::CSTeamSendJoinRequestReq req;
    protobuf_copy_message(*req.mutable_team_key(), team_test::make_team_key(kTeamE));
    req.set_team_source_type(atfw::team::EN_TEAM_SOURCE_TYPE_FRIEND);
    gsl::string_view rpc_name = rpc::lobbysvrclientservice::packer::get_full_name_of_team_send_join_request();
    atframework::CSMsg rsp_msg;
    CASE_EXPECT_TRUE(post_admission_cs_request(test, client, rpc_name, req, rsp_msg));
    // 过期记录不得拦截重复申请: 不能返回 EN_ERR_TEAM_JOIN_REQUEST_ALREADY_EXISTS
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
  }
  CASE_EXPECT_EQ(1, static_cast<int>(ss_capture.add_join_request_reqs.size()));
  if (!ss_capture.add_join_request_reqs.empty()) {
    CASE_EXPECT_EQ(kTeamE, ss_capture.add_join_request_reqs.front().join_request().team_key().team_id());
  }
  // 上行成功不重建本地缓存(缓存只由 apply_join_request 建立); refresh 清理掉过期记录后 get 返回空
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_join_request(team_test::make_team_key(kTeamE)));
  CASE_EXPECT_TRUE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamC)));

  // 第三段: 推进到 300s 边界之后, 长有效期的 C 也被清理
  team_test::now_offset_guard::advance(std::chrono::seconds(150));
  CASE_EXPECT_TRUE(run_second_refresh(test, user_inst));
  CASE_EXPECT_FALSE(!!user_inst->get_user_team_manager().get_pending_invitation(team_test::make_team_key(kTeamC)));

  CASE_EXPECT_EQ(0, test.stop());
}

// ADM-SELF-10: 个人频道 sequence 小于/等于已处理序号不重复处理, 大于已处理序号只处理一次;
// 已处理序号变化设置 manager dirty 并随 table dump 正确落地。
CASE_TEST(lobbysvr_user_team, self_private_channel_sequence_watermark) {
  atfw::testing::runtime test;
  CASE_EXPECT_TRUE(team_test::start_team_runtime(test));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kUserId = 30116;
  constexpr int64_t kTeamA = 8361;
  constexpr int64_t kTeamB = 8362;

  user::ptr_t user_inst;
  std::string subscriber_key;
  atframework::dtmq::DChannelIdKey private_channel_key;
  CASE_EXPECT_TRUE(team_test::setup_team_user(test, kUserId, user_inst, subscriber_key, private_channel_key));
  if (!user_inst) {
    test.stop();
    return;
  }

  team_test::now_offset_guard time_guard;
  const auto expiry = team_test::now_offset_guard::logical_now() + std::chrono::seconds(300);

  auto& mgr = user_inst->get_user_team_manager();
  CASE_EXPECT_FALSE(mgr.is_dirty());
  CASE_EXPECT_EQ(0, mgr.get_processed_private_chat_channel_sequence());

  auto make_invited_action = [&](int64_t team_id) {
    atfw::team::DTeamMemberAction action;
    auto* invited = action.mutable_invited();
    protobuf_copy_message(*invited->mutable_team_key(), team_test::make_team_key(team_id));
    protobuf_copy_message(*invited->mutable_inviter(), team_test::make_user_key(team_test::kCaptainUserId));
    protobuf_copy_message(*invited->mutable_invitee(), team_test::make_user_key(kUserId));
    *invited->mutable_expired_timepoint() = protobuf_from_system_clock(expiry);
    return action;
  };

  // 1. seq=1 的邀请事件: 正常处理, 已处理序号前进到 1, manager 置脏
  auto action_a = make_invited_action(kTeamA);
  std::vector<atframework::dtmq::DChannelMessage> msgs_a{team_test::make_event_message(1, action_a)};
  uint64_t hash_a = team_test::chain_message_hashes(msgs_a, 0);
  CASE_EXPECT_TRUE(
      team_test::receive_channel_event(test, team_test::make_incremental_event(private_channel_key, 1, msgs_a)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!mgr.get_pending_invitation(team_test::make_team_key(kTeamA));
  }));
  CASE_EXPECT_EQ(1, mgr.get_processed_private_chat_channel_sequence());
  CASE_EXPECT_TRUE(mgr.is_dirty());

  // 已处理序号变化随 table dump 落地
  {
    PROJECT_NAMESPACE_ID::table_user table;
    rpc::context ctx{rpc::context::create_without_task()};
    CASE_EXPECT_EQ(0, mgr.dump(ctx, table));
    CASE_EXPECT_EQ(1, table.team_data().processed_private_chat_channel_sequence());
  }
  mgr.clear_dirty();
  CASE_EXPECT_FALSE(mgr.is_dirty());

  // 2. 重放 seq<=1 的旧事件: 不重复处理(内容不变化), 已处理序号不前进, 不置脏
  //    (重放是负路径, 订阅层或 manager 已处理序号任一拦截都满足契约, 不断言注入本身的结果码)
  team_test::receive_channel_event(test, team_test::make_incremental_event(private_channel_key, 1, msgs_a));
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_EQ(1, mgr.get_processed_private_chat_channel_sequence());
  CASE_EXPECT_FALSE(mgr.is_dirty());
  {
    auto pending = mgr.get_pending_invitation(team_test::make_team_key(kTeamA));
    CASE_EXPECT_TRUE(!!pending);
    CASE_EXPECT_FALSE(!!mgr.get_pending_invitation(team_test::make_team_key(kTeamB)));
  }

  // 3. seq=2 的新事件: 只处理一次, 已处理序号前进到 2 并置脏
  auto action_b = make_invited_action(kTeamB);
  std::vector<atframework::dtmq::DChannelMessage> msgs_b{team_test::make_event_message(2, action_b)};
  uint64_t hash_b = team_test::chain_message_hashes(msgs_b, hash_a);
  CASE_EXPECT_TRUE(
      team_test::receive_channel_event(test, team_test::make_incremental_event(private_channel_key, 2, msgs_b)));
  CASE_EXPECT_TRUE(team_test::pump_until(test, [&] {
    return !!mgr.get_pending_invitation(team_test::make_team_key(kTeamB));
  }));
  CASE_EXPECT_EQ(2, mgr.get_processed_private_chat_channel_sequence());
  CASE_EXPECT_TRUE(mgr.is_dirty());
  mgr.clear_dirty();

  // 4. 以最新 last_sequence 元数据重放 seq=1/2 的旧消息: 均不重复处理、不置脏
  team_test::receive_channel_event(test, team_test::make_incremental_event(private_channel_key, 2, msgs_b));
  std::vector<atframework::dtmq::DChannelMessage> replay_a{msgs_a.front()};
  {
    auto event_sync = team_test::make_incremental_event(private_channel_key, 2, replay_a);
    event_sync.mutable_channel_metadata()->set_last_hash_code(hash_b);
    team_test::receive_channel_event(test, event_sync);
  }
  team_test::pump_rounds(test, 3);
  CASE_EXPECT_EQ(2, mgr.get_processed_private_chat_channel_sequence());
  CASE_EXPECT_FALSE(mgr.is_dirty());
  CASE_EXPECT_TRUE(!!mgr.get_pending_invitation(team_test::make_team_key(kTeamA)));
  CASE_EXPECT_TRUE(!!mgr.get_pending_invitation(team_test::make_team_key(kTeamB)));

  CASE_EXPECT_EQ(0, test.stop());
}

// Copyright 2026 atframework

#include "logic/room/team_room.h"

#include <log/log_wrapper.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>
#include <string/string_format.h>
#include <time/time_utility.h>

#include <atframe/atapp_conf.h>
#include <config/logic_config.h>
#include <logic/logic_server_setup.h>
#include <memory/object_allocator.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/team_room.config.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/dtmq/dtmq_client_api.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "logic/room/team_room_manager.h"

namespace {
const atfw::team::config::teamsvr_room_cfg& get_teamsvr_room_cfg() noexcept {
  return logic_config::me()->get_server_instance_config<atfw::team::config::teamsvr_room_cfg>();
}

gsl::string_view get_timer_event_name(team_room_timer_event_type event_type) noexcept {
  switch (event_type) {
    case team_room_timer_event_type::kAcquireLock:
      return "team_room.timer_event.acquire_lock";
    case team_room_timer_event_type::kMaintenance:
      return "team_room.timer_event.maintenance";
    case team_room_timer_event_type::kKickOfflineMember:
      return "team_room.timer_event.kick_offline_member";
    case team_room_timer_event_type::kDestroyEmptyRoom:
      return "team_room.timer_event.destroy_empty_room";
    case team_room_timer_event_type::kDestroyChannel:
      return "team_room.timer_event.destroy_channel";
    default:
      return "team_room.timer_event.unknown";
  }
}

// 仅拷贝 EN_TEAM_PERMISSION_TYPE_PUBLIC 权限的数据(下发给未入队玩家时隐藏成员权限数据)
static void copy_public_permission_data(const google::protobuf::Map<int32_t, atfw::team::DTeamAnyData>& from,
                                        google::protobuf::Map<int32_t, atfw::team::DTeamAnyData>* output) {
  if (nullptr == output) {
    return;
  }
  for (const auto& kv : from) {
    if (kv.second.permission() == atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      protobuf_copy_message((*output)[kv.first], kv.second);
    }
  }
}

// 逐元素比较 DTeamAnyData 的 map 字段(protobuf_equal 只接受 Message，不能直接比较 Map 容器)
static bool team_any_data_map_equal(const google::protobuf::Map<int32_t, atfw::team::DTeamAnyData>& l,
                                    const google::protobuf::Map<int32_t, atfw::team::DTeamAnyData>& r) {
  if (l.size() != r.size()) {
    return false;
  }
  for (const auto& kv : l) {
    auto it = r.find(kv.first);
    if (it == r.end() || !atfw::atapp::protobuf_equal(kv.second, it->second)) {
      return false;
    }
  }
  return true;
}

team_room* get_team_room_from_subscriber(const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                         gsl::string_view callback_name) {
  if (!subscriber) {
    return nullptr;
  }
  auto local_private_data = subscriber->get_local_private_data();
  if (local_private_data.empty()) {
    FWLOGERROR("team_room {} callback missing local_private_data", callback_name);
    return nullptr;
  }

  static_assert(sizeof(team_room*) == sizeof(*local_private_data.data()), "team_room* size mismatch");
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return reinterpret_cast<team_room*>(*local_private_data.data());
}

rpc::dtmq::client_subscriber::event_callback_set_ptr_t build_shared_team_room_channel_event_callback_set() {
  rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
      rpc::dtmq::client_subscriber::create_event_callback_set();

  rpc::dtmq::client_subscriber::set_event_callback_on_receive_snapshot_finished(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelSnapshot& /*snapshot*/, int32_t result_code) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_receive_snapshot_finished");
        if (room != nullptr) {
          room->on_receive_snapshot_finished(ctx, subscriber, result_code);
        }
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_receive_event(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelMessage& data) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_receive_event");
        if (room != nullptr) {
          room->on_receive_event(ctx, subscriber, data);
        }
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_update_optimistic_lock(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelOptimisticLock& from, const ::atfw::dtmq::DChannelOptimisticLock& to) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_update_optimistic_lock");
        if (room != nullptr) {
          room->on_update_optimistic_lock(ctx, subscriber, from, to);
        }
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber, int64_t /*log_sequence*/,
               std::chrono::system_clock::time_point /*destroy_time*/) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_destroyed");
        if (room != nullptr) {
          room->on_destroyed(ctx, subscriber);
        }
      });
  return ret;
}

rpc::dtmq::client_subscriber::event_callback_set_ptr_t& get_shared_team_room_channel_event_callback_set() {
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
      build_shared_team_room_channel_event_callback_set();
  return ret;
}
}  // namespace

struct team_room::iterating_member_protect_t {
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, member_ptr_t, user_key_hash_t, user_key_equal_t> pending_to_add;
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, member_ptr_t, user_key_hash_t, user_key_equal_t>
      pending_to_remove;
};

team_room::team_room(ctor_guard&, int64_t team_id, const atfw::dtmq::DChannelIdKey& channel_key,
                     std::string&& subscriber_key, std::string&& lock_holder,
                     atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&& subscriber)
    : team_id_(team_id),
      subscriber_key_(std::move(subscriber_key)),
      lock_holder_(std::move(lock_holder)),
      subscriber_(std::move(subscriber)) {
  protobuf_copy_message(channel_key_, channel_key);
  storage_.mutable_team_key()->set_team_id(team_id_);

  // 创建之后，加载快照数据前重置一下用于延迟删除的空房间计时器，避免在创建后立即触发删除
  refresh_empty_tracking(restore_timepoint_);
}

team_room::ptr_t team_room::create(rpc::context& ctx, int64_t team_id) {
  // 乐观锁持有者标识与服务节点名和节点ID相关，节点切换后新节点据此区分老锁
  gsl::string_view server_name = logic_config::me()->get_local_server_name();

  std::string lock_holder;
  if (server_name.empty()) {
    uint64_t server_id = logic_config::me()->get_local_server_id();
    lock_holder = atfw::util::string::format("teamsvr-room:{:#x}", server_id);
  } else {
    lock_holder = atfw::util::string::format("teamsvr-room:{}", server_name);
  }
  std::string subscriber_key = atfw::util::string::format("team_room:{}", team_id);

  rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key};
  subscribe_options.with_private_data = true;
  subscribe_options.event_callback_set = get_shared_team_room_channel_event_callback_set();
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(atfw::team::EN_TEAM_CHANNEL_TYPE_TEAM_ROOM);
  channel_key.set_channel_id(atfw::util::string::format("{}", team_id));
  auto subscriber = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
  if (!subscriber) {
    FWLOGERROR("team_room create subscriber of channel {}:{} failed, maybe configure is missing",
               channel_key.channel_type(), channel_key.channel_id());
    return nullptr;
  }

  ctor_guard guard;
  auto room = atfw::component::memory::stl::make_strong_rc<team_room>(
      guard, team_id, channel_key, std::move(subscriber_key), std::move(lock_holder), std::move(subscriber));
  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(room.get())};
  room->subscriber_->set_local_private_data(local_private_data);
  if (!room->subscriber_->get_shared_event_callback_set()) {
    room->subscriber_->set_shared_event_callback_set(get_shared_team_room_channel_event_callback_set());
  }
  // 已就绪(共享层复用)则不会再触发 snapshot_finished，直接恢复当前缓存快照
  if (room->subscriber_->is_ready()) {
    if (!room->restore_snapshot(ctx)) {
      FCTXLOGERROR(ctx, "team room {} restore reused subscriber snapshot failed", team_id);
    }
  }

  // 每个房间有且只有一个定时器，创建后即开始调度
  room->schedule_next_timer();

  return room;
}

int64_t team_room::get_team_id() const noexcept { return team_id_; }

const atfw::dtmq::DChannelIdKey& team_room::get_channel_key() const noexcept { return channel_key_; }

bool team_room::is_subscriber_ready() const noexcept { return subscriber_->is_ready(); }

bool team_room::is_lock_holder() const noexcept { return lock_acquired_ && !subscriber_->is_destroyed(); }

void team_room::on_remove() { timer_watcher_.reset(); }

rpc::result_code_type team_room::await_ready(rpc::context& ctx) {
  if (!subscriber_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (!subscriber_->is_ready()) {
    auto await_ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_await_pending_heartbeat(ctx));
    if (await_ret != 0) {
      FCTXLOGERROR(ctx, "team room {} await pending heartbeat failed: {}", team_id_, await_ret);
      RPC_RETURN_CODE(await_ret);
    }
    if (!subscriber_ || !subscriber_->is_ready()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
  }
  if (!snapshot_restored_) {
    if (!restore_snapshot(ctx)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM_BAD_PACKAGE);
    }
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::send_action(rpc::context& ctx, const atfw::team::DTeamAction& action, bool no_wait) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  // 移除成员: 记录移除原因并加入重试队列，等待频道事件回环后真正移除；
  // 已在重试队列中说明移除消息在途，不再重复发送
  if (action.has_remove_member()) {
    const auto& user_key = action.remove_member().user_key();
    if (member_retry_remove_.end() != member_retry_remove_.find(user_key)) {
      RPC_RETURN_CODE(0);
    }
    auto member = find_member(user_key, false);
    if (member) {
      member->exit_reason = action.remove_member().remove_member_reason();

      auto retry_data = atfw::component::memory::stl::make_strong_rc<member_retry_data>();
      if (retry_data) {
        retry_data->next_retry_timepoint =
            atfw::util::time::time_utility::now() +
            protobuf_to_system_clock(get_teamsvr_room_cfg().member_channel_notification_retry_interval());
        member_retry_remove_.insert_key_value(user_key, std::move(retry_data));
        schedule_next_timer();
      }
    }
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(do_send_action(ctx, action, no_wait)));
}

rpc::result_code_type team_room::do_send_action(rpc::context& ctx, const atfw::team::DTeamAction& action,
                                                bool no_wait) {
  auto event_data = rpc::make_shared_message<google::protobuf::Any>(ctx);
  if (!event_data->PackFrom(action)) {
    FCTXLOGERROR(ctx, "team room {} pack DTeamAction failed", team_id_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_event_with_lock(ctx, std::move(*event_data), no_wait)));
}

rpc::result_code_type team_room::send_member_action(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key,
                                                    const atfw::team::DTeamMemberAction& action) {
  if (channel_key.channel_type() == 0 || channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::dtmq::DChannelMessageDetail> detail(ctx);
  if (!detail->mutable_event()->PackFrom(action)) {
    FCTXLOGERROR(ctx, "team room {} pack DTeamMemberAction failed", team_id_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  atfw::dtmq::channel_subscriber no_subscriber;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(ctx, std::move(no_subscriber), channel_key,
                                                                std::move(*detail), nullptr, nullptr, false, true)));
}

rpc::result_code_type team_room::heartbeat(rpc::context& ctx, const atfw::team::SSTeamRoomHeartbeatReq& req) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  if (!lock_acquired_) {
    auto lock_ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
    if (lock_ret != 0) {
      RPC_RETURN_CODE(lock_ret);
    }
  }

  auto member = find_member(req.user_key(), true);
  if (!member) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND);
  }

  // 更新成员已确认的日志序号(随 custom_data 下发，用于成员侧补差量)
  if (req.sequence() > member->member_data.acknowledge_action_sequence()) {
    member->member_data.set_acknowledge_action_sequence(req.sequence());
    member->member_data.set_acknowledge_action_hash_code(req.hash_code());
  }

  // 服务器如果触发反订阅，这里会传0
  if (req.user_router_server_id() > 0) {
    member->last_heartbeat_timepoint = atfw::util::time::time_utility::now();
    *member->member_data.mutable_last_heartbeat_timepoint() =
        protobuf_from_system_clock(member->last_heartbeat_timepoint);
  }
  member->user_router_server_id = req.user_router_server_id();
  member->member_data.set_user_router_server_id(req.user_router_server_id());
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::create_team(rpc::context& ctx, const atfw::team::SSTeamRoomCreateReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  if (req.sender_user_key().zone_id() == 0 || req.sender_user_key().user_id() == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  if (team_created_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }

  // 先占位再让出协程，避免并发的 create_team 在 acquire_lock 挂起期间同时通过检查而重复创建
  team_created_ = true;
  if (!lock_acquired_) {
    auto lock_ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
    if (lock_ret != 0) {
      team_created_ = false;
      RPC_RETURN_CODE(lock_ret);
    }
  }
  auto now = atfw::util::time::time_utility::now();
  auto public_data = rpc::make_shared_message<atfw::team::DTeamStorage>(ctx);
  auto private_data = rpc::make_shared_message<atfw::team::DTeamRoomPrivateData>(ctx);
  dump_team_key(*public_data->mutable_team_key());
  protobuf_copy_message(*public_data->mutable_captain_user_key(), req.sender_user_key());
  protobuf_copy_message(*public_data->mutable_configure(), req.configure());
  for (const auto& kv : req.shared_team_data()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    protobuf_copy_message((*public_data->mutable_shared_team_data())[kv.first], kv.second);
  }

  auto* member_data = public_data->add_member();
  protobuf_copy_message(*member_data->mutable_user_key(), req.sender_user_key());
  protobuf_copy_message(*member_data->mutable_user_channel(), req.sender_user_channel());
  member_data->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  *member_data->mutable_joined_timepoint() = protobuf_from_system_clock(now);
  *member_data->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now);
  for (const auto& kv : req.shared_member_data()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    protobuf_copy_message((*member_data->mutable_shared_member_data())[kv.first], kv.second);
  }

  private_data->set_team_created(true);

  rpc::dtmq::client_subscriber::update_option options;
  options.save = true;
  options.custom_data = public_data.get();
  options.private_data = private_data.get();
  auto checker = make_write_lock_checker();
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_update(ctx, options, checker, rsp_checker));
  if (ret != 0) {
    team_created_ = false;
    if (ret == PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED) {
      if (rsp_checker && rsp_checker->has_real_value()) {
        handle_lock_conflict(ctx, rsp_checker->real_value());
      } else {
        step_down();
      }
    }
    RPC_RETURN_CODE(ret);
  }

  protobuf_copy_message(*storage_.mutable_team_key(), public_data->team_key());
  protobuf_copy_message(*storage_.mutable_captain_user_key(), public_data->captain_user_key());
  protobuf_copy_message(*storage_.mutable_configure(), public_data->configure());
  *storage_.mutable_shared_team_data() = public_data->shared_team_data();
  apply_add_member(std::move(*public_data->mutable_member(0)), now);
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::add_invitation(rpc::context& ctx, const atfw::team::SSTeamRoomAddInvitationReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  const auto& invitation = req.invitation();
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  if (!user_key_equal_t()(req.sender_user_key(), invitation.inviter())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  // 邀请人必须是队伍成员且有发起邀请的权限(默认所有成员)
  auto inviter = find_member(invitation.inviter(), false);
  if (!inviter) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM);
  }
  if (inviter->member_data.role() < get_invite_role()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  if (find_member(invitee, false)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM);
  }

  auto now = atfw::util::time::time_utility::now();
  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  auto* add_data = action->mutable_add_invitation();
  auto existing = pending_invitation_by_invitee_.find(invitee);
  if (existing != pending_invitation_by_invitee_.end() && existing->second) {
    // 数据未变化时不重复写入频道事件(其他数据由team_room填充，不用参与判定)
    if (existing->second->team_source_type() == invitation.team_source_type() &&
        existing->second->team_source_data().type_url() == invitation.team_source_data().type_url() &&
        existing->second->team_source_data().value() == invitation.team_source_data().value() &&
        protobuf_to_system_clock(existing->second->expired_timepoint()) >=
            protobuf_to_system_clock(invitation.expired_timepoint()) &&
        atfw::atapp::protobuf_equal(existing->second->invitee_private_channel(),
                                    invitation.invitee_private_channel())) {
      RPC_RETURN_CODE(0);
    }

    // 已存在待处理的邀请: 保留原数据(开始时间等)，仅更新被邀请人的频道信息;
    // 请求方要求更晚的过期时间时顺延(与上方跳过条件保持一致)
    protobuf_copy_message(*add_data, *existing->second);
    protobuf_copy_message(*add_data->mutable_invitee_private_channel(), invitation.invitee_private_channel());
    if (protobuf_to_system_clock(invitation.expired_timepoint()) >
        protobuf_to_system_clock(add_data->expired_timepoint())) {
      protobuf_copy_message(*add_data->mutable_expired_timepoint(), invitation.expired_timepoint());
    }
    // 事件应用后会向被邀请人补发一次 DTeamMemberAction
  } else {
    protobuf_copy_message(*add_data, invitation);
    add_data->mutable_team_key()->set_team_id(team_id_);
    if (add_data->start_timepoint().seconds() <= 0) {
      *add_data->mutable_start_timepoint() = protobuf_from_system_clock(now);
    }
    if (add_data->expired_timepoint().seconds() <= 0) {
      *add_data->mutable_expired_timepoint() =
          protobuf_from_system_clock(now + protobuf_to_system_clock(get_teamsvr_room_cfg().invitation_expire()));
    }
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::approve_invitation(rpc::context& ctx,
                                                    const atfw::team::SSTeamRoomApproveInvitationReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 被邀请人本人(包括游客)才能接受邀请
  if (req.sender_user_key().user_id() != req.invitee().user_id() ||
      req.sender_user_key().zone_id() != req.invitee().zone_id()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  auto iter = pending_invitation_by_invitee_.find(req.invitee());
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }
  // 持有数据副本(引用计数)，避免协程挂起期间记录被其他任务移除后访问失效
  auto invitation_ptr = iter->second;
  // 有效期判定: 已过期的邀请视为不存在(由定期维护流程移除)
  auto now = atfw::util::time::time_utility::now();
  const auto& invitation = *invitation_ptr;
  if (invitation.expired_timepoint().seconds() > 0 && protobuf_to_system_clock(invitation.expired_timepoint()) <= now) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }

  if (!find_member(req.invitee(), false)) {
    rpc::context::message_holder<atfw::team::DTeamAction> add_action(ctx);
    auto* add_member = add_action->mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), req.invitee());
    protobuf_copy_message(*add_member->mutable_user_channel(), invitation.invitee_private_channel());
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    // 入队方式和额外数据从邀请里获取
    add_member->set_team_source_type(invitation.team_source_type());
    if (invitation.has_team_source_data()) {
      protobuf_copy_message(*add_member->mutable_team_source_data(), invitation.team_source_data());
    }
    // 邀请阶段邀请者没有有效的成员数据，被邀请人的版本/路由/共享数据由其本人在同意时上报
    add_member->set_client_version(req.client_version());
    add_member->set_user_router_server_id(req.user_router_server_id());
    *add_member->mutable_joined_timepoint() = protobuf_from_system_clock(now);
    *add_member->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now);
    for (const auto& kv : req.shared_member_data()) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      protobuf_copy_message((*add_member->mutable_shared_member_data())[kv.first], kv.second);
    }
    auto ret = RPC_AWAIT_CODE_RESULT(send_action(ctx, *add_action));
    if (0 != ret) {
      RPC_RETURN_CODE(ret);
    }
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  protobuf_copy_message(*action->mutable_approve_invitation(), invitation);
  action->mutable_approve_invitation()->mutable_team_key()->set_team_id(team_id_);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::reject_invitation(rpc::context& ctx,
                                                   const atfw::team::SSTeamRoomRejectInvitationReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 被邀请人本人(包括游客)可以否决发给自己的邀请;
  // 以其他成员身份否决(撤回邀请等管理操作)需要 reject_invitation_role(默认 ADMIN)
  if (req.sender_user_key().user_id() != req.invitee().user_id() ||
      req.sender_user_key().zone_id() != req.invitee().zone_id()) {
    auto operator_member = find_member(req.sender_user_key(), false);
    if (!operator_member || operator_member->member_data.role() < get_reject_invitation_role()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
    }
  }
  auto iter = pending_invitation_by_invitee_.find(req.invitee());
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }
  // 拒绝可以忽略有效期过期，视为成功即可
  const auto& invitation = *iter->second;
  if (invitation.expired_timepoint().seconds() > 0 &&
      protobuf_to_system_clock(invitation.expired_timepoint()) <= atfw::util::time::time_utility::now()) {
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  protobuf_copy_message(*action->mutable_reject_invitation(), invitation);
  action->mutable_reject_invitation()->mutable_team_key()->set_team_id(team_id_);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::add_join_request(rpc::context& ctx,
                                                  const atfw::team::SSTeamRoomAddJoinRequestReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  const auto& join_request = req.join_request();
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  if (!user_key_equal_t()(req.sender_user_key(), requester)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  if (find_member(requester, false)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM);
  }
  // 队伍不存在(从未创建)时直接拒绝，避免给不存在的队伍写入加入请求
  if (!team_created_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ROOM_NOT_FOUND);
  }
  // 默认所有非成员都可以发起加入请求，可配置为禁止外部人员申请(私人小队，仅邀请加入)
  if (!is_join_request_allowed()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  auto* add_data = action->mutable_add_join_request();
  auto existing = pending_join_request_by_requester_.find(requester);
  if (existing != pending_join_request_by_requester_.end() && existing->second) {
    // 数据未变化时不重复写入频道事件(其他数据由team_room填充，不用参与判定)
    if (existing->second->team_source_type() == join_request.team_source_type() &&
        existing->second->team_source_data().type_url() == join_request.team_source_data().type_url() &&
        existing->second->team_source_data().value() == join_request.team_source_data().value() &&
        existing->second->client_version() == join_request.client_version() &&
        existing->second->user_router_server_id() == join_request.user_router_server_id() &&
        protobuf_to_system_clock(existing->second->expired_timepoint()) >=
            protobuf_to_system_clock(join_request.expired_timepoint()) &&
        atfw::atapp::protobuf_equal(existing->second->requester_private_channel(),
                                    join_request.requester_private_channel()) &&
        team_any_data_map_equal(existing->second->member_admission_data(), join_request.member_admission_data())) {
      RPC_RETURN_CODE(0);
    }

    // 已存在待处理的加入请求: 保留原数据，仅更新申请人的频道信息;
    // 请求方要求更晚的过期时间时顺延(与上方跳过条件保持一致)
    protobuf_copy_message(*add_data, *existing->second);
    protobuf_copy_message(*add_data->mutable_requester_private_channel(), join_request.requester_private_channel());
    if (protobuf_to_system_clock(join_request.expired_timepoint()) >
        protobuf_to_system_clock(add_data->expired_timepoint())) {
      protobuf_copy_message(*add_data->mutable_expired_timepoint(), join_request.expired_timepoint());
    }
  } else {
    protobuf_copy_message(*add_data, join_request);
    add_data->mutable_team_key()->set_team_id(team_id_);
    if (add_data->expired_timepoint().seconds() <= 0) {
      *add_data->mutable_expired_timepoint() =
          protobuf_from_system_clock(atfw::util::time::time_utility::now() +
                                     protobuf_to_system_clock(get_teamsvr_room_cfg().join_request_expire()));
    }
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::approve_join_request(rpc::context& ctx,
                                                      const atfw::team::SSTeamRoomApproveJoinRequestReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 默认所有成员都有同意他人的加入请求的权限(可由 DTeamConfigure 配置)
  auto operator_member = find_member(req.sender_user_key(), false);
  if (!operator_member || operator_member->member_data.role() < get_approve_join_request_role()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  auto iter = pending_join_request_by_requester_.find(req.applicant());
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND);
  }
  // 持有数据副本(引用计数)，避免协程挂起期间记录被其他任务移除后访问失效
  auto join_request_ptr = iter->second;
  // 有效期判定: 已过期的加入请求视为不存在(由定期维护流程移除)
  auto now = atfw::util::time::time_utility::now();
  const auto& join_request = *join_request_ptr;
  if (join_request.expired_timepoint().seconds() > 0 &&
      protobuf_to_system_clock(join_request.expired_timepoint()) <= now) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND);
  }

  if (!find_member(req.applicant(), false)) {
    rpc::context::message_holder<atfw::team::DTeamAction> add_action(ctx);
    auto* add_member = add_action->mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), join_request.requester());
    protobuf_copy_message(*add_member->mutable_user_channel(), join_request.requester_private_channel());
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    // 入队方式和额外数据从加入请求里获取
    add_member->set_team_source_type(join_request.team_source_type());
    if (join_request.has_team_source_data()) {
      protobuf_copy_message(*add_member->mutable_team_source_data(), join_request.team_source_data());
    }
    add_member->set_client_version(join_request.client_version());
    add_member->set_user_router_server_id(join_request.user_router_server_id());
    *add_member->mutable_joined_timepoint() = protobuf_from_system_clock(now);
    *add_member->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now);
    // 加入请求携带的 member_admission_data 包含申请人的 shared_member_data 数据
    for (const auto& kv : join_request.member_admission_data()) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      protobuf_copy_message((*add_member->mutable_shared_member_data())[kv.first], kv.second);
    }
    auto ret = RPC_AWAIT_CODE_RESULT(send_action(ctx, *add_action));
    if (0 != ret) {
      RPC_RETURN_CODE(ret);
    }
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  protobuf_copy_message(*action->mutable_approve_join_request(), join_request);
  action->mutable_approve_join_request()->mutable_team_key()->set_team_id(team_id_);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::reject_join_request(rpc::context& ctx,
                                                     const atfw::team::SSTeamRoomRejectJoinRequestReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 默认所有成员都有否决他人的加入请求的权限(可由 DTeamConfigure 配置)
  auto operator_member = find_member(req.sender_user_key(), false);
  if (!operator_member || operator_member->member_data.role() < get_approve_join_request_role()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  auto iter = pending_join_request_by_requester_.find(req.applicant());
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND);
  }
  // 拒绝可以忽略有效期过期，视为成功即可
  const auto& join_request = *iter->second;
  if (join_request.expired_timepoint().seconds() > 0 &&
      protobuf_to_system_clock(join_request.expired_timepoint()) <= atfw::util::time::time_utility::now()) {
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  protobuf_copy_message(*action->mutable_reject_join_request(), join_request);
  action->mutable_reject_join_request()->mutable_team_key()->set_team_id(team_id_);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

bool team_room::restore_snapshot(rpc::context& ctx) {
  // 先完整验证 custom_data/private_data，再修改本地状态。损坏快照不能进入可写主控状态。
  const auto& custom_data = subscriber_->get_custom_data_content();
  rpc::context::message_holder<atfw::team::DTeamStorage> public_data(ctx);
  if (!custom_data.type_url().empty() && !custom_data.UnpackTo(&(*public_data))) {
    FCTXLOGERROR(ctx, "team room {} unpack custom_data failed", team_id_);
    return false;
  }

  const auto& private_data = subscriber_->get_private_data_content();
  rpc::context::message_holder<atfw::team::DTeamRoomPrivateData> private_storage(ctx);
  if (!private_data.type_url().empty() && !private_data.UnpackTo(&(*private_storage))) {
    FCTXLOGERROR(ctx, "team room {} unpack private_data failed", team_id_);
    return false;
  }

  restore_timepoint_ = atfw::util::time::time_utility::now();
  // 快照覆盖与增量回放期间不发送个人频道副作用；回放结束后再按 DTMQ 当前锁恢复主控状态。
  lock_acquired_ = false;

  // 从 custom_data 恢复所有成员可见的队伍状态。
  protobuf_copy_message(*storage_.mutable_team_key(), public_data->team_key());
  storage_.mutable_team_key()->set_team_id(team_id_);
  protobuf_copy_message(*storage_.mutable_captain_user_key(), public_data->captain_user_key());
  protobuf_copy_message(*storage_.mutable_configure(), public_data->configure());
  storage_.set_acknowledge_action_sequence(public_data->acknowledge_action_sequence());
  storage_.set_acknowledge_action_hash_code(public_data->acknowledge_action_hash_code());
  storage_.set_saved_action_sequence(public_data->saved_action_sequence());
  team_created_ = private_storage->team_created() || public_data->saved_action_sequence() > 0 ||
                  !public_data->member().empty() || public_data->has_captain_user_key();

  if (subscriber_->is_destroyed()) {
    destroyed_ = true;
  }
  last_compact_sequence_ = private_storage->last_compact_sequence();
  last_compact_timepoint_ = protobuf_to_system_clock(private_storage->last_compact_timepoint());

  private_team_data_.clear();
  for (const auto& data : private_storage->private_team_data()) {
    protobuf_copy_message(private_team_data_[data.first], data.second);
  }

  std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_member_key;
  foreach_member([&expired_member_key](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    expired_member_key.insert(member->member_data.user_key());
    return true;
  });

  std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> snapshot_member_keys;
  std::vector<const atfw::team::DTeamMember * ATFW_UTIL_MACRO_NONNULL> members_by_visit_time;
  members_by_visit_time.reserve(static_cast<size_t>(public_data->member().size()));
  for (const auto& data : public_data->member()) {
    if (data.user_key().zone_id() == 0 || data.user_key().user_id() == 0 ||
        !snapshot_member_keys.insert(data.user_key()).second) {
      continue;
    }
    members_by_visit_time.push_back(&data);
  }
  // 按访问时间排序，确保回复快照时，LRU map前面的member先过期
  std::sort(members_by_visit_time.begin(), members_by_visit_time.end(),
            [](const atfw::team::DTeamMember* ATFW_UTIL_MACRO_NONNULL l,
               const atfw::team::DTeamMember* ATFW_UTIL_MACRO_NONNULL r) {
              auto lv = (std::max)(protobuf_to_system_clock(l->joined_timepoint()),
                                   protobuf_to_system_clock(l->last_heartbeat_timepoint()));
              auto rv = (std::max)(protobuf_to_system_clock(r->joined_timepoint()),
                                   protobuf_to_system_clock(r->last_heartbeat_timepoint()));
              return lv < rv || (lv == rv && user_key_less_t()(l->user_key(), r->user_key()));
            });
  bool member_restore_succeeded = true;
  for (const auto* data_ptr : members_by_visit_time) {
    const auto& data = *data_ptr;
    auto member = find_member(data.user_key(), false);
    if (!member) {
      member = mutable_member(data.user_key());
    }
    if (!member) {
      FCTXLOGERROR(ctx, "team room {} restore_snapshot member {} but allocate failed", team_id_,
                   data.user_key().user_id());
      member_restore_succeeded = false;
      continue;
    }
    expired_member_key.erase(member->member_data.user_key());

    member->user_router_server_id = data.user_router_server_id();
    member->last_heartbeat_timepoint = protobuf_to_system_clock(data.last_heartbeat_timepoint());
    protobuf_copy_message(member->member_data, data);
    member->member_data.set_user_router_server_id(member->user_router_server_id);
    *member->member_data.mutable_last_heartbeat_timepoint() =
        protobuf_from_system_clock(member->last_heartbeat_timepoint);
  }
  if (!member_restore_succeeded) {
    return false;
  }

  for (const auto& removed_key : expired_member_key) {
    remove_member(ctx, removed_key, atfw::team::EN_TEAM_EXIT_REASON_DEFAULT, false);
  }
  // 加载快照时重置重试删除队列
  member_retry_remove_.clear();

  // invitation
  expired_member_key.clear();
  for (const auto& invitation : pending_invitation_by_invitee_) {
    expired_member_key.insert(invitation.first);
  }
  for (const auto& invitation : public_data->pending_invitation()) {
    if (invitation.invitee().user_id() == 0 || invitation.invitee().zone_id() == 0) {
      continue;
    }

    expired_member_key.erase(invitation.invitee());
    auto& data_ptr = pending_invitation_by_invitee_[invitation.invitee()];
    if (!data_ptr) {
      data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamInvitation>();
    }
    protobuf_copy_message(*data_ptr, invitation);
  }
  for (const auto& removed_key : expired_member_key) {
    pending_invitation_by_invitee_.erase(removed_key);
  }

  // join_request
  expired_member_key.clear();
  for (const auto& invitation : pending_join_request_by_requester_) {
    expired_member_key.insert(invitation.first);
  }
  for (const auto& join_request : public_data->pending_join_request()) {
    if (join_request.requester().user_id() == 0 || join_request.requester().zone_id() == 0) {
      continue;
    }

    expired_member_key.erase(join_request.requester());
    auto& data_ptr = pending_join_request_by_requester_[join_request.requester()];
    if (!data_ptr) {
      data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamJoinRequest>();
    }
    protobuf_copy_message(*data_ptr, join_request);
  }
  for (const auto& removed_key : expired_member_key) {
    pending_join_request_by_requester_.erase(removed_key);
  }

  // shared team data
  *storage_.mutable_shared_team_data() = public_data->shared_team_data();

  change_captain(public_data->captain_user_key());

  // 回放压缩点之后的增量日志
  rpc::dtmq::client_subscriber::query_options options;
  options.start_sequence = last_compact_sequence_ + 1;
  bool replay_succeeded = true;
  subscriber_->query_cached_message(
      ctx,
      [this, &ctx, &replay_succeeded](const ::atfw::dtmq::DChannelMessage& message) {
        if (message.sequence() <= last_compact_sequence_) {
          return true;
        }
        replay_succeeded = apply_event_message(ctx, message);
        return replay_succeeded;
      },
      options);
  if (!replay_succeeded) {
    return false;
  }

  // 接管当前乐观锁状态
  current_lock_ = subscriber_->get_lock();
  lock_acquired_ = !current_lock_.lock_holder().empty() && current_lock_.lock_holder() == lock_holder_;

  // 恢复最早的未压缩日志时间点(用于按时间维度压缩的调度与触发)
  refresh_oldest_log_timepoint(ctx);

  if (lock_acquired_) {
    next_renew_lock_timepoint_ = restore_timepoint_ + get_lock_renew_interval();
    if (storage_.captain_user_key().user_id() == 0 && !member_.empty()) {
      team_room_manager::me()->mark_room_pending_flush(*this);
    }
  }

  // 加载snapshot之后重置一下用于延迟删除的空房间计时器，避免在恢复快照后立即触发删除
  refresh_empty_tracking(restore_timepoint_);
  snapshot_restored_ = true;
  schedule_next_timer();
  return true;
}

bool team_room::apply_event_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& message) {
  if (message.sequence() <= last_compact_sequence_) {
    return true;
  }

  const auto& detail = message.detail();
  if (detail.has_event() && !detail.event().type_url().empty()) {
    const auto& event = detail.event();
    if (event.Is<atfw::team::DTeamAction>()) {
      rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
      if (event.UnpackTo(&(*action))) {
        apply_action(ctx, *action, message.sequence(), message.hash_code(),
                     protobuf_to_system_clock(message.create_timepoint()));
      } else {
        FCTXLOGERROR(ctx, "team room {} unpack DTeamAction failed, got type_url: {}", team_id_, event.type_url());
        return false;
      }
    }
  } else if (detail.has_destroy()) {
    destroyed_ = true;
  }

  // 事件按序到达且 sequence 只保证递增不保证连续: 压缩点之后的首条事件即最早的未压缩日志
  if (oldest_log_timepoint_ == std::chrono::system_clock::from_time_t(0) &&
      message.sequence() > last_compact_sequence_) {
    oldest_log_timepoint_ = protobuf_to_system_clock(message.create_timepoint());
  }
  update_acknowledge(message.sequence(), message.hash_code());
  return true;
}

void team_room::update_acknowledge(int64_t sequence, uint64_t hash_code) {
  if (sequence > storage_.acknowledge_action_sequence()) {
    storage_.set_acknowledge_action_sequence(sequence);
    storage_.set_acknowledge_action_hash_code(hash_code);
  }
}

void team_room::apply_action(rpc::context& ctx, atfw::team::DTeamAction& action, int64_t sequence, uint64_t hash_code,
                             std::chrono::system_clock::time_point event_timepoint) {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam:
      destroyed_ = true;
      break;
    case atfw::team::DTeamAction::kAddMember:
      apply_add_member(std::move(*action.mutable_add_member()), event_timepoint);
      break;
    case atfw::team::DTeamAction::kRemoveMember:
      remove_member(ctx, action.remove_member().user_key(), action.remove_member().remove_member_reason(), true);
      break;
    case atfw::team::DTeamAction::kMemberUpdate:
      apply_member_update(action.member_update());
      break;
    case atfw::team::DTeamAction::kTeamUpdate:
      apply_team_update(action.team_update());
      break;
    case atfw::team::DTeamAction::kElectionCaptain:
      change_captain(action.election_captain().user_key());
      break;
    case atfw::team::DTeamAction::kAddInvitation:
      apply_add_invitation(action.add_invitation());
      break;
    case atfw::team::DTeamAction::kApproveInvitation:
      apply_approve_invitation(action.approve_invitation());
      break;
    case atfw::team::DTeamAction::kRejectInvitation:
      apply_reject_invitation(action.reject_invitation());
      break;
    case atfw::team::DTeamAction::kAddJoinRequest:
      apply_add_join_request(action.add_join_request());
      break;
    case atfw::team::DTeamAction::kApproveJoinRequest:
      apply_approve_join_request(action.approve_join_request());
      break;
    case atfw::team::DTeamAction::kRejectJoinRequest:
      apply_reject_join_request(action.reject_join_request());
      break;
    default:
      break;
  }
  update_acknowledge(sequence, hash_code);
}

void team_room::apply_member_update(const atfw::team::DTeamMemberUpdateData& update_data) {
  // 只有成员自己发起的更新(携带 user_router_server_id)才刷新 LRU 访问位置，
  // 管理员更新他人数据不应影响离线淘汰顺序
  auto member = find_member(update_data.user_key(), false);
  if (!member) {
    return;
  }

  if (!update_data.client_version().empty()) {
    member->member_data.set_client_version(update_data.client_version());
  }

  // 正常用户切换节点后会重新心跳更新自己的user_router_server_id。
  // update协议可能被管理员或队长发起，修改其他人的数据。
  // 此时他人的user_router_server_id是不可信的，所以置0。
  // 如果是自己更新自己的数据，总是可以更新 user_router_server_id。
  if (update_data.user_router_server_id() != 0) {
    member->member_data.set_user_router_server_id(update_data.user_router_server_id());
  }

  // 更新私有频道
  if (update_data.has_user_channel()) {
    protobuf_copy_message(*member->member_data.mutable_user_channel(), update_data.user_channel());
  }

  for (const auto& kv : update_data.shared_member_data()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    protobuf_copy_message((*member->member_data.mutable_shared_member_data())[kv.first], kv.second);
  }
}

void team_room::apply_team_update(const atfw::team::DTeamUpdateData& update_data) {
  if (update_data.has_configure()) {
    protobuf_copy_message(*storage_.mutable_configure(), update_data.configure());
  }

  for (const auto& kv : update_data.shared_team_data()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    protobuf_copy_message((*storage_.mutable_shared_team_data())[kv.first], kv.second);
  }
}

void team_room::apply_add_invitation(const atfw::team::DTeamInvitation& invitation) {
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    return;
  }
  // 被邀请人已在队伍中则忽略该邀请
  if (find_member(invitee, false)) {
    return;
  }

  auto& data_ptr = pending_invitation_by_invitee_[invitee];
  if (!data_ptr) {
    data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamInvitation>();
  }
  protobuf_copy_message(*data_ptr, invitation);

  // 推送被邀请人: 仅携带 PUBLIC 权限的队伍数据和所有成员的 PUBLIC 权限数据
  atfw::team::DTeamMemberAction notify_action;
  auto* invited = notify_action.mutable_invited();
  protobuf_copy_message(*invited, invitation);
  invited->clear_team_admission_data();
  invited->clear_member_admission_data();
  copy_public_permission_data(storage_.shared_team_data(), invited->mutable_team_admission_data());
  foreach_member([invited](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    auto* member_data = invited->add_member_admission_data();
    protobuf_copy_message(*member_data->mutable_user_key(), member->member_data.user_key());
    copy_public_permission_data(member->member_data.shared_member_data(), member_data->mutable_member_admission_data());
    return true;
  });

  atfw::dtmq::DChannelIdKey channel_id = invitation.invitee_private_channel();
  append_team_member_channel_notification(invitee, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_approve_invitation(const atfw::team::DTeamInvitation& invitation) {
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    return;
  }
  // 幂等: 邀请不存在(已处理或已过期)则忽略。
  // 成员由 approve_invitation 流程先写入的 add_member 事件添加，这里移除邀请并通知。
  auto iter = pending_invitation_by_invitee_.find(invitee);
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时被邀请人的成员数据可能尚未建立)
  auto record = iter->second;
  pending_invitation_by_invitee_.erase(iter);

  // 通知被邀请人已入队(除新增邀请外的事件不携带 admission 数据)
  atfw::team::DTeamMemberAction notify_action;
  auto* joined = notify_action.mutable_joined_team();
  protobuf_copy_message(*joined, invitation);
  joined->clear_team_admission_data();
  joined->clear_member_admission_data();
  atfw::dtmq::DChannelIdKey channel_id = record->invitee_private_channel();
  append_team_member_channel_notification(invitee, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_reject_invitation(const atfw::team::DTeamInvitation& invitation) {
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    return;
  }
  // 幂等: 邀请不存在(已处理或已过期)则忽略
  auto iter = pending_invitation_by_invitee_.find(invitee);
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时被邀请人不是成员，没有成员数据)
  auto record = iter->second;
  pending_invitation_by_invitee_.erase(iter);

  // 通知被邀请人邀请已否决(作为其操作回执; 邀请人是队伍成员，可通过队伍频道日志感知)
  atfw::team::DTeamMemberAction notify_action;
  auto* rejected = notify_action.mutable_reject_invitation();
  protobuf_copy_message(*rejected, invitation);
  rejected->clear_team_admission_data();
  rejected->clear_member_admission_data();
  atfw::dtmq::DChannelIdKey channel_id = record->invitee_private_channel();
  append_team_member_channel_notification(invitee, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_add_join_request(const atfw::team::DTeamJoinRequest& join_request) {
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    return;
  }
  // 申请人已在队伍中则忽略该请求
  if (find_member(requester, false)) {
    return;
  }

  auto& data_ptr = pending_join_request_by_requester_[requester];
  if (!data_ptr) {
    data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamJoinRequest>();
  }
  protobuf_copy_message(*data_ptr, join_request);
  // 不需要额外通知队长审批，队长会通过队伍的频道获取到数据
}

void team_room::apply_approve_join_request(const atfw::team::DTeamJoinRequest& join_request) {
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    return;
  }
  // 幂等: 加入请求不存在(已处理或已过期)则忽略。
  // 成员由 approve_join_request 流程先写入的 add_member 事件添加，这里移除申请并通知。
  auto iter = pending_join_request_by_requester_.find(requester);
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时申请人的成员数据可能尚未建立)
  auto record = iter->second;
  pending_join_request_by_requester_.erase(iter);

  // 通知申请人已入队(不携带 admission 数据)
  atfw::team::DTeamMemberAction notify_action;
  auto* joined = notify_action.mutable_joined_team();
  joined->mutable_team_key()->set_team_id(team_id_);
  protobuf_copy_message(*joined->mutable_invitee(), requester);
  protobuf_copy_message(*joined->mutable_invitee_private_channel(), record->requester_private_channel());
  joined->set_team_source_type(join_request.team_source_type());
  if (join_request.has_team_source_data()) {
    protobuf_copy_message(*joined->mutable_team_source_data(), join_request.team_source_data());
  }
  atfw::dtmq::DChannelIdKey channel_id = record->requester_private_channel();
  append_team_member_channel_notification(requester, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_reject_join_request(const atfw::team::DTeamJoinRequest& join_request) {
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    return;
  }
  // 幂等: 加入请求不存在(已处理或已过期)则忽略
  auto iter = pending_join_request_by_requester_.find(requester);
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时申请人不是成员，没有成员数据)
  auto record = iter->second;
  pending_join_request_by_requester_.erase(iter);

  // 通知申请人已被拒绝(不携带 admission 数据)
  atfw::team::DTeamMemberAction notify_action;
  auto* rejected = notify_action.mutable_reject_join_request();
  protobuf_copy_message(*rejected, join_request);
  rejected->clear_member_admission_data();
  atfw::dtmq::DChannelIdKey channel_id = record->requester_private_channel();
  append_team_member_channel_notification(requester, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_add_member(atfw::team::DTeamMember&& member_data,
                                 std::chrono::system_clock::time_point event_timepoint) {
  if (member_data.user_key().zone_id() == 0 || member_data.user_key().user_id() == 0) {
    return;
  }

  // 入队/心跳时间缺失时以频道事件创建时间兜底，保证各节点状态一致
  if (member_data.joined_timepoint().seconds() <= 0) {
    *member_data.mutable_joined_timepoint() = protobuf_from_system_clock(event_timepoint);
  }
  if (member_data.last_heartbeat_timepoint().seconds() <= 0) {
    *member_data.mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(event_timepoint);
  }

  auto member = find_member(member_data.user_key(), true);
  bool is_new_member = false;
  if (!member) {
    is_new_member = true;
    member = mutable_member(member_data.user_key());
  }
  if (!member) {
    return;
  }
  team_created_ = true;

  auto joined_timepoint = member->member_data.joined_timepoint();
  auto previous_heartbeat_timepoint = member->last_heartbeat_timepoint;
  uint64_t previous_user_router_server_id = member->user_router_server_id;
  auto member_key = member->member_data.user_key();
  member->member_data = std::move(member_data);
  // Key不允许修改
  protobuf_copy_message(*member->member_data.mutable_user_key(), member_key);
  // 重复 add_member 只保留更早的入队时间，不能因重试改变成员加入顺序。
  auto old_joined_timepoint = protobuf_to_system_clock(joined_timepoint);
  auto new_joined_timepoint = protobuf_to_system_clock(member->member_data.joined_timepoint());
  if (old_joined_timepoint > std::chrono::system_clock::from_time_t(0) && old_joined_timepoint < new_joined_timepoint) {
    protobuf_copy_message(*member->member_data.mutable_joined_timepoint(), joined_timepoint);
  }

  auto incoming_heartbeat_timepoint = protobuf_to_system_clock(member->member_data.last_heartbeat_timepoint());
  if (is_new_member) {
    member->last_heartbeat_timepoint = incoming_heartbeat_timepoint;
    member->user_router_server_id = member->member_data.user_router_server_id();
  } else if (incoming_heartbeat_timepoint > previous_heartbeat_timepoint) {
    member->last_heartbeat_timepoint = incoming_heartbeat_timepoint;
    member->user_router_server_id = member->member_data.user_router_server_id();
  } else {
    // 延迟到达的重复 add_member 不能回退运行时心跳与路由状态。
    member->last_heartbeat_timepoint = previous_heartbeat_timepoint;
    member->user_router_server_id = previous_user_router_server_id;
    *member->member_data.mutable_last_heartbeat_timepoint() =
        protobuf_from_system_clock(member->last_heartbeat_timepoint);
    member->member_data.set_user_router_server_id(member->user_router_server_id);
  }

  // 首位成员成为队长
  if (!storage_.has_captain_user_key() || 0 == storage_.captain_user_key().user_id()) {
    protobuf_copy_message(*storage_.mutable_captain_user_key(), member->member_data.user_key());
    // 队长一定是owner
    member->member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    schedule_next_timer();
  }

  if (is_new_member) {
    refresh_empty_tracking(atfw::util::time::time_utility::now());
    schedule_next_timer();
  }
}

rpc::result_code_type team_room::elect_captain_after_remove(rpc::context& ctx) {
  member_ptr_t next_captain;
  foreach_member([&next_captain](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    if (!next_captain) {
      next_captain = member;
      return true;
    }

    auto member_joined = protobuf_to_system_clock(member->member_data.joined_timepoint());
    auto current_joined = protobuf_to_system_clock(next_captain->member_data.joined_timepoint());
    if (member_joined < current_joined ||
        (member_joined == current_joined &&
         user_key_less_t()(member->member_data.user_key(), next_captain->member_data.user_key()))) {
      next_captain = member;
    }
    return true;
  });
  if (!next_captain) {
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  auto* election_captain = action->mutable_election_captain();
  protobuf_copy_message(*election_captain, next_captain->member_data);
  election_captain->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action, true)));
}

std::chrono::system_clock::time_point team_room::get_member_offline_deadline(const member_runtime_data& member_data) {
  std::chrono::system_clock::time_point baseline = restore_timepoint_;
  auto member_baseline = protobuf_to_system_clock(member_data.member_data.joined_timepoint());
  if (member_baseline > baseline) {
    baseline = member_baseline;
  }

  if (member_data.last_heartbeat_timepoint > baseline) {
    baseline = member_data.last_heartbeat_timepoint;
  }

  return baseline + protobuf_to_system_clock(get_teamsvr_room_cfg().member_offline_expire());
}

void team_room::refresh_empty_tracking(std::chrono::system_clock::time_point now) {
  if (member_.empty()) {
    if (empty_since_timepoint_ == std::chrono::system_clock::from_time_t(0)) {
      empty_since_timepoint_ = now;
    }
  } else {
    empty_since_timepoint_ = std::chrono::system_clock::from_time_t(0);
  }
}

team_room::member_ptr_t team_room::mutable_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  // mutable接口总是由有效的成员调用，所以总是可以刷新LRU的最近访问时间
  member_ptr_t ret = find_member(user_key, true);
  if (ret) {
    return ret;
  }

  // 递归期间增加的member
  if (iterating_member_protect_ != nullptr) {
    auto pending_iter = iterating_member_protect_->pending_to_add.find(user_key);
    if (pending_iter != iterating_member_protect_->pending_to_add.end()) {
      return pending_iter->second;
    }
  }

  ret = atfw::component::memory::stl::make_strong_rc<member_runtime_data>();
  if (!ret) {
    return ret;
  }
  ret->last_heartbeat_timepoint = atfw::util::time::time_utility::now();
  *ret->member_data.mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(ret->last_heartbeat_timepoint);
  protobuf_copy_message(*ret->member_data.mutable_user_key(), user_key);

  // 防止递归期间增删member，延迟处理
  if (iterating_member_protect_ != nullptr) {
    iterating_member_protect_->pending_to_remove.erase(user_key);
    iterating_member_protect_->pending_to_add.insert({user_key, ret});
    return ret;
  }

  member_.insert_key_value(user_key, ret);

  // 收到新的新增消息，移除删除重试队列
  member_retry_remove_.erase(user_key);
  return ret;
}

team_room::member_ptr_t team_room::find_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool update_visit) {
  auto iter = member_.find(user_key, update_visit);
  if (iter == member_.end()) {
    return nullptr;
  }
  return iter->second;
}

bool team_room::remove_member(rpc::context& /*ctx*/, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                              atfw::team::EnTeamExitReason reason, bool with_notify) {
  auto iter = member_.find(user_key, false);

  // 已经收到删除消息，移除重试队列
  member_retry_remove_.erase(user_key);

  if (iter == member_.end()) {
    return false;
  }

  // 防止递归期间增删member，延迟处理
  if (iterating_member_protect_ != nullptr) {
    iterating_member_protect_->pending_to_remove.insert({user_key, iter->second});
    iterating_member_protect_->pending_to_add.erase(user_key);
    return true;
  }
  auto member_ptr = iter->second;

  member_.erase(iter);
  // 队长退出后由 flush_pending_channel_message 发送 election_captain 频道事件。
  const auto& captain = storage_.captain_user_key();
  if (captain.user_id() == user_key.user_id() && captain.zone_id() == user_key.zone_id()) {
    storage_.clear_captain_user_key();
    if (is_lock_holder()) {
      team_room_manager::me()->mark_room_pending_flush(*this);
    }
  }
  refresh_empty_tracking(atfw::util::time::time_utility::now());
  schedule_next_timer();

  // 主动通知私有频道
  if (with_notify && member_ptr && member_ptr->member_data.user_channel().channel_type() != 0 &&
      !member_ptr->member_data.user_channel().channel_id().empty()) {
    atfw::dtmq::DChannelIdKey channel_id = member_ptr->member_data.user_channel();
    atfw::team::DTeamMemberAction action;
    action.mutable_remove_member()->mutable_team_key()->set_team_id(team_id_);
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), member_ptr->member_data.user_key());
    action.mutable_remove_member()->set_remove_member_reason(reason);

    append_team_member_channel_notification(member_ptr->member_data.user_key(), std::move(channel_id),
                                            std::move(action));
  }
  return true;
}

void team_room::foreach_member(
    atfw::util::nostd::function_ref<bool(atfw::util::nostd::nonnull<const member_ptr_t>&)> fn) {
  iterating_member_protect_t current_protect_instance;
  if (iterating_member_protect_ == nullptr) {
    iterating_member_protect_ = &current_protect_instance;
  }
  for (auto iter = member_.cbegin(); iter != member_.cend(); ++iter) {
    if (!iter->second) {
      continue;
    }

    if (!fn(iter->second)) {
      break;
    }
  }

  if (iterating_member_protect_ == &current_protect_instance) {
    iterating_member_protect_ = nullptr;

    for (const auto& pair : current_protect_instance.pending_to_add) {
      member_.insert_key_value(pair.first, pair.second);
      // 收到新的新增消息，移除删除重试队列
      member_retry_remove_.erase(pair.first);
    }

    for (const auto& pair : current_protect_instance.pending_to_remove) {
      member_.erase(pair.first);
      if (storage_.captain_user_key().user_id() == pair.first.user_id() &&
          storage_.captain_user_key().zone_id() == pair.first.zone_id()) {
        storage_.clear_captain_user_key();
      }
      // 收到新的新增消息，移除删除重试队列
      member_retry_remove_.erase(pair.first);
    }

    if (!current_protect_instance.pending_to_remove.empty()) {
      if (storage_.captain_user_key().user_id() == 0 && !member_.empty() && is_lock_holder()) {
        team_room_manager::me()->mark_room_pending_flush(*this);
      }
      refresh_empty_tracking(atfw::util::time::time_utility::now());
      schedule_next_timer();
    }
  }
}

void team_room::append_team_member_channel_notification(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                                        atfw::dtmq::DChannelIdKey&& channel_id,
                                                        atfw::team::DTeamMemberAction&& action) {
  // 频道事件会被主备 room 节点同时只读订阅，只有当前主控节点负责向个人频道发送副作用通知。
  if (!is_lock_holder()) {
    return;
  }
  if (channel_id.channel_type() == 0 || channel_id.channel_id().empty()) {
    return;
  }

  if (user_key.zone_id() == 0 || user_key.user_id() == 0) {
    return;
  }

  // 队列由空变为非空时注册到 manager，由一组事件处理完后统一发送
  if (pending_member_channel_actions_.empty()) {
    team_room_manager::me()->mark_room_pending_flush(*this);
  }

  auto& pending_channel = pending_member_channel_actions_[user_key];
  pending_channel.emplace_back(std::move(channel_id), std::move(action));
}

std::chrono::system_clock::duration team_room::get_lock_lease() const {
  std::chrono::system_clock::duration ret = std::chrono::system_clock::duration::zero();
  if (subscriber_) {
    const auto& configure = subscriber_->get_configure();
    // 租约时长不低于频道配置的订阅者心跳过期淘汰时间
    ret = protobuf_to_system_clock(configure.subscriber_timeout());
    if (ret <= std::chrono::system_clock::duration::zero()) {
      // 配置未就绪时回退到与 normalize_dtmq_channel_configure 一致的推导: 2*心跳+重试
      ret = protobuf_to_system_clock(configure.heartbeat_interval()) +
            protobuf_to_system_clock(configure.heartbeat_interval()) +
            protobuf_to_system_clock(configure.heartbeat_retry_interval());
    }
  }
  if (ret <= std::chrono::system_clock::duration::zero()) {
    ret = std::chrono::seconds{60};
  }
  return ret;
}

std::chrono::system_clock::duration team_room::get_lock_renew_interval() const {
  auto ret = get_lock_lease() / 2;
  if (ret <= std::chrono::seconds{1}) {
    ret = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds{1});
  }

  return ret;
}

int64_t team_room::get_gc_log_count() const {
  if (subscriber_) {
    const auto& configure = subscriber_->get_configure();
    if (configure.gc_log_count() > 0) {
      return configure.gc_log_count();
    }
  }
  // 与 normalize_dtmq_channel_configure 默认值一致
  return 30;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::chrono::system_clock::duration team_room::get_compact_log_keep_time() const {
  const auto& cfg = get_teamsvr_room_cfg();
  auto start_time = protobuf_to_system_clock(cfg.compact_log_start_time());
  auto keep_time = protobuf_to_system_clock(cfg.compact_log_keep_time());
  if (keep_time <= std::chrono::system_clock::duration::zero()) {
    // 缺省取开始压缩时长的一半
    keep_time = start_time / 2;
  }
  // 保留窗口不能大于触发窗口，否则按时间维度永远无法压缩
  return (std::min)(keep_time, start_time);
}

atfw::team::EnTeamPermissionRole team_room::get_manage_member_role() const {
  auto role = storage_.configure().manage_member_role();
  if (role == atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
    return atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN;
  }
  return role;
}

atfw::team::EnTeamPermissionRole team_room::get_approve_join_request_role() const {
  auto role = storage_.configure().approve_join_request_role();
  if (role == atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
  }
  return role;
}

atfw::team::EnTeamPermissionRole team_room::get_invite_role() const {
  auto role = storage_.configure().invite_role();
  if (role == atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
  }
  return role;
}

atfw::team::EnTeamPermissionRole team_room::get_update_team_data_role() const {
  auto role = storage_.configure().update_team_data_role();
  if (role == atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
    return atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
  }
  return role;
}

atfw::team::EnTeamPermissionRole team_room::get_reject_invitation_role() const {
  auto role = storage_.configure().reject_invitation_role();
  if (role == atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
    return atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN;
  }
  return role;
}

bool team_room::is_join_request_allowed() const { return !storage_.configure().disable_join_request(); }

void team_room::touch_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) { find_member(user_key, true); }

rpc::result_code_type team_room::check_action_permission(const PROJECT_NAMESPACE_ID::DUserIDKey& operator_key,
                                                         const atfw::team::DTeamAction& action) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }

  auto is_self = [&operator_key](const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
    return operator_key.user_id() == user_key.user_id() && operator_key.zone_id() == user_key.zone_id();
  };
  // 要求操作者是队伍成员且角色不低于 role_limit
  auto require_role = [this, &operator_key](atfw::team::EnTeamPermissionRole role_limit) -> int32_t {
    auto operator_member = find_member(operator_key, false);
    if (!operator_member) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM;
    }
    if (operator_member->member_data.role() < role_limit) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
    }
    return 0;
  };
  // 要求操作者是队伍成员(操作自己)
  auto require_member = [this, &operator_key]() -> int32_t {
    if (!find_member(operator_key, false)) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM;
    }
    return 0;
  };

  int32_t ret = 0;
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kRemoveMember:
      // 所有成员都可以删除自己(视为主动退出)，直接删除其他成员需要 manage_member_role(默认 ADMIN)
      ret = is_self(action.remove_member().user_key()) ? require_member() : require_role(get_manage_member_role());
      break;
    case atfw::team::DTeamAction::kAddMember:
      // 直接添加成员需要 manage_member_role(默认 ADMIN)，邀请/加入请求流程的入队不经由此处。
      // 已存在成员不能通过重放完整 DTeamMember 改写自身角色；也不能授予高于操作者的角色。
      ret = require_role(get_manage_member_role());
      if (ret == 0) {
        const auto& new_member = action.add_member();
        if (new_member.user_key().zone_id() == 0 || new_member.user_key().user_id() == 0) {
          ret = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
        } else if (find_member(new_member.user_key(), false)) {
          ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM;
        } else {
          auto operator_member = find_member(operator_key, false);
          if (!operator_member || new_member.role() > operator_member->member_data.role()) {
            ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
          }
        }
      }
      break;
    case atfw::team::DTeamAction::kMemberUpdate:
      // 所有成员都可以 member_update 自己的数据，更新他人的信息需要 manage_member_role(默认 ADMIN)
      ret = is_self(action.member_update().user_key()) ? require_member() : require_role(get_manage_member_role());
      break;
    case atfw::team::DTeamAction::kTeamUpdate:
      ret = require_role(get_update_team_data_role());
      break;
    case atfw::team::DTeamAction::kElectionCaptain:
      // 当前队长可以主动转让；其他操作者无条件修改队长固定要求 OWNER。
      ret =
          is_self(storage_.captain_user_key()) ? require_member() : require_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      if (ret == 0 && !find_member(action.election_captain().user_key(), false)) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND;
      }
      break;
    case atfw::team::DTeamAction::kDestroyTeam:
      // 解散队伍固定要求 OWNER
      ret = require_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      break;
    case atfw::team::DTeamAction::kAddInvitation:
      // 只能以本人身份发起邀请，且需要有发起邀请的权限(默认所有成员)
      if (!is_self(action.add_invitation().inviter())) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
      } else {
        ret = require_role(get_invite_role());
      }
      break;
    case atfw::team::DTeamAction::kApproveInvitation:
      // 所有人(包括游客)都可以同意发给自己的邀请
      if (!is_self(action.approve_invitation().invitee())) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
      }
      break;
    case atfw::team::DTeamAction::kRejectInvitation:
      // 所有人(包括游客)都可以否决发给自己的邀请，
      // 以其他成员身份否决(撤回邀请等管理操作)需要 reject_invitation_role(默认 ADMIN)
      if (!is_self(action.reject_invitation().invitee())) {
        ret = require_role(get_reject_invitation_role());
      }
      break;
    case atfw::team::DTeamAction::kAddJoinRequest:
      // 所有人(包括游客)都可以以本人身份发起加入请求，
      // 可配置为禁止外部人员申请(私人小队，仅邀请加入)
      if (!is_self(action.add_join_request().requester()) || !is_join_request_allowed()) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
      }
      break;
    case atfw::team::DTeamAction::kApproveJoinRequest:
    case atfw::team::DTeamAction::kRejectJoinRequest:
      // 默认所有成员都有同意和否决他人的加入请求的权限
      ret = require_role(get_approve_join_request_role());
      break;
    default:
      ret = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      break;
  }
  RPC_RETURN_CODE(ret);
}

void team_room::refresh_oldest_log_timepoint(rpc::context& ctx) {
  oldest_log_timepoint_ = std::chrono::system_clock::from_time_t(0);
  if (!subscriber_ || !subscriber_->is_ready()) {
    return;
  }

  int64_t oldest_sequence = (std::max)(last_compact_sequence_, subscriber_->get_last_removed_sequence()) + 1;
  if (oldest_sequence > subscriber_->get_last_message_sequence()) {
    return;
  }

  // sequence 只保证递增不保证连续: query_cached_message 内部按 lower_bound 二分查找
  // 第一条不早于 oldest_sequence 的日志(通常就是缓存里最早的日志)，取到一条即中断遍历
  rpc::dtmq::client_subscriber::query_options query_opts;
  query_opts.start_sequence = oldest_sequence;
  query_opts.max_count = 1;
  subscriber_->query_cached_message(
      ctx,
      [this](const ::atfw::dtmq::DChannelMessage& message) {
        oldest_log_timepoint_ = protobuf_to_system_clock(message.create_timepoint());
        return false;
      },
      query_opts);
}

int64_t team_room::pick_compact_sequence(rpc::context& ctx, std::chrono::system_clock::time_point now) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    return 0;
  }
  int64_t last_sequence = subscriber_->get_last_message_sequence();
  if (last_sequence <= last_compact_sequence_) {
    return 0;
  }

  const auto& cfg = get_teamsvr_room_cfg();
  // 数量维度保留条数: keep_percent 与 keep_count 取较大者
  int64_t keep_by_count = (std::max)(get_gc_log_count() * cfg.compact_log_keep_percent() / 100,
                                     static_cast<int64_t>(cfg.compact_log_keep_count()));
  auto keep_deadline = now - get_compact_log_keep_time();

  // sequence 只保证递增不保证连续: 未压缩日志的真实数量由订阅者缓存统计(内部二分定位起点)
  int64_t log_count = static_cast<int64_t>(subscriber_->get_cached_log_count(last_compact_sequence_ + 1));
  // 数量维度需要裁掉的日志条数(0 表示数量维度不限制)
  int64_t delete_by_count = log_count > keep_by_count ? log_count - keep_by_count : 0;

  // 遍历压缩点之后的缓存日志: 数量维度的裁剪点是第 delete_by_count 条日志，
  // 时间维度的裁剪点是最后一条超出保留窗口前的日志，两个裁剪点都确定后立即中断遍历
  int64_t visit_index = 0;
  int64_t cutoff_by_count = 0;
  int64_t cutoff_by_time = 0;
  rpc::dtmq::client_subscriber::query_options query_opts;
  query_opts.start_sequence = last_compact_sequence_ + 1;
  subscriber_->query_cached_message(
      ctx,
      [&visit_index, &cutoff_by_count, &cutoff_by_time, delete_by_count,
       keep_deadline](const ::atfw::dtmq::DChannelMessage& message) {
        ++visit_index;
        if (visit_index == delete_by_count) {
          cutoff_by_count = message.sequence();
        }
        // 日志按时间点有序，保留窗口内的日志不按时间维度裁剪
        bool in_keep_window = protobuf_to_system_clock(message.create_timepoint()) > keep_deadline;
        if (!in_keep_window) {
          cutoff_by_time = message.sequence();
        }
        // 数量维度裁剪点未到达，或时间维度裁剪点还可能推进时继续遍历
        return visit_index < delete_by_count || !in_keep_window;
      },
      query_opts);

  // 两种保留策略都要满足，取更保守(更小)的裁剪点; 某一维度不限制时取另一维度
  int64_t compact_sequence = 0;
  if (cutoff_by_count > 0 && cutoff_by_time > 0) {
    compact_sequence = (std::min)(cutoff_by_count, cutoff_by_time);
  } else {
    compact_sequence = (std::max)(cutoff_by_count, cutoff_by_time);
  }
  if (compact_sequence <= last_compact_sequence_) {
    return 0;
  }
  return compact_sequence;
}

::atfw::dtmq::DChannelOptimisticLock team_room::make_self_lock(std::chrono::system_clock::time_point now) const {
  ::atfw::dtmq::DChannelOptimisticLock ret;
  ret.set_lock_holder(lock_holder_);
  *ret.mutable_timeout() = protobuf_from_system_clock(now + get_lock_lease());
  return ret;
}

atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker> team_room::make_write_lock_checker() const {
  auto checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  // 服务端只比较 lock_holder，写入时要求持有者必须是本节点
  checker->mutable_expect_value()->set_lock_holder(lock_holder_);
  return checker;
}

rpc::result_code_type team_room::acquire_lock(rpc::context& ctx) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (subscriber_->is_destroyed()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }

  auto now = atfw::util::time::time_utility::now();
  for (int retry = 0; retry < 2 && !lock_acquired_; ++retry) {
    auto self_lock = make_self_lock(now);
    auto checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
    checker->set_allow_empty_real_value(true);
    // 新节点订阅后使用看到的老乐观锁做 CAS 切换
    *checker->mutable_expect_value() = (0 == retry ? subscriber_->get_lock() : current_lock_);
    *checker->mutable_reset_value() = self_lock;
    auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();

    auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_reset_lock(ctx, checker, rsp_checker));
    if (0 == ret) {
      current_lock_ = self_lock;
      lock_acquired_ = true;
      next_renew_lock_timepoint_ = now;
      next_renew_lock_timepoint_ += get_lock_renew_interval();
      FCTXLOGINFO(ctx, "team room {} acquire optimistic lock success", team_id_);
      break;
    }
    if (rsp_checker && rsp_checker->has_real_value()) {
      current_lock_ = rsp_checker->real_value();
      if (current_lock_.lock_holder() == lock_holder_) {
        // 已是本节点持有(可能上次设置成功但响应丢失)
        lock_acquired_ = true;
        next_renew_lock_timepoint_ = now;
        next_renew_lock_timepoint_ += get_lock_renew_interval();
        break;
      }
    }
    if (ret != PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED) {
      // 非锁冲突错误不重试
      FCTXLOGERROR(ctx, "team room {} acquire optimistic lock failed: {}", team_id_, ret);
      RPC_RETURN_CODE(ret);
    }
  }
  if (!lock_acquired_) {
    FCTXLOGERROR(ctx, "team room {} acquire optimistic lock conflict, current holder: {}", team_id_,
                 current_lock_.lock_holder());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
  }
  RPC_RETURN_CODE(0);
}

void team_room::handle_lock_conflict(rpc::context& ctx, const ::atfw::dtmq::DChannelOptimisticLock& real_lock) {
  current_lock_ = real_lock;
  if (real_lock.lock_holder() == lock_holder_) {
    // 仍是本节点持有(可能上次设置成功但响应丢失)
    lock_acquired_ = true;
    return;
  }
  FCTXLOGWARNING(ctx, "team room {} optimistic lock taken by {}, step down", team_id_, real_lock.lock_holder());
  step_down();
}

void team_room::step_down() {
  lock_acquired_ = false;
  // 退位后转为备用节点: 在锁过期后尝试接管
  schedule_next_timer();
}

rpc::result_code_type team_room::send_event_with_lock(rpc::context& ctx, ::google::protobuf::Any&& event_data,
                                                      bool no_wait) {
  if (!lock_acquired_) {
    auto lock_ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
    if (lock_ret != 0) {
      RPC_RETURN_CODE(lock_ret);
    }
  }

  auto checker = make_write_lock_checker();
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  auto ret =
      RPC_AWAIT_CODE_RESULT(subscriber_->send_event(ctx, std::move(event_data), checker, rsp_checker, true, no_wait));
  if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED == ret) {
    // 老节点锁失效: 记录真实持有者并退位
    if (rsp_checker && rsp_checker->has_real_value()) {
      handle_lock_conflict(ctx, rsp_checker->real_value());
    } else {
      step_down();
    }
  } else if (0 != ret) {
    FCTXLOGERROR(ctx, "team room {} send event failed: {}", team_id_, ret);
  }
  RPC_RETURN_CODE(ret);
}

team_room_timer_event team_room::get_next_timer_event(std::chrono::system_clock::time_point now) {
  team_room_timer_event ret;
  if (!subscriber_->is_ready() || !snapshot_restored_ || subscriber_->is_destroyed()) {
    return ret;
  }

  // 非主控节点: 在当前乐观锁过期后尝试接管(容灾切换，老节点再写入时锁自然失败)
  if (!lock_acquired_) {
    ret.type = team_room_timer_event_type::kAcquireLock;
    if (current_lock_.lock_holder().empty()) {
      ret.timeout = now;
    } else if (current_lock_.has_timeout() && current_lock_.timeout().seconds() > 0) {
      ret.timeout = protobuf_to_system_clock(current_lock_.timeout());
    } else {
      // 锁未设置超时则按租约周期定期检查
      ret.timeout = now + get_lock_lease();
    }
    return ret;
  }

  // 主控节点: 已解散队伍尽快销毁频道
  if (destroyed_) {
    if (!channel_destroy_sent_) {
      ret.type = team_room_timer_event_type::kDestroyChannel;
      ret.timeout = now;
    }
    return ret;
  }

  // 定期维护: 乐观锁续租+过期数据清理+日志压缩(不要求时间非常精确，常规维护时总会尝试压缩日志)
  const auto& cfg = get_teamsvr_room_cfg();
  ret.type = team_room_timer_event_type::kMaintenance;
  ret.timeout = next_renew_lock_timepoint_;
  // 压缩加速触发: 仅作为因日志数量/时间因素提前触发维护的加速点。
  // 两个触发条件都保证维护能推进压缩点(见 pick_compact_sequence)，不会因保留策略限制而空转:
  // 按时间维度: 最早的未压缩日志过了 compact_log_start_time 后提前触发维护
  //             (keep_time <= start_time，触发时最早的日志必然超出保留窗口)
  if (oldest_log_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    ret.timeout =
        (std::min)(ret.timeout, oldest_log_timepoint_ + protobuf_to_system_clock(cfg.compact_log_start_time()));
  }
  // 按数量维度: 未压缩日志数量(sequence 只保证递增不保证连续，必须用真实缓存数量)超过
  //             gc_log_count * compact_log_over_percent / 100 且超过压缩保留条数时立即触发维护
  {
    int64_t gc_log_count = get_gc_log_count();
    int64_t keep_by_count = (std::max)(gc_log_count * cfg.compact_log_keep_percent() / 100,
                                       static_cast<int64_t>(cfg.compact_log_keep_count()));
    int64_t count_trigger_line = (std::max)(gc_log_count * cfg.compact_log_over_percent() / 100, keep_by_count);
    if (static_cast<int64_t>(subscriber_->get_cached_log_count(last_compact_sequence_ + 1)) > count_trigger_line) {
      ret.timeout = (std::min)(ret.timeout, now);
    }
  }

  // 剔除最久未心跳的成员(LRU front)
  if (!member_.empty()) {
    const auto& oldest = member_.front();
    if (oldest.second) {
      std::chrono::system_clock::time_point deadline = get_member_offline_deadline(*oldest.second);
      if (deadline < ret.timeout) {
        ret.type = team_room_timer_event_type::kKickOfflineMember;
        ret.timeout = deadline;
      }
    }
  }
  // 移除成员的重试队列(LRU front)
  if (!member_retry_remove_.empty()) {
    const auto& oldest = member_retry_remove_.front();
    if (oldest.second) {
      std::chrono::system_clock::time_point deadline = oldest.second->next_retry_timepoint;
      if (deadline < ret.timeout) {
        ret.type = team_room_timer_event_type::kKickOfflineMember;
        ret.timeout = deadline;
      }
    }
  }

  // 空队伍保留到期后解散
  if (member_.empty() && empty_since_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    std::chrono::system_clock::time_point deadline = empty_since_timepoint_ + get_room_destroy_delay();
    if (deadline < ret.timeout) {
      ret.type = team_room_timer_event_type::kDestroyEmptyRoom;
      ret.timeout = deadline;
    }
  }

  return ret;
}

void team_room::schedule_next_timer() {
  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();

  if (subscriber_->is_destroyed() && now >= empty_since_timepoint_ + get_room_destroy_delay()) {
    // 房间即将被回收，不再调度定时器
    team_room_manager::me()->remove_room(get_team_id(), this);
    return;
  }

  auto event = get_next_timer_event(now);
  if (team_room_timer_event_type::kNone == event.type) {
    // 暂无定时事件(订阅未就绪等)，按续租周期保底重查
    event.timeout = now + get_lock_renew_interval();
  }

  // 应该要判定定时器只能提前，不能延后，避免定时器被延迟到过期事件之后才触发
  team_room_manager::me()->reset_room_timer(*this, event.timeout);
}

void team_room::on_timer(rpc::context& ctx) {
  timer_watcher_.reset();
  if (subscriber_->is_destroyed()) {
    schedule_next_timer();
    return;
  }

  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();
  auto event = get_next_timer_event(now);
  if (team_room_timer_event_type::kNone == event.type || event.timeout > now) {
    // 事件未到期，重新调度(定时器总是指向当前最近的定时 action)
    schedule_next_timer();
    return;
  }

  if (!task_type_trait::empty(maintenance_task_) && !task_type_trait::is_exiting(maintenance_task_)) {
    // 上一次定时 action 仍在执行，本次是冗余的定时器触发，直接忽略即可
    return;
  }

  auto self = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, get_timer_event_name(event.type),
      [self, event_type = event.type](rpc::context& child_ctx) mutable -> rpc::result_code_type {
        RPC_AWAIT_CODE_RESULT(self->execute_timer_event(child_ctx, event_type));

        if (!task_type_trait::empty(self->maintenance_task_) &&
            task_type_trait::get_task_id(self->maintenance_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self->maintenance_task_);
        }

        // 先发送数据，再插入定时器
        RPC_AWAIT_IGNORE_RESULT(self->flush_pending_channel_message(child_ctx));

        // 定时 action 完成后重设下一个定时器
        self->schedule_next_timer();
        RPC_RETURN_CODE(0);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "team room {} kickoff timer event {} failed", team_id_, static_cast<int32_t>(event.type));
    schedule_next_timer();
    return;
  }
  if (!task_type_trait::empty(*invoke_result.get_success()) &&
      !task_type_trait::is_exiting(*invoke_result.get_success())) {
    maintenance_task_ = std::move(*invoke_result.get_success());
  }
}

rpc::result_code_type team_room::execute_timer_event(rpc::context& ctx, team_room_timer_event_type event_type) {
  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();
  switch (event_type) {
    case team_room_timer_event_type::kAcquireLock: {
      auto ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
      if (0 != ret && PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED != ret) {
        FCTXLOGERROR(ctx, "team room {} timer acquire lock failed: {}", team_id_, ret);
      }
      break;
    }
    case team_room_timer_event_type::kMaintenance:
      RPC_AWAIT_CODE_RESULT(do_maintenance(ctx));
      break;
    case team_room_timer_event_type::kKickOfflineMember:
      RPC_AWAIT_CODE_RESULT(kick_due_offline_members(ctx, now));
      break;
    case team_room_timer_event_type::kDestroyEmptyRoom:
      RPC_AWAIT_CODE_RESULT(destroy_empty_room(ctx));
      break;
    case team_room_timer_event_type::kDestroyChannel: {
      if (subscriber_ && subscriber_->is_ready() && lock_acquired_ && !channel_destroy_sent_) {
        auto checker = make_write_lock_checker();
        auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_destroy(ctx, checker));
        if (0 == ret) {
          channel_destroy_sent_ = true;
        } else if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED == ret) {
          channel_destroy_sent_ = true;
          step_down();
        } else {
          FCTXLOGERROR(ctx, "team room {} destroy channel failed: {}", team_id_, ret);
        }
      }
      break;
    }
    default:
      break;
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::do_maintenance(rpc::context& ctx) {
  if (!subscriber_ || !subscriber_->is_ready() || !lock_acquired_) {
    RPC_RETURN_CODE(0);
  }

  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();

  // 过期数据清理(过期的邀请和加入请求，对端有自身的超时失效机制，无需通知)
  RPC_AWAIT_CODE_RESULT(cleanup_expired_admissions(ctx, now));
  if (!lock_acquired_) {
    RPC_RETURN_CODE(0);
  }

  int64_t last_sequence = subscriber_->get_last_message_sequence();

  // 最早的未压缩日志时间点缺失时刷新，用于按时间维度加速触发的调度
  if (oldest_log_timepoint_ == std::chrono::system_clock::from_time_t(0) && last_sequence > last_compact_sequence_) {
    refresh_oldest_log_timepoint(ctx);
  }

  // 每次 send_update 都尝试压缩日志以减少数据量，
  // compact_log_over_percent 和 compact_log_start_time 仅作为因日志数量/时间因素提前触发维护的加速点
  int64_t compact_sequence = pick_compact_sequence(ctx, now);

  // 一次 send_update 完成乐观锁续租(reset_value)，可压缩时同时保存快照并裁剪日志
  auto self_lock = make_self_lock(now);
  auto checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  *checker->mutable_expect_value() = current_lock_;
  *checker->mutable_reset_value() = self_lock;
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();

  rpc::dtmq::client_subscriber::update_option options;
  auto private_data = rpc::make_shared_message<atfw::team::DTeamRoomPrivateData>(ctx);
  auto public_data = rpc::make_shared_message<atfw::team::DTeamStorage>(ctx);
  if (compact_sequence > 0) {
    // 当前状态信息存入 custom_data(成员清单、加入请求和加入邀请列表)和 private_data(主控私有数据)，
    // custom_data 状态覆盖到最新日志，裁剪点之前的日志才被压缩移除
    dump_public_data(*public_data);
    public_data->set_saved_action_sequence(last_sequence);
    // private_data 必须记录本次请求成功后的压缩边界，而不是成员变量中的上一次边界。
    dump_private_data(*private_data, compact_sequence, now);
    options.save = true;
    options.compact_sequence = compact_sequence;
    options.stateful_sequence = compact_sequence;
    options.custom_data = public_data.get();
    options.private_data = private_data.get();
  }

  auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_update(ctx, options, checker, rsp_checker));
  if (0 == ret) {
    current_lock_ = self_lock;
    next_renew_lock_timepoint_ = now + get_lock_renew_interval();
    if (compact_sequence > 0) {
      storage_.set_saved_action_sequence(last_sequence);
      last_compact_sequence_ = compact_sequence;
      last_compact_timepoint_ = now;
      // 压缩点推进后，最早的未压缩日志随之变化
      refresh_oldest_log_timepoint(ctx);
    }
    RPC_RETURN_CODE(0);
  }
  if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED == ret) {
    if (rsp_checker && rsp_checker->has_real_value()) {
      handle_lock_conflict(ctx, rsp_checker->real_value());
    } else {
      step_down();
    }
    RPC_RETURN_CODE(0);
  }
  FCTXLOGERROR(ctx, "team room {} maintenance send_update failed: {}", team_id_, ret);
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type team_room::cleanup_expired_admissions(rpc::context& /*ctx*/,
                                                            std::chrono::system_clock::time_point now) {
  // 清理过期邀请
  {
    std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> expired_invitations;
    expired_invitations.reserve(pending_invitation_by_invitee_.size());
    for (const auto& invitation : pending_invitation_by_invitee_) {
      if (!invitation.second) {
        expired_invitations.push_back(invitation.first);
        continue;
      }
      if (protobuf_to_system_clock(invitation.second->expired_timepoint()) <= now) {
        expired_invitations.push_back(invitation.first);
      }
    }

    for (const auto& key : expired_invitations) {
      pending_invitation_by_invitee_.erase(key);
    }
  }

  // 清理过期加入请求
  {
    std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> expired_join_request;
    expired_join_request.reserve(pending_join_request_by_requester_.size());
    for (const auto& join_request : pending_join_request_by_requester_) {
      if (!join_request.second) {
        expired_join_request.push_back(join_request.first);
        continue;
      }
      if (protobuf_to_system_clock(join_request.second->expired_timepoint()) <= now) {
        expired_join_request.push_back(join_request.first);
      }
    }

    for (const auto& key : expired_join_request) {
      pending_join_request_by_requester_.erase(key);
    }
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::kick_due_offline_members(rpc::context& ctx,
                                                          std::chrono::system_clock::time_point now) {
  // 重试队列计时: 重发到期的移除消息
  std::vector<member_ptr_t> retry_members;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> invalid_keys;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> force_remove_keys;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> actived_retry_keys;
  retry_members.reserve(8);
  actived_retry_keys.reserve(8);
  uint32_t max_retry_times = get_teamsvr_room_cfg().member_channel_notification_retry_times();
  auto retry_interval = protobuf_to_system_clock(get_teamsvr_room_cfg().member_channel_notification_retry_interval());
  for (const auto& pair : member_retry_remove_) {
    if (!pair.second) {
      invalid_keys.push_back(pair.first);
      continue;
    }

    // 重试时间未到
    if (pair.second->next_retry_timepoint > now) {
      break;
    }

    // 需要更新访问位置
    auto member_iter = member_.find(pair.first);
    if (member_iter == member_.end() || !member_iter->second) {
      invalid_keys.push_back(pair.first);
      continue;
    }

    // 超过重试上限，移除
    if (pair.second->retry_times >= max_retry_times) {
      invalid_keys.push_back(pair.first);
      force_remove_keys.push_back(pair.first);
      FCTXLOGWARNING(ctx, "team_room {} retry to remove member {}:{} but retry time exceeded", get_team_id(),
                     pair.first.zone_id(), pair.first.user_id());
      continue;
    }

    retry_members.push_back(member_iter->second);
    actived_retry_keys.push_back(pair.first);
  }

  for (const auto& key : actived_retry_keys) {
    auto iter = member_retry_remove_.find(key);
    if (iter == member_retry_remove_.end()) {
      continue;
    }

    if (!iter->second) {
      invalid_keys.push_back(key);
      continue;
    }

    ++iter->second->retry_times;
    iter->second->next_retry_timepoint = now + retry_interval;
  }

  for (const auto& key : invalid_keys) {
    member_retry_remove_.erase(key);
  }

  // 故障强制移除
  for (const auto& key : force_remove_keys) {
    remove_member(ctx, key, atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED, true);
  }

  // 重发到期的移除消息(成员已在重试队列中，绕过 send_action 的去重直接发送;
  // 移除消息是无条件的且有重试机制，no_wait 避免阻塞)
  for (const auto& user_ptr : retry_members) {
    rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
    *action->mutable_remove_member()->mutable_user_key() = user_ptr->member_data.user_key();
    action->mutable_remove_member()->set_remove_member_reason(user_ptr->exit_reason);
    RPC_AWAIT_IGNORE_RESULT(do_send_action(ctx, *action, true));
  }

  // LRU front 为最久未心跳的成员，队伍规模小，全量扫描收集所有到期成员
  std::vector<member_ptr_t> offline_members;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> touch_keys;
  offline_members.reserve(8);
  touch_keys.reserve(member_retry_remove_.size());
  invalid_keys.clear();
  for (const auto& pair : member_) {
    if (member_retry_remove_.end() != member_retry_remove_.find(pair.first)) {
      // 正在重试删除的成员走重试队列
      touch_keys.push_back(pair.first);
      continue;
    }

    if (!pair.second) {
      invalid_keys.push_back(pair.first);
      continue;
    }

    if (get_member_offline_deadline(*pair.second) > now) {
      break;
    }

    offline_members.push_back(pair.second);
  }

  for (const auto& key : invalid_keys) {
    member_.erase(key);
  }

  // 刷新member_里额外需要更新的访问位置
  for (const auto& key : touch_keys) {
    member_.find(key);
  }

  // 发送remove_member消息到team channel频道, send_action 会记录移除原因并加入重试队列
  // (移除消息是无条件的且有重试机制，no_wait 避免阻塞)
  for (const auto& user_ptr : offline_members) {
    rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
    *action->mutable_remove_member()->mutable_user_key() = user_ptr->member_data.user_key();
    action->mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED);
    RPC_AWAIT_IGNORE_RESULT(send_action(ctx, *action, true));
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::destroy_empty_room(rpc::context& ctx) {
  if (destroyed_ || !member_.empty()) {
    RPC_RETURN_CODE(0);
  }
  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  action->mutable_destroy_team()->set_team_id(team_id_);
  auto ret = RPC_AWAIT_CODE_RESULT(send_action(ctx, *action));
  if (0 != ret && PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED != ret) {
    FCTXLOGERROR(ctx, "team room {} destroy empty team failed: {}", team_id_, ret);
  }
  RPC_RETURN_CODE(0);
}

void team_room::dump_private_data(atfw::team::DTeamRoomPrivateData& output, int64_t compact_sequence,
                                  std::chrono::system_clock::time_point compact_timepoint) {
  output.set_last_compact_sequence(compact_sequence);
  *output.mutable_last_compact_timepoint() = protobuf_from_system_clock(compact_timepoint);
  output.set_team_created(team_created_);

  for (const auto& data : private_team_data_) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    protobuf_copy_message((*output.mutable_private_team_data())[data.first], data.second);
  }
}

void team_room::dump_public_data(atfw::team::DTeamStorage& output) {
  protobuf_copy_message(output, storage_);
  dump_team_key(*output.mutable_team_key());

  // dump成员数据
  foreach_member([&output](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    protobuf_copy_message(*output.add_member(), member->member_data);
    return true;
  });

  // dump 邀请数据
  for (const auto& invitation : pending_invitation_by_invitee_) {
    if (!invitation.second) {
      continue;
    }

    protobuf_copy_message(*output.add_pending_invitation(), *invitation.second);
  }

  // dump 加入请求数据
  for (const auto& join_request : pending_join_request_by_requester_) {
    if (!join_request.second) {
      continue;
    }

    protobuf_copy_message(*output.add_pending_join_request(), *join_request.second);
  }
}

void team_room::on_receive_snapshot_finished(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                             int32_t result_code) {
  if (subscriber != subscriber_) {
    return;
  }
  if (result_code < 0) {
    FCTXLOGERROR(ctx, "team room {} receive subscriber snapshot failed: {}({})", team_id_, result_code,
                 protobuf_mini_dumper_get_error_msg(result_code));
    return;
  }
  if (!restore_snapshot(ctx)) {
    FCTXLOGERROR(ctx, "team room {} restore subscriber snapshot failed", team_id_);
  }
  // 就绪后重设定时器: 非主控节点将由定时器驱动接管乐观锁成为主控节点
  schedule_next_timer();
}

void team_room::on_receive_event(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                 const ::atfw::dtmq::DChannelMessage& message) {
  if (subscriber != subscriber_) {
    return;
  }
  if (!apply_event_message(ctx, message)) {
    // 保留已成功加载的快照状态，后续仍可通过 snapshot_finished 覆盖恢复。
    step_down();
  }
}

void team_room::on_update_optimistic_lock(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                          const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                          ATFW_EXPLICIT_UNUSED_ATTR const ::atfw::dtmq::DChannelOptimisticLock& from,
                                          const ::atfw::dtmq::DChannelOptimisticLock& to) {
  if (subscriber != subscriber_) {
    return;
  }
  current_lock_ = to;
  if (to.lock_holder() == lock_holder_) {
    lock_acquired_ = true;
    next_renew_lock_timepoint_ = atfw::util::time::time_utility::now() + get_lock_renew_interval();
    if (storage_.captain_user_key().user_id() == 0 && !member_.empty()) {
      team_room_manager::me()->mark_room_pending_flush(*this);
    }
  } else if (lock_acquired_) {
    // 锁已被其他节点接管，本节点退位
    lock_acquired_ = false;
  }
  // 锁状态或过期时间变化，重算下一个定时器事件
  schedule_next_timer();
}

void team_room::on_destroyed(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                             const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
  if (subscriber != subscriber_) {
    return;
  }
  destroyed_ = true;
  lock_acquired_ = false;
  // 可能需要重置定时器
  schedule_next_timer();
}

std::chrono::system_clock::duration team_room::get_room_destroy_delay() noexcept {
  std::chrono::system_clock::duration timeout =
      protobuf_to_system_clock(get_teamsvr_room_cfg().empty_room_destroy_delay());
  if (timeout < std::chrono::seconds{1}) {
    timeout = std::chrono::seconds{5};
  }
  return timeout;
}

void team_room::change_captain(const PROJECT_NAMESPACE_ID::DUserIDKey& new_captain_key) {
  auto old_captain = find_member(storage_.captain_user_key(), false);
  auto new_captain = find_member(new_captain_key, false);
  if (old_captain == new_captain) {
    return;
  }

  if (!new_captain) {
    return;
  }

  protobuf_copy_message(*storage_.mutable_captain_user_key(), new_captain->member_data.user_key());
  new_captain->member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  if (old_captain) {
    old_captain->member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  }
}

rpc::result_code_type team_room::flush_pending_channel_message(rpc::context& ctx) {
  // 队列即将被清空(或本已为空)，从 manager 注册表注销，避免遗留无意义记录
  team_room_manager::me()->unmark_room_pending_flush(*this);

  int32_t result = 0;
  if (is_lock_holder() && storage_.captain_user_key().user_id() == 0 && !member_.empty()) {
    // 失败了没关系，下次会再重试的
    RPC_AWAIT_IGNORE_RESULT(elect_captain_after_remove(ctx));
  }

  if (pending_member_channel_actions_.empty()) {
    RPC_RETURN_CODE(result);
  }

  pending_team_member_channel_t pending_member_channel_actions;
  pending_member_channel_actions.swap(pending_member_channel_actions_);

  for (auto& pending_channel : pending_member_channel_actions) {
    for (const auto& pending_message : pending_channel.second) {
      auto ret = RPC_AWAIT_CODE_RESULT(send_member_action(ctx, pending_message.first, pending_message.second));
      if (0 != ret) {
        FCTXLOGERROR(ctx, "team room {} send member action to {}:{} failed: {}({})", team_id_,
                     pending_message.first.channel_type(), pending_message.first.channel_id(), ret,
                     protobuf_mini_dumper_get_error_msg(ret));
      }
    }
  }

  RPC_RETURN_CODE(result);
}

void team_room::dump_team_key(atfw::team::DTeamKey& output) const { output.set_team_id(team_id_); }

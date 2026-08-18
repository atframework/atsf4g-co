// Copyright 2026 atframework

#include "logic/room/team_room.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <string/string_format.h>
#include <time/time_utility.h>

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

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "logic/room/team_room_manager.h"

namespace {
const atframework::team::config::teamsvr_room_cfg& get_teamsvr_room_cfg() noexcept {
  return logic_config::me()->get_server_instance_config<atframework::team::config::teamsvr_room_cfg>();
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
  return reinterpret_cast<team_room*>(local_private_data[0]);
}

rpc::dtmq::client_subscriber::event_callback_set_ptr_t build_shared_team_room_channel_event_callback_set() {
  rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
      rpc::dtmq::client_subscriber::create_event_callback_set();

  rpc::dtmq::client_subscriber::set_event_callback_on_ready(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_ready");
        if (room != nullptr) {
          room->on_ready(ctx, subscriber);
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

team_room::team_room(int64_t team_id, std::string&& subscriber_key, std::string&& lock_holder)
    : team_id_(team_id), subscriber_key_(std::move(subscriber_key)), lock_holder_(std::move(lock_holder)) {
  channel_key_.set_channel_type(atframework::team::EN_TEAM_CHANNEL_TYPE_TEAM_ROOM);
  channel_key_.set_channel_id(atfw::util::string::format("{}", team_id_));
  storage_.mutable_team_key()->set_team_id(team_id_);
}

int32_t team_room::create(rpc::context& ctx) {
  if (subscriber_) {
    return 0;
  }

  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(this)};
  rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
  subscribe_options.with_private_data = true;
  subscribe_options.event_callback_set = get_shared_team_room_channel_event_callback_set();
  subscriber_ = rpc::dtmq::client_subscriber::create(channel_key_, subscribe_options);
  if (!subscriber_) {
    FWLOGERROR("team_room create subscriber of channel {}:{} failed, maybe configure is missing",
               channel_key_.channel_type(), channel_key_.channel_id());
    return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND;
  }
  subscriber_->set_local_private_data(local_private_data);
  if (!subscriber_->get_shared_event_callback_set()) {
    subscriber_->set_shared_event_callback_set(get_shared_team_room_channel_event_callback_set());
  }

  // 已就绪(共享层复用)则不会再触发 on_ready，直接恢复快照
  if (subscriber_->is_ready()) {
    restore_snapshot(ctx, subscriber_);
  }
  // 每个房间有且只有一个定时器，创建后即开始调度
  schedule_next_timer();
  return 0;
}

int64_t team_room::get_team_id() const noexcept { return team_id_; }

const atfw::dtmq::DChannelIdKey& team_room::get_channel_key() const noexcept { return channel_key_; }

bool team_room::is_subscriber_ready() const noexcept { return subscriber_ && subscriber_->is_ready(); }

bool team_room::is_master() const noexcept { return lock_acquired_ && !destroyed_; }

bool team_room::is_destroyed() const noexcept { return destroyed_; }

bool team_room::ready_to_destroy() const noexcept {
  return !subscriber_ || channel_destroyed_ || subscriber_->is_destroyed();
}

void team_room::on_remove() {
  subscriber_ = nullptr;
  timer_watcher_.reset();
}

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
    restore_snapshot(ctx, subscriber_);
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::send_action(rpc::context& ctx, const atframework::team::DTeamAction& action) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }

  // 基本状态校验，并发写由频道日志定序、应用幂等收敛
  switch (action.action_case()) {
    case atframework::team::DTeamAction::kDestroyTeam:
      break;
    case atframework::team::DTeamAction::kAddMember:
      if (find_member(action.add_member().user_key()) != nullptr) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM);
      }
      break;
    case atframework::team::DTeamAction::kRemoveMember:
      if (find_member(action.remove_member().user_key()) == nullptr) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND);
      }
      break;
    case atframework::team::DTeamAction::kMemberConfirm:
      if (find_member(action.member_confirm().user_key()) == nullptr) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND);
      }
      break;
    case atframework::team::DTeamAction::kElectionCaptain:
      if (find_member(action.election_captain().user_key()) == nullptr) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND);
      }
      break;
    default:
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  auto event_data = rpc::make_shared_message<google::protobuf::Any>(ctx);
  if (!event_data->PackFrom(action)) {
    FCTXLOGERROR(ctx, "team room {} pack DTeamAction failed", team_id_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_event_with_lock(ctx, std::move(*event_data))));
}

rpc::result_code_type team_room::send_member_action(rpc::context& ctx,
                                                    const atframework::team::DTeamMemberAction& action) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }

  int64_t now = atfw::util::time::time_utility::get_now();
  const auto& cfg = get_teamsvr_room_cfg();
  auto normalized = rpc::make_shared_message<atframework::team::DTeamMemberAction>(ctx);
  normalized->CopyFrom(action);
  switch (normalized->action_case()) {
    case atframework::team::DTeamMemberAction::kInvited: {
      auto* invitation = normalized->mutable_invited();
      if (find_member(invitation->invitee()) != nullptr) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM);
      }
      invitation->mutable_team_key()->set_team_id(team_id_);
      if (0 == invitation->start_timepoint().seconds()) {
        invitation->mutable_start_timepoint()->set_seconds(now);
      }
      if (0 == invitation->expired_timepoint().seconds()) {
        invitation->mutable_expired_timepoint()->set_seconds(now + cfg.invitation_expire_sec());
      }
      break;
    }
    case atframework::team::DTeamMemberAction::kApplyJoinRequest: {
      auto* apply = normalized->mutable_apply_join_request();
      if (find_member(apply->user_key()) != nullptr) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM);
      }
      apply->mutable_team_key()->set_team_id(team_id_);
      if (0 == apply->expired_timepoint().seconds()) {
        apply->mutable_expired_timepoint()->set_seconds(now + cfg.join_request_expire_sec());
      }
      break;
    }
    case atframework::team::DTeamMemberAction::kRejectInvitation:
    case atframework::team::DTeamMemberAction::kCancelInvitation:
    case atframework::team::DTeamMemberAction::kRejectJoinRequest:
    case atframework::team::DTeamMemberAction::kCancelJoinRequest:
    case atframework::team::DTeamMemberAction::kJoinInTeam:
    case atframework::team::DTeamMemberAction::kRemoveMember:
      break;
    default:
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  auto event_data = rpc::make_shared_message<google::protobuf::Any>(ctx);
  if (!event_data->PackFrom(*normalized)) {
    FCTXLOGERROR(ctx, "team room {} pack DTeamMemberAction failed", team_id_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_event_with_lock(ctx, std::move(*event_data))));
}

rpc::result_code_type team_room::heartbeat(rpc::context& ctx, const atframework::team::SSTeamRoomHeartbeatReq& req) {
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

  auto* member = mutable_member(req.user_key());
  if (nullptr == member) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND);
  }

  // 更新成员确认位点(随 custom_data 下发，用于成员侧补差量)
  if (req.sequence() > member->acknowledge_action_sequence()) {
    member->set_acknowledge_action_sequence(req.sequence());
    member->set_acknowledge_action_hash_code(req.hash_code());
  }
  if (0 != req.user_router_server_id()) {
    member->set_user_router_server_id(req.user_router_server_id());
  }

  // 在线簿记(LRU 更新最近访问成员，随 private_data 仅在主控节点间同步，不下发给成员)。
  // 心跳只会推迟离线过期时间点，不会提前任何定时器事件，因此此处无需重设定时器。
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  auto& runtime = member_heartbeats_[req.user_key()];
  runtime.last_heartbeat_timepoint = atfw::util::time::time_utility::get_now();
  runtime.user_router_server_id = req.user_router_server_id();
  RPC_RETURN_CODE(0);
}

void team_room::restore_snapshot(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
  if (snapshot_restored_ || !subscriber) {
    return;
  }
  snapshot_restored_ = true;
  restore_timepoint_ = atfw::util::time::time_utility::get_now();

  // 从 custom_data 恢复队伍状态(成员清单、加入请求和加入邀请列表)
  const auto& custom_data = subscriber->get_custom_data_content();
  if (!custom_data.type_url().empty()) {
    if (!custom_data.UnpackTo(&storage_)) {
      FCTXLOGERROR(ctx, "team room {} unpack custom_data failed", team_id_);
    }
  }
  storage_.mutable_team_key()->set_team_id(team_id_);
  if (subscriber->get_destroy_sequence() > 0) {
    destroyed_ = true;
  }

  // 从 private_data 恢复主控私有簿记。
  // 先把所有成员插入 LRU 前部(无心跳簿记者视为最久未访问)，再按 private_data 顺序(心跳由旧到新)恢复簿记
  for (const auto& member : storage_.member()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    member_heartbeats_[member.user_key()];
  }
  const auto& private_data = subscriber->get_private_data_content();
  if (!private_data.type_url().empty()) {
    atframework::team::DTeamRoomPrivateData private_storage;
    if (private_data.UnpackTo(&private_storage)) {
      last_compact_sequence_ = private_storage.last_compact_sequence();
      last_compact_timepoint_ = private_storage.last_compact_timepoint().seconds();
      for (const auto& runtime : private_storage.member_runtime()) {
        if (find_member(runtime.user_key()) == nullptr) {
          continue;
        }
        auto iter = member_heartbeats_.find(runtime.user_key());
        if (iter != member_heartbeats_.end() && iter->second) {
          iter->second->last_heartbeat_timepoint = runtime.last_heartbeat_timepoint().seconds();
          iter->second->user_router_server_id = runtime.user_router_server_id();
        }
      }
    } else {
      FCTXLOGERROR(ctx, "team room {} unpack private_data failed", team_id_);
    }
  }

  // 回放压缩水位之后的增量日志
  rpc::dtmq::client_subscriber::query_options options;
  options.start_sequence = storage_.saved_action_sequence() + 1;
  subscriber->query_cached_message(
      ctx,
      [this, &ctx](const ::atfw::dtmq::DChannelMessage& message) {
        apply_event_message(ctx, message);
        return true;
      },
      options);

  // 接管当前乐观锁状态
  current_lock_ = subscriber->get_lock();
  lock_acquired_ = !current_lock_.lock_holder().empty() && current_lock_.lock_holder() == lock_holder_;

  next_compact_timepoint_ = restore_timepoint_ + get_teamsvr_room_cfg().compact_interval_sec();
  if (lock_acquired_) {
    next_renew_lock_timepoint_ = restore_timepoint_ + get_lock_renew_interval_sec();
  }
  refresh_empty_tracking(restore_timepoint_);
}

void team_room::apply_event_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& message) {
  const auto& detail = message.detail();
  if (detail.has_event() && !detail.event().type_url().empty()) {
    const auto& event = detail.event();
    if (event.Is<atframework::team::DTeamAction>()) {
      atframework::team::DTeamAction action;
      if (event.UnpackTo(&action)) {
        apply_action(ctx, action, message.sequence(), message.hash_code());
      }
    } else if (event.Is<atframework::team::DTeamMemberAction>()) {
      atframework::team::DTeamMemberAction action;
      if (event.UnpackTo(&action)) {
        apply_member_action(ctx, action, message.sequence(), message.hash_code());
      }
    }
  } else if (detail.has_destroy()) {
    destroyed_ = true;
  }
  update_acknowledge(message.sequence(), message.hash_code());
}

void team_room::update_acknowledge(int64_t sequence, uint64_t hash_code) {
  if (sequence > storage_.acknowledge_action_sequence()) {
    storage_.set_acknowledge_action_sequence(sequence);
    storage_.set_acknowledge_action_hash_code(hash_code);
  }
}

void team_room::apply_action(rpc::context& ctx, const atframework::team::DTeamAction& action, int64_t sequence,
                             uint64_t hash_code) {
  switch (action.action_case()) {
    case atframework::team::DTeamAction::kDestroyTeam:
      destroyed_ = true;
      break;
    case atframework::team::DTeamAction::kAddMember: {
      auto* exist = mutable_member(action.add_member().user_key());
      if (nullptr == exist) {
        auto* member = storage_.add_member();
        member->CopyFrom(action.add_member());
        if (0 == member->joined_timepoint().seconds()) {
          member->mutable_joined_timepoint()->set_seconds(atfw::util::time::time_utility::get_now());
        }
        // 新成员进入 LRU(尚无心跳簿记，视为最久未访问)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        member_heartbeats_[member->user_key()];
        // 首位成员成为队长
        if (!storage_.has_captain_user_key() || 0 == storage_.captain_user_key().user_id()) {
          *storage_.mutable_captain_user_key() = member->user_key();
        }
        refresh_empty_tracking(atfw::util::time::time_utility::get_now());
        schedule_next_timer();
      } else {
        auto joined_timepoint = exist->joined_timepoint();
        exist->CopyFrom(action.add_member());
        if (0 == exist->joined_timepoint().seconds()) {
          *exist->mutable_joined_timepoint() = joined_timepoint;
        }
      }
      break;
    }
    case atframework::team::DTeamAction::kRemoveMember:
    case atframework::team::DTeamAction::kMemberConfirm:
    case atframework::team::DTeamAction::kElectionCaptain: {
      atframework::team::DTeamMemberAction member_action;
      if (action.action_case() == atframework::team::DTeamAction::kRemoveMember) {
        *member_action.mutable_remove_member() = action.remove_member();
      }
      apply_member_action(ctx, member_action, sequence, hash_code);
      if (action.action_case() == atframework::team::DTeamAction::kMemberConfirm) {
        auto* member = mutable_member(action.member_confirm().user_key());
        if (nullptr != member) {
          auto joined_timepoint = member->joined_timepoint();
          member->CopyFrom(action.member_confirm());
          if (0 == member->joined_timepoint().seconds()) {
            *member->mutable_joined_timepoint() = joined_timepoint;
          }
        }
      } else if (action.action_case() == atframework::team::DTeamAction::kElectionCaptain) {
        if (find_member(action.election_captain().user_key()) != nullptr) {
          *storage_.mutable_captain_user_key() = action.election_captain().user_key();
        }
      }
      break;
    }
    default:
      break;
  }
  update_acknowledge(sequence, hash_code);
}

void team_room::apply_member_action(rpc::context& ctx, const atframework::team::DTeamMemberAction& action,
                                    int64_t sequence, uint64_t hash_code) {
  switch (action.action_case()) {
    case atframework::team::DTeamMemberAction::kInvited: {
      // 同一被邀请人只保留一份待处理邀请
      for (int i = storage_.pending_invitation_size() - 1; i >= 0; --i) {
        const auto& old = storage_.pending_invitation(i);
        if (old.invitee().user_id() == action.invited().invitee().user_id() &&
            old.invitee().zone_id() == action.invited().invitee().zone_id()) {
          storage_.mutable_pending_invitation()->DeleteSubrange(i, 1);
        }
      }
      *storage_.add_pending_invitation() = action.invited();
      break;
    }
    case atframework::team::DTeamMemberAction::kRejectInvitation:
    case atframework::team::DTeamMemberAction::kCancelInvitation: {
      const auto& invitation = action.action_case() == atframework::team::DTeamMemberAction::kRejectInvitation
                                   ? action.reject_invitation()
                                   : action.cancel_invitation();
      for (int i = storage_.pending_invitation_size() - 1; i >= 0; --i) {
        const auto& old = storage_.pending_invitation(i);
        if (old.invitee().user_id() == invitation.invitee().user_id() &&
            old.invitee().zone_id() == invitation.invitee().zone_id()) {
          storage_.mutable_pending_invitation()->DeleteSubrange(i, 1);
        }
      }
      break;
    }
    case atframework::team::DTeamMemberAction::kApplyJoinRequest: {
      for (int i = storage_.pending_apply_size() - 1; i >= 0; --i) {
        const auto& old = storage_.pending_apply(i);
        if (old.user_key().user_id() == action.apply_join_request().user_key().user_id() &&
            old.user_key().zone_id() == action.apply_join_request().user_key().zone_id()) {
          storage_.mutable_pending_apply()->DeleteSubrange(i, 1);
        }
      }
      *storage_.add_pending_apply() = action.apply_join_request();
      break;
    }
    case atframework::team::DTeamMemberAction::kRejectJoinRequest:
    case atframework::team::DTeamMemberAction::kCancelJoinRequest: {
      const auto& join_request = action.action_case() == atframework::team::DTeamMemberAction::kRejectJoinRequest
                                     ? action.reject_join_request()
                                     : action.cancel_join_request();
      for (int i = storage_.pending_apply_size() - 1; i >= 0; --i) {
        const auto& old = storage_.pending_apply(i);
        if (old.user_key().user_id() == join_request.user_key().user_id() &&
            old.user_key().zone_id() == join_request.user_key().zone_id()) {
          storage_.mutable_pending_apply()->DeleteSubrange(i, 1);
        }
      }
      break;
    }
    case atframework::team::DTeamMemberAction::kJoinInTeam: {
      // 接受邀请: 移除邀请并把被邀请人加入成员清单
      const auto& invitation = action.join_in_team();
      for (int i = storage_.pending_invitation_size() - 1; i >= 0; --i) {
        const auto& old = storage_.pending_invitation(i);
        if (old.invitee().user_id() == invitation.invitee().user_id() &&
            old.invitee().zone_id() == invitation.invitee().zone_id()) {
          storage_.mutable_pending_invitation()->DeleteSubrange(i, 1);
        }
      }
      atframework::team::DTeamAction add_action;
      auto* member = add_action.mutable_add_member();
      *member->mutable_user_key() = invitation.invitee();
      *member->mutable_team_source_data() = invitation.team_source_data();
      member->set_team_source_type(invitation.team_source_type());
      member->mutable_joined_timepoint()->set_seconds(atfw::util::time::time_utility::get_now());
      apply_action(ctx, add_action, sequence, hash_code);
      break;
    }
    case atframework::team::DTeamMemberAction::kRemoveMember: {
      const auto& user_key = action.remove_member().user_key();
      for (int i = storage_.member_size() - 1; i >= 0; --i) {
        const auto& old = storage_.member(i);
        if (old.user_key().user_id() == user_key.user_id() && old.user_key().zone_id() == user_key.zone_id()) {
          storage_.mutable_member()->DeleteSubrange(i, 1);
        }
      }
      member_heartbeats_.erase(user_key);
      // 踢出的成员如果是队长则自动触发换队长(确定性选主，后续可扩展竞选流程)
      elect_captain_after_remove(user_key);
      refresh_empty_tracking(atfw::util::time::time_utility::get_now());
      schedule_next_timer();
      break;
    }
    default:
      break;
  }
  update_acknowledge(sequence, hash_code);
}

void team_room::elect_captain_after_remove(const PROJECT_NAMESPACE_ID::DUserIDKey& removed_user_key) {
  if (storage_.member_size() == 0) {
    storage_.clear_captain_user_key();
    return;
  }
  const auto& captain = storage_.captain_user_key();
  bool captain_alive = captain.user_id() != 0 && (captain.user_id() != removed_user_key.user_id() ||
                                                  captain.zone_id() != removed_user_key.zone_id());
  if (captain_alive) {
    return;
  }
  // 确定性地选出加入时间最早的成员作为新队长
  int captain_index = 0;
  for (int i = 1; i < storage_.member_size(); ++i) {
    const auto& lhs = storage_.member(i).joined_timepoint();
    const auto& rhs = storage_.member(captain_index).joined_timepoint();
    if (lhs.seconds() < rhs.seconds() || (lhs.seconds() == rhs.seconds() && lhs.nanos() < rhs.nanos())) {
      captain_index = i;
    }
  }
  *storage_.mutable_captain_user_key() = storage_.member(captain_index).user_key();
}

int64_t team_room::get_member_offline_deadline(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  int64_t baseline = restore_timepoint_;
  auto iter = member_heartbeats_.find(user_key, false);
  if (iter != member_heartbeats_.end() && iter->second && iter->second->last_heartbeat_timepoint > baseline) {
    baseline = iter->second->last_heartbeat_timepoint;
  }
  const auto* member = find_member(user_key);
  if (nullptr != member && member->joined_timepoint().seconds() > baseline) {
    baseline = member->joined_timepoint().seconds();
  }
  return baseline + get_teamsvr_room_cfg().member_offline_expire_sec();
}

void team_room::refresh_empty_tracking(int64_t now) {
  if (0 == storage_.member_size()) {
    if (0 == empty_since_timepoint_) {
      empty_since_timepoint_ = now;
    }
  } else {
    empty_since_timepoint_ = 0;
  }
}

atframework::team::DTeamMember* team_room::mutable_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  for (int i = 0; i < storage_.member_size(); ++i) {
    auto* member = storage_.mutable_member(i);
    if (member->user_key().user_id() == user_key.user_id() && member->user_key().zone_id() == user_key.zone_id()) {
      return member;
    }
  }
  return nullptr;
}

const atframework::team::DTeamMember* team_room::find_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const {
  for (const auto& member : storage_.member()) {
    if (member.user_key().user_id() == user_key.user_id() && member.user_key().zone_id() == user_key.zone_id()) {
      return &member;
    }
  }
  return nullptr;
}

int64_t team_room::get_lock_lease_sec() const {
  int64_t ret = 0;
  if (subscriber_) {
    const auto& configure = subscriber_->get_configure();
    // 租约时长不低于频道配置的订阅者心跳过期淘汰时间
    ret = configure.subscriber_timeout().seconds();
    if (ret <= 0) {
      // 配置未就绪时回退到与 normalize_dtmq_channel_configure 一致的推导: 2*心跳+重试
      ret = configure.heartbeat_interval().seconds() + configure.heartbeat_interval().seconds() +
            configure.heartbeat_retry_interval().seconds();
    }
  }
  if (ret <= 0) {
    ret = 60;
  }
  return ret;
}

int64_t team_room::get_lock_renew_interval_sec() const {
  return (std::max)(get_lock_lease_sec() / 2, static_cast<int64_t>(1));
}

int64_t team_room::get_compact_log_count() const {
  if (subscriber_) {
    const auto& configure = subscriber_->get_configure();
    if (configure.gc_log_count() > 0) {
      return configure.gc_log_count();
    }
  }
  // 与 normalize_dtmq_channel_configure 默认值一致
  return 30;
}

::atfw::dtmq::DChannelOptimisticLock team_room::make_self_lock(int64_t now) const {
  ::atfw::dtmq::DChannelOptimisticLock ret;
  ret.set_lock_holder(lock_holder_);
  ret.mutable_timeout()->set_seconds(now + get_lock_lease_sec());
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
  if (channel_destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }

  int64_t now = atfw::util::time::time_utility::get_now();
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
      next_renew_lock_timepoint_ = now + get_lock_renew_interval_sec();
      FCTXLOGINFO(ctx, "team room {} acquire optimistic lock success", team_id_);
      break;
    }
    if (rsp_checker && rsp_checker->has_real_value()) {
      current_lock_ = rsp_checker->real_value();
      if (current_lock_.lock_holder() == lock_holder_) {
        // 已是本节点持有(可能上次设置成功但响应丢失)
        lock_acquired_ = true;
        next_renew_lock_timepoint_ = now + get_lock_renew_interval_sec();
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

rpc::result_code_type team_room::send_event_with_lock(rpc::context& ctx, ::google::protobuf::Any&& event_data) {
  if (!lock_acquired_) {
    auto lock_ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
    if (lock_ret != 0) {
      RPC_RETURN_CODE(lock_ret);
    }
  }

  auto checker = make_write_lock_checker();
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_event(ctx, std::move(event_data), checker, rsp_checker));
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

team_room_timer_event team_room::get_next_timer_event(int64_t now) {
  team_room_timer_event ret;
  if (!subscriber_ || !subscriber_->is_ready() || channel_destroyed_) {
    return ret;
  }

  // 非主控节点: 在当前乐观锁过期后尝试接管(容灾切换，老节点再写入时锁自然失败)
  if (!lock_acquired_) {
    ret.type = team_room_timer_event_type::kAcquireLock;
    if (current_lock_.lock_holder().empty()) {
      ret.timepoint = now;
    } else if (current_lock_.has_timeout() && current_lock_.timeout().seconds() > 0) {
      ret.timepoint = current_lock_.timeout().seconds();
    } else {
      // 锁未设置超时则按租约周期定期检查
      ret.timepoint = now + get_lock_lease_sec();
    }
    return ret;
  }

  // 主控节点: 已解散队伍尽快销毁频道
  if (destroyed_) {
    if (!channel_destroy_sent_) {
      ret.type = team_room_timer_event_type::kDestroyChannel;
      ret.timepoint = now;
    }
    return ret;
  }

  // 定期维护: 乐观锁续租+过期数据清理+日志压缩(不要求时间非常精确)
  ret.type = team_room_timer_event_type::kMaintenance;
  ret.timepoint = (std::min)(next_renew_lock_timepoint_, next_compact_timepoint_);
  // 日志数量达到频道配置阈值时立即触发压缩
  if (subscriber_->get_last_message_sequence() - last_compact_sequence_ >= get_compact_log_count()) {
    ret.timepoint = (std::min)(ret.timepoint, now);
  }

  // 剔除最久未心跳的成员(LRU front)
  if (!member_heartbeats_.empty()) {
    const auto& oldest = member_heartbeats_.front();
    int64_t deadline = get_member_offline_deadline(oldest.first);
    if (deadline < ret.timepoint) {
      ret.type = team_room_timer_event_type::kKickOfflineMember;
      ret.timepoint = deadline;
    }
  }

  // 空队伍保留到期后解散
  if (0 == storage_.member_size() && empty_since_timepoint_ > 0) {
    int64_t deadline = empty_since_timepoint_ + get_teamsvr_room_cfg().empty_room_destroy_delay_sec();
    if (deadline < ret.timepoint) {
      ret.type = team_room_timer_event_type::kDestroyEmptyRoom;
      ret.timepoint = deadline;
    }
  }
  return ret;
}

void team_room::schedule_next_timer() {
  if (channel_destroyed_) {
    // 房间即将被回收，不再调度定时器
    team_room_manager::me()->remove_room_timer(*this);
    return;
  }

  int64_t now = atfw::util::time::time_utility::get_now();
  auto event = get_next_timer_event(now);
  if (team_room_timer_event_type::kNone == event.type) {
    // 暂无定时事件(订阅未就绪等)，按续租周期保底重查
    event.timepoint = now + get_lock_renew_interval_sec();
  }
  team_room_manager::me()->reset_room_timer(*this, event.timepoint);
}

void team_room::on_timer(rpc::context& ctx) {
  timer_watcher_.reset();
  if (channel_destroyed_) {
    return;
  }

  int64_t now = atfw::util::time::time_utility::get_now();
  auto event = get_next_timer_event(now);
  if (team_room_timer_event_type::kNone == event.type || event.timepoint > now) {
    // 事件未到期，重新调度(定时器总是指向当前最近的定时 action)
    schedule_next_timer();
    return;
  }

  if (!task_type_trait::empty(maintenance_task_) && !task_type_trait::is_exiting(maintenance_task_)) {
    // 上一次定时 action 仍在执行，稍后重试
    team_room_manager::me()->reset_room_timer(*this, now + 1);
    return;
  }

  auto self = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, get_timer_event_name(event.type),
                        [self, event_type = event.type](rpc::context& child_ctx) mutable -> rpc::result_code_type {
                          RPC_AWAIT_CODE_RESULT(self->execute_timer_event(child_ctx, event_type));
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
  int64_t now = atfw::util::time::time_utility::get_now();
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

  int64_t now = atfw::util::time::time_utility::get_now();
  const auto& cfg = get_teamsvr_room_cfg();

  // 过期数据清理(过期的邀请和加入请求)
  RPC_AWAIT_CODE_RESULT(cleanup_expired_admissions(ctx, now));
  if (!lock_acquired_) {
    RPC_RETURN_CODE(0);
  }

  // 一次 send_update 完成乐观锁续租(reset_value)，日志数量或时间到期时同时压缩日志
  bool need_compact = now >= next_compact_timepoint_ ||
                      (subscriber_->get_last_message_sequence() - last_compact_sequence_) >= get_compact_log_count();

  auto self_lock = make_self_lock(now);
  auto checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  *checker->mutable_expect_value() = current_lock_;
  *checker->mutable_reset_value() = self_lock;
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();

  rpc::dtmq::client_subscriber::update_option options;
  auto private_data = rpc::make_shared_message<atframework::team::DTeamRoomPrivateData>(ctx);
  if (need_compact) {
    // 当前状态信息存入 custom_data(成员清单、加入请求和加入邀请列表)和 private_data(主控私有簿记)
    storage_.set_saved_action_sequence(subscriber_->get_last_message_sequence());
    dump_private_data(*private_data);
    options.save = true;
    options.compact_sequence = storage_.saved_action_sequence();
    options.stateful_sequence = storage_.saved_action_sequence();
    options.custom_data = &storage_;
    options.private_data = private_data.get();
  }

  auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_update(ctx, options, checker, rsp_checker));
  if (0 == ret) {
    current_lock_ = self_lock;
    next_renew_lock_timepoint_ = now + get_lock_renew_interval_sec();
    if (need_compact) {
      last_compact_sequence_ = storage_.saved_action_sequence();
      last_compact_timepoint_ = now;
      next_compact_timepoint_ = now + cfg.compact_interval_sec();
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

rpc::result_code_type team_room::cleanup_expired_admissions(rpc::context& ctx, int64_t now) {
  // 清理过期邀请
  {
    std::vector<atframework::team::DTeamInvitation> expired_invitations;
    for (const auto& invitation : storage_.pending_invitation()) {
      if (0 != invitation.expired_timepoint().seconds() && invitation.expired_timepoint().seconds() <= now) {
        expired_invitations.push_back(invitation);
      }
    }
    for (const auto& invitation : expired_invitations) {
      atframework::team::DTeamMemberAction action;
      *action.mutable_cancel_invitation() = invitation;
      auto ret = RPC_AWAIT_CODE_RESULT(send_member_action(ctx, action));
      if (0 != ret) {
        FCTXLOGERROR(ctx, "team room {} cancel expired invitation of {}/{} failed: {}", team_id_,
                     invitation.invitee().zone_id(), invitation.invitee().user_id(), ret);
      }
    }
  }

  // 清理过期加入请求
  {
    std::vector<atframework::team::DTeamJoinRequest> expired_apply;
    for (const auto& apply : storage_.pending_apply()) {
      if (0 != apply.expired_timepoint().seconds() && apply.expired_timepoint().seconds() <= now) {
        expired_apply.push_back(apply);
      }
    }
    for (const auto& apply : expired_apply) {
      atframework::team::DTeamMemberAction action;
      *action.mutable_cancel_join_request() = apply;
      auto ret = RPC_AWAIT_CODE_RESULT(send_member_action(ctx, action));
      if (0 != ret) {
        FCTXLOGERROR(ctx, "team room {} cancel expired join request of {}/{} failed: {}", team_id_,
                     apply.user_key().zone_id(), apply.user_key().user_id(), ret);
      }
    }
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::kick_due_offline_members(rpc::context& ctx, int64_t now) {
  // LRU front 为最久未心跳的成员，队伍规模小，全量扫描收集所有到期成员
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> offline_members;
  for (const auto& pair : member_heartbeats_) {
    if (get_member_offline_deadline(pair.first) <= now) {
      offline_members.push_back(pair.first);
    }
  }

  for (const auto& user_key : offline_members) {
    atframework::team::DTeamMemberAction action;
    *action.mutable_remove_member()->mutable_user_key() = user_key;
    action.mutable_remove_member()->set_remove_member_reason(atframework::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED);
    auto ret = RPC_AWAIT_CODE_RESULT(send_member_action(ctx, action));
    if (0 != ret) {
      FCTXLOGERROR(ctx, "team room {} remove offline member {}/{} failed: {}", team_id_, user_key.zone_id(),
                   user_key.user_id(), ret);
    }
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::destroy_empty_room(rpc::context& ctx) {
  if (destroyed_ || 0 != storage_.member_size()) {
    RPC_RETURN_CODE(0);
  }
  atframework::team::DTeamAction action;
  action.mutable_destroy_team()->set_team_id(team_id_);
  auto ret = RPC_AWAIT_CODE_RESULT(send_action(ctx, action));
  if (0 != ret && PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED != ret) {
    FCTXLOGERROR(ctx, "team room {} destroy empty team failed: {}", team_id_, ret);
  }
  RPC_RETURN_CODE(0);
}

void team_room::dump_private_data(atframework::team::DTeamRoomPrivateData& output) const {
  // 按 LRU 顺序(心跳由旧到新)导出，恢复时可重建 LRU 访问顺序
  for (auto iter = member_heartbeats_.cbegin(); iter != member_heartbeats_.cend(); ++iter) {
    if (find_member(iter->first) == nullptr || !iter->second) {
      // 已退出成员的簿记不持久化
      continue;
    }
    auto* runtime = output.add_member_runtime();
    *runtime->mutable_user_key() = iter->first;
    runtime->mutable_last_heartbeat_timepoint()->set_seconds(iter->second->last_heartbeat_timepoint);
    runtime->set_user_router_server_id(iter->second->user_router_server_id);
  }
  output.set_last_compact_sequence(last_compact_sequence_);
  output.mutable_last_compact_timepoint()->set_seconds(last_compact_timepoint_);
}

void team_room::on_ready(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
  if (subscriber != subscriber_) {
    return;
  }
  restore_snapshot(ctx, subscriber_);
  // 就绪后重设定时器: 非主控节点将由定时器驱动接管乐观锁成为主控节点
  schedule_next_timer();
}

void team_room::on_receive_event(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                 const ::atfw::dtmq::DChannelMessage& message) {
  if (subscriber != subscriber_) {
    return;
  }
  apply_event_message(ctx, message);
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
    next_renew_lock_timepoint_ = atfw::util::time::time_utility::get_now() + get_lock_renew_interval_sec();
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
  channel_destroyed_ = true;
  lock_acquired_ = false;
  // 频道已销毁，房间等待回收，不再调度定时器
  team_room_manager::me()->remove_room_timer(*this);
}

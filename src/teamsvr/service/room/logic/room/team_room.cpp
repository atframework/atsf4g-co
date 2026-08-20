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
  // 已就绪(共享层复用)则不会再触发 on_ready，直接恢复快照
  if (room->subscriber_->is_ready()) {
    room->restore_snapshot(ctx);
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
    restore_snapshot(ctx);
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::send_action(rpc::context& ctx, const atfw::team::DTeamAction& action) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  auto event_data = rpc::make_shared_message<google::protobuf::Any>(ctx);
  if (!event_data->PackFrom(action)) {
    FCTXLOGERROR(ctx, "team room {} pack DTeamAction failed", team_id_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_event_with_lock(ctx, std::move(*event_data))));
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

  // 更新成员确认位点(随 custom_data 下发，用于成员侧补差量)
  if (req.sequence() > member->member_data.acknowledge_action_sequence()) {
    member->member_data.set_acknowledge_action_sequence(req.sequence());
    member->member_data.set_acknowledge_action_hash_code(req.hash_code());
  }

  // 服务器如果触发反订阅，这里会传0
  if (req.user_router_server_id() > 0) {
    member->last_heartbeat_timepoint = atfw::util::time::time_utility::now();
  }
  member->user_router_server_id = req.user_router_server_id();
  member->member_data.set_user_router_server_id(req.user_router_server_id());
  RPC_RETURN_CODE(0);
}

void team_room::restore_snapshot(rpc::context& ctx) {
  if (snapshot_restored_) {
    return;
  }
  snapshot_restored_ = true;
  restore_timepoint_ = atfw::util::time::time_utility::now();

  // 从 custom_data 恢复队伍状态(成员清单、加入请求和加入邀请列表)
  const auto& custom_data = subscriber_->get_custom_data_content();
  if (!custom_data.type_url().empty()) {
    if (!custom_data.UnpackTo(&storage_)) {
      FCTXLOGERROR(ctx, "team room {} unpack custom_data failed", team_id_);
    }
  }
  storage_.mutable_team_key()->set_team_id(team_id_);
  if (subscriber_->is_destroyed()) {
    destroyed_ = true;
  }
  do {
    const auto& private_data = subscriber_->get_private_data_content();
    if (private_data.type_url().empty()) {
      break;
    }

    atfw::team::DTeamRoomPrivateData private_storage;
    if (!private_data.UnpackTo(&private_storage)) {
      FCTXLOGERROR(ctx, "team room {} unpack private_data failed", team_id_);
      break;
    }

    last_compact_sequence_ = private_storage.last_compact_sequence();
    last_compact_timepoint_ = protobuf_to_system_clock(private_storage.last_compact_timepoint());
    storage_.set_saved_action_sequence(last_compact_sequence_);

    // 配置
    protobuf_copy_message(*storage_.mutable_configure(), private_storage.configure());

    // member
    std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_member_key;
    foreach_member([&expired_member_key](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
      expired_member_key.insert(member->member_data.user_key());
      return true;
    });

    for (const auto& runtime : private_storage.member_runtime()) {
      auto member = find_member(runtime.public_member_data().user_key(), false);
      if (!member) {
        member = mutable_member(runtime.public_member_data().user_key());
      }
      if (!member) {
        FCTXLOGERROR(ctx, "team room {} restore_snapshot member {} but allocate failed", team_id_,
                     runtime.public_member_data().user_key().user_id());
        continue;
      }
      expired_member_key.erase(member->member_data.user_key());

      member->user_router_server_id = runtime.user_router_server_id();
      member->last_heartbeat_timepoint = protobuf_to_system_clock(runtime.last_heartbeat_timepoint());
      protobuf_copy_message(member->member_data, runtime.public_member_data());
    }

    for (const auto& removed_key : expired_member_key) {
      remove_member(ctx, removed_key, atfw::team::EN_TEAM_EXIT_REASON_DEFAULT, false);
    }

    // invitation
    expired_member_key.clear();
    for (const auto& invitation : pending_invitation_by_invitee_) {
      expired_member_key.insert(invitation.first);
    }
    for (const auto& invitation : private_storage.pending_invitation()) {
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
    for (const auto& join_request : private_storage.pending_join_request()) {
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
    *storage_.mutable_shared_team_data() = private_storage.shared_team_data();

    change_captain(private_storage.captain_user_key());
  } while (false);

  // 回放压缩点之后的增量日志
  rpc::dtmq::client_subscriber::query_options options;
  options.start_sequence = storage_.saved_action_sequence() + 1;
  subscriber_->query_cached_message(
      ctx,
      [this, &ctx](const ::atfw::dtmq::DChannelMessage& message) {
        apply_event_message(ctx, message);
        return true;
      },
      options);

  // 接管当前乐观锁状态
  current_lock_ = subscriber_->get_lock();
  lock_acquired_ = !current_lock_.lock_holder().empty() && current_lock_.lock_holder() == lock_holder_;

  next_compact_timepoint_ = restore_timepoint_ + protobuf_to_system_clock(get_teamsvr_room_cfg().compact_interval());
  if (lock_acquired_) {
    next_renew_lock_timepoint_ = restore_timepoint_ + get_lock_renew_interval();
  }

  // 加载snapshot之后重置一下用于延迟删除的空房间计时器，避免在恢复快照后立即触发删除
  refresh_empty_tracking(restore_timepoint_);
  schedule_next_timer();
}

void team_room::apply_event_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& message) {
  const auto& detail = message.detail();
  if (detail.has_event() && !detail.event().type_url().empty()) {
    const auto& event = detail.event();
    if (event.Is<atfw::team::DTeamAction>()) {
      atfw::team::DTeamAction action;
      if (event.UnpackTo(&action)) {
        apply_action(ctx, action, message.sequence(), message.hash_code());
      } else {
        FCTXLOGERROR(ctx, "team room {} unpack DTeamAction failed, got type_url: {}", team_id_, event.type_url());
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

void team_room::apply_action(rpc::context& ctx, const atfw::team::DTeamAction& action, int64_t sequence,
                             uint64_t hash_code) {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam:
      destroyed_ = true;
      break;
    case atfw::team::DTeamAction::kAddMember: {
      auto member = find_member(action.add_member().user_key(), true);
      bool is_new_member = false;
      if (!member) {
        is_new_member = true;
        member = mutable_member(action.add_member().user_key());
      }
      if (!member) {
        break;
      }

      auto joined_timepoint = member->member_data.joined_timepoint();
      auto member_key = member->member_data.user_key();
      protobuf_copy_message(member->member_data, action.add_member());
      // Key不允许修改
      protobuf_copy_message(*member->member_data.mutable_user_key(), member_key);
      if (joined_timepoint.seconds() > member->member_data.joined_timepoint().seconds()) {
        protobuf_copy_message(*member->member_data.mutable_joined_timepoint(), joined_timepoint);
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

      // 移除相关的invitation和join_request
      pending_invitation_by_invitee_.erase(member_key);
      pending_join_request_by_requester_.erase(member_key);
      break;
    }
    case atfw::team::DTeamAction::kRemoveMember: {
      const auto& user_key = action.remove_member().user_key();
      remove_member(ctx, user_key, action.remove_member().remove_member_reason(), true);
      break;
    }
    case atfw::team::DTeamAction::kMemberUpdate: {
      const auto& update_data = action.member_update();
      auto member = find_member(update_data.user_key(), true);
      if (!member) {
        break;
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
      break;
    }
    case atfw::team::DTeamAction::kElectionCaptain: {
      change_captain(action.election_captain().user_key());
      break;
    }
    case atfw::team::DTeamAction::kAddInvitation:
    case atfw::team::DTeamAction::kApproveInvitation:
    case atfw::team::DTeamAction::kRejectInvitation:
    case atfw::team::DTeamAction::kAddJoinRequest:
    case atfw::team::DTeamAction::kApproveJoinRequest:
    case atfw::team::DTeamAction::kRejectJoinRequest: {
      // TODO(owent): implement these actions
      break;
    }

    default:
      break;
  }
  update_acknowledge(sequence, hash_code);
}

void team_room::elect_captain_after_remove() {
  foreach_member([this](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    protobuf_copy_message(*storage_.mutable_captain_user_key(), member->member_data.user_key());
    // 队长一定是owner
    member->member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    return true;
  });
}

std::chrono::system_clock::time_point team_room::get_member_offline_deadline(
    const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  std::chrono::system_clock::time_point baseline = restore_timepoint_;
  auto member = find_member(user_key, false);
  if (!member) {
    return baseline + protobuf_to_system_clock(get_teamsvr_room_cfg().member_offline_expire());
  }

  auto member_baseline = protobuf_to_system_clock(member->member_data.joined_timepoint());
  if (nullptr != member && member_baseline > baseline) {
    baseline = member_baseline;
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
  // mutable接口总是由有效的成员调用，若所以总是可以刷新LRU的最近访问时间
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
  protobuf_copy_message(*ret->member_data.mutable_user_key(), user_key);

  // 防止递归期间增删member，延迟处理
  if (iterating_member_protect_ != nullptr) {
    iterating_member_protect_->pending_to_remove.erase(user_key);
    iterating_member_protect_->pending_to_add.insert({user_key, ret});
    return ret;
  }

  member_.insert_key_value(user_key, ret);
  return ret;
}

team_room::member_ptr_t team_room::find_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool update_visit) {
  auto iter = member_.find(user_key, update_visit);
  if (iter == member_.end()) {
    return nullptr;
  }
  return *iter->second;
}

bool team_room::remove_member(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                              atfw::team::EnTeamExitReason reason, bool with_notify) {
  auto iter = member_.find(user_key, false);
  if (iter == member_.end()) {
    return false;
  }

  // 防止递归期间增删member，延迟处理
  if (iterating_member_protect_ != nullptr) {
    iterating_member_protect_->pending_to_remove.insert({user_key, *iter->second});
    iterating_member_protect_->pending_to_add.erase(user_key);
    return true;
  }
  auto member_ptr = *iter->second;

  member_.erase(iter);
  // 踢出的成员如果是队长则自动触发换队长(确定性选主，后续可扩展竞选流程)
  const auto& captain = storage_.captain_user_key();
  if (captain.user_id() == user_key.user_id() && captain.zone_id() == user_key.zone_id()) {
    storage_.clear_captain_user_key();
    elect_captain_after_remove();
  }
  refresh_empty_tracking(atfw::util::time::time_utility::now());
  schedule_next_timer();

  // 主动通知私有频道
  if (with_notify && member_ptr && member_ptr->member_data.user_channel().channel_type() != 0 &&
      !member_ptr->member_data.user_channel().channel_id().empty()) {
    auto& pending_channel = pending_member_channel_actions_[member_ptr->member_data.user_key()];
    pending_channel.emplace_back();
    pending_team_member_message_t& msg = pending_channel.back();
    protobuf_copy_message(msg.first, member_ptr->member_data.user_channel());
    rpc::context::message_holder<atfw::team::DTeamMemberAction> action{ctx};
    *msg.second.mutable_remove_member()->mutable_user_key() = member_ptr->member_data.user_key();
    msg.second.mutable_remove_member()->set_remove_member_reason(reason);
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

    if (!fn(*iter->second)) {
      break;
    }
  }

  if (iterating_member_protect_ == &current_protect_instance) {
    iterating_member_protect_ = nullptr;

    for (const auto& pair : current_protect_instance.pending_to_add) {
      member_.insert_key_value(pair.first, pair.second);
    }

    for (const auto& pair : current_protect_instance.pending_to_remove) {
      member_.erase(pair.first);
      if (storage_.captain_user_key().user_id() == pair.first.user_id() &&
          storage_.captain_user_key().zone_id() == pair.first.zone_id()) {
        storage_.clear_captain_user_key();
        elect_captain_after_remove();
      }
    }

    if (storage_.captain_user_key().user_id() == 0 && !member_.empty()) {
      elect_captain_after_remove();
    }

    if (!current_protect_instance.pending_to_remove.empty()) {
      refresh_empty_tracking(atfw::util::time::time_utility::now());
      schedule_next_timer();
    }
  }
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

team_room_timer_event team_room::get_next_timer_event(std::chrono::system_clock::time_point now) {
  team_room_timer_event ret;
  if (subscriber_->is_destroyed()) {
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

  // 定期维护: 乐观锁续租+过期数据清理+日志压缩(不要求时间非常精确)
  ret.type = team_room_timer_event_type::kMaintenance;
  ret.timeout = (std::min)(next_renew_lock_timepoint_, next_compact_timepoint_);
  // 日志数量达到频道配置阈值时立即触发压缩
  if (subscriber_->get_last_message_sequence() - last_compact_sequence_ >= get_compact_log_count()) {
    ret.timeout = (std::min)(ret.timeout, now);
  }

  // 剔除最久未心跳的成员(LRU front)
  if (!member_.empty()) {
    const auto& oldest = member_.front();
    std::chrono::system_clock::time_point deadline = get_member_offline_deadline(oldest.first);
    if (deadline < ret.timeout) {
      ret.type = team_room_timer_event_type::kKickOfflineMember;
      ret.timeout = deadline;
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
    // 上一次定时 action 仍在执行，出现荣誉定时器，直接忽略即可
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

        // 定时 action 完成后重设下一个定时器
        self->schedule_next_timer();

        RPC_AWAIT_IGNORE_RESULT(self->flush_pending_channel_message(child_ctx));
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
  auto private_data = rpc::make_shared_message<atfw::team::DTeamRoomPrivateData>(ctx);
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
    next_renew_lock_timepoint_ = now + get_lock_renew_interval();
    if (need_compact) {
      last_compact_sequence_ = storage_.saved_action_sequence();
      last_compact_timepoint_ = now;
      next_compact_timepoint_ = now + protobuf_to_system_clock(cfg.compact_interval());
      storage_.set_saved_action_sequence(last_compact_sequence_);
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
  // LRU front 为最久未心跳的成员，队伍规模小，全量扫描收集所有到期成员
  std::vector<member_ptr_t> offline_members;
  for (const auto& pair : member_) {
    if (get_member_offline_deadline(pair.first) > now) {
      break;
    }
    if (get_member_offline_deadline(pair.first) <= now && *pair.second) {
      offline_members.push_back(*pair.second);
    }
  }

  // TODO(owent): 发送remove_member消息到team channel频道,注意不要短期内发太多次

  for (const auto& user_ptr : offline_members) {
    atfw::team::DTeamMemberAction action;
    *action.mutable_remove_member()->mutable_user_key() = user_ptr->member_data.user_key();
    action.mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED);
    if (user_ptr->member_data.user_channel().channel_type() != 0 &&
        !user_ptr->member_data.user_channel().channel_id().empty()) {
      auto ret = RPC_AWAIT_CODE_RESULT(send_member_action(ctx, user_ptr->member_data.user_channel(), action));
      if (0 != ret) {
        FCTXLOGERROR(ctx, "team room {} remove offline member {}/{} failed: {}", team_id_,
                     user_ptr->member_data.user_key().zone_id(), user_ptr->member_data.user_key().user_id(), ret);
      }
    }
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::destroy_empty_room(rpc::context& ctx) {
  if (destroyed_ || !member_.empty()) {
    RPC_RETURN_CODE(0);
  }
  atfw::team::DTeamAction action;
  action.mutable_destroy_team()->set_team_id(team_id_);
  auto ret = RPC_AWAIT_CODE_RESULT(send_action(ctx, action));
  if (0 != ret && PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED != ret) {
    FCTXLOGERROR(ctx, "team room {} destroy empty team failed: {}", team_id_, ret);
  }
  RPC_RETURN_CODE(0);
}

void team_room::dump_team_key(atfw::team::DTeamKey& output) const { output.set_team_id(team_id_); }

void team_room::dump_private_data(atfw::team::DTeamRoomPrivateData& output) {
  dump_team_key(*output.mutable_team_key());
  protobuf_copy_message(*output.mutable_captain_user_key(), storage_.captain_user_key());

  // dump 配置
  protobuf_copy_message(*output.mutable_configure(), storage_.configure());

  // dump成员数据
  foreach_member([&output](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    auto* runtime = output.add_member_runtime();
    protobuf_copy_message(*runtime->mutable_public_member_data(), member->member_data);
    *runtime->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(member->last_heartbeat_timepoint);
    runtime->set_user_router_server_id(member->user_router_server_id);
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

  output.set_last_compact_sequence(last_compact_sequence_);
  *output.mutable_last_compact_timepoint() = protobuf_from_system_clock(last_compact_timepoint_);

  // dump shared_team_data
  *output.mutable_shared_team_data() = storage_.shared_team_data();
}

void team_room::dump_public_data(atfw::team::DTeamStorage& output) {
  protobuf_copy_message(output, storage_);
  dump_team_key(*output.mutable_team_key());

  // dump成员数据
  foreach_member([&output](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    auto* data = output.add_member();
    protobuf_copy_message(*data, member->member_data);
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

void team_room::on_ready(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
  if (subscriber != subscriber_) {
    return;
  }
  restore_snapshot(ctx);
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
    next_renew_lock_timepoint_ = atfw::util::time::time_utility::now() + get_lock_renew_interval();
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
  if (pending_member_channel_actions_.empty()) {
    RPC_RETURN_CODE(0);
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

  RPC_RETURN_CODE(0);
}

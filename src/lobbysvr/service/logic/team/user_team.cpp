// Copyright 2026 atframework

#include "logic/team/user_team.h"

#include <memory/object_allocator.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/lobbysvr_config.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/team/team_room_client_api.h>

#include <utility/protobuf_mini_dumper.h>

#include <data/user_key_hash_helper.h>

#include <chrono>
#include <utility>

#include "rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h"

#include "data/user.h"

#include "logic/chat/user_chat_manager.h"
#include "logic/team/user_team_manager.h"

namespace {
std::chrono::system_clock::duration get_exit_team_request_retry_interval() noexcept {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().exit_retry_interval().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().exit_retry_interval());
  }
  return std::chrono::seconds(5);
}

std::chrono::system_clock::duration get_exit_team_timeout() noexcept {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().exit_timeout().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().exit_timeout());
  }
  return std::chrono::seconds(30);
}

std::chrono::system_clock::duration get_wait_add_member_timeout() noexcept {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().wait_add_member_timeout().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().wait_add_member_timeout());
  }
  return std::chrono::seconds(30);
}

static user_team* get_user_team(const rpc::dtmq::client_subscriber::ptr_t& subscriber, gsl::string_view callback_name) {
  if (!subscriber) {
    return nullptr;
  }
  auto local_private_data = subscriber->get_local_private_data();
  if (local_private_data.empty()) {
    FWLOGERROR("user_team {} callback missing local_private_data", callback_name);
    return nullptr;
  }

  static_assert(sizeof(user_team*) == sizeof(*local_private_data.data()), "user_team* size mismatch");
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return reinterpret_cast<user_team*>(*local_private_data.data());
}

}  // namespace

class user_team_utility {
 private:
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t build_event_callback_set() {
    rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
        rpc::dtmq::client_subscriber::create_event_callback_set();

    rpc::dtmq::client_subscriber::set_event_callback_on_receive_snapshot_finished(
        *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                 const ::atfw::dtmq::DChannelSnapshot& /*snapshot*/, int32_t result_code) {
          user_team* team_ptr = get_user_team(subscriber, "on_receive_snapshot_finished");
          if (team_ptr != nullptr && result_code >= 0) {
            auto hold_lifetime = team_ptr->shared_from_this();
            hold_lifetime->load_snapshot(ctx);
          }
        });

    rpc::dtmq::client_subscriber::set_event_callback_on_receive_raw_message(
        *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                 const ::atfw::dtmq::DChannelMessage& data) {
          user_team* team_ptr = get_user_team(subscriber, "on_receive_raw_message");
          if (team_ptr != nullptr) {
            auto hold_lifetime = team_ptr->shared_from_this();
            hold_lifetime->on_receive_raw_message(ctx, data);
          }
        });

    rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
        *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber, int64_t log_sequence,
                 std::chrono::system_clock::time_point /*destroy_time*/) {
          user_team* team_ptr = get_user_team(subscriber, "on_destroyed");
          // 可能先删除频道，而后重新创建的流程。所以要忽略之前的频道销毁通知
          if (team_ptr != nullptr && log_sequence >= team_ptr->channel_create_sequence_) {
            auto hold_lifetime = team_ptr->shared_from_this();
            hold_lifetime->is_member_ = false;
            FCTXLOGDEBUG(ctx, "{} channel for team {}:{} destroyed, sequence:{}", hold_lifetime->owner_->get_owner(),
                         hold_lifetime->team_key_.zone_id(), hold_lifetime->team_key_.team_id(), log_sequence);
            hold_lifetime->owner_->remove_team(ctx, hold_lifetime->get_team_key(),
                                               atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
          }
        });

    return ret;
  }

 public:
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t get_event_callback_set() {
    static rpc::dtmq::client_subscriber::event_callback_set_ptr_t callback_set = build_event_callback_set();
    return callback_set;
  }
};

struct user_team::ctor_guard_t {};

// NOLINTNEXTLINE(modernize-pass-by-value)
user_team::user_team(ctor_guard_t&, rpc::context& /*ctx*/, user_team_manager& owner,
                     atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&& channel_subscriber,
                     // NOLINTNEXTLINE(modernize-pass-by-value)
                     uint32_t team_type, const atfw::team::DTeamKey& team_key)
    : owner_(&owner),
      team_type_(team_type),
      team_key_(team_key),
      channel_subscriber_(channel_subscriber),
      is_member_(false),
      channel_create_sequence_(0),
      channel_saved_sequence_(0),
      last_exit_team_request_timepoint_(std::chrono::system_clock::from_time_t(0)),
      last_exit_team_reason_(atfw::team::EN_TEAM_EXIT_REASON_DEFAULT),
      cached_permission_role_(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(this)};
  channel_subscriber_->set_local_private_data(local_private_data);
}

user_team::~user_team() {}

user_team::ptr_t user_team::create(rpc::context& ctx, user_team_manager& owner, uint32_t team_type,
                                   const atfw::team::DTeamKey& team_key, const atfw::dtmq::DChannelIdKey& channel_key) {
  ctor_guard_t guard;
  rpc::dtmq::client_subscriber::subscriber_options options{
      owner.get_owner().get_user_chat_manager().get_subscriber_key()};

  // auto_create_channel 设为false，如果是恢复已经失效的频道，后续会通过 on_destroyed 回调移除
  options.auto_create_channel = false;
  options.event_callback_set = user_team_utility::get_event_callback_set();
  auto channel_subscriber = rpc::dtmq::client_subscriber::create(channel_key, options);
  if (!channel_subscriber) {
    return nullptr;
  }

  return atfw::component::memory::stl::make_strong_rc<user_team>(guard, ctx, owner, std::move(channel_subscriber),
                                                                 team_type, team_key);
}

void user_team::init_cached_data(const PROJECT_NAMESPACE_ID::DUserIDKey& captain_user_key,
                                 atfw::team::EnTeamPermissionRole permission_role) {
  cached_captain_user_key_ = captain_user_key;
  cached_permission_role_ = permission_role;
}

void user_team::dump(atfw::team::DTeamMemberJoinData& join_data) const {
  protobuf_copy_message(*join_data.mutable_team_key(), get_team_key());
  protobuf_copy_message(*join_data.mutable_team_channel(), get_channel_key());
  join_data.mutable_user_key()->set_user_id(owner_->get_owner().get_user_id());
  join_data.mutable_user_key()->set_zone_id(owner_->get_owner().get_zone_id());

  join_data.mutable_captain_user_key()->CopyFrom(cached_captain_user_key_);
  join_data.set_user_role(cached_permission_role_);
}

bool user_team::can_be_removed(rpc::context& ctx) const noexcept {
  // 频道已销毁，可以直接移除
  if (channel_subscriber_->is_destroyed()) {
    return true;
  }

  if (last_exit_team_request_timepoint_ <= std::chrono::system_clock::from_time_t(0)) {
    return false;
  }

  if (ctx.logical_now() >= last_exit_team_request_timepoint_ + get_exit_team_timeout()) {
    return true;
  }

  if (!channel_subscriber_->is_ready()) {
    return false;
  }

  return !is_member_;
}

bool user_team::wait_to_be_member_but_timeout(rpc::context& ctx) const noexcept {
  // 频道已销毁，可以直接视为member添加超时
  if (channel_subscriber_->is_destroyed()) {
    return true;
  }

  if (is_member_) {
    return false;
  }

  return ctx.logical_now() >= actived_timepoint_ + get_wait_add_member_timeout();
}

bool user_team::is_exiting() const noexcept {
  return last_exit_team_request_timepoint_ > std::chrono::system_clock::from_time_t(0);
}

const atfw::dtmq::DChannelIdKey& user_team::get_channel_key() const noexcept {
  return channel_subscriber_->get_channel_key();
}

bool user_team::check_permission(atfw::team::EnTeamPermissionRole checked) const noexcept {
  return cached_permission_role_ >= checked;
}

void user_team::make_current_actived(rpc::context& ctx) {
  last_exit_team_request_timepoint_ = std::chrono::system_clock::from_time_t(0);
  actived_timepoint_ = ctx.logical_now();
}

void user_team::send_exit_team_request(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason) {
  set_exit_team(ctx, exit_reason);

  // 如果频道已销毁，则不用再发送退出
  if (channel_subscriber_->is_destroyed()) {
    return;
  }

  auto self = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "user_team.send_exit_team_request", [self, exit_reason](rpc::context& child_ctx) -> rpc::result_code_type {
        rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req_body{child_ctx};
        rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp_body{child_ctx};
        protobuf_copy_message(*req_body->mutable_team_key(), self->team_key_);
        req_body->mutable_sender_user_key()->set_zone_id(self->owner_->get_owner().get_zone_id());
        req_body->mutable_sender_user_key()->set_user_id(self->owner_->get_owner().get_user_id());

        auto* remove_member_action = req_body->mutable_action()->mutable_remove_member();
        remove_member_action->mutable_team_key()->CopyFrom(self->team_key_);
        protobuf_copy_message(*remove_member_action->mutable_user_key(), req_body->sender_user_key());
        remove_member_action->set_remove_member_reason(exit_reason);

        RPC_AWAIT_IGNORE_RESULT(rpc::team::team_api::send_message(child_ctx, *req_body, *rsp_body, true));

        RPC_RETURN_CODE(0);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "team {}:{} send_exit_team_request: async_invoke failed, error={}({})", team_key_.zone_id(),
                 team_key_.team_id(), *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

void user_team::set_exit_team(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason) {
  last_exit_team_reason_ = exit_reason;
  last_exit_team_request_timepoint_ = ctx.logical_now();
}

void user_team::retry_send_exit_team_request(rpc::context& ctx) {
  if (last_exit_team_request_timepoint_ <= std::chrono::system_clock::from_time_t(0)) {
    return;
  }

  if (last_exit_team_request_timepoint_ + get_exit_team_request_retry_interval() >= ctx.logical_now()) {
    return;
  }

  send_exit_team_request(ctx, last_exit_team_reason_);
}

void user_team::try_load_snapshot(rpc::context& ctx) {
  if (!channel_subscriber_->is_ready()) {
    return;
  }

  load_snapshot(ctx);
}

bool user_team::load_dtmq_custom_data(rpc::context& ctx, const ::google::protobuf::Any& custom_data) {
  rpc::context::message_holder<atfw::team::DTeamStorage> team_snapshot{ctx};
  if (!custom_data.UnpackTo(&(*team_snapshot))) {
    FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, unpack snapshot failed, type_url: {}, error message: {}",
                 owner_->get_owner(), team_key_.zone_id(), team_key_.team_id(), custom_data.type_url(),
                 team_snapshot->InitializationErrorString());
    return false;
  }

  channel_saved_sequence_ = team_snapshot->saved_action_sequence();
  cached_captain_user_key_ = team_snapshot->captain_user_key();
  cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
  cached_configure_ = team_snapshot->configure();

  is_member_ = false;
  for (const auto& member : team_snapshot->member()) {
    if (member.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
        member.user_key().user_id() == owner_->get_owner().get_user_id()) {
      is_member_ = true;
      cached_permission_role_ = member.role();
      break;
    }
  }

  return true;
}

bool user_team::load_team_action(rpc::context& ctx, const ::atfw::team::DTeamAction& action) {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam: {
      is_member_ = false;
      cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
      owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
      break;
    }
    case atfw::team::DTeamAction::kAddMember: {
      const auto& member_data = action.add_member();
      if (member_data.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
          member_data.user_key().user_id() == owner_->get_owner().get_user_id()) {
        is_member_ = true;
        cached_permission_role_ = member_data.role();
      }
      break;
    }
    case atfw::team::DTeamAction::kRemoveMember: {
      const auto& member_data = action.remove_member();
      if (member_data.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
          member_data.user_key().user_id() == owner_->get_owner().get_user_id()) {
        is_member_ = false;
        cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
      }
      break;
    }
    case atfw::team::DTeamAction::kMemberUpdate: {
      // const auto& member_update = action.member_update();
      // if (member_update.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
      //     member_update.user_key().user_id() == owner_->get_owner().get_user_id()) {
      //   // cached_permission_role_ = member_update;
      // }
      break;
    }
    case atfw::team::DTeamAction::kMemberSetRole: {
      const auto& set_role = action.member_set_role();
      if (set_role.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
          set_role.user_key().user_id() == owner_->get_owner().get_user_id()) {
        cached_permission_role_ = set_role.role();
      }
      break;
    }
    case atfw::team::DTeamAction::kElectionCaptain: {
      cached_captain_user_key_ = action.election_captain().user_key();
      break;
    }
    case atfw::team::DTeamAction::kTeamUpdate: {
      if (action.team_update().has_configure()) {
        cached_configure_ = action.team_update().configure();
      }

      // TODO(owent): 处理EN_TEAM_SHARED_MODULE_TYPE_BATTLE+EN_TEAM_SHARED_DATA_BATTLE_MATCHING
      // 如果转移成正在matching则要发起匹配的启动/恢复流程
      break;
    }
    case atfw::team::DTeamAction::kAddInvitation:
    case atfw::team::DTeamAction::kApproveInvitation:
    case atfw::team::DTeamAction::kRejectInvitation:
    case atfw::team::DTeamAction::kAddJoinRequest:
    case atfw::team::DTeamAction::kApproveJoinRequest:
    case atfw::team::DTeamAction::kRejectJoinRequest: {
      // 队伍内的邀请和加入请求本地暂不用记录
      break;
    }
    default:
      break;
  }
  return true;
}

void user_team::load_snapshot(rpc::context& ctx) {
  FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, load a snapshot, last sequence:{}", owner_->get_owner(),
               team_key_.zone_id(), team_key_.team_id(), channel_subscriber_->get_last_message_sequence());

  channel_create_sequence_ = channel_subscriber_->get_create_sequence();

  // 加载快照
  if (!load_dtmq_custom_data(ctx, channel_subscriber_->get_custom_data_content())) {
    return;
  }

  // 回放压缩点之后的增量日志
  rpc::dtmq::client_subscriber::query_options options;
  options.start_sequence = channel_subscriber_->get_last_removed_sequence() + 1;
  channel_subscriber_->query_cached_message(
      ctx,
      [this, &ctx](const ::atfw::dtmq::DChannelMessage& message) {
        on_receive_raw_message(ctx, message);
        return true;
      },
      options);

  // 如果已经可以被删除，则直接通知 manager 移除
  if (can_be_removed(ctx)) {
    FCTXLOGINFO(ctx, "{} can_be_removed for team {}:{} after loadsnapshot", owner_->get_owner(), team_key_.zone_id(),
                team_key_.team_id());
    owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
  } else if (wait_to_be_member_but_timeout(ctx)) {
    FCTXLOGINFO(ctx, "{} wait_to_be_member_but_timeout for team {}:{} after loadsnapshot", owner_->get_owner(),
                team_key_.zone_id(), team_key_.team_id());
    owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_EXPIRED);
  }
}

void user_team::on_receive_raw_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& data) {
  if (data.sequence() <= channel_saved_sequence_) {
    // 已经处理过，直接忽略
    return;
  }

  FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, receive a raw message, sequence:{}", owner_->get_owner(),
               team_key_.zone_id(), team_key_.team_id(), data.sequence());

  switch (data.detail().command_case()) {
    case atfw::dtmq::DChannelMessageDetail::kCreate: {
      if (data.sequence() > channel_create_sequence_) {
        channel_create_sequence_ = data.sequence();
      }
      break;
    }
    case atfw::dtmq::DChannelMessageDetail::kDestroy: {
      is_member_ = false;
      owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
      break;
    }
    case atfw::dtmq::DChannelMessageDetail::kEvent: {
      rpc::context::message_holder<atfw::team::DTeamAction> team_action{ctx};
      if (!data.detail().event().UnpackTo(&(*team_action))) {
        FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, unpack event failed, type_url: {}, error message: {}",
                     owner_->get_owner(), team_key_.zone_id(), team_key_.team_id(), data.detail().event().type_url(),
                     team_action->InitializationErrorString());
        break;
      }

      load_team_action(ctx, *team_action);
      break;
    }
    case atfw::dtmq::DChannelMessageDetail::kUpdateCustomData: {
      load_dtmq_custom_data(ctx, channel_subscriber_->get_custom_data_content());
      break;
    }
    default:
      break;
  }
}

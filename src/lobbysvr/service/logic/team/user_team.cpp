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

#include <chrono>

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
}  // namespace

struct user_team::ctor_guard_t {};

// NOLINTNEXTLINE(modernize-pass-by-value)
user_team::user_team(ctor_guard_t&, user_team_manager& owner, uint32_t team_type, const atfw::team::DTeamKey& team_key,
                     const atfw::dtmq::DChannelIdKey& channel_key)
    : owner_(&owner),
      team_type_(team_type),
      team_key_(team_key),
      last_exit_team_request_timepoint_(std::chrono::system_clock::from_time_t(0)),
      last_exit_team_reason_(atfw::team::EN_TEAM_EXIT_REASON_DEFAULT) {
  rpc::dtmq::client_subscriber::subscriber_options options{
      owner.get_owner().get_user_chat_manager().get_subscriber_key()};
  options.auto_create_channel = true;
  // options.event_callback_set =
  channel_subscriber_ = rpc::dtmq::client_subscriber::create(channel_key, options);
}

user_team::~user_team() {}

user_team::ptr_t user_team::create(user_team_manager& owner, uint32_t team_type, const atfw::team::DTeamKey& team_key,
                                   const atfw::dtmq::DChannelIdKey& channel_key) {
  ctor_guard_t guard;
  return atfw::component::memory::stl::make_strong_rc<user_team>(guard, owner, team_type, team_key, channel_key);
}

void user_team::make_current_actived(rpc::context& /*ctx*/) {
  last_exit_team_request_timepoint_ = std::chrono::system_clock::from_time_t(0);
}

void user_team::send_exit_team_request(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason) {
  last_exit_team_reason_ = exit_reason;
  last_exit_team_request_timepoint_ = ctx.logical_now();

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

void user_team::retry_send_exit_team_request(rpc::context& ctx) {
  if (last_exit_team_request_timepoint_ <= std::chrono::system_clock::from_time_t(0)) {
    return;
  }

  if (last_exit_team_request_timepoint_ + get_exit_team_request_retry_interval() >= ctx.logical_now()) {
    return;
  }

  send_exit_team_request(ctx, last_exit_team_reason_);
}

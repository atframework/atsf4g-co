// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-06 12:31:24

#include "logic/chat/task_action_chat_get_all_channel.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/user.h>

#include <rpc/rpc_context.h>

#include <utility>

#include "logic/chat/user_chat_manager.h"

GAMECLIENT_SERVICE_API task_action_chat_get_all_channel::task_action_chat_get_all_channel(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAMECLIENT_SERVICE_API task_action_chat_get_all_channel::~task_action_chat_get_all_channel() {}

GAMECLIENT_SERVICE_API const char* task_action_chat_get_all_channel::name() const {
  return "task_action_chat_get_all_channel";
}

GAMECLIENT_SERVICE_API task_action_chat_get_all_channel::result_type task_action_chat_get_all_channel::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user_inst->get_user_chat_manager().foreach_channel(
      [&rsp_body, this](const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>& channel) {
        auto* metadata = rsp_body.add_channel_metadata();
        if (metadata != nullptr) {
          user_chat_manager::dump_dtmq_to_chat_channel_metadata(get_shared_context(), *channel, *metadata, true);
        }

        return true;
      });

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_SERVICE_API int task_action_chat_get_all_channel::on_success() { return get_result(); }

GAMECLIENT_SERVICE_API int task_action_chat_get_all_channel::on_failed() { return get_result(); }

// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-06 12:31:24

#include "logic/chat/task_action_chat_send_message.h"

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

GAMECLIENT_SERVICE_API task_action_chat_send_message::task_action_chat_send_message(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAMECLIENT_SERVICE_API task_action_chat_send_message::~task_action_chat_send_message() {}

GAMECLIENT_SERVICE_API const char* task_action_chat_send_message::name() const {
  return "task_action_chat_send_message";
}

GAMECLIENT_SERVICE_API task_action_chat_send_message::result_type task_action_chat_send_message::operator()() {
  rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  atfw::dtmq::DChannelIdKey dtmq_channel_key;
  int32_t response_code =
      user_chat_manager::build_dtmq_channel_key_from_chat_channel_key(req_body.channel_key(), dtmq_channel_key);

  if (response_code < 0) {
    set_response_code(response_code);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  switch (req_body.detail().command_case()) {
    case atfw::dtmq::DChannelMessageDetail::CommandCase::kText:
      response_code = RPC_AWAIT_CODE_RESULT(user_inst->get_user_chat_manager().send_text_message(
          get_shared_context(), dtmq_channel_key, req_body.detail().text()));
      break;
    case atfw::dtmq::DChannelMessageDetail::CommandCase::kEvent:
      response_code = RPC_AWAIT_CODE_RESULT(user_inst->get_user_chat_manager().send_event_message(
          get_shared_context(), dtmq_channel_key, std::move(*req_body.mutable_detail()->mutable_event())));
      break;
    default:
      response_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      break;
  }

  set_response_code(response_code);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_SERVICE_API int task_action_chat_send_message::on_success() { return get_result(); }

GAMECLIENT_SERVICE_API int task_action_chat_send_message::on_failed() { return get_result(); }

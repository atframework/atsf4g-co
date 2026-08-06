// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-06 12:31:24

#include "logic/chat/task_action_chat_channel_unsubscribe.h"

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

#include <data/player.h>

#include <utility>

GAMECLIENT_SERVICE_API task_action_chat_channel_unsubscribe::task_action_chat_channel_unsubscribe(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAMECLIENT_SERVICE_API task_action_chat_channel_unsubscribe::~task_action_chat_channel_unsubscribe() {}

GAMECLIENT_SERVICE_API const char* task_action_chat_channel_unsubscribe::name() const {
  return "task_action_chat_channel_unsubscribe";
}

GAMECLIENT_SERVICE_API task_action_chat_channel_unsubscribe::result_type
task_action_chat_channel_unsubscribe::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_SERVICE_API int task_action_chat_channel_unsubscribe::on_success() { return get_result(); }

GAMECLIENT_SERVICE_API int task_action_chat_channel_unsubscribe::on_failed() { return get_result(); }

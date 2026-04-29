// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-29 11:35:05

#include "task_action_cache_api_notify_meta.h"

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

GAMECLIENT_SERVICE_API task_action_cache_api_notify_meta::task_action_cache_api_notify_meta(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAMECLIENT_SERVICE_API task_action_cache_api_notify_meta::~task_action_cache_api_notify_meta() {}

GAMECLIENT_SERVICE_API const char* task_action_cache_api_notify_meta::name() const {
  return "task_action_cache_api_notify_meta";
}

GAMECLIENT_SERVICE_API task_action_cache_api_notify_meta::result_type task_action_cache_api_notify_meta::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(hello::err::EN_SUCCESS);
  }

  // TODO ...

  TASK_ACTION_RETURN_CODE(hello::err::EN_SUCCESS);
}

GAMECLIENT_SERVICE_API int task_action_cache_api_notify_meta::on_success() { return get_result(); }

GAMECLIENT_SERVICE_API int task_action_cache_api_notify_meta::on_failed() { return get_result(); }

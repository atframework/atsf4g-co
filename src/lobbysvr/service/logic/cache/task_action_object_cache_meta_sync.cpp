// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-24 21:34:18

#include "task_action_object_cache_meta_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

GAME_SERVICE_API task_action_object_cache_meta_sync::task_action_object_cache_meta_sync(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAME_SERVICE_API task_action_object_cache_meta_sync::~task_action_object_cache_meta_sync() {}

GAME_SERVICE_API const char* task_action_object_cache_meta_sync::name() const {
  return "task_action_object_cache_meta_sync";
}

GAME_SERVICE_API task_action_object_cache_meta_sync::result_type task_action_object_cache_meta_sync::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  // TODO ...

  TASK_ACTION_RETURN_CODE(hello::err::EN_SUCCESS);
}

GAME_SERVICE_API int task_action_object_cache_meta_sync::on_success() { return get_result(); }

GAME_SERVICE_API int task_action_object_cache_meta_sync::on_failed() { return get_result(); }

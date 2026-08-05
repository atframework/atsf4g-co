// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-04 17:26:47

#include "logic/action/task_action_subscribe.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/orbit_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <logic/room/orbit_room_manager.h>

#include <utility>

GAME_SERVICE_API task_action_subscribe::task_action_subscribe(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAME_SERVICE_API task_action_subscribe::~task_action_subscribe() {}

GAME_SERVICE_API const char* task_action_subscribe::name() const { return "task_action_subscribe"; }

GAME_SERVICE_API task_action_subscribe::result_type task_action_subscribe::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  // gamesvr 订阅房间事件：对接 WAL handle subscribe（下发快照 + 增量，DTMQ 替换后生效）
  int32_t result = orbit_room_manager::me()->subscribe_room(get_shared_context(), req_body, rsp_body);
  TASK_ACTION_RETURN_CODE(result);
}

GAME_SERVICE_API int task_action_subscribe::on_success() { return get_result(); }

GAME_SERVICE_API int task_action_subscribe::on_failed() { return get_result(); }

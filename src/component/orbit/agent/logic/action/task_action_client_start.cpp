// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-05-11 15:53:52

#include "task_action_client_start.h"

#include <logic/orbit_agent_manager.h>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/client_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <rpc/agenttocontrollerservice/agenttocontrollerservice.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

ORBIT_AGENT_SERVICE_API task_action_client_start::task_action_client_start(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_AGENT_SERVICE_API task_action_client_start::~task_action_client_start() {}

ORBIT_AGENT_SERVICE_API const char* task_action_client_start::name() const { return "task_action_client_start"; }

ORBIT_AGENT_SERVICE_API task_action_client_start::result_type task_action_client_start::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      orbit_agent_manager::me()->handle_client_start(get_shared_context(), get_request_node_id(), req_body, rsp_body)));
}

ORBIT_AGENT_SERVICE_API int task_action_client_start::on_success() { return get_result(); }

ORBIT_AGENT_SERVICE_API int task_action_client_start::on_failed() { return get_result(); }

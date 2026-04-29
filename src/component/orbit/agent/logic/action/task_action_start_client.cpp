// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-04-29 11:47:42

#include "task_action_start_client.h"

#include <logic/orbit_agent_manager.h>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/agent_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <rpc/agenttoclientservice/agenttoclientservice.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

ORBIT_AGENT_SERVICE_API task_action_start_client::task_action_start_client(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_AGENT_SERVICE_API task_action_start_client::~task_action_start_client() {}

ORBIT_AGENT_SERVICE_API const char* task_action_start_client::name() const { return "task_action_start_client"; }

ORBIT_AGENT_SERVICE_API task_action_start_client::result_type task_action_start_client::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  if (orbit_agent_manager::me()->get_controller_server_id() != get_request_node_id()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CONTROLLER_SERVER_ID_NOT_MATCH);
  }

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(orbit_agent_manager::me()->handle_start_client(get_shared_context(), req_body, rsp_body));
  TASK_ACTION_RETURN_CODE(rpc_result);
}

ORBIT_AGENT_SERVICE_API int task_action_start_client::on_success() { return get_result(); }

ORBIT_AGENT_SERVICE_API int task_action_start_client::on_failed() { return get_result(); }

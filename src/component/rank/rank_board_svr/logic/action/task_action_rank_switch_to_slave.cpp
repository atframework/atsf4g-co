// Copyright 2024 atframework
// @brief Created by marvinfang with generate-for-pb.py at 2024-12-09 10:37:42

#include "task_action_rank_switch_to_slave.h"

#include <curl/curl.h>
#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rank_board_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include "protocol/pbdesc/com.const.pb.h"

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>
#include "logic/rank_manager.h"
#include "rpc/rpc_common_types.h"

RANK_SERVICE_API task_action_rank_switch_to_slave::task_action_rank_switch_to_slave(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}

RANK_SERVICE_API task_action_rank_switch_to_slave::~task_action_rank_switch_to_slave() {}

RANK_SERVICE_API const char* task_action_rank_switch_to_slave::name() const {
  return "task_action_rank_switch_to_slave";
}

RANK_SERVICE_API task_action_rank_switch_to_slave::result_type task_action_rank_switch_to_slave::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  rank_ptr_type rank_ptr = nullptr;
  auto ret =
      RPC_AWAIT_CODE_RESULT(rank_manager::me()->mutable_rank(get_shared_context(), req_body.rank_key(), rank_ptr));
  if (ret != 0) {
    FWLOGERROR("mutable_rank failed, rank:({}:{}:{}:{}) ret:{}", req_body.rank_key().rank_type(),
               req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(),
               req_body.rank_key().sub_rank_instance_id(), ret);
    TASK_ACTION_RETURN_CODE(ret);
  }
  if (!rank_ptr) {
    FWLOGERROR("mutable_rank failed, rank:({}:{}:{}:{}) rank_ptr is nullptr", req_body.rank_key().rank_type(),
               req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(),
               req_body.rank_key().sub_rank_instance_id());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }
  auto& router_data = rank_ptr->get_router_data();
  if (router_data.router_version() > req_body.router_data().router_version()) {
    // 当前的路由数据版本比请求版本高，说明请求数据不是最新的，直接返回
    FWLOGWARNING("node:{} rank:({}:{}:{}:{}) router_data.version:{} > req.version:{}",
                 logic_config::me()->get_local_server_id(), req_body.rank_key().rank_type(),
                 req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(),
                 req_body.rank_key().sub_rank_instance_id(), router_data.router_version(),
                 req_body.router_data().router_version());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (rank_ptr->get_data_version() > req_body.data_version()) {
    // 当前的数据版本比请求版本高，理论上不应该出现这种情况，这种会出现数据覆盖（重新选主？）
    FWLOGWARNING("node:{} rank:({}:{}:{}:{}) data_version:{} > req.version:{}",
                 logic_config::me()->get_local_server_id(), req_body.rank_key().rank_type(),
                 req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(),
                 req_body.rank_key().sub_rank_instance_id(), rank_ptr->get_data_version(), req_body.data_version());
  }
  rank_ptr->switch_to_slave(get_shared_context(), req_body.router_data());
  FWLOGDEBUG("req_node:{} cur_node:{} rank:({}:{}:{}:{}) switch to slave success", get_request_node_id(),
             logic_config::me()->get_local_server_id(), req_body.rank_key().rank_type(),
             req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(),
             req_body.rank_key().sub_rank_instance_id());
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

RANK_SERVICE_API int task_action_rank_switch_to_slave::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_switch_to_slave::on_failed() { return get_result(); }

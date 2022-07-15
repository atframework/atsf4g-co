// Copyright 2021 atframework
// Created by owent with generate-for-pb.py at 2020-07-10 21:34:16

#include "router/action/task_action_router_transfer.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <memory>

#include "router/router_manager_base.h"
#include "router/router_manager_set.h"

task_action_router_transfer::task_action_router_transfer(dispatcher_start_data_t&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_router_transfer::~task_action_router_transfer() {}

bool task_action_router_transfer::is_stream_rpc() const { return false; }

task_action_router_transfer::result_type task_action_router_transfer::operator()() {
  const rpc_request_type& req_body = get_request_body();

  router_manager_base* mgr = router_manager_set::me()->get_manager(req_body.object().object_type_id());
  if (nullptr == mgr) {
    FWLOGERROR("router manager {} invalid", req_body.object().object_type_id());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  router_manager_base::key_t key(req_body.object().object_type_id(), req_body.object().object_zone_id(),
                                 req_body.object().object_inst_id());
  std::shared_ptr<router_object_base> obj = mgr->get_base_cache(key);

  // 如果本地版本号更高就不用远程拉取了
  if (obj && obj->get_router_version() >= req_body.object().router_version()) {
    if (logic_config::me()->get_local_server_id() != obj->get_router_server_id()) {
      set_response_code(PROJECT_NAMESPACE_ID::err::EN_ROUTER_IN_OTHER_SERVER);
    }

    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto res = RPC_AWAIT_CODE_RESULT(mgr->mutable_object(get_shared_context(), obj, key, nullptr));
  set_response_code(res);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_router_transfer::on_success() { return get_result(); }

int task_action_router_transfer::on_failed() { return get_result(); }

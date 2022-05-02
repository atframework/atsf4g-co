// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-01-27 10:39:20

#include "task_action_query.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include "logic/transaction_manager.h"

task_action_query::task_action_query(dispatcher_start_data_t&& param) : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_query::~task_action_query() {}

const char* task_action_query::name() const { return "task_action_query"; }

task_action_query::result_type task_action_query::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  if (req_body.metadata().transaction_uuid().empty()) {
    FWLOGERROR("try to find transaction id from request {} for {} failed.", "SSDistributeTransactionQueryReq",
               "task_action_query");
    set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  transaction_manager::transaction_ptr_type trans;
  set_response_code(RPC_AWAIT_CODE_RESULT(
      transaction_manager::me()->mutable_transaction(get_shared_context(), req_body.metadata(), trans)));
  if (0 != get_response_code()) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (!trans) {
    set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  protobuf_copy_message(*rsp_body.mutable_storage(), trans->data_object);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_query::on_success() { return get_result(); }

int task_action_query::on_failed() { return get_result(); }

// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-01-27 10:39:20

#include "task_action_reject.h"

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

task_action_reject::task_action_reject(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}
task_action_reject::~task_action_reject() {}

const char* task_action_reject::name() const { return "task_action_reject"; }

task_action_reject::result_type task_action_reject::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  if (req_body.metadata().transaction_uuid().empty()) {
    FWLOGERROR("try to find transaction id from request {} for {} failed.", "SSDistributeTransactionRejectReq",
               "task_action_reject");
    set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  transaction_manager::transaction_ptr_type trans;
  set_response_code(RPC_AWAIT_CODE_RESULT(
      transaction_manager::me()->mutable_transaction(get_shared_context(), req_body.metadata(), trans)));
  if (0 != get_response_code()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (!trans) {
    set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  set_response_code(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(get_shared_context(), trans)));
  protobuf_copy_message(*rsp_body.mutable_metadata(), trans->data_object.metadata());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_reject::on_success() { return get_result(); }

int task_action_reject::on_failed() { return get_result(); }

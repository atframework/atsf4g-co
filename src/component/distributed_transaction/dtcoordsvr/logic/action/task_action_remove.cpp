// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-01-27 10:39:20

#include "task_action_remove.h"

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

task_action_remove::task_action_remove(dispatcher_start_data_type&& param) : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_remove::~task_action_remove() {}

const char* task_action_remove::name() const { return "task_action_remove"; }

task_action_remove::result_type task_action_remove::operator()() {
  const rpc_request_type& req_body = get_request_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  if (req_body.metadata().transaction_uuid().empty()) {
    FWLOGERROR("try to find transaction uuid from request {} for {} failed.", "SSDistributeTransactionRemoveReq",
               "task_action_remove");
    set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  set_response_code(
      RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(get_shared_context(), req_body.metadata())));

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_remove::on_success() { return get_result(); }

int task_action_remove::on_failed() { return get_result(); }

// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-02-25 18:47:22

#include "task_action_reject_participator.h"

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

task_action_reject_participator::task_action_reject_participator(dispatcher_start_data_t&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_reject_participator::~task_action_reject_participator() {}

const char* task_action_reject_participator::name() const { return "task_action_reject_participator"; }

task_action_reject_participator::result_type task_action_reject_participator::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  if (req_body.metadata().transaction_uuid().empty()) {
    FWLOGERROR("try to find transaction id from request {} for {} failed.",
               "SSDistributeTransactionRejectParticipatorReq", "task_action_reject_participator");
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

  set_response_code(RPC_AWAIT_CODE_RESULT(
      transaction_manager::me()->try_reject(get_shared_context(), trans, req_body.participator_key())));
  protobuf_copy_message(*rsp_body.mutable_metadata(), trans->data_object.metadata());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_reject_participator::on_success() { return get_result(); }

int task_action_reject_participator::on_failed() { return get_result(); }

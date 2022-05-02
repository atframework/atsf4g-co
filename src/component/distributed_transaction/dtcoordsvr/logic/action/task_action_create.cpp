// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-01-27 10:39:20

#include "task_action_create.h"

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

task_action_create::task_action_create(dispatcher_start_data_t&& param) : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_create::~task_action_create() {}

const char* task_action_create::name() const { return "task_action_create"; }

task_action_create::result_type task_action_create::operator()() {
  rpc_request_type& req_body = get_request_body();

  set_response_code(RPC_AWAIT_CODE_RESULT(
      transaction_manager::me()->create_transaction(get_shared_context(), std::move(*req_body.mutable_storage()))));

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_create::on_success() { return get_result(); }

int task_action_create::on_failed() { return get_result(); }

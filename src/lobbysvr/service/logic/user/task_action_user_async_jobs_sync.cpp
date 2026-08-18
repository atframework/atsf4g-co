// Copyright 2022 atframework
// Created by owent with generate-for-pb.py at 2021-10-30 00:43:04
//

#include "logic/user/task_action_user_async_jobs_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <logic/user_manager.h>

#include <rpc/rpc_context.h>

#include <utility>

#include "data/user.h"
#include "logic/async_jobs/user_async_jobs_manager.h"

task_action_user_async_jobs_sync::task_action_user_async_jobs_sync(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}
task_action_user_async_jobs_sync::~task_action_user_async_jobs_sync() {}

const char* task_action_user_async_jobs_sync::name() const { return "task_action_user_async_jobs_sync"; }

task_action_user_async_jobs_sync::result_type task_action_user_async_jobs_sync::operator()() {
  msg_cref_type req_msg = get_request();
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  uint64_t user_id = req_msg.head().user_user_id();
  uint32_t zone_id = req_msg.head().user_zone_id();

  auto user_inst = user_manager::me()->find_as<user>(user_id, zone_id);
  if (user_inst) {
    if (req_body.job_type() != 0) {
      user_inst->get_user_async_jobs_manager().force_async_job(req_body.job_type());
    }
    user_inst->get_user_async_jobs_manager().try_async_jobs(get_shared_context());
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_user_async_jobs_sync::on_success() { return get_result(); }

int task_action_user_async_jobs_sync::on_failed() { return get_result(); }

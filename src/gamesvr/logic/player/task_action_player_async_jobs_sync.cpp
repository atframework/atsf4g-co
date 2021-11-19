/**
 * @brief Created by owent with generate-for-pb.py at 2021-10-30 00:43:04
 */

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include "task_action_player_async_jobs_sync.h"

task_action_player_async_jobs_sync::task_action_player_async_jobs_sync(dispatcher_start_data_t&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_player_async_jobs_sync::~task_action_player_async_jobs_sync() {}

const char* task_action_player_async_jobs_sync::name() const { return "task_action_player_async_jobs_sync"; }

task_action_player_async_jobs_sync::result_type task_action_player_async_jobs_sync::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  // TODO ...

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_player_async_jobs_sync::on_success() { return get_result(); }

int task_action_player_async_jobs_sync::on_failed() { return get_result(); }

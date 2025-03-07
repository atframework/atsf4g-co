// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-01 11:05:42

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <ctime>

#include "task_action_set_server_time.h"

task_action_set_server_time::task_action_set_server_time(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}
task_action_set_server_time::~task_action_set_server_time() {}

const char* task_action_set_server_time::name() const { return "task_action_set_server_time"; }

task_action_set_server_time::result_type task_action_set_server_time::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::seconds(req_body.global_now_offset())));

  FWLOGINFO("Reset global time offset to {}", req_body.global_now_offset());
  struct tm tt;
  time_t now = atfw::util::time::time_utility::get_now();
  UTIL_STRFUNC_LOCALTIME_S(&now, &tt);
  char str[256] = {0};
  strftime(str, sizeof(str) - 1, "%Y-%m-%d %H:%M:%S", &tt);
  FWLOGWARNING("Current time: ", str);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_set_server_time::on_success() { return get_result(); }

int task_action_set_server_time::on_failed() { return get_result(); }

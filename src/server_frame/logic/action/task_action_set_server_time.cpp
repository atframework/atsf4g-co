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

task_action_set_server_time::task_action_set_server_time(dispatcher_start_data_t&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_set_server_time::~task_action_set_server_time() {}

const char* task_action_set_server_time::name() const { return "task_action_set_server_time"; }

int task_action_set_server_time::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_rsp_msg();

  ::util::time::time_utility::set_global_now_offset(std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::seconds(req_body.global_now_offset())));

  FWLOGINFO("Reset global time offset to {}", req_body.global_now_offset());
  struct tm tt;
  time_t now = ::util::time::time_utility::get_now();
  UTIL_STRFUNC_LOCALTIME_S(&now, &tt);
  char str[256] = {0};
  strftime(str, sizeof(str) - 1, "%Y-%m-%d %H:%M:%S", &tt);
  FWLOGWARNING("Current time: ", str);

  return hello::err::EN_SUCCESS;
}

int task_action_set_server_time::on_success() { return get_ret_code(); }

int task_action_set_server_time::on_failed() { return get_ret_code(); }

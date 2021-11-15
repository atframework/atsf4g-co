// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:33:44

#include "logic/player/task_action_player_get_info.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/player.h>

task_action_player_get_info::task_action_player_get_info(dispatcher_start_data_t&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_player_get_info::~task_action_player_get_info() {}

const char* task_action_player_get_info::name() const { return "task_action_player_get_info"; }

int task_action_player_get_info::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }

  // TODO ...

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_player_get_info::on_success() { return get_result(); }

int task_action_player_get_info::on_failed() { return get_result(); }

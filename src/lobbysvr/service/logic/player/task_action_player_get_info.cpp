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

task_action_player_get_info::task_action_player_get_info(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}
task_action_player_get_info::~task_action_player_get_info() {}

const char* task_action_player_get_info::name() const { return "task_action_player_get_info"; }

task_action_player_get_info::result_type task_action_player_get_info::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  const auto& handle_list = player::get_get_info_handle();
  for (const auto& handle : handle_list) {
    if ((req_body.*(handle.first))()) {
      (handle.second)(get_shared_context(), rsp_body, *user);
    }
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_player_get_info::on_success() { return get_result(); }

int task_action_player_get_info::on_failed() { return get_result(); }

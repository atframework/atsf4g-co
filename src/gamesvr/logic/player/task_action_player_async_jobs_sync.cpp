// Copyright 2022 atframework
// Created by owent with generate-for-pb.py at 2021-10-30 00:43:04
//

#include "logic/player/task_action_player_async_jobs_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <logic/player_manager.h>

#include "data/player.h"
#include "logic/async_jobs/user_async_jobs_manager.h"

task_action_player_async_jobs_sync::task_action_player_async_jobs_sync(dispatcher_start_data_t&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_player_async_jobs_sync::~task_action_player_async_jobs_sync() {}

const char* task_action_player_async_jobs_sync::name() const { return "task_action_player_async_jobs_sync"; }

task_action_player_async_jobs_sync::result_type task_action_player_async_jobs_sync::operator()() {
  msg_cref_type req_msg = get_request();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  uint64_t user_id = req_msg.head().player_user_id();
  uint32_t zone_id = req_msg.head().player_zone_id();

  auto user = player_manager::me()->find_as<player>(user_id, zone_id);
  if (user) {
    user->get_user_async_jobs_manager().try_async_jobs(get_shared_context());
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_player_async_jobs_sync::on_success() { return get_result(); }

int task_action_player_async_jobs_sync::on_failed() { return get_result(); }

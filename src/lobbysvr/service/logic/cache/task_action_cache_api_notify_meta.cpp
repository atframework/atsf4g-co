// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-29 11:35:05

#include "task_action_cache_api_notify_meta.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>

#include <data/user.h>
#include <logic/cache/user_cache_manager.h>

task_action_cache_api_notify_meta::task_action_cache_api_notify_meta(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_cache_api_notify_meta::~task_action_cache_api_notify_meta() {}

const char* task_action_cache_api_notify_meta::name() const {
  return "task_action_cache_api_notify_meta";
}

task_action_cache_api_notify_meta::result_type task_action_cache_api_notify_meta::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "{}", "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // Client can only trigger self's cache expired
  if (req_body.cache_type() == PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER) {
    user_inst->get_user_cache_manager().set_user_meta_expired();
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_cache_api_notify_meta::on_success() { return get_result(); }

int task_action_cache_api_notify_meta::on_failed() { return get_result(); }

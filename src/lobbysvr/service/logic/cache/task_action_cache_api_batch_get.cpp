// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-29 11:35:05

#include "task_action_cache_api_batch_get.h"

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

#include <data/player.h>
#include <logic/cache/user_cache_manager.h>

GAMECLIENT_SERVICE_API task_action_cache_api_batch_get::task_action_cache_api_batch_get(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAMECLIENT_SERVICE_API task_action_cache_api_batch_get::~task_action_cache_api_batch_get() {}

GAMECLIENT_SERVICE_API const char* task_action_cache_api_batch_get::name() const {
  return "task_action_cache_api_batch_get";
}

GAMECLIENT_SERVICE_API task_action_cache_api_batch_get::result_type task_action_cache_api_batch_get::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  rsp_body.set_client_tag(req_body.client_tag());
  if (0 == req_body.keys_with_status().size()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> query_keys;
  query_keys.Reserve(req_body.keys_with_status().size());

  for (const auto& key : req_body.keys_with_status()) {
    if (key.zone_id() == 0 && key.instance_id() == 0) {
      continue;
    }
    auto unit = query_keys.Add();
    if (nullptr != unit) {
      unit->set_get_type(key.get_type());
      unit->mutable_cache_key()->set_cache_type(req_body.cache_type());
      unit->mutable_cache_key()->set_zone_id(key.zone_id());
      unit->mutable_cache_key()->set_instance_id(key.instance_id());
    }
  }

  if (0 == query_keys.size()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  // ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheKey> not_found_keys;
  auto ret = RPC_AWAIT_CODE_RESULT(user->get_user_cache_manager().pull_cache(
      get_shared_context(), query_keys, *rsp_body.mutable_results(), true, rsp_body.mutable_not_found_keys()));
  if (ret < 0) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    TASK_ACTION_RETURN_CODE(ret);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_SERVICE_API int task_action_cache_api_batch_get::on_success() { return get_result(); }

GAMECLIENT_SERVICE_API int task_action_cache_api_batch_get::on_failed() { return get_result(); }

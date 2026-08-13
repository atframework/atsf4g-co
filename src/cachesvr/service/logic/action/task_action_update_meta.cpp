// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-11-03 23:57:58

#include "task_action_update_meta.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/cache_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <rpc/cache/cache_api.h>

#include <logic/cache_group.h>
#include <logic/cache_group_manager.h>

task_action_update_meta::task_action_update_meta(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_update_meta::~task_action_update_meta() {}

const char* task_action_update_meta::name() const { return "task_action_update_meta"; }

task_action_update_meta::result_type task_action_update_meta::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  for (int i = 0; i < req_body.object_metas_size(); ++i) {
    const PROJECT_NAMESPACE_ID::object_cache_meta& meta = req_body.object_metas(i);
    PROJECT_NAMESPACE_ID::object_cache_key key;
    rpc::cache_api::pick_key_from_meta(get_shared_context(), key, meta.cache_meta());
    if (key.cache_type() == PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_UNKNOWN) {
      FWLOGERROR("invalid meta data {}", protobuf_mini_dumper_get_readable(meta));
      continue;
    }

    cache_group_base* group = cache_group_manager::me()->get_group(key.cache_type());
    if (nullptr == group) {
      FWLOGERROR("invalid cache group {}", static_cast<uint32_t>(key.cache_type()));
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      continue;
    }

    RPC_AWAIT_IGNORE_RESULT(group->update_meta(get_shared_context(), key, meta));
  }

  TASK_ACTION_RETURN_CODE(ret);
}

int task_action_update_meta::on_success() { return get_result(); }

int task_action_update_meta::on_failed() { return get_result(); }

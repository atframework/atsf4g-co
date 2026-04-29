// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-29 11:35:05

#pragma once

#include <config/compile_optimize.h>

#include <dispatcher/task_action_cs_req_base.h>

#ifndef GAMECLIENT_SERVICE_API
#  define GAMECLIENT_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_cache_api_unwatch
    : public task_action_cs_rpc_base<hello::CSCacheApiUnwatchReq, hello::SCCacheApiUnwatchRsp> {
 public:
  using base_type = task_action_cs_rpc_base<hello::CSCacheApiUnwatchReq, hello::SCCacheApiUnwatchRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  GAMECLIENT_SERVICE_API explicit task_action_cache_api_unwatch(dispatcher_start_data_type&& param);
  GAMECLIENT_SERVICE_API ~task_action_cache_api_unwatch();

  GAMECLIENT_SERVICE_API const char* name() const override;

  GAMECLIENT_SERVICE_API result_type operator()() override;

  GAMECLIENT_SERVICE_API int on_success() override;
  GAMECLIENT_SERVICE_API int on_failed() override;
};

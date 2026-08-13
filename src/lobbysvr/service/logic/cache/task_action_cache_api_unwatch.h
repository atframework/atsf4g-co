// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-29 11:35:05

#pragma once

#include <config/compile_optimize.h>

#include <dispatcher/task_action_cs_req_base.h>



class task_action_cache_api_unwatch : public task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSCacheApiUnwatchReq,
                                                                     PROJECT_NAMESPACE_ID::SCCacheApiUnwatchRsp> {
 public:
  using base_type =
      task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSCacheApiUnwatchReq, PROJECT_NAMESPACE_ID::SCCacheApiUnwatchRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  explicit task_action_cache_api_unwatch(dispatcher_start_data_type&& param);
  ~task_action_cache_api_unwatch();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};

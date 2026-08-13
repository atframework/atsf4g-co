// Copyright 2026 atframework
// Created by owent with generate-for-pb.py at 2020-12-15 16:29:08
//

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/cache_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#include <rpc/cache/cache_algorithm.h>

class task_action_pull_caches : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSCachePullCacheReq,
                                                               PROJECT_NAMESPACE_ID::SSCachePullCacheRsp> {
 public:
  using base_type =
      task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSCachePullCacheReq, PROJECT_NAMESPACE_ID::SSCachePullCacheRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  task_action_pull_caches(dispatcher_start_data_type&& param);
  ~task_action_pull_caches();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};

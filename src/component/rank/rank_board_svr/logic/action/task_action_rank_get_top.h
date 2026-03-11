// Copyright 2024 atframework
// @brief Created by jijunliang with generate-for-pb.py at 2024-11-14 16:54:56

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rank_board_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef RANK_SERVICE_API
#  define RANK_SERVICE_API UTIL_SYMBOL_VISIBLE
#endif

class task_action_rank_get_top : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSRankGetTopReq, PROJECT_NAMESPACE_ID::SSRankGetTopRsp> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSRankGetTopReq, PROJECT_NAMESPACE_ID::SSRankGetTopRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  RANK_SERVICE_API explicit task_action_rank_get_top(dispatcher_start_data_type&& param);
  RANK_SERVICE_API ~task_action_rank_get_top();

  RANK_SERVICE_API const char* name() const override;

  RANK_SERVICE_API result_type operator()() override;

  RANK_SERVICE_API int on_success() override;
  RANK_SERVICE_API int on_failed() override;
};

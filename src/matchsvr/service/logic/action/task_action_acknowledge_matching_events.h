// Copyright 2026 atframework
// @brief Created by jijunliang with mako-generator.py at 2026-08-31 16:09:40

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/match_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API
#  define ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_acknowledge_matching_events : public task_action_ss_rpc_base<atframework::shared::SSMatchingEventAckReq, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::shared::SSMatchingEventAckReq, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type  = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API explicit task_action_acknowledge_matching_events(dispatcher_start_data_type&& param);
  ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API ~task_action_acknowledge_matching_events() override;

  ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API const char *name() const override;

  ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API result_type operator()() override;

  ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API int on_success() override;
  ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API int on_failed() override;

};

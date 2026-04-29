// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-04-29 11:47:42

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/server_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ORBIT_MASTER_SERVICE_API
#  define ORBIT_MASTER_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_client_end_notify
    : public task_action_ss_rpc_base<orbit::CTMClientEndNotify, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<orbit::CTMClientEndNotify, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ORBIT_MASTER_SERVICE_API explicit task_action_client_end_notify(dispatcher_start_data_type&& param);
  ORBIT_MASTER_SERVICE_API ~task_action_client_end_notify();

  ORBIT_MASTER_SERVICE_API const char* name() const override;

  ORBIT_MASTER_SERVICE_API result_type operator()() override;

  ORBIT_MASTER_SERVICE_API int on_success() override;
  ORBIT_MASTER_SERVICE_API int on_failed() override;
};

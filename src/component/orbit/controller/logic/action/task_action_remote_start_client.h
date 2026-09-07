// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-09-07 13:38:50

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/controller_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API
#  define ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_remote_start_client : public task_action_ss_rpc_base<atframework::orbit::ATCRemoteStartClientReq,
                                                                       atframework::orbit::CTARemoteStartClientRsp> {
 public:
  using base_type =
      task_action_ss_rpc_base<atframework::orbit::ATCRemoteStartClientReq, atframework::orbit::CTARemoteStartClientRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API explicit task_action_remote_start_client(
      dispatcher_start_data_type&& param);
  ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API ~task_action_remote_start_client() override;

  ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API const char* name() const override;

  ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API result_type operator()() override;

  ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API int on_success() override;
  ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API int on_failed() override;
};

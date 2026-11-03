// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-11-02 14:32:30

#pragma once

#include <config/compile_optimize.h>
// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.orbit.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <logic/task_action_orbit_req_base.h>

#ifndef ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API
#  define ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_user_end_join : public task_action_orbit_rpc_base<atframework::shared::OrbitServerUserEndJoinReq,
                                                                    atframework::shared::OrbitServerUserEndJoinRsp> {
 public:
  using base_type = task_action_orbit_rpc_base<atframework::shared::OrbitServerUserEndJoinReq,
                                               atframework::shared::OrbitServerUserEndJoinRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using base_type::operator();

 public:
  ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API explicit task_action_user_end_join(dispatcher_start_data_type &&param);
  ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API ~task_action_user_end_join() override;

  ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API const char *name() const override;

  ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API result_type operator()() override;

  ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API int on_success() override;
  ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API int on_failed() override;
};

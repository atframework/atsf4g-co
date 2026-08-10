// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-04 17:26:47

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/orbit_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef LOBBY_SERVICE_API
#  define LOBBY_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_join_room : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomReq,
                                                             PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomRsp> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomReq,
                                            PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  LOBBY_SERVICE_API explicit task_action_join_room(dispatcher_start_data_type&& param);
  LOBBY_SERVICE_API ~task_action_join_room() override;

  LOBBY_SERVICE_API const char* name() const override;

  LOBBY_SERVICE_API result_type operator()() override;

  LOBBY_SERVICE_API int on_success() override;
  LOBBY_SERVICE_API int on_failed() override;
};

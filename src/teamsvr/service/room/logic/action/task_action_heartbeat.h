// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-10 20:11:30

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef TEAM_SERVICE_ROOM_API
#  define TEAM_SERVICE_ROOM_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_heartbeat : public task_action_ss_rpc_base<atframework::team::SSTeamRoomHeartbeatReq,
                                                             atframework::team::SSTeamRoomHeartbeatRsp> {
 public:
  using base_type =
      task_action_ss_rpc_base<atframework::team::SSTeamRoomHeartbeatReq, atframework::team::SSTeamRoomHeartbeatRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  TEAM_SERVICE_ROOM_API explicit task_action_heartbeat(dispatcher_start_data_type&& param);
  TEAM_SERVICE_ROOM_API ~task_action_heartbeat() override;

  TEAM_SERVICE_ROOM_API const char* name() const override;

  TEAM_SERVICE_ROOM_API result_type operator()() override;

  TEAM_SERVICE_ROOM_API int on_success() override;
  TEAM_SERVICE_ROOM_API int on_failed() override;
};

// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-20 19:15:11

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

#ifndef ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API
#  define ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_add_join_request : public task_action_ss_rpc_base<atframework::team::SSTeamRoomAddJoinRequestReq,
                                                                    atframework::team::SSTeamRoomAddJoinRequestRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::team::SSTeamRoomAddJoinRequestReq,
                                            atframework::team::SSTeamRoomAddJoinRequestRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API explicit task_action_add_join_request(dispatcher_start_data_type&& param);
  ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API ~task_action_add_join_request() override;

  ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API const char* name() const override;

  ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API result_type operator()() override;

  ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API int on_success() override;
  ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API int on_failed() override;
};

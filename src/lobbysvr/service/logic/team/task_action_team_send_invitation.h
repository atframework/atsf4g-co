// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-11 21:34:28

#pragma once

#include <config/compile_optimize.h>

#include <dispatcher/task_action_cs_req_base.h>

#ifndef GAMECLIENT_SERVICE_API
#  define GAMECLIENT_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_team_send_invitation : public task_action_cs_rpc_base<atframework::team::CSTeamSendInvitationReq,
                                                                        atframework::team::SCTeamSendInvitationRsp> {
 public:
  using base_type =
      task_action_cs_rpc_base<atframework::team::CSTeamSendInvitationReq, atframework::team::SCTeamSendInvitationRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  GAMECLIENT_SERVICE_API explicit task_action_team_send_invitation(dispatcher_start_data_type&& param);
  GAMECLIENT_SERVICE_API ~task_action_team_send_invitation() override;

  GAMECLIENT_SERVICE_API const char* name() const override;

  GAMECLIENT_SERVICE_API result_type operator()() override;

  GAMECLIENT_SERVICE_API int on_success() override;
  GAMECLIENT_SERVICE_API int on_failed() override;
};

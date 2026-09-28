// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-28 17:11:31

#pragma once

#include <config/compile_optimize.h>

#include <dispatcher/task_action_cs_req_base.h>

#ifndef ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API
#  define ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_friend_get_all : public task_action_cs_rpc_base<atframework::friend_api::CSFriendGetAllReq,
                                                                  atframework::friend_api::SCFriendGetAllRsp> {
 public:
  using base_type =
      task_action_cs_rpc_base<atframework::friend_api::CSFriendGetAllReq, atframework::friend_api::SCFriendGetAllRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API explicit task_action_friend_get_all(dispatcher_start_data_type&& param);
  ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API ~task_action_friend_get_all() override;

  ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* name() const override;

  ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API result_type operator()() override;

  ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int on_success() override;
  ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int on_failed() override;
};

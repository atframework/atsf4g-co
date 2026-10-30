// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-10-30 21:17:11

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API
#  define ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_mail_update_global_mails
    : public task_action_ss_rpc_base<atframework::shared::SSMailUpdateGlobalMailsSync, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::shared::SSMailUpdateGlobalMailsSync, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API explicit task_action_mail_update_global_mails(
      dispatcher_start_data_type&& param);
  ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API ~task_action_mail_update_global_mails() override;

  ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API const char* name() const override;

  ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API result_type operator()() override;

  ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API int on_success() override;
  ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API int on_failed() override;
};

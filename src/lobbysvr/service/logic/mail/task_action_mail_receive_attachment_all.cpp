// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-11-02 11:45:32

#include "logic/mail/task_action_mail_receive_attachment_all.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel_config_mail_index.h>
#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>

#include <utility>

#include "data/user.h"
#include "logic/mail/user_mail_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API
task_action_mail_receive_attachment_all::task_action_mail_receive_attachment_all(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API
task_action_mail_receive_attachment_all::~task_action_mail_receive_attachment_all() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_mail_receive_attachment_all::name() const {
  return "task_action_mail_receive_attachment_all";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_mail_receive_attachment_all::result_type
task_action_mail_receive_attachment_all::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (req_body.major_type() <= 0 || !excel::mail::is_valid_user_mail(req_body.major_type())) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_INVALID_MAJOR_TYPE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int res = RPC_AWAIT_CODE_RESULT(user_inst->get_user_mail_manager().wait_for_async_task(get_shared_context()));
  if (res < 0) {
    FCTXLOGERROR(get_shared_context(), "{} wait for async mail task failed, res: {}({})", *user_inst, res,
                 protobuf_mini_dumper_get_error_msg(res));
    if (res == PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT) {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT);
    } else {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }
    TASK_ACTION_RETURN_CODE(res);
  }

  int32_t rsp_code = RPC_AWAIT_CODE_RESULT(
      user_inst->get_user_mail_manager().receive_all(get_shared_context(), req_body.major_type(), req_body.minor_type(),
                                                     rsp_body.mutable_mails(), req_body.need_remove()));
  set_response_code(rsp_code);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_mail_receive_attachment_all::on_success() {
  return get_result();
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_mail_receive_attachment_all::on_failed() {
  return get_result();
}

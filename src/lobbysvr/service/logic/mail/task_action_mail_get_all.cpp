// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-11-02 11:45:32

#include "logic/mail/task_action_mail_get_all.h"

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

#include <algorithm>
#include <utility>
#include <vector>

#include "data/user.h"
#include "logic/mail/user_mail_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_mail_get_all::task_action_mail_get_all(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_mail_get_all::~task_action_mail_get_all() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_mail_get_all::name() const {
  return "task_action_mail_get_all";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_mail_get_all::result_type
task_action_mail_get_all::operator()() {
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
  if (req_body.page_no() < 0 || req_body.page_count() < 0) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  rsp_body.set_page_count(req_body.page_count());
  rsp_body.set_page_no(req_body.page_no());
  rsp_body.set_major_type(req_body.major_type());

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

  int64_t skip_count = 0;
  if (req_body.page_count() > 0 && req_body.page_no() > 1) {
    skip_count = (static_cast<int64_t>(req_body.page_no()) - 1) * req_body.page_count();
  }

  std::vector<mail_data_ptr_t> visible_mails;
  auto mail_box = user_inst->get_user_mail_manager().get_mail_box_by_major_type(req_body.major_type());
  if (mail_box) {
    visible_mails.reserve(mail_box->mails.size());
    for (auto& mail : mail_box->mails) {
      if (user_mail_manager::is_mail_shown(get_shared_context(), mail.second)) {
        visible_mails.push_back(mail.second);
      }
    }
  }
  std::sort(visible_mails.begin(), visible_mails.end(), [](const mail_data_ptr_t& left, const mail_data_ptr_t& right) {
    auto left_delivery_time = protobuf_to_system_clock(left->record->delivery_time());
    auto right_delivery_time = protobuf_to_system_clock(right->record->delivery_time());
    if (left_delivery_time != right_delivery_time) {
      return left_delivery_time > right_delivery_time;
    }
    return left->record->mail_id() > right->record->mail_id();
  });

  rsp_body.set_mail_count(static_cast<int32_t>(visible_mails.size()));
  size_t start_index = 0;
  if (skip_count > 0) {
    if (static_cast<uint64_t>(skip_count) >= visible_mails.size()) {
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    start_index = static_cast<size_t>(skip_count);
  }
  for (size_t index = start_index; index < visible_mails.size(); ++index) {
    if (req_body.page_count() > 0 && rsp_body.mails_size() >= req_body.page_count()) {
      break;
    }

    PROJECT_NAMESPACE_ID::DMailContent* out = rsp_body.add_mails();
    if (out == nullptr) {
      break;
    }

    atfw::mail::mail_merge_content_and_record(get_shared_context(), *out, *visible_mails[index]->content,
                                              *visible_mails[index]->record);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_mail_get_all::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_mail_get_all::on_failed() { return get_result(); }

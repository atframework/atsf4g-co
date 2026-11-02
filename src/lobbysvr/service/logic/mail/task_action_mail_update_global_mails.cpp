// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-11-02 21:09:59

#include "logic/mail/task_action_mail_update_global_mails.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

namespace {
namespace details {
static atfw::util::lock::spin_rw_lock& get_global_mails_lock() {
  static atfw::util::lock::spin_rw_lock g_lock;
  return g_lock;
}

static std::function<rpc::result_code_type(::rpc::context&)>& get_global_mails_func() {
  static std::function<rpc::result_code_type(::rpc::context&)> g_functor;
  return g_functor;
}

static std::function<rpc::result_code_type(::rpc::context&)> get_callback() {
  atfw::util::lock::read_lock_holder<atfw::util::lock::spin_rw_lock> lock_guard{get_global_mails_lock()};
  return get_global_mails_func();
}

static void set_callback(std::function<rpc::result_code_type(::rpc::context&)> fn) {
  atfw::util::lock::write_lock_holder<atfw::util::lock::spin_rw_lock> lock_guard{get_global_mails_lock()};
  get_global_mails_func() = std::move(fn);
}
}  // namespace details
}  // namespace

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API task_action_mail_update_global_mails::task_action_mail_update_global_mails(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API task_action_mail_update_global_mails::~task_action_mail_update_global_mails() {}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API const char* task_action_mail_update_global_mails::name() const {
  return "task_action_mail_update_global_mails";
}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API task_action_mail_update_global_mails::result_type
task_action_mail_update_global_mails::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  auto fn = details::get_callback();
  if (fn) {
    auto result = RPC_AWAIT_CODE_RESULT(fn(get_shared_context()));
    TASK_ACTION_RETURN_CODE(result);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API int task_action_mail_update_global_mails::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API int task_action_mail_update_global_mails::on_failed() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API void task_action_mail_update_global_mails::set_callback(
    std::function<rpc::result_code_type(::rpc::context&)> fn) {
  details::set_callback(std::move(fn));
}

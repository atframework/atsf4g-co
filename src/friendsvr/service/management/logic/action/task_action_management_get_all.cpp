// Copyright 2026 atframework

#include "logic/action/task_action_management_get_all.h"

#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility>

task_action_management_get_all::task_action_management_get_all(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}
task_action_management_get_all::~task_action_management_get_all() = default;

const char* task_action_management_get_all::name() const { return "task_action_management_get_all"; }

task_action_management_get_all::result_type task_action_management_get_all::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  // TODO(friendsvr): 实现好友管理获取全部好友逻辑。

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_management_get_all::on_success() { return get_result(); }
int task_action_management_get_all::on_failed() { return get_result(); }

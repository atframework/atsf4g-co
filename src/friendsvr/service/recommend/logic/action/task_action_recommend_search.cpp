// Copyright 2026 atframework

#include "logic/action/task_action_recommend_search.h"

#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility>

task_action_recommend_search::task_action_recommend_search(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}
task_action_recommend_search::~task_action_recommend_search() = default;

const char* task_action_recommend_search::name() const { return "task_action_recommend_search"; }

task_action_recommend_search::result_type task_action_recommend_search::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  // TODO(friendsvr): 实现好友推荐搜索逻辑。

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_recommend_search::on_success() { return get_result(); }
int task_action_recommend_search::on_failed() { return get_result(); }

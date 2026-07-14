// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_find_message.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

#include "data/mq_channel.h"
#include "logic/mq_channel_manager.h"

DTMQ_PROXY_SERVICE_API task_action_find_message::task_action_find_message(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_find_message::~task_action_find_message() {}

DTMQ_PROXY_SERVICE_API const char* task_action_find_message::name() const { return "task_action_find_message"; }

DTMQ_PROXY_SERVICE_API task_action_find_message::result_type task_action_find_message::operator()() {
  rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  mq_channel_manager::mq_channel_ptr_type channel;
  uint64_t forward_server_id = 0;
  auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel(
      get_shared_context(), channel, forward_server_id, req_body.channel_key(), false));
  if (res < 0) {
    set_response_code(res);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 请求转发
  if (0 != forward_server_id) {
    bool forward_ok = false;
    TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(forward_rpc(forward_server_id, true, forward_ok)));
  }

  if (!channel) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
  }

  // find_message 会创建channel
  auto ret = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->find_message(
      get_shared_context(), channel, req_body.sequence(), *rsp_body.mutable_channel_message()));
  rsp_body.set_client_result(ret);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_find_message::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_find_message::on_failed() { return get_result(); }

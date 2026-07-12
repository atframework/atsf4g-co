// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_destroy_channel.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/dtmq_proxy.config.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

#include "rpc/dtmq/dtmq_client_api.h"

#include "data/mq_channel.h"
#include "logic/mq_channel_manager.h"

DTMQ_PROXY_SERVICE_API task_action_destroy_channel::task_action_destroy_channel(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_destroy_channel::~task_action_destroy_channel() {}

DTMQ_PROXY_SERVICE_API const char* task_action_destroy_channel::name() const { return "task_action_destroy_channel"; }

DTMQ_PROXY_SERVICE_API task_action_destroy_channel::result_type task_action_destroy_channel::operator()() {
  rpc_request_type& req_body = get_request_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  const auto& channel_key = req_body.channel_key();
  mq_channel_manager::mq_channel_ptr_type channel;

  uint64_t forward_server_id = 0;
  auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
      get_shared_context(), channel, forward_server_id, channel_key, false));

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

  channel->tick(get_shared_context());

  if (req_body.has_compare_and_maybe_reset_lock() &&
      !channel->compare_and_maybe_reset_lock(get_shared_context(), *req_body.mutable_compare_and_maybe_reset_lock(),
                                             true)) {
    FWLOGDEBUG("channel {} ignore destroy because lock failed:\n{}", req_body.channel_key().channel_id(),
               req_body.compare_and_maybe_reset_lock().DebugString());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  res = RPC_AWAIT_CODE_RESULT(channel->destroy(get_shared_context()));
  if (res < 0) {
    FWLOGERROR("destroy chat channel {} failed, res: {}({})", channel_key.channel_id(), res,
               protobuf_mini_dumper_get_error_msg(res));
  }
  set_response_code(res);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_destroy_channel::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_destroy_channel::on_failed() { return get_result(); }

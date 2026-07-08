// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_reset_lock.h"

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

DTMQ_PROXY_SERVICE_API
task_action_reset_lock::task_action_reset_lock(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_reset_lock::~task_action_reset_lock() {}

DTMQ_PROXY_SERVICE_API const char* task_action_reset_lock::name() const { return "task_action_reset_lock"; }

DTMQ_PROXY_SERVICE_API task_action_reset_lock::result_type task_action_reset_lock::operator()() {
  rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  mq_channel_manager::mq_channel_ptr_type channel;
  uint64_t forward_server_id = 0;
  auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
      get_shared_context(), channel, forward_server_id, req_body.channel_key(), req_body.auto_create_channel()));
  if (res < 0) {
    set_response_code(res);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 请求转发
  if (0 != forward_server_id) {
    const auto& dtmq_proxysvr_cfg =
        logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

    if (req_body.forward_ttl() > dtmq_proxysvr_cfg.forward_request_max_ttl()) {
      FWLOGERROR("forward channel {} forward reset lock to {} failed, transfer ttl exceeded",
                 req_body.channel_key().channel_id(), forward_server_id);
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_FORWARD_TTL_EXCEEDED_LIMIT);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_FORWARD_TTL_EXCEEDED_LIMIT);
    }

    FWLOGDEBUG("channel {} forward reset lock to {}", req_body.channel_key().channel_id(), forward_server_id);
    req_body.set_forward_ttl(req_body.forward_ttl() + 1);
    TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
        rpc::dtmq::reset_lock(get_shared_context(), forward_server_id, req_body, rsp_body, is_stream_rpc())));
  }

  if (!channel) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
  }

  if (req_body.has_compare_and_maybe_reset_lock() &&
      !channel->compare_and_maybe_reset_lock(get_shared_context(), *req_body.mutable_compare_and_maybe_reset_lock(),
                                             true)) {
    FWLOGDEBUG("channel {} ignore reset lock because lock failed:\n{}", req_body.channel_key().channel_id(),
               req_body.compare_and_maybe_reset_lock().DebugString());

    protobuf_copy_message(*rsp_body.mutable_compare_and_maybe_reset_lock(), req_body.compare_and_maybe_reset_lock());
    rsp_body.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_reset_lock::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_reset_lock::on_failed() { return get_result(); }

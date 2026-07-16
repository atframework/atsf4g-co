// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_send_message.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/dtmq_proxy.config.pb.h>
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

#include "rpc/dtmq/dtmq_client_api.h"

#include "data/mq_channel.h"
#include "logic/mq_channel_manager.h"

DTMQ_PROXY_SERVICE_API task_action_send_message::task_action_send_message(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_send_message::~task_action_send_message() {}

DTMQ_PROXY_SERVICE_API const char* task_action_send_message::name() const { return "task_action_send_message"; }

DTMQ_PROXY_SERVICE_API task_action_send_message::result_type task_action_send_message::operator()() {
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
    bool forward_ok = false;
    TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(forward_rpc(forward_server_id, true, forward_ok)));
  }

  if (!channel) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
  }

  if (req_body.has_compare_and_maybe_reset_lock() &&
      !channel->compare_and_maybe_reset_lock(get_shared_context(), *req_body.mutable_compare_and_maybe_reset_lock(),
                                             true)) {
    FWLOGDEBUG("channel {} ignore message because lock failed:\n{}", req_body.channel_key().channel_id(),
               req_body.compare_and_maybe_reset_lock().DebugString());

    protobuf_copy_message(*rsp_body.mutable_compare_and_maybe_reset_lock(), req_body.compare_and_maybe_reset_lock());
    rsp_body.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
    rsp_body.set_last_sequence(channel->get_last_message_sequence());
    rsp_body.set_last_hash_code(channel->get_last_hash_code());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int32_t result = 0;
  mq_channel_wal_object_context param{get_shared_context(), result};

  // 正常发送接口只允许追加，不允许插入消息
  req_body.mutable_message_content()->set_sequence(0);

  auto message = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                           req_body.message_content().detail().command_case(), param,
                                                           req_body.message_content());
  result_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  if (message) {
    FWLOGDEBUG("channel {} receive message {}.", req_body.channel_key().channel_id(), message->sequence());
    rsp_body.set_message_sequence(message->sequence());
    channel->get_wal_publisher().emplace_back_log(std::move(message), param);
    channel->tick(get_shared_context());
  } else {
    FWLOGERROR("malloc wal log for mq channel {} failed", req_body.channel_key().channel_id());
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
    result = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
  }

  // Allow to update subscriber
  if (req_body.has_subscriber() && req_body.subscriber().subscriber_server_id() != 0) {
    channel->subscribe(get_shared_context(), req_body.subscriber(), req_body.subscriber_last_sequence(),
                       req_body.subscriber_last_hash_code(), false);
  }

  rsp_body.set_client_result(result);
  rsp_body.set_last_sequence(channel->get_last_message_sequence());
  rsp_body.set_last_hash_code(channel->get_last_hash_code());
  TASK_ACTION_RETURN_CODE(ret);
}

DTMQ_PROXY_SERVICE_API int task_action_send_message::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_send_message::on_failed() { return get_result(); }

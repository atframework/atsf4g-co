// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_update.h"

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

#include <rpc/rpc_context.h>

#include <utility>

#include "data/mq_channel.h"
#include "logic/mq_channel_manager.h"

DTMQ_PROXY_SERVICE_API task_action_update::task_action_update(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_update::~task_action_update() {}

DTMQ_PROXY_SERVICE_API const char* task_action_update::name() const { return "task_action_update"; }

DTMQ_PROXY_SERVICE_API task_action_update::result_type task_action_update::operator()() {
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
    FWLOGDEBUG("channel {} ignore update custom data because lock failed:\n{}", req_body.channel_key().channel_id(),
               req_body.compare_and_maybe_reset_lock().DebugString());

    protobuf_copy_message(*rsp_body.mutable_compare_and_maybe_reset_lock(), req_body.compare_and_maybe_reset_lock());

    rsp_body.set_last_sequence(channel->get_last_message_sequence());
    rsp_body.set_last_hash_code(channel->get_last_hash_code());
    rsp_body.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 更新custom data
  bool append_update_custom_data = false;
  if (req_body.clear_custom_data_action()) {
    channel->clear_custom_data();
    append_update_custom_data = true;
  } else if (req_body.has_custom_data()) {
    channel->set_custom_data(req_body.custom_data());
    append_update_custom_data = true;
  }

  // 更新private data
  if (req_body.clear_private_data_action()) {
    channel->clear_private_data();
  } else if (req_body.has_private_data()) {
    channel->set_private_data(req_body.private_data());
  }

  int32_t result = 0;

  if (append_update_custom_data && !req_body.custom_data_skip_notify()) {
    mq_channel_wal_object_context param{get_shared_context(), result};

    rpc::context::message_holder<atfw::dtmq::DChannelMessage> update_message{get_shared_context()};
    update_message->mutable_detail()->set_update_custom_data(true);

    if (req_body.has_subscriber()) {
      update_message->set_sender_key(req_body.subscriber().subscriber_key());
    }

    auto message = channel->get_wal_publisher().allocate_log(util::time::time_utility::now(),
                                                             atfw::dtmq::DChannelMessageDetail::kUpdateCustomData,
                                                             param, std::move(*update_message));
    if (message) {
      channel->get_wal_publisher().emplace_back_log(std::move(message), param);

      // 重置一下custom_data_sequence，确保如果只有这一条log，custom_data也能下发
      channel->reset_custom_data_sequence();
    } else {
      FWLOGERROR("malloc wal log for chat channel {} failed", req_body.channel_key().channel_id());
    }
  }

  channel->compact_stateful_sequence(req_body.stateful_sequence());
  channel->compact_sequence(req_body.compact_sequence());

  channel->tick(get_shared_context());

  // 最后再补全订阅者，否则会重复推送event log
  if (req_body.has_subscriber()) {
    if (!req_body.subscriber().subscriber_key().empty() &&
        req_body.subscriber().last_heartbeat_timepoint().seconds() > 0) {
      channel->subscribe(get_shared_context(), req_body.subscriber(), req_body.subscriber().last_heartbeat_sequence(),
                         req_body.subscriber().last_heartbeat_hash_code(), true);
    }
  }

  for (const auto& other_subscriber : req_body.update_others()) {
    if (other_subscriber.last_heartbeat_timepoint().seconds() > 0) {
      channel->subscribe(get_shared_context(), other_subscriber, other_subscriber.last_heartbeat_sequence(),
                         other_subscriber.last_heartbeat_hash_code(), true);
    }
  }

  // 触发保存
  if (req_body.save()) {
    result = RPC_AWAIT_CODE_RESULT(channel->await_io_task(get_shared_context()));
    if (result < 0) {
      rsp_body.set_client_result(result);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    channel->async_save(get_shared_context());
    result = RPC_AWAIT_CODE_RESULT(channel->await_io_task(get_shared_context()));
  }

  rsp_body.set_client_result(result);
  rsp_body.set_last_sequence(channel->get_last_message_sequence());
  rsp_body.set_last_hash_code(channel->get_last_hash_code());
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_update::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_update::on_failed() { return get_result(); }

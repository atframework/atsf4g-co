// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_pull.h"

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

DTMQ_PROXY_SERVICE_API task_action_pull::task_action_pull(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_pull::~task_action_pull() {}

DTMQ_PROXY_SERVICE_API const char* task_action_pull::name() const { return "task_action_pull"; }

DTMQ_PROXY_SERVICE_API task_action_pull::result_type task_action_pull::operator()() {
  rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  mq_channel_manager::mq_channel_ptr_type channel;
  uint64_t forward_server_id = 0;
  auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel(
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

  channel->tick(get_shared_context());

  if (req_body.need_snapshot()) {
    channel->dump(*rsp_body.mutable_channel_data(), true, true, true);
  } else {
    auto* metadata = rsp_body.mutable_channel_data()->mutable_channel_metadata();
    if (nullptr != metadata) {
      channel->dump(
          *metadata, req_body.need_configure(),
          req_body.need_custom_data() || channel->get_custom_data_sequence() >= req_body.need_message_sequence_begin());
    }
    auto* runtime = rsp_body.mutable_channel_data()->mutable_channel_runtime();
    if (nullptr != runtime) {
      channel->dump(*runtime, req_body.need_private_data() ||
                                  channel->get_private_data_sequence() >= req_body.need_message_sequence_begin());
    }

    // Message
    int64_t compact_stateful_sequence = channel->get_compact_stateful_sequence();
    int64_t log_end = req_body.need_message_sequence_end();
    auto iter = channel->get_wal_publisher().get_log_manager().log_lower_bound(req_body.need_message_sequence_begin());

    for (; iter != channel->get_wal_publisher().get_log_manager().log_end(); ++iter) {
      if (!*iter) {
        continue;
      }

      if (log_end >= 0 && (*iter)->sequence() > log_end) {
        break;
      }

      if ((*iter)->sequence() <= compact_stateful_sequence) {
        continue;
      }

      protobuf_copy_message(*rsp_body.mutable_channel_data()->add_messages(), **iter);
    }

    if (req_body.need_lock()) {
      protobuf_copy_message(*rsp_body.mutable_channel_data()->mutable_lock(), channel->get_lock());
    }
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_pull::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_pull::on_failed() { return get_result(); }

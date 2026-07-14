// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_unsubscribe.h"

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

#include <string>
#include <utility>

#include "data/mq_channel.h"
#include "data/mq_channel_wal_handle.h"
#include "logic/mq_channel_manager.h"

DTMQ_PROXY_SERVICE_API task_action_unsubscribe::task_action_unsubscribe(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_unsubscribe::~task_action_unsubscribe() {}

DTMQ_PROXY_SERVICE_API const char* task_action_unsubscribe::name() const { return "task_action_unsubscribe"; }

DTMQ_PROXY_SERVICE_API task_action_unsubscribe::result_type task_action_unsubscribe::operator()() {
  const rpc_request_type& req_body = get_request_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // Prepare maybe need forward
  int32_t result = 0;
  mq_channel_wal_object_context param{get_shared_context(), result};

  for (const auto& channel_id : req_body.channel_id()) {
    // 找不到直接忽略，下一次消息同步会自动再出发返订阅
    mq_channel_manager::mq_channel_ptr_type channel = mq_channel_manager::me()->get_channel(channel_id);
    if (!channel) {
      continue;
    }

    // Update subscriber
    if (req_body.has_subscriber() && req_body.subscriber().subscriber_server_id() != 0) {
      std::string subscriber_key = make_subscriber_key(req_body.subscriber());
      auto subscriber = channel->get_wal_publisher().find_subscriber(subscriber_key, param);
      if (subscriber &&
          subscriber->get_private_data().subscriber_server_id() == req_body.subscriber().subscriber_server_id()) {
        channel->unsubscribe(get_shared_context(), subscriber_key);
      }
    }

    channel->tick(get_shared_context());
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_unsubscribe::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_unsubscribe::on_failed() { return get_result(); }

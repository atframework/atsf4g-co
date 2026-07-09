// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-08 11:28:13

#include "logic/dtmq/task_action_channel_event_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <memory/rc_ptr.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

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

DTMQ_PROXY_SERVICE_API task_action_channel_event_sync::task_action_channel_event_sync(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_channel_event_sync::~task_action_channel_event_sync() {}

DTMQ_PROXY_SERVICE_API const char* task_action_channel_event_sync::name() const {
  return "task_action_channel_event_sync";
}

DTMQ_PROXY_SERVICE_API task_action_channel_event_sync::result_type task_action_channel_event_sync::operator()() {
  rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  if (mq_channel::should_be_writable(req_body.channel_metadata().channel_key())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_MAIN_REPLICATE_SWITCH);
  }

  int32_t result = 0;
  mq_channel_wal_object_context client_param{get_shared_context(), result};
  auto channel = mq_channel_manager::me()->get_channel(req_body.channel_metadata().channel_key().channel_id());
  if (!channel) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (channel->should_be_writable()) {
    FWLOGWARNING("writable channel {} received sync message and will be ignored",
                 req_body.channel_metadata().channel_key().channel_id());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  channel->maybe_create_wal_client();

  if (!channel->get_wal_client()) {
    FWLOGERROR("wal_client is not init! channel id: {}", channel->get_channel_key().channel_id());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  bool sync_now = false;
  // 直接快照覆盖
  if (req_body.has_channel_snapshot()) {
    rpc::context::message_holder<atfw::dtmq::channel_snapshot> channel_snapshot{get_shared_context()};
    protobuf_move_message(*channel_snapshot->mutable_channel_data(), std::move(*req_body.mutable_channel_snapshot()));
    channel->load_snapshot(get_shared_context(), std::move(*channel_snapshot), true);

    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 增量消息
  for (const auto& event_data : req_body.channel_message()) {
    mq_channel_wal_object_type::log_pointer log_ptr =
        atfw::util::memory::make_strong_rc<atfw::dtmq::DChannelMessage>(std::move(event_data));

    auto receive_result =
        channel->get_wal_client()->receive_hole_log(client_param, mq_channel_wal_object_type::log_pointer{log_ptr});
    if (util::distributed_system::wal_result_code::kHashCodeMismatch == receive_result) {
      rpc::dtmq::collect_invalid_event(channel->get_channel_key().channel_id(), *channel->get_wal_client());
      sync_now = true;
      break;
    }

    if (receive_result < util::distributed_system::wal_result_code::kOk) {
      break;
    }

    mq_channel_wal_object_context publisher_param{get_shared_context(), result};
    channel->get_wal_publisher().push_back_log(log_ptr, publisher_param);
  }

  // 出现Hash不匹配，需要重新同步
  if (sync_now) {
    channel->hash_mismatch_increase();
    channel->async_send_subscribe_to_writable(get_shared_context());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // Merge data, merge 的过程会触发GC
  channel->load(req_body.channel_metadata(), req_body.channel_runtime());
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_channel_event_sync::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_channel_event_sync::on_failed() { return get_result(); }

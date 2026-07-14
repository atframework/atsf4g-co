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

#include <rpc/rpc_common_types.h>
#include <rpc/rpc_context.h>

#include <utility>

#include "rpc/dtmq/dtmqproxysvrservice.atfw.gen.h"

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

  auto channel = mq_channel_manager::me()->get_channel(req_body.channel_metadata().channel_key().channel_id());
  uint64_t writable_server_id = 0;
  if (channel) {
    if (channel->is_writable()) {
      RPC_AWAIT_IGNORE_RESULT(unsubscribe());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
  } else if (mq_channel::should_be_writable_or_get_server_id(req_body.channel_metadata().channel_key(),
                                                             writable_server_id)) {
    RPC_AWAIT_IGNORE_RESULT(unsubscribe());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int32_t result = 0;
  mq_channel_wal_object_context client_param{get_shared_context(), result};
  if (!channel) {
    RPC_AWAIT_IGNORE_RESULT(unsubscribe());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  channel->maybe_create_wal_client();

  if (!channel->get_wal_client() || !channel->is_readonly()) {
    FWLOGINFO("wal_client is not init or not readonly! channel id: {}", channel->get_channel_key().channel_id());
    RPC_AWAIT_IGNORE_RESULT(unsubscribe());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  bool sync_now = false;
  // 直接快照覆盖
  if (req_body.has_channel_snapshot()) {
    rpc::context::message_holder<atfw::dtmq::channel_snapshot> channel_snapshot{get_shared_context()};
    protobuf_move_message(*channel_snapshot->mutable_channel_data(), std::move(*req_body.mutable_channel_snapshot()));
    // 保持当前节点的replicate_index不变，避免writable/readonly角色变化。
    // 这里是从其他writable节点同步只读数据过来，这时候本地总是视为readonly节点。
    channel_snapshot->set_replicate_index(channel->get_current_replicate_index());
    channel->load_snapshot(get_shared_context(), std::move(*channel_snapshot));

    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 增量消息
  for (const auto& event_data : req_body.channel_message()) {
    mq_channel_wal_object_type::log_pointer log_ptr =
        atfw::util::memory::make_strong_rc<atfw::dtmq::DChannelMessage>(std::move(event_data));

    auto receive_result = channel->get_wal_client()->receive_hole_log(client_param, log_ptr);
    if (atfw::util::distributed_system::wal_result_code::kHashCodeMismatch == receive_result) {
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
  } else {
    // Merge data, merge 的过程会触发GC
    channel->load(req_body.channel_metadata(), req_body.channel_runtime());
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_channel_event_sync::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_channel_event_sync::on_failed() { return get_result(); }

rpc::result_code_type task_action_channel_event_sync::unsubscribe() {
  rpc::context::message_holder<atfw::dtmq::SSChannelUnsubscribeReq> request_body{get_shared_context()};

  request_body->add_channel_id(get_request_body().channel_metadata().channel_key().channel_id());
  request_body->mutable_subscriber()->set_subscriber_server_id(logic_config::me()->get_local_server_id());

  google::protobuf::Empty response_body;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      rpc::dtmq::unsubscribe(get_shared_context(), get_request_node_id(), *request_body, response_body, true)));
}

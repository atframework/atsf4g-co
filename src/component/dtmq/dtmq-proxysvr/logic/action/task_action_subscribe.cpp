// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_subscribe.h"

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

#include <list>
#include <unordered_map>
#include <utility>

#include "rpc/dtmq/dtmq_client_api.h"

#include "data/mq_channel.h"
#include "data/mq_channel_wal_handle.h"
#include "logic/mq_channel_manager.h"

DTMQ_PROXY_SERVICE_API task_action_subscribe::task_action_subscribe(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_subscribe::~task_action_subscribe() {}

DTMQ_PROXY_SERVICE_API const char* task_action_subscribe::name() const { return "task_action_subscribe"; }

DTMQ_PROXY_SERVICE_API task_action_subscribe::result_type task_action_subscribe::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // Prepare maybe need forward, 按目标server_id分组
  std::unordered_map<uint64_t, std::list<const atfw::dtmq::DChannelSyncPoint*>> forward_by_server_id;

  for (const auto& heartbeat : req_body.heartbeat()) {
    mq_channel_manager::mq_channel_ptr_type channel;
    uint64_t forward_server_id = 0;
    auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel_with_replicate_index(
        get_shared_context(), channel, forward_server_id, heartbeat.readonly_index(), heartbeat.channel_key(),
        heartbeat.auto_create_channel()));
    if (res < 0) {
      set_response_code(res);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    // 请求转发
    if (0 != forward_server_id) {
      if (req_body.forward_ttl() > logic_config::me()->get_logic_cfg().router().transfer_max_ttl()) {
        rsp_body.add_not_found_channel_ids(heartbeat.channel_key().channel_id());
        continue;
      }

      forward_by_server_id[forward_server_id].push_back(&heartbeat);
      continue;
    }

    if (!channel) {
      rsp_body.add_not_found_channel_ids(heartbeat.channel_key().channel_id());
      continue;
    }

    // Update subscriber
    if (req_body.has_subscriber() && req_body.subscriber().subscriber_server_id() != 0) {
      channel->subscribe(get_shared_context(), req_body.subscriber(), heartbeat.last_sequence(),
                         heartbeat.last_hash_code(), false);
      auto& add_subscribe_node = *rsp_body.mutable_subscribe_node()->Add();
      protobuf_copy_message(*add_subscribe_node.mutable_channel_key(), heartbeat.channel_key());
      add_subscribe_node.set_server_id(logic_config::me()->get_local_server_id());
      add_subscribe_node.set_readonly_index(heartbeat.readonly_index());
    }

    channel->tick(get_shared_context());
  }

  for (auto& forward_group : forward_by_server_id) {
    rpc::context::message_holder<rpc_request_type> rpc_req_body{get_shared_context()};
    rpc::context::message_holder<rpc_response_type> rpc_rsp_body{get_shared_context()};

    protobuf_copy_message(*rpc_req_body->mutable_subscriber(), req_body.subscriber());
    rpc_req_body->set_forward_ttl(req_body.forward_ttl() + 1);
    rpc_req_body->mutable_heartbeat()->Reserve(static_cast<int>(forward_group.second.size()));
    for (auto& heartbeat : forward_group.second) {
      protobuf_copy_message(*rpc_req_body->add_heartbeat(), *heartbeat);
    }

    auto res = RPC_AWAIT_CODE_RESULT(
        rpc::dtmq::subscribe(get_shared_context(), forward_group.first, *rpc_req_body, *rpc_rsp_body, is_stream_rpc()));
    if (res < 0) {
      FWLOGERROR("forward subscriber {} to server {:#x} failed, res: {}({})",
                 make_subscriber_key(req_body.subscriber()), forward_group.first, res,
                 protobuf_mini_dumper_get_error_msg(res));
    }

    for (const auto& channel_id : rpc_rsp_body->not_found_channel_ids()) {
      rsp_body.add_not_found_channel_ids(channel_id);
    }

    for (auto& subscribe_node : *rpc_rsp_body->mutable_subscribe_node()) {
      auto* add_subscribe_node = rsp_body.mutable_subscribe_node()->Add();
      if (nullptr == add_subscribe_node) {
        FWLOGERROR("forward subscriber {} to server {:#x} failed, malloc subscribe_node failed",
                   make_subscriber_key(req_body.subscriber()), forward_group.first);
        continue;
      }
      protobuf_move_message(*add_subscribe_node, std::move(subscribe_node));
    }
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_subscribe::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_subscribe::on_failed() { return get_result(); }

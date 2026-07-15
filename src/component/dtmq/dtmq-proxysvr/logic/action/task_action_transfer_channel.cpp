// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_transfer_channel.h"

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
#include "logic/mq_channel_manager.h"
#include "rpc/rpc_common_types.h"

DTMQ_PROXY_SERVICE_API task_action_transfer_channel::task_action_transfer_channel(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_transfer_channel::~task_action_transfer_channel() {}

DTMQ_PROXY_SERVICE_API const char* task_action_transfer_channel::name() const { return "task_action_transfer_channel"; }

DTMQ_PROXY_SERVICE_API task_action_transfer_channel::result_type task_action_transfer_channel::operator()() {
  rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // Prepare maybe need forward
  std::unordered_map<uint64_t, std::list<atfw::dtmq::channel_snapshot*>> forward_by_server_id;

  for (auto& channel_snapshot : *req_body.mutable_snapshot()) {
    // make_readable_channel或make_readable_channel_with_replicate_index会检查是否允许可写或者读权限
    mq_channel_manager::mq_channel_ptr_type channel;
    uint64_t forward_server_id = 0;
    rpc::result_code_type::value_type res = 0;
    if (channel_snapshot.replicate_index() > 0) {
      res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel_with_replicate_index(
          get_shared_context(), channel, forward_server_id, channel_snapshot.replicate_index(),
          channel_snapshot.channel_data().channel_metadata().channel_key(), true));
    } else {
      res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
          get_shared_context(), channel, forward_server_id,
          channel_snapshot.channel_data().channel_metadata().channel_key(), true));
    }
    if (res < 0) {
      set_response_code(res);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    // 请求转发
    if (0 != forward_server_id) {
      if (req_body.forward_ttl() > logic_config::me()->get_logic_cfg().router().transfer_max_ttl()) {
        FWLOGERROR("mq channel {} forward ttl exceeded limit",
                   channel_snapshot.channel_data().channel_metadata().channel_key().channel_id());

        *rsp_body.add_failed_channel_key() = channel_snapshot.channel_data().channel_metadata().channel_key();
        continue;
      }
      forward_by_server_id[forward_server_id].push_back(&channel_snapshot);
      continue;
    }

    if (!channel || !channel->load_snapshot(get_shared_context(), std::move(channel_snapshot))) {
      FWLOGERROR("mq channel {} load snapshot failed",
                 channel_snapshot.channel_data().channel_metadata().channel_key().channel_id());
      *rsp_body.add_failed_channel_key() = channel_snapshot.channel_data().channel_metadata().channel_key();
    } else {
      // 触发数据下发
      channel->tick(get_shared_context());
    }
  }

  for (auto& forward_group : forward_by_server_id) {
    rpc::context::message_holder<rpc_request_type> rpc_req_body{get_shared_context()};
    rpc::context::message_holder<rpc_response_type> rpc_rsp_body{get_shared_context()};

    rpc_req_body->set_forward_ttl(req_body.forward_ttl() + 1);
    rpc_req_body->mutable_snapshot()->Reserve(static_cast<int>(forward_group.second.size()));
    for (auto& snapshot : forward_group.second) {
      protobuf_move_message(*rpc_req_body->add_snapshot(), std::move(*snapshot));
    }

    auto res = RPC_AWAIT_CODE_RESULT(rpc::dtmq::transfer_channel(get_shared_context(), forward_group.first,
                                                                 *rpc_req_body, *rpc_rsp_body, is_stream_rpc()));
    if (res < 0) {
      FWLOGERROR("forward transfer {} mq channel(s) to server {:#x} failed, res: {}({})", forward_group.second.size(),
                 forward_group.first, res, protobuf_mini_dumper_get_error_msg(res));

      for (int i = 0; i < rpc_req_body->snapshot_size(); ++i) {
        const auto& snapshot = rpc_req_body->snapshot(i);
        *rsp_body.add_failed_channel_key() = snapshot.channel_data().channel_metadata().channel_key();
      }
    } else {
      for (const auto& failed_channel_key : rpc_rsp_body->failed_channel_key()) {
        *rsp_body.add_failed_channel_key() = failed_channel_key;
      }
    }
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_transfer_channel::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_transfer_channel::on_failed() { return get_result(); }

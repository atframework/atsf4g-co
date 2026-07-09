// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "logic/action/task_action_notify_readonly.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

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

#include <list>
#include <unordered_map>
#include <utility>

#include "data/mq_channel.h"
#include "logic/mq_channel_manager.h"

DTMQ_PROXY_SERVICE_API task_action_notify_readonly::task_action_notify_readonly(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_notify_readonly::~task_action_notify_readonly() {}

DTMQ_PROXY_SERVICE_API const char* task_action_notify_readonly::name() const { return "task_action_notify_readonly"; }

DTMQ_PROXY_SERVICE_API task_action_notify_readonly::result_type task_action_notify_readonly::operator()() {
  const rpc_request_type& req_body = get_request_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }
  // Prepare maybe need forward
  std::unordered_map<uint64_t, std::list<const atfw::dtmq::DChannelSyncPoint*>> forward_by_server_id;
  for (const auto& sync : req_body.sync_data()) {
    // 主节点不接受其他节点同步请求
    if (mq_channel::should_be_writable(sync.channel_key())) {
      continue;
    }

    uint64_t forward_server_id = 0;
    mq_channel_manager::mq_channel_ptr_type channel;
    auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel(
        get_shared_context(), channel, forward_server_id, sync.channel_key(),
        mq_channel::get_suggest_readonly_replicate_index(sync.channel_key()), false));
    if (res < 0) {
      FWLOGWARNING("make_readable_channel failed, res: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
    }

    // 有变化会重新通知，不需要转发
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_notify_readonly::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_notify_readonly::on_failed() { return get_result(); }

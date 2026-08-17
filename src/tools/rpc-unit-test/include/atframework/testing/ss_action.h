// Copyright 2026 atframework

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/descriptor.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "dispatcher/ss_msg_dispatcher.h"
#include "dispatcher/task_action_ss_req_base.h"
#include "dispatcher/task_manager.h"
#include "rpc/rpc_async_invoke.h"

namespace atframework {
namespace testing {

// Source metadata written to the synthetic inbound SSMsg. Zero values represent an anonymous/system source.
// Tests for forwarding, tracing, replies, or source-sensitive authorization should set every relevant field explicitly.
struct ss_inbound_source {
  uint64_t node_id = 0;
  std::string node_name;
  uint64_t source_task_id = 0;
  uint64_t sequence = 0;
};

// Options for invoke_ss_action(). The RPC name is required and owned by this object. Use the generated
// packer::get_full_name_of_<rpc>() accessor.
struct ss_action_invoke_options {
  ATFW_UTIL_FORCEINLINE explicit ss_action_invoke_options(gsl::string_view input_rpc_name) {
    if (!input_rpc_name.empty()) {
      rpc_name.assign(input_rpc_name.data(), input_rpc_name.size());
    }
  }

  std::string rpc_name;
  ss_inbound_source source;
};

namespace detail {

ATFW_UTIL_FORCEINLINE bool validate_ss_action_method(const ss_action_invoke_options& options,
                                                     const google::protobuf::Descriptor* request_descriptor,
                                                     const google::protobuf::Descriptor* response_descriptor) {
  if (nullptr == request_descriptor || nullptr == response_descriptor) {
    return false;
  }

  const auto* descriptor_pool = google::protobuf::DescriptorPool::generated_pool();
  const auto* method = descriptor_pool->FindMethodByName(options.rpc_name);
  if (nullptr == method) {
    const auto separator = options.rpc_name.find('/');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= options.rpc_name.size() ||
        options.rpc_name.find('/', separator + 1) != std::string::npos) {
      return false;
    }

    std::string protobuf_method_name = options.rpc_name;
    protobuf_method_name[separator] = '.';
    method = descriptor_pool->FindMethodByName(protobuf_method_name);
  }

  return nullptr != method && method->input_type() == request_descriptor &&
         method->output_type() == response_descriptor;
}

}  // namespace detail

// Invoke one concrete SS request action inside an active rpc-unit-test runtime task.
//
// This helper deliberately performs the real SSMsg serialization/unpack and task-manager create/start/wait sequence,
// but creates TAction directly rather than testing the dispatcher's RPC-to-action registry. The request parameter is
// TAction::rpc_request_type, so an action/request mismatch is rejected at compile time. The required generated RPC name
// is also checked against the action's protobuf request and response descriptors before any action is created. Request
// and options are accepted by value so their lifetimes are safely bound to the coroutine frame, including when the
// caller passes temporaries. Call it with RPC_AWAIT_CODE_RESULT() from runtime::run_task(); the returned code is the
// action's final task result, not a business code stored in its response protobuf.
template <class TAction>
ATFW_EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE rpc::result_code_type invoke_ss_action(
    rpc::context& ctx, typename TAction::rpc_request_type request_body, ss_action_invoke_options options) {
  static_assert(std::is_base_of<task_action_ss_req_base, TAction>::value,
                "TAction must derive from task_action_ss_req_base");

  using request_type = typename TAction::rpc_request_type;
  using response_type = typename TAction::rpc_response_type;
  const auto* request_descriptor = request_type::descriptor();
  const auto* response_descriptor = response_type::descriptor();
  if (!detail::validate_ss_action_method(options, request_descriptor, response_descriptor)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  atfw::SSMsg input_message;
  auto* head = input_message.mutable_head();
  head->set_node_id(options.source.node_id);
  head->set_node_name(options.source.node_name);
  head->set_source_task_id(options.source.source_task_id);
  head->set_sequence(options.source.sequence);
  auto* rpc_request = head->mutable_rpc_request();
  rpc_request->set_rpc_name(options.rpc_name);
  rpc_request->set_type_url(request_descriptor->full_name());
  if (!request_body.SerializeToString(input_message.mutable_body_bin())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  std::string serialized_message;
  if (!input_message.SerializeToString(&serialized_message)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  // unpacked_message must stay in this coroutine frame until the nested action exits because raw_message points to it.
  atfw::SSMsg unpacked_message;
  dispatcher_raw_message raw_message = dispatcher_make_default<dispatcher_raw_message>();
  int32_t result = ss_msg_dispatcher::me()->unpack_protobuf_msg(unpacked_message, raw_message,
                                                                serialized_message.data(), serialized_message.size());
  if (result < 0) {
    RPC_RETURN_CODE(result);
  }

  dispatcher_start_data_type constructor_data = dispatcher_make_default<dispatcher_start_data_type>();
  constructor_data.message = raw_message;
  constructor_data.context = &ctx;
  task_type_trait::task_type action_task;
  result = task_manager::me()->create_task<TAction>(action_task, std::move(constructor_data));
  if (result < 0 || task_type_trait::empty(action_task)) {
    RPC_RETURN_CODE(result < 0 ? result : PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
  }

  dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
  start_data.context = &ctx;
  result = task_manager::me()->start_task(action_task, start_data);
  if (result < 0) {
    RPC_RETURN_CODE(result);
  }

  result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, action_task));
  if (result < 0) {
    RPC_RETURN_CODE(result);
  }
  RPC_RETURN_CODE(task_type_trait::get_result(action_task));
}

}  // namespace testing
}  // namespace atframework

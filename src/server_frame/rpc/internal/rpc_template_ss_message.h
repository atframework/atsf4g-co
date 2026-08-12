// Copyright 2026 atframework
// Created by owent on 2016/08/02.
//

#pragma once

#include <nostd/string_view.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframe/atapp_conf.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/etcdcli/etcd_discovery.h>

#include <config/server_frame_build_feature.h>

#include "rpc/internal/rpc_template.h"  // IWYU pragma: keep
#include "rpc/rpc_utils.h"              // IWYU pragma: keep

namespace rpc {
namespace internal {
ATFW_UTIL_FORCEINLINE bool is_invalid_server_node(const atfw::atapp::etcd_discovery_node& destination_server) {
  return destination_server.get_discovery_info().id() == 0 || destination_server.get_discovery_info().name().empty();
}

ATFW_UTIL_FORCEINLINE bool is_invalid_server_node(uint64_t destination_server) { return destination_server == 0; }

SERVER_FRAME_API int setup_ss_rpc_request_header(atframework::SSMsgHead& head, task_type_trait::id_type task_id,
                                                 atfw::util::nostd::string_view service_full_name,
                                                 atfw::util::nostd::string_view rpc_full_name,
                                                 atfw::util::nostd::string_view type_full_name);

template <class TResponseBody>
ATFW_UTIL_SYMBOL_VISIBLE result_code_type wait_and_unpack_ss_response(context& ctx, TResponseBody& response_body,
                                                                      atfw::util::nostd::string_view rpc_full_name,
                                                                      atfw::util::nostd::string_view type_full_name,
                                                                      dispatcher_await_options& await_options) {
  atframework::SSMsg* rsp_msg_ptr = ctx.create<atframework::SSMsg>();
  if (nullptr == rsp_msg_ptr) {
    FCTXLOGERROR(ctx, "rpc {} create response message failed", rpc_full_name);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  atframework::SSMsg& rsp_msg = *rsp_msg_ptr;
  result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, rsp_msg, await_options));

  if (rsp_msg.head().rpc_response().type_url() != type_full_name) {
    if (res >= 0 || !rsp_msg.head().rpc_response().type_url().empty()) {
      FCTXLOGERROR(ctx, "rpc {} expect response message {}, but got {}", rpc_full_name, type_full_name,
                   rsp_msg.head().rpc_response().type_url());
    }
  } else if (!rsp_msg.body_bin().empty()) {
    res = unpack_rpc_body(response_body, rsp_msg.body_bin(), rpc_full_name, type_full_name);
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  if (rsp_msg.has_head() && rsp_msg.head().error_code() != 0) {
    RPC_RETURN_CODE(rsp_msg.head().error_code());
  }
  RPC_RETURN_CODE(res);
}
}  // namespace internal
}  // namespace rpc

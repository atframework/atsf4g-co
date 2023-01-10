// Copyright 2022 atframework
// Created by owent on 2022-02-15.
//

#include "rpc/rpc_common_types.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include "utility/protobuf_mini_dumper.h"

namespace rpc {

bool is_exiting_error_code(int32_t code) {
  switch (code) {
    case PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT:
    case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED:
    case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED:
    case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING:
    case PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT:
      return true;
    default:
      return false;
  }
}

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE

int32_t rpc_error_code_transform::operator()(copp::promise_status in) const noexcept {
  if (in < copp::promise_status::kDone) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
  }

  if (in == copp::promise_status::kCancle) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
  } else if (in == copp::promise_status::kKilled) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
  } else if (in == copp::promise_status::kTimeout) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
}

#else
int32_t rpc_get_not_ready_code() { return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL; }

result_void_type::result_void_type() {}
result_void_type::result_void_type(bool is_ready) : result_data_(is_ready) {}
#endif
}  // namespace rpc

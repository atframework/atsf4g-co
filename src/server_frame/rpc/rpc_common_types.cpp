// Copyright 2022 atframework
// Created by owent on 2022-02-15.
//

#include "rpc/rpc_common_types.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include "utility/protobuf_mini_dumper.h"

namespace rpc {
result_code_type::result_code_type() {}
result_code_type::result_code_type(int32_t code) : result_data_(code) {}
result_code_type::operator int32_t() const noexcept {
  if (!result_data_.is_ready()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
  }

  const int32_t* ret = result_data_.data();
  if (nullptr == ret) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  }

  return *ret;
}

result_void_type::result_void_type() {}
result_void_type::result_void_type(bool is_ready) : result_data_(is_ready) {}

}  // namespace rpc

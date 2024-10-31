// Copyright 2024 Tencent
// Created by owent on 2024-10-29.
//

#include "rpc/rpc_shared_message.h"

#include <memory>

#include "rpc/rpc_context.h"

namespace rpc {

SERVER_FRAME_API const std::shared_ptr<::google::protobuf::Arena> &get_shared_arena(const context &ctx) {
  return ctx.get_protobuf_arena();
}

}  // namespace rpc

// Copyright 2024 atframework
// Created by owent on 2024-10-29.
//

#include "rpc/rpc_shared_message.h"

#include <log/log_wrapper.h>

#include <memory>

#include "rpc/rpc_context.h"
#include "rpc/telemetry/opentelemetry_utility.h"

namespace rpc {

SERVER_FRAME_API const std::shared_ptr<::google::protobuf::Arena> &get_shared_arena(const context &ctx) {
  return ctx.get_protobuf_arena();
}

SERVER_FRAME_API void report_shared_message_defer_after_moved(const std::string &demangle_name) {
  auto object_full_name = atfw::util::log::format("rpc::shared_message<{}>", demangle_name);
  auto message = atfw::util::log::format("{} should not be defered after moved", object_full_name);
  FWLOGERROR("{}", message);

  rpc::context ctx{rpc::context::create_without_task()};
  rpc::telemetry::opentelemetry_utility::send_notification_event(
      ctx, rpc::telemetry::notification_domain::kErrorWithStackTrace,
      atfw::util::log::format("rpc.shared_message.defer_after_moved.{}", object_full_name), message,
      {{"object_type", demangle_name}});
}

}  // namespace rpc

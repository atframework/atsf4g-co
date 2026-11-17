// Copyright 2026 atframework
//

#include "rpc/internal/rpc_template_db_message.h"

#include <dispatcher/db_msg_dispatcher.h>

#include <memory>
#include <utility>

namespace rpc {
namespace internal {
SERVER_FRAME_API telemetry::tracer make_db_rpc_tracer(context& child_context,
                                                      atfw::util::nostd::string_view rpc_full_name,
                                                      telemetry::trace_attributes_type attributes) {
  telemetry::trace_start_option trace_option;
  trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  trace_option.is_remote = true;
  trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  trace_option.attributes = attributes;
  return child_context.make_tracer({rpc_full_name.data(), rpc_full_name.size()}, std::move(trace_option));
}
}  // namespace internal
}  // namespace rpc

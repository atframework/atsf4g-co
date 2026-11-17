// Copyright 2026 atframework
//

#pragma once

#include <nostd/string_view.h>

#include <opentelemetry/semconv/incubating/db_attributes.h>
#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <config/server_frame_build_feature.h>

#include <cstdint>

#include "rpc/rpc_context.h"

namespace rpc {
namespace internal {
SERVER_FRAME_API telemetry::tracer make_db_rpc_tracer(context& child_context,
                                                      atfw::util::nostd::string_view rpc_full_name,
                                                      telemetry::trace_attributes_type attributes);

class db_rpc_trace {
 public:
  ATFW_UTIL_FORCEINLINE db_rpc_trace(context& parent, atfw::util::nostd::string_view rpc_full_name)
      : child_context(parent),
        attributes{
            {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.db"},
            {
                opentelemetry::semconv::rpc::kRpcMethod,
                opentelemetry::nostd::string_view{
                    rpc_full_name.data(),
                    rpc_full_name.size(),
                },
            },
            {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemNameValues::kRedis},
        },
        tracer(make_db_rpc_tracer(child_context, rpc_full_name, attributes)) {}

  ATFW_UTIL_FORCEINLINE int32_t finish(int32_t result) { return tracer.finish({result, attributes}); }

  context child_context;

 private:
  telemetry::trace_attribute_pair_type attributes[3];
  telemetry::tracer tracer;
};
}  // namespace internal
}  // namespace rpc

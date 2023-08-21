// Copyright 2022 atframework
// Created by owent on 2021-07-13.
//

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/timestamp.pb.h>

#include <protocol/pbdesc/atframework.pb.h>

#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/tracer.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <stdint.h>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class dispatcher_implement;

namespace rpc {

namespace telemetry {

struct UTIL_SYMBOL_VISIBLE trace_option {
  using string_view = opentelemetry::nostd::string_view;
  using span_ptr_type = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>;
  using trace_id_span = opentelemetry::nostd::span<const uint8_t, opentelemetry::trace::TraceId::kSize>;
  using span_id_span = opentelemetry::nostd::span<const uint8_t, opentelemetry::trace::SpanId::kSize>;
  using link_pair_type = std::pair<opentelemetry::trace::SpanContext,
                                   std::unordered_map<std::string, opentelemetry::common::AttributeValue>>;
  using links_type = std::vector<link_pair_type>;

  std::shared_ptr<dispatcher_implement> dispatcher;
  atframework::RpcTraceSpan::SpanKind kind;
  bool is_remote;
  const ::atframework::RpcTraceSpan* parent_network_span;
  span_ptr_type parent_memory_span;
  const links_type* links;

  inline trace_option()
      : dispatcher(nullptr),
        kind(atframework::RpcTraceSpan::SPAN_KIND_INTERNAL),
        is_remote(true),
        parent_network_span(nullptr),
        parent_memory_span(nullptr),
        links(nullptr) {}
};

class tracer {
 public:
  using string_view = trace_option::string_view;
  using span_ptr_type = trace_option::span_ptr_type;
  using trace_id_span = trace_option::trace_id_span;
  using span_id_span = trace_option::span_id_span;
  using link_pair_type = trace_option::link_pair_type;
  using links_type = trace_option::links_type;

 public:
  SERVER_FRAME_API tracer();
  SERVER_FRAME_API ~tracer();

  SERVER_FRAME_API tracer(tracer&& other);
  SERVER_FRAME_API tracer& operator=(tracer&& other) noexcept;

  SERVER_FRAME_API bool start(
      string_view name, trace_option&& options,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          attributes = {});
  SERVER_FRAME_API int return_code(int ret);

  UTIL_FORCEINLINE const tracer::span_ptr_type& get_trace_span() const { return trace_span_; }

  /**
   * @brief Set the trace name
   *
   * @param name
   */
  SERVER_FRAME_API void update_trace_name(string_view name);

 private:
  friend class context;
  int32_t result_;
  std::chrono::system_clock::time_point start_system_timepoint_;
  std::chrono::steady_clock::time_point start_steady_timepoint_;
  span_ptr_type trace_span_;
  std::shared_ptr<dispatcher_implement> dispatcher_;
};

}  // namespace telemetry

}  // namespace rpc

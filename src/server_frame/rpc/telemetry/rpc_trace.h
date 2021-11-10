// Copyright 2021 atframework
// Created by owent on 2021/07/13.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/timestamp.pb.h>

#include <protocol/pbdesc/atframework.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <stdint.h>
#include <memory>
#include <utility>

#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/tracer.h"

class dispatcher_implement;

namespace rpc {

namespace telemetry {

struct trace_option {
  std::shared_ptr<dispatcher_implement> dispatcher;
  const ::atframework::RpcTraceSpan* parent_trace_span;
  atframework::RpcTraceSpan::SpanKind kind;
  bool is_remote;

  trace_option();
};

class tracer {
 public:
  using string_view = ::opentelemetry::nostd::string_view;
  using span_ptr_type = ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Span>;
  using trace_id_span = ::opentelemetry::nostd::span<const uint8_t, ::opentelemetry::trace::TraceId::kSize>;
  using span_id_span = ::opentelemetry::nostd::span<const uint8_t, ::opentelemetry::trace::SpanId::kSize>;

 public:
  tracer();
  ~tracer();

  bool start(string_view name, trace_option&& options, const span_ptr_type& parent_span);
  int return_code(int ret);

  inline const tracer::span_ptr_type& get_trace_span() const { return trace_span_; }

  /**
   * @brief Set the trace name
   *
   * @param name
   */
  void update_trace_name(string_view name);

 private:
  friend class context;
  int32_t result_;
  span_ptr_type trace_span_;
  std::shared_ptr<dispatcher_implement> dispatcher_;
};

}  // namespace telemetry

}  // namespace rpc

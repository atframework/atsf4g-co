// Copyright 2022 atframework
// Created by owent on 2021-07-13.
//

#include "rpc/telemetry/rpc_trace.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/util/time_util.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/trace/span_context.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>

#include <atframe/atapp.h>

#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "config/logic_config.h"
#include "rpc/telemetry/rpc_global_service.h"

namespace rpc {

namespace telemetry {

SERVER_FRAME_API tracer::tracer() : trace_span_(nullptr) {}

SERVER_FRAME_API tracer::~tracer() {
  if (trace_span_) {
    finish({0, {}});
  }
}

SERVER_FRAME_API tracer::tracer(tracer &&other)
    : start_system_timepoint_(other.start_system_timepoint_),
      start_steady_timepoint_(other.start_steady_timepoint_),
      trace_span_(std::move(other.trace_span_)),
      span_kind_(std::move(other.span_kind_)),
      dispatcher_(std::move(other.dispatcher_)) {
  other.trace_span_ = span_ptr_type();
  other.dispatcher_.reset();
}

SERVER_FRAME_API tracer &tracer::operator=(tracer &&other) noexcept {
  start_system_timepoint_ = other.start_system_timepoint_;
  start_steady_timepoint_ = other.start_steady_timepoint_;
  trace_span_ = std::move(other.trace_span_);
  span_kind_ = std::move(other.span_kind_);
  dispatcher_ = std::move(other.dispatcher_);

  other.trace_span_ = span_ptr_type();
  other.dispatcher_.reset();

  return *this;
}

SERVER_FRAME_API bool tracer::start(string_view name, trace_start_option &&options) {
  if (trace_span_) {
    return true;
  }

  auto tracer_obj = global_service::get_current_default_tracer();
  if (!tracer_obj) {
    return false;
  }

  opentelemetry::trace::StartSpanOptions span_options;
  start_system_timepoint_ = std::chrono::system_clock::now();
  start_steady_timepoint_ = std::chrono::steady_clock::now();
  span_options.start_system_time = opentelemetry::common::SystemTimestamp(start_system_timepoint_);
  span_options.start_steady_time = opentelemetry::common::SteadyTimestamp(start_steady_timepoint_);

  switch (options.kind) {
    case ::atframework::RpcTraceSpan::SPAN_KIND_INTERNAL:
      span_options.kind = opentelemetry::trace::SpanKind::kInternal;
      span_kind_ = "internal";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_SERVER:
      span_options.kind = opentelemetry::trace::SpanKind::kServer;
      span_kind_ = "server";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT:
      span_options.kind = opentelemetry::trace::SpanKind::kClient;
      span_kind_ = "client";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_PRODUCER:
      span_options.kind = opentelemetry::trace::SpanKind::kProducer;
      span_kind_ = "producer";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_CONSUMER:
      span_options.kind = opentelemetry::trace::SpanKind::kConsumer;
      span_kind_ = "consumer";
      break;
    default:
      span_kind_ = "unknown";
      break;
  }

  if (nullptr != options.parent_network_span &&
      options.parent_network_span->trace_id().size() == trace_id_span::extent &&
      options.parent_network_span->span_id().size() == span_id_span::extent) {
    const uint8_t *parent_trace_id = reinterpret_cast<const uint8_t *>(options.parent_network_span->trace_id().c_str());
    const uint8_t *parent_span_id = reinterpret_cast<const uint8_t *>(options.parent_network_span->span_id().c_str());
    span_options.parent = opentelemetry::trace::SpanContext{
        opentelemetry::trace::TraceId{trace_id_span{parent_trace_id, trace_id_span::extent}},
        opentelemetry::trace::SpanId{span_id_span{parent_span_id, span_id_span::extent}},
        opentelemetry::trace::TraceFlags{opentelemetry::trace::TraceFlags::kIsSampled}, options.is_remote};
  } else if (options.parent_memory_span) {
    if (!options.parent_memory_span->IsRecording()) {
      return false;
    }
    span_options.parent = options.parent_memory_span->GetContext();
  }

  if (options.attributes.empty() && options.links.empty()) {
    trace_span_ = tracer_obj->StartSpan(name, span_options);
  } else if (options.links.empty()) {
    trace_span_ = tracer_obj->StartSpan(name, options.attributes, span_options);
  } else {
    trace_span_ = tracer_obj->StartSpan(name, options.attributes, options.links, span_options);
  }

  dispatcher_ = std::move(options.dispatcher);
  trace_span_name_ = static_cast<std::string>(name);
  return !!trace_span_;
}

SERVER_FRAME_API int32_t tracer::finish(trace_finish_option &&options) {
  if (trace_span_) {
    switch (options.result_code) {
      case PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND:
      case PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND:
      case PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION: {
        gsl::string_view error_msg = protobuf_mini_dumper_get_error_msg(options.result_code);
        trace_span_->SetStatus(opentelemetry::trace::StatusCode::kUnset, {error_msg.data(), error_msg.size()});
        break;
      }
      default: {
        if (options.result_code < 0) {
          gsl::string_view error_msg = protobuf_mini_dumper_get_error_msg(options.result_code);
          trace_span_->SetStatus(opentelemetry::trace::StatusCode::kError, {error_msg.data(), error_msg.size()});
        } else {
          trace_span_->SetStatus(opentelemetry::trace::StatusCode::kOk, "success");
        }
        break;
      }
    }
    trace_span_->SetAttribute("rpc.atrpc.result_code", options.result_code);

    std::chrono::steady_clock::time_point end_steady_timepoint = std::chrono::steady_clock::now();
    opentelemetry::trace::EndSpanOptions end_options;
    end_options.end_steady_time = opentelemetry::common::SteadyTimestamp(end_steady_timepoint);

    const std::string &additional_metrics_name =
        logic_config::me()->get_logic().telemetry().opentelemetry().trace().additional_metrics_name();
    if (!additional_metrics_name.empty()) {
      opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>> trace_metric =
          rpc::telemetry::global_service::mutable_metrics_histogram_uint64(additional_metrics_name,
                                                                           {additional_metrics_name, "", "us"});
      if (trace_metric) {
        trace_attribute_pair_type result_attributes[] = {{"rpc.atrpc.result_code", options.result_code},
                                                         {"rpc.atrpc.kind", span_kind_},
                                                         {"rpc.atrpc.span_name", trace_span_name_}};
        trace_attributes_type attributes_array[] = {options.attributes, result_attributes};
        multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
            opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
        trace_metric->Record(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                       end_steady_timepoint - start_steady_timepoint_)
                                                       .count()),
                             concat_attributes, opentelemetry::context::Context{});
      }
    }

    trace_span_->End(end_options);
    trace_span_ = span_ptr_type();
  }

  return options.result_code;
}

SERVER_FRAME_API void tracer::update_trace_name(string_view name) {
  if (trace_span_) {
    trace_span_->UpdateName(name);
    trace_span_name_ = static_cast<std::string>(name);
  }
}

}  // namespace telemetry

}  // namespace rpc

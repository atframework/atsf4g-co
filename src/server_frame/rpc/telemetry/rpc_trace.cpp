// Copyright 2022 atframework
// Created by owent on 2021-07-13.
//

#include "rpc/telemetry/rpc_trace.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/util/time_util.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/trace/span_context.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframe/atapp.h>

#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

#include "rpc/telemetry/rpc_global_service.h"

namespace rpc {

namespace telemetry {

trace_option::trace_option()
    : dispatcher(nullptr),
      kind(atframework::RpcTraceSpan::SPAN_KIND_INTERNAL),
      is_remote(true),
      parent_network_span(nullptr),
      parent_memory_span(nullptr),
      links(nullptr) {}

tracer::tracer() : result_(0), trace_span_(nullptr) {}

tracer::~tracer() {
  if (trace_span_) {
    std::chrono::steady_clock::time_point end_steady_timepoint = std::chrono::steady_clock::now();
    opentelemetry::trace::EndSpanOptions end_options;
    end_options.end_steady_time = opentelemetry::common::SteadyTimestamp(end_steady_timepoint);
    trace_span_->End(end_options);
  }
}

bool tracer::start(
    string_view name, trace_option &&options,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attributes) {
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
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_SERVER:
      span_options.kind = opentelemetry::trace::SpanKind::kServer;
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT:
      span_options.kind = opentelemetry::trace::SpanKind::kClient;
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_PRODUCER:
      span_options.kind = opentelemetry::trace::SpanKind::kProducer;
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_CONSUMER:
      span_options.kind = opentelemetry::trace::SpanKind::kConsumer;
      break;
    default:
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

  if (nullptr == options.links) {
    trace_span_ = tracer_obj->StartSpan(name, attributes, span_options);
  } else {
    trace_span_ = tracer_obj->StartSpan(name, attributes, *options.links, span_options);
  }

  dispatcher_ = std::move(options.dispatcher);
  return !!trace_span_;
}

int tracer::return_code(int ret) {
  result_ = ret;
  if (trace_span_) {
    switch (ret) {
      case PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND:
      case PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND:
      case PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION: {
        trace_span_->SetStatus(opentelemetry::trace::StatusCode::kUnset, protobuf_mini_dumper_get_error_msg(ret));
        break;
      }
      default: {
        if (ret < 0) {
          trace_span_->SetStatus(opentelemetry::trace::StatusCode::kError, protobuf_mini_dumper_get_error_msg(ret));
        } else {
          trace_span_->SetStatus(opentelemetry::trace::StatusCode::kOk, "success");
        }
        break;
      }
    }
    trace_span_->SetAttribute("result_code", ret);
  }
  return ret;
}

void tracer::update_trace_name(string_view name) {
  if (trace_span_) {
    trace_span_->UpdateName(name);
  }
}

}  // namespace telemetry

}  // namespace rpc

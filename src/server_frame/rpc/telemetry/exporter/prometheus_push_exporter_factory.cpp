// Copyright 2023 atframework
// Created by owent on 2023/07/19.
//

#include "rpc/telemetry/exporter/prometheus_push_exporter_factory.h"

#include <opentelemetry/sdk/metrics/metric_reader.h>
#include <opentelemetry/version.h>

#include <memory>

#include "rpc/telemetry/exporter/prometheus_push_exporter.h"
#include "rpc/telemetry/exporter/prometheus_push_exporter_options.h"

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

SERVER_FRAME_API std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> PrometheusPushExporterFactory::Create(
    const PrometheusPushExporterOptions &options) {
  return std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter>(new PrometheusPushExporter(options));
}

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

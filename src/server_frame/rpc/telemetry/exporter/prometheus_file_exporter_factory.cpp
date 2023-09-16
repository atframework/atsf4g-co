// Copyright 2023 atframework
// Created by owent on 2023-09-14.
//

#include "rpc/telemetry/exporter/prometheus_file_exporter_factory.h"

#include <opentelemetry/sdk/metrics/metric_reader.h>
#include <opentelemetry/version.h>

#include <memory>

#include "rpc/telemetry/exporter/prometheus_file_exporter.h"
#include "rpc/telemetry/exporter/prometheus_file_exporter_options.h"

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

SERVER_FRAME_API std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> PrometheusFileExporterFactory::Create(
    const PrometheusFileExporterOptions &options) {
  return std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter>(new PrometheusFileExporter(options));
}

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

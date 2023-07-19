// Copyright 2023 atframework
// Created by owent on 2023/07/19.
//

#pragma once

#include <opentelemetry/sdk/metrics/push_metric_exporter.h>
#include <opentelemetry/version.h>

#include <memory>

#include "rpc/telemetry/exporter/prometheus_push_exporter_options.h"

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

struct PrometheusPushExporterOptions;

/**
 * Factory class for PrometheusExporter.
 */
class PrometheusPushExporterFactory {
 public:
  /**
   * Create a PrometheusExporter using the given options.
   */
  SERVER_FRAME_API static std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Create(
      const PrometheusPushExporterOptions &options);
};

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

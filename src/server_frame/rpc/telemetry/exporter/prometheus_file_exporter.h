// Copyright 2023 atframework
// Created by owent on 2023-09-14.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <opentelemetry/exporters/prometheus/exporter_utils.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/sdk/common/env_variables.h>
#include <opentelemetry/version.h>

#include <opentelemetry/sdk/metrics/push_metric_exporter.h>

#include <chrono>
#include <memory>

#include "rpc/telemetry/exporter/prometheus_file_exporter_options.h"

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

class PrometheusFileBackend;

class PrometheusFileExporter : public ::opentelemetry::sdk::metrics::PushMetricExporter {
 public:
  /**
   * Constructor - binds an exposer and collector to the exporter
   * @param options: options for an exposer that exposes
   *  an HTTP endpoint for the exporter to connect to
   */
  SERVER_FRAME_API explicit PrometheusFileExporter(const PrometheusFileExporterOptions &options);

  /**
   * Get the AggregationTemporality for Prometheus exporter
   *
   * @return AggregationTemporality
   */
  SERVER_FRAME_API ::opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      ::opentelemetry::sdk::metrics::InstrumentType instrument_type) const noexcept override;

  /**
   * Exports a batch of Metric Records.
   * @param records: a collection of records to export
   * @return: returns a ReturnCode detailing a success, or type of failure
   */
  SERVER_FRAME_API ::opentelemetry::sdk::common::ExportResult Export(
      const ::opentelemetry::sdk::metrics::ResourceMetrics &data) noexcept override;

  /**
   * Force flush the exporter.
   */
  SERVER_FRAME_API bool ForceFlush(
      std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

  /**
   * Shuts down the exporter and does cleanup.
   * Since Prometheus is a pull based interface,
   * we cannot serve data remaining in the intermediate
   * collection to to client an HTTP request being sent,
   * so we flush the data.
   */
  SERVER_FRAME_API bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds(0)) noexcept override;

  SERVER_FRAME_API std::size_t GetMaxCollectionSize() const noexcept;

  /**
   * @return: Gets the shutdown status of the exporter
   */
  SERVER_FRAME_API bool IsShutdown() const;

 private:
  // The configuration options associated with this exporter.
  const PrometheusFileExporterOptions options_;

  /**
   * exporter shutdown status
   */
  bool is_shutdown_;

  std::shared_ptr<PrometheusFileBackend> backend_;
};

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

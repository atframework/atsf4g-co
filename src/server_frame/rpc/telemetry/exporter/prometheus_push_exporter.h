// Copyright 2022 atframework
// Created by owent on 2022/09/02.
//

#pragma once

#ifdef _WIN32
#  include <io.h>        // NOLINT
#  include <winsock2.h>  // NOLINT
#else
#  include <unistd.h>  // NOLINT
#endif

#include <config/server_frame_build_feature.h>

#include <prometheus/gateway.h>

#include <opentelemetry/exporters/prometheus/exporter_utils.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/sdk/common/env_variables.h>
#include <opentelemetry/version.h>

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
#  include <opentelemetry/sdk/metrics/push_metric_exporter.h>
#else
#  include <opentelemetry/sdk/metrics/metric_exporter.h>
#endif

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

class PrometheusPushCollector;

/**
 * Struct to hold Prometheus exporter options.
 */
struct PrometheusPushExporterOptions {
  std::string host;
  std::string port;
  std::string jobname;
  ::prometheus::Labels labels;
  std::string username;
  std::string password;

  opentelemetry::sdk::metrics::AggregationTemporality aggregation_temporality =
      opentelemetry::sdk::metrics::AggregationTemporality::kDelta;
};

class PrometheusPushExporter : public
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
                               ::opentelemetry::sdk::metrics::PushMetricExporter
#else
                               ::opentelemetry::sdk::metrics::MetricExporter
#endif
{
 public:
  /**
   * Constructor - binds an exposer and collector to the exporter
   * @param options: options for an exposer that exposes
   *  an HTTP endpoint for the exporter to connect to
   */
  explicit PrometheusPushExporter(const PrometheusPushExporterOptions &options);

  /**
   * Get the AggregationTemporality for Prometheus exporter
   *
   * @return AggregationTemporality
   */
  ::opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      ::opentelemetry::sdk::metrics::InstrumentType instrument_type) const noexcept override;

  /**
   * Exports a batch of Metric Records.
   * @param records: a collection of records to export
   * @return: returns a ReturnCode detailing a success, or type of failure
   */
  ::opentelemetry::sdk::common::ExportResult Export(
      const ::opentelemetry::sdk::metrics::ResourceMetrics &data) noexcept override;

  /**
   * Force flush the exporter.
   */
  bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

  /**
   * Shuts down the exporter and does cleanup.
   * Since Prometheus is a pull based interface,
   * we cannot serve data remaining in the intermediate
   * collection to to client an HTTP request being sent,
   * so we flush the data.
   */
  bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds(0)) noexcept override;

  /**
   * @return: returns a shared_ptr to
   * the PrometheusPushCollector instance
   */
  std::shared_ptr<PrometheusPushCollector> &GetCollector();

  /**
   * @return: Gets the shutdown status of the exporter
   */
  bool IsShutdown() const;

 private:
  // The configuration options associated with this exporter.
  const PrometheusPushExporterOptions options_;
  /**
   * exporter shutdown status
   */
  bool is_shutdown_;

  /**
   * Pointer to a
   * PrometheusPushCollector instance
   */
  std::shared_ptr<PrometheusPushCollector> collector_;

  /**
   * Pointer to an
   * Gateway instance
   */
  std::unique_ptr<::prometheus::Gateway> gateway_;
};

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

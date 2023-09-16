// Copyright 2023 atframework
// Created by owent on 2023/07/19.
//

#pragma once

#include <string>

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

class PrometheusUtility {
 public:
  /**
   * Create a PrometheusExporter using the given options.
   */
  SERVER_FRAME_API static std::string SanitizePrometheusName(std::string name, bool label);
};

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

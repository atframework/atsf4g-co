// Copyright 2023 atframework
// Created by owent on 2023/07/19.
//

#pragma once

#include <config/compile_optimize.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

/**
 * Struct to hold Prometheus exporter options.
 */
struct UTIL_SYMBOL_VISIBLE PrometheusPushExporterOptions {
  std::string host;
  std::string port;
  std::string jobname;
  std::unordered_map<std::string, std::string> labels;
  std::string username;
  std::string password;

  std::size_t max_collection_size = 2000;

  // Populating target_info
  bool populate_target_info = true;

  // Populating otel_scope_name/otel_scope_labels attributes
  bool without_otel_scope = false;

  inline PrometheusPushExporterOptions() noexcept {}
};

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

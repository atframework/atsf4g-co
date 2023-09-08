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

  inline PrometheusPushExporterOptions() noexcept {}
};

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

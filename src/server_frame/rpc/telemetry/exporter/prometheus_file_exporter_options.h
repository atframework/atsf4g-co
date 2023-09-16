// Copyright 2023 atframework
// Created by owent on 2023-09-14.
//

#pragma once

#include <config/compile_optimize.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

/**
 * Struct to hold Prometheus exporter options.
 * @note Available placeholder for file_pattern and alias_pattern:
 *     %Y:  writes year as a 4 digit decimal number
 *     %y:  writes last 2 digits of year as a decimal number (range [00,99])
 *     %m:  writes month as a decimal number (range [01,12])
 *     %j:  writes day of the year as a decimal number (range [001,366])
 *     %d:  writes day of the month as a decimal number (range [01,31])
 *     %w:  writes weekday as a decimal number, where Sunday is 0 (range [0-6])
 *     %H:  writes hour as a decimal number, 24 hour clock (range [00-23])
 *     %I:  writes hour as a decimal number, 12 hour clock (range [01,12])
 *     %M:  writes minute as a decimal number (range [00,59])
 *     %S:  writes second as a decimal number (range [00,60])
 *     %F:  equivalent to "%Y-%m-%d" (the ISO 8601 date format)
 *     %T:  equivalent to "%H:%M:%S" (the ISO 8601 time format)
 *     %R:  equivalent to "%H:%M"
 *     %N:  rotate index, start from 0
 *     %n:  rotate index, start from 1
 */
struct UTIL_SYMBOL_VISIBLE PrometheusFileExporterOptions {
  std::string file_pattern = "%Y-%m-%d.prometheus.%N.log";
  std::string alias_pattern = "%Y-%m-%d.prometheus.log";
  std::chrono::microseconds flush_interval = std::chrono::microseconds{30000000};
  std::size_t flush_count = 256;
  std::size_t file_size = 20 * 1024 * 1024;
  std::size_t rotate_size = 3;

  inline PrometheusFileExporterOptions() noexcept {}
};

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc

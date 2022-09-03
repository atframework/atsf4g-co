
// Copyright 2022 atframework
// Created by owent on 2021/10/18.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <config/compiler/protobuf_prefix.h>

#include <opentelemetry/logs/logger.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/trace/tracer.h>

#include <config/compiler/protobuf_suffix.h>

#include <stdint.h>
#include <memory>
#include <utility>

namespace atapp {
class app;
}

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_telemetry_cfg;
}
PROJECT_NAMESPACE_END

namespace rpc {

namespace telemetry {

struct meter_instrument_key {
  opentelemetry::nostd::string_view name;
  opentelemetry::nostd::string_view description;
  opentelemetry::nostd::string_view unit;

  inline meter_instrument_key(opentelemetry::nostd::string_view input_name = "",
                              opentelemetry::nostd::string_view input_description = "",
                              opentelemetry::nostd::string_view input_unit = "")
      : name(input_name), description(input_description), unit(input_unit) {}
};

class global_service {
 public:
  /**
   * @brief Get the current default tracer
   *
   * @return ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_current_default_tracer();

  /**
   * @brief Get tracer
   *
   * @return ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer(
      opentelemetry::nostd::string_view library_name, opentelemetry::nostd::string_view library_version = "",
      opentelemetry::nostd::string_view schema_url = "");

  /**
   * @brief Get long counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<long>> get_metrics_counter_long(
      opentelemetry::nostd::string_view meter_name = "", meter_instrument_key key = {});

  /**
   * @brief Get long histogram
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<long>> get_metrics_Histogram_long(
      opentelemetry::nostd::string_view meter_name = "", meter_instrument_key key = {});

  /**
   * @brief Get long up down counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<long>> get_metrics_up_down_counter_long(
      opentelemetry::nostd::string_view meter_name = "", meter_instrument_key key = {});

  /**
   * @brief Get the current default logger
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> get_current_default_logger();

  /**
   * @brief Get logger
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> get_logger(
      opentelemetry::nostd::string_view logger_name, opentelemetry::nostd::string_view options = "",
      opentelemetry::nostd::string_view library_name = "", opentelemetry::nostd::string_view library_version = "",
      opentelemetry::nostd::string_view schema_url = "");

  /**
   * @brief Set the current service object, it's used for tracer
   * @param app atapp instance
   * @param telemetry telemetry configure
   */
  static void set_current_service(atapp::app& app, const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg& telemetry);
};

}  // namespace telemetry

}  // namespace rpc

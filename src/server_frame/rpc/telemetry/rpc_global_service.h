
// Copyright 2021 atframework
// Created by owent on 2021/10/18.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <stdint.h>
#include <memory>
#include <utility>

#include "opentelemetry/logs/logger.h"
#include "opentelemetry/trace/tracer.h"

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
   * @brief Get the current default logger
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> get_current_default_logger();

  /**
   * @brief Get logger
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
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

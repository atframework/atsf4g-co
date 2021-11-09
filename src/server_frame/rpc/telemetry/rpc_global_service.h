
// Copyright 2021 atframework
// Created by owent on 2021/10/18.
//

#pragma once

#include <stdint.h>
#include <memory>
#include <utility>

#include "opentelemetry/trace/tracer.h"

namespace atapp {
class app;
}

namespace hello {
namespace config {
class logic_telemetry_cfg;
}
}  // namespace hello

namespace rpc {

namespace telemetry {

class global_service {
 public:
  /**
   * @brief Get the current default tracer
   *
   * @return ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer>
   */
  static ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer> get_current_default_tracer();

  /**
   * @brief Set the current service object, it's used for tracer
   * @param app atapp instance
   * @param telemetry telemetry configure
   */
  static void set_current_service(const atapp::app& app, const hello::config::logic_telemetry_cfg& telemetry);
};

}  // namespace telemetry

}  // namespace rpc

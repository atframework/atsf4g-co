// Copyright 2023 atframework
// Created by owent on 2023-09-14.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <opentelemetry/nostd/string_view.h>

namespace rpc {

namespace telemetry {
struct UTIL_SYMBOL_VISIBLE meter_instrument_key {
  opentelemetry::nostd::string_view name;
  opentelemetry::nostd::string_view description;
  opentelemetry::nostd::string_view unit;

  inline meter_instrument_key(opentelemetry::nostd::string_view input_name = "",
                              opentelemetry::nostd::string_view input_description = "",
                              opentelemetry::nostd::string_view input_unit = "")
      : name(input_name), description(input_description), unit(input_unit) {}
};
}  // namespace telemetry
}  // namespace rpc

// Copyright 2023 atframework
// Created by owent on 2022-03-03.
//

#pragma once

#include <config/compile_optimize.h>
#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/message.h>

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/sdk/common/attribute_utils.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "rpc/telemetry/opentelemetry_types.h"

namespace rpc {

namespace telemetry {
enum class UTIL_SYMBOL_VISIBLE metrics_observable_type : int32_t {
  kGauge = 0,
  kCounter = 1,
  kUnDownCounter = 2,
};

class opentelemetry_utility {
 public:
  struct UTIL_SYMBOL_VISIBLE attributes_map_type {
    using type = std::unordered_map<std::string, opentelemetry::common::AttributeValue>;
    using value_type = std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue>;
    using string_view_vec_type = std::vector<opentelemetry::nostd::string_view>;

    type attributes;
    value_type owned_attributes;

    std::list<string_view_vec_type> string_view_storages;
    std::list<std::unique_ptr<bool[]>> bool_view_storages;
  };

 public:
  SERVER_FRAME_API static void protobuf_to_otel_attributes(const google::protobuf::Message& message,
                                                           attributes_map_type& output,
                                                           gsl::string_view key_prefix = "");

  SERVER_FRAME_API static opentelemetry::common::AttributeValue convert_attribute_value_wihtout_array(
      const opentelemetry::sdk::common::OwnedAttributeValue& value);

  SERVER_FRAME_API static bool add_global_metics_observable_int64(metrics_observable_type type,
                                                                  opentelemetry::nostd::string_view meter_name,
                                                                  meter_instrument_key metrics_key,
                                                                  std::function<int64_t()> fn);

  SERVER_FRAME_API static bool add_global_metics_observable_double(metrics_observable_type type,
                                                                   opentelemetry::nostd::string_view meter_name,
                                                                   meter_instrument_key metrics_key,
                                                                   std::function<double()> fn);
};
}  // namespace telemetry
}  // namespace rpc

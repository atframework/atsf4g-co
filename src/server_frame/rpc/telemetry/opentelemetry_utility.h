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
#include <opentelemetry/metrics/observer_result.h>
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
#include <utility>
#include <vector>

#include "rpc/telemetry/opentelemetry_types.h"
#include "rpc/telemetry/rpc_global_service.h"

namespace rpc {

namespace telemetry {
struct group_type;

enum class metrics_observable_type : int32_t {
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

  using attribute_pair_type = std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>;
  using attribute_span_type = opentelemetry::nostd::span<const attribute_pair_type>;

 public:
  SERVER_FRAME_API static void protobuf_to_otel_attributes(const google::protobuf::Message& message,
                                                           attributes_map_type& output,
                                                           gsl::string_view key_prefix = "");

  SERVER_FRAME_API static opentelemetry::common::AttributeValue convert_attribute_value_wihtout_array(
      const opentelemetry::sdk::common::OwnedAttributeValue& value);

  SERVER_FRAME_API static std::string convert_attribute_value_to_string(
      const opentelemetry::common::AttributeValue& value);

  SERVER_FRAME_API static bool add_global_metics_observable_int64(
      metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
      std::function<void(opentelemetry::metrics::ObserverResult&)> fn);

  SERVER_FRAME_API static bool add_global_metics_observable_double(
      metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
      std::function<void(opentelemetry::metrics::ObserverResult&)> fn);

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record(opentelemetry::metrics::ObserverResult& result,
                                                               ValueType&& value) {
    std::shared_ptr<::rpc::telemetry::group_type> __lifetime;
    if (opentelemetry::nostd::holds_alternative<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<int64_t>(value), rpc::telemetry::global_service::get_metrics_labels(__lifetime));
      }
    } else if (opentelemetry::nostd::holds_alternative<
                   opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<double>(value), rpc::telemetry::global_service::get_metrics_labels(__lifetime));
      }
    }
  }

  template <class ValueType, class AttributeType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record(opentelemetry::metrics::ObserverResult& result,
                                                               ValueType&& value, AttributeType&& attributes) {
    if (opentelemetry::nostd::holds_alternative<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<int64_t>(value), attributes);
      }
    } else if (opentelemetry::nostd::holds_alternative<
                   opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<double>(value), attributes);
      }
    }
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      opentelemetry::metrics::ObserverResult& result, ValueType&& value, attribute_span_type extend_attributes) {
    std::shared_ptr<::rpc::telemetry::group_type> __lifetime;
    attribute_span_type attributes_array[] = {rpc::telemetry::global_service::get_metrics_labels_view(__lifetime),
                                              extend_attributes};

    rpc::telemetry::multiple_key_value_iterable_view<attribute_span_type> concat_attributes{
        opentelemetry::nostd::span<const attribute_span_type>{attributes_array}};

    if (opentelemetry::nostd::holds_alternative<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<int64_t>(value), concat_attributes);
      }
    } else if (opentelemetry::nostd::holds_alternative<
                   opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<double>(value), concat_attributes);
      }
    }
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      opentelemetry::metrics::ObserverResult& result, ValueType&& value,
      std::shared_ptr<::rpc::telemetry::group_type>& __lifetime, attribute_span_type extend_attributes) {
    attribute_span_type attributes_array[] = {rpc::telemetry::global_service::get_metrics_labels_view(__lifetime),
                                              extend_attributes};

    rpc::telemetry::multiple_key_value_iterable_view<attribute_span_type> concat_attributes{
        opentelemetry::nostd::span<const attribute_span_type>{attributes_array}};

    if (opentelemetry::nostd::holds_alternative<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<int64_t>(value), concat_attributes);
      }
    } else if (opentelemetry::nostd::holds_alternative<
                   opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
      auto observer =
          opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(
              result);
      if (observer) {
        observer->Observe(static_cast<double>(value), concat_attributes);
      }
    }
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      opentelemetry::metrics::ObserverResult& result, ValueType&& value,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          extend_attributes) {
    global_metics_observe_record_extend_attrubutes(
        result, std::forward<ValueType>(value),
        attribute_span_type{extend_attributes.begin(), extend_attributes.end()});
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      opentelemetry::metrics::ObserverResult& result, ValueType&& value,
      std::shared_ptr<::rpc::telemetry::group_type>& __lifetime,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          extend_attributes) {
    global_metics_observe_record_extend_attrubutes(
        result, std::forward<ValueType>(value), __lifetime,
        attribute_span_type{extend_attributes.begin(), extend_attributes.end()});
  }
};
}  // namespace telemetry
}  // namespace rpc

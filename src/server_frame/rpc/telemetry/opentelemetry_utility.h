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
#include <memory/rc_ptr.h>
#include <nostd/type_traits.h>

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

class context;

namespace telemetry {
struct group_type;

enum class metrics_observable_type : int32_t {
  kGauge = 0,
  kCounter = 1,
  kUnDownCounter = 2,
};

enum class notification_domain : int32_t {
  kNotice = 0,
  kWarning = 1,
  kError = 2,
  kCritical = 3,

  // stack trace flag = 0x100
  kStackTraceBitFlag = 256,       // 0x100
  kNoticeWithStackTrace = 256,    // 0x100
  kWarningWithStackTrace = 257,   // 0x101
  kErrorWithStackTrace = 258,     // 0x102
  kCriticalWithStackTrace = 259,  // 0x103
};

class opentelemetry_utility {
 public:
  struct UTIL_SYMBOL_VISIBLE metrics_attributes_with_lifetime;
  struct UTIL_SYMBOL_VISIBLE metrics_record;
  struct UTIL_SYMBOL_VISIBLE metrics_observer;

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

 private:
  UTIL_FORCEINLINE static opentelemetry::nostd::variant<int64_t, double> global_metics_observe_record_parse_value(
      const opentelemetry::nostd::variant<int64_t, double>& value) noexcept {
    return value;
  }

  template <class ValueType>
  UTIL_FORCEINLINE static opentelemetry::nostd::variant<int64_t, double> global_metics_observe_record_parse_value(
      const ValueType& value) noexcept {
    return static_cast<typename std::conditional<std::is_integral<ValueType>::value, int64_t, double>::type>(value);
  }

 public:
  SERVER_FRAME_API static void setup();

  SERVER_FRAME_API static int tick();

  SERVER_FRAME_API static void stop();

  SERVER_FRAME_API static util::memory::strong_rc_ptr<metrics_attributes_with_lifetime>
  create_attributes_with_lifetime();

  SERVER_FRAME_API static const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& get_attributes(
      const metrics_attributes_with_lifetime& source);

  SERVER_FRAME_API static void populate_attributes(metrics_attributes_with_lifetime& target,
                                                   const opentelemetry::common::KeyValueIterable& attributes);

  SERVER_FRAME_API static void populate_attributes(metrics_attributes_with_lifetime& target,
                                                   attribute_span_type attributes);

  SERVER_FRAME_API static void populate_attributes(
      metrics_attributes_with_lifetime& target,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          attributes);

  SERVER_FRAME_API static void protobuf_to_otel_attributes(const google::protobuf::Message& message,
                                                           attributes_map_type& output,
                                                           gsl::string_view key_prefix = "");

  SERVER_FRAME_API static opentelemetry::common::AttributeValue convert_attribute_value_wihtout_array(
      const opentelemetry::sdk::common::OwnedAttributeValue& value);

  SERVER_FRAME_API static std::string convert_attribute_value_to_string(
      const opentelemetry::common::AttributeValue& value);

  SERVER_FRAME_API static std::string convert_attribute_value_to_string(
      const opentelemetry::sdk::common::OwnedAttributeValue& value);

  SERVER_FRAME_API static bool add_global_metics_observable_int64(metrics_observable_type type,
                                                                  opentelemetry::nostd::string_view meter_name,
                                                                  meter_instrument_key metrics_key,
                                                                  std::function<void(metrics_observer&)> fn);

  SERVER_FRAME_API static bool add_global_metics_observable_double(metrics_observable_type type,
                                                                   opentelemetry::nostd::string_view meter_name,
                                                                   meter_instrument_key metrics_key,
                                                                   std::function<void(metrics_observer&)> fn);

  SERVER_FRAME_API static void global_metics_observe_record(metrics_observer& observer,
                                                            opentelemetry::nostd::variant<int64_t, double> value,
                                                            const opentelemetry::common::KeyValueIterable& attributes);

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record(metrics_observer& observer, ValueType&& value) {
    std::shared_ptr<::rpc::telemetry::group_type> __lifetime;
    global_metics_observe_record(
        observer, global_metics_observe_record_parse_value(value),
        opentelemetry::common::MakeAttributes(rpc::telemetry::global_service::get_metrics_labels(__lifetime)));
  }

  template <class ValueType, class AttributeType,
            class = util::nostd::enable_if_t<
                !std::is_base_of<opentelemetry::common::KeyValueIterable, util::nostd::decay_t<AttributeType>>::value>>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record(metrics_observer& observer, ValueType&& value,
                                                               AttributeType&& attributes) {
    global_metics_observe_record(observer, global_metics_observe_record_parse_value(value),
                                 opentelemetry::common::MakeAttributes(attributes));
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      metrics_observer& observer, ValueType&& value, attribute_span_type extend_attributes) {
    std::shared_ptr<::rpc::telemetry::group_type> __lifetime;
    attribute_span_type attributes_array[] = {rpc::telemetry::global_service::get_metrics_labels_view(__lifetime),
                                              extend_attributes};

    rpc::telemetry::multiple_key_value_iterable_view<attribute_span_type> concat_attributes{
        opentelemetry::nostd::span<const attribute_span_type>{attributes_array}};

    global_metics_observe_record(observer, global_metics_observe_record_parse_value(value), concat_attributes);
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      metrics_observer& observer, ValueType&& value, std::shared_ptr<::rpc::telemetry::group_type>& __lifetime,
      attribute_span_type extend_attributes) {
    attribute_span_type attributes_array[] = {rpc::telemetry::global_service::get_metrics_labels_view(__lifetime),
                                              extend_attributes};

    rpc::telemetry::multiple_key_value_iterable_view<attribute_span_type> concat_attributes{
        opentelemetry::nostd::span<const attribute_span_type>{attributes_array}};

    global_metics_observe_record(observer, global_metics_observe_record_parse_value(value), concat_attributes);
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      metrics_observer& observer, ValueType&& value,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          extend_attributes) {
    global_metics_observe_record_extend_attrubutes(
        observer, global_metics_observe_record_parse_value(value),
        attribute_span_type{extend_attributes.begin(), extend_attributes.end()});
  }

  template <class ValueType>
  UTIL_SYMBOL_VISIBLE static void global_metics_observe_record_extend_attrubutes(
      metrics_observer& observer, ValueType&& value, std::shared_ptr<::rpc::telemetry::group_type>& __lifetime,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          extend_attributes) {
    global_metics_observe_record_extend_attrubutes(
        observer, std::forward<ValueType>(value), __lifetime,
        attribute_span_type{extend_attributes.begin(), extend_attributes.end()});
  }

  SERVER_FRAME_API static void send_notification_event(rpc::context& ctx, notification_domain event_domain,
                                                       gsl::string_view event_name, gsl::string_view message,
                                                       attribute_span_type attrbites = {});

  SERVER_FRAME_API static void send_notification_event(
      rpc::context& ctx, notification_domain event_domain, gsl::string_view event_name, gsl::string_view message,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          attrbites);

  SERVER_FRAME_API static void send_log_to_default_group(rpc::context& ctx, gsl::string_view event_domain,
                                                         gsl::string_view event_name, gsl::string_view message,
                                                         attribute_span_type attrbites = {});

  SERVER_FRAME_API static void send_log_to_default_group(
      rpc::context& ctx, gsl::string_view event_domain, gsl::string_view event_name, gsl::string_view message,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          attrbites);

  SERVER_FRAME_API static void send_log_to_default_group(rpc::context& ctx, opentelemetry::logs::Severity severity,
                                                         gsl::string_view event_domain, gsl::string_view event_name,
                                                         gsl::string_view message, attribute_span_type attrbites = {});

  SERVER_FRAME_API static void send_log_to_default_group(
      rpc::context& ctx, opentelemetry::logs::Severity severity, gsl::string_view event_domain,
      gsl::string_view event_name, gsl::string_view message,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          attrbites);

  SERVER_FRAME_API static void send_log_to_default_group(rpc::context& ctx, gsl::string_view logger_name,
                                                         opentelemetry::logs::Severity severity,
                                                         gsl::string_view event_domain, gsl::string_view event_name,
                                                         gsl::string_view message, attribute_span_type attrbites = {});

  SERVER_FRAME_API static void send_log_to_default_group(
      rpc::context& ctx, gsl::string_view logger_name, opentelemetry::logs::Severity severity,
      gsl::string_view event_domain, gsl::string_view event_name, gsl::string_view message,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          attrbites);
};
}  // namespace telemetry
}  // namespace rpc

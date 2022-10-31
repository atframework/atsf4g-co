
// Copyright 2022 atframework
// Created by owent on 2021/10/18.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <config/compiler/protobuf_prefix.h>

#include <opentelemetry/logs/logger.h>
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/sdk/common/attribute_utils.h>
#include <opentelemetry/trace/tracer.h>

#include <config/compiler/protobuf_suffix.h>

#include <stdint.h>
#include <memory>
#include <string>
#include <unordered_map>
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
  static const opentelemetry::sdk::common::AttributeMap& get_common_owned_attributes();

  static const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& get_common_attributes();

  /**
   * @brief Get the metrics labels object
   * @note 指标数据维度信息，和环境相关，会建立索引，可以和环境有关，不要和用户相关或出现随机值
   *
   * @return * const std::unordered_map<std::string, opentelemetry::common::AttributeValue>&
   */
  static const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& get_metrics_labels();

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
 * @brief Get or create a counter
 *
 * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter>
 */
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>> mutable_metrics_counter_uint64(
      opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#else
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<long>> mutable_metrics_counter_long(
      opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#endif

  /**
   * @brief Get or create a double counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>> mutable_metrics_counter_double(
      opentelemetry::nostd::string_view meter_name, meter_instrument_key key);

/**
 * @brief Get or create a histogram
 *
 * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram>
 */
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>> mutable_metrics_histogram_uint64(
      opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#else
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<long>> mutable_metrics_histogram_long(
      opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#endif

  /**
   * @brief Get or create a double histogram
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>> mutable_metrics_histogram_double(
      opentelemetry::nostd::string_view meter_name, meter_instrument_key key);

/**
 * @brief Get or create a up down counter
 *
 * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter>
 */
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>
  mutable_metrics_up_down_counter_int64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#else
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<long>>
  mutable_metrics_up_down_counter_long(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#endif

  /**
   * @brief Get or create a double up down counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>
  mutable_metrics_up_down_counter_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);

  /**
   * @brief Get observable
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> get_metrics_observable(
      opentelemetry::nostd::string_view meter_name, meter_instrument_key key);

/**
 * @brief Get or create a observable counter
 * @note callback of observable instrument mey be called in a different thread
 *
 * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
 */
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_counter_int64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#else
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_counter_long(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#endif

  /**
   * @brief Get or create a double observable counter
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_counter_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);

/**
 * @brief Get or create a long observable gauge
 * @note callback of observable instrument mey be called in a different thread
 *
 * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
 */
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_gauge_int64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#else
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_gauge_long(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);
#endif

  /**
   * @brief Get or create a double observable gauge
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_gauge_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key);

/**
 * @brief Get or create a observable up down counter
 * @note callback of observable instrument mey be called in a different thread
 *
 * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
 */
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_up_down_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                   meter_instrument_key key);
#else
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_up_down_counter_long(opentelemetry::nostd::string_view meter_name,
                                                  meter_instrument_key key);
#endif

  /**
   * @brief Get or create a double observable up down counter
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_up_down_counter_double(opentelemetry::nostd::string_view meter_name,
                                                    meter_instrument_key key);

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

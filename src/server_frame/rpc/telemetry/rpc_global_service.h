// Copyright 2023 atframework
// Created by owent on 2021/10/18.
//

#pragma once

#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/logs/logger.h>
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/sdk/common/attribute_utils.h>
#include <opentelemetry/trace/tracer.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>

#include <stdint.h>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "rpc/telemetry/opentelemetry_types.h"

namespace atapp {
class app;
}

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_telemetry_cfg;
class opentelemetry_cfg;
class opentelemetry_agent_cfg;
class opentelemetry_trace_cfg;
class opentelemetry_metrics_cfg;
class opentelemetry_logs_cfg;
}  // namespace config
PROJECT_NAMESPACE_END

namespace rpc {

namespace telemetry {

struct group_type;

class global_service {
 public:
  using group_event_callback_type = std::function<void(const std::shared_ptr<group_type>&)>;
  using group_event_callback_handle = std::list<group_event_callback_type>::iterator;
  using global_event_callback_type = std::function<void()>;
  using global_event_callback_handle = std::list<global_event_callback_type>::iterator;

 public:
  /**
   * @brief 注册Ready回调
   * @note reload和第一次初始化完成后回调
   * @note 部分Metrics采用回调形式，可以在ready之后重新设置
   *
   * @param fn 回调函数
   * @return 回调handle
   */
  SERVER_FRAME_API static std::pair<bool, global_event_callback_handle> add_on_ready(global_event_callback_type fn);

  /**
   * @brief 删除Ready回调
   * @param handle 回调handle
   */
  SERVER_FRAME_API static bool remove_on_ready(global_event_callback_handle& handle);

  /**
   * @brief 注册可观测性分组移除回调
   * @param fn 回调函数
   * @return 回调handle
   */
  SERVER_FRAME_API static std::pair<bool, group_event_callback_handle> add_on_group_destroy(
      group_event_callback_type fn);

  /**
   * @brief 删除可观测性分组移除回调
   * @param handle 回调handle
   */
  SERVER_FRAME_API static bool remove_on_group_destroy(group_event_callback_handle& handle);

  /**
   * @brief 注册可观测性分组创建回调
   * @param fn 回调函数
   * @return 回调handle
   */
  SERVER_FRAME_API static std::pair<bool, group_event_callback_handle> add_on_group_create(
      group_event_callback_type fn);

  /**
   * @brief 删除可观测性分组创建回调
   * @param handle 回调handle
   */
  SERVER_FRAME_API static bool remove_on_group_create(group_event_callback_handle& handle);

  SERVER_FRAME_API static const opentelemetry::sdk::common::AttributeMap& get_common_owned_attributes(
      std::shared_ptr<group_type>& group);

  SERVER_FRAME_API static const std::unordered_map<std::string, opentelemetry::common::AttributeValue>&
  get_common_attributes(std::shared_ptr<group_type>& group);

  /**
   * @brief Get the metrics labels object
   * @note 指标数据维度信息，和环境相关，会建立索引，可以和环境有关，不要和用户相关或出现随机值
   *
   * @return * const std::unordered_map<std::string, opentelemetry::common::AttributeValue>&
   */
  SERVER_FRAME_API static const std::unordered_map<std::string, opentelemetry::common::AttributeValue>&
  get_metrics_labels(std::shared_ptr<group_type>& group);

  /**
   * @brief Get the metrics labels span
   * @note 指标数据维度(可用于Prometheus附加OTEL的Resouce数据)
   *
   * @return 附加的Metric维度信息视图
   */
  SERVER_FRAME_API static opentelemetry::nostd::span<
      std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
  get_metrics_labels_view(std::shared_ptr<group_type>& group);

  /**
   * @brief Get configure
   * @return configure
   */
  SERVER_FRAME_API static const PROJECT_NAMESPACE_ID::config::opentelemetry_cfg& get_configure(
      std::shared_ptr<group_type>& group);

  /**
   * @brief Get if group has agent configure
   * @return true if group has agent configure
   */
  SERVER_FRAME_API static bool has_agent_configure(std::shared_ptr<group_type>& group);

  /**
   * @brief Get agent configure
   * @return agent configure
   */
  SERVER_FRAME_API static const PROJECT_NAMESPACE_ID::config::opentelemetry_agent_cfg& get_agent_configure(
      std::shared_ptr<group_type>& group);

  /**
   * @brief Get trace configure
   * @return trace configure
   */
  SERVER_FRAME_API static const PROJECT_NAMESPACE_ID::config::opentelemetry_trace_cfg& get_trace_configure(
      std::shared_ptr<group_type>& group);

  /**
   * @brief Get metrics configure
   * @return metrics configure
   */
  SERVER_FRAME_API static const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_cfg& get_metrics_configure(
      std::shared_ptr<group_type>& group);

  /**
   * @brief Get logs configure
   * @return logs configure
   */
  SERVER_FRAME_API static const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_cfg& get_logs_configure(
      std::shared_ptr<group_type>& group);

  /**
   * @brief Get the current trace exporter count
   * @note it may be used to find out if trace is enabled
   *
   * @return current trace exporter count
   */
  SERVER_FRAME_API static size_t get_trace_exporter_count(std::shared_ptr<group_type> group = nullptr) noexcept;

  /**
   * @brief Get the current metrics exporter count
   * @note it may be used to find out if metrics is enabled
   *
   * @return current metrics exporter count
   */
  SERVER_FRAME_API static size_t get_metrics_exporter_count(std::shared_ptr<group_type> group = nullptr) noexcept;

  /**
   * @brief Get the current logs exporter count
   * @note it may be used to find out if logs is enabled
   *
   * @return current logs exporter count
   */
  SERVER_FRAME_API static size_t get_logs_exporter_count(std::shared_ptr<group_type> group = nullptr) noexcept;

  /**
   * @brief Get the current default tracer
   *
   * @return ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_current_default_tracer(
      std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get tracer
   *
   * @return ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer(
      opentelemetry::nostd::string_view library_name, opentelemetry::nostd::string_view library_version = "",
      opentelemetry::nostd::string_view schema_url = "");

  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer(
      std::shared_ptr<group_type> group, opentelemetry::nostd::string_view library_name,
      opentelemetry::nostd::string_view library_version = "", opentelemetry::nostd::string_view schema_url = "");

  /**
   * @brief Get or create a counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>
  mutable_metrics_counter_uint64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                 std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a double counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>
  mutable_metrics_counter_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                 std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a histogram
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>
  mutable_metrics_histogram_uint64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                   std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a double histogram
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>
  mutable_metrics_histogram_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                   std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a up down counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>
  mutable_metrics_up_down_counter_int64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                        std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a double up down counter
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>
  mutable_metrics_up_down_counter_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                         std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get observable
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  get_metrics_observable(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                         std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a observable counter
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_counter_int64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                           std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a double observable counter
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_counter_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                            std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a long observable gauge
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_gauge_int64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                         std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a double observable gauge
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_gauge_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                          std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a observable up down counter
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_up_down_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                   meter_instrument_key key,
                                                   std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get or create a double observable up down counter
   * @note callback of observable instrument mey be called in a different thread
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
  mutable_metrics_observable_up_down_counter_double(opentelemetry::nostd::string_view meter_name,
                                                    meter_instrument_key key,
                                                    std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get the current default logger
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> get_current_default_logger(
      std::shared_ptr<group_type> group = nullptr);

  /**
   * @brief Get logger
   *
   * @return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>
   */
  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> get_logger(
      opentelemetry::nostd::string_view logger_name, opentelemetry::nostd::string_view library_name = "",
      opentelemetry::nostd::string_view library_version = "", opentelemetry::nostd::string_view schema_url = "");

  SERVER_FRAME_API static opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> get_logger(
      std::shared_ptr<group_type> group, opentelemetry::nostd::string_view logger_name,
      opentelemetry::nostd::string_view library_name = "", opentelemetry::nostd::string_view library_version = "",
      opentelemetry::nostd::string_view schema_url = "");

  /**
   * @brief Set the current service object, it's used for tracer
   * @param app atapp instance
   * @param telemetry telemetry configure
   */
  SERVER_FRAME_API static void set_current_service(atapp::app& app,
                                                   const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg& telemetry);

  /**
   * @brief 获取自定义可观测性分组
   * @param group_name 分组名
   * @return
   */
  SERVER_FRAME_API static std::shared_ptr<group_type> get_group(gsl::string_view group_name);

  SERVER_FRAME_API static std::shared_ptr<group_type> get_default_group();
};

}  // namespace telemetry

}  // namespace rpc

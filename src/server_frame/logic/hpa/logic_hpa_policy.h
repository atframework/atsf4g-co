// Copyright 2024 atframework
// Created by owent

#pragma once

#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/nostd/shared_ptr.h>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>
#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

#include <rpc/telemetry/rpc_global_service.h>

#include <gsl/select-gsl.h>

#include <chrono>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "logic/hpa/logic_hpa_data_type.h"

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_hpa_cfg;
class logic_hpa_policy;
}  // namespace config
PROJECT_NAMESPACE_END

class logic_hpa_controller;

class logic_hpa_observer;
class logic_hpa_puller;

class logic_hpa_policy {
 public:
  enum class custom_observer_register_type : int8_t {
    kInt64 = 0,
    kDouble = 1,
  };

  using observable_callback_int64 = std::function<int64_t(logic_hpa_policy&)>;
  using observable_callback_double = std::function<double(logic_hpa_policy&)>;
  using observable_callback_custom = std::function<void(logic_hpa_policy&, logic_hpa_observer&)>;

  struct observable_callback_int64_data;
  using observable_callback_int64_handle = std::shared_ptr<observable_callback_int64_data>;

  struct observable_callback_double_data;
  using observable_callback_double_handle = std::shared_ptr<observable_callback_double_data>;

  struct observable_callback_custom_data;
  using observable_callback_custom_handle = std::shared_ptr<observable_callback_custom_data>;

  template <class CallbackType>
  struct UTIL_SYMBOL_VISIBLE event_callback_data {
    CallbackType callback;
    logic_hpa_event_active_type active;

    template <class FactorType>
    inline event_callback_data(FactorType&& fn, logic_hpa_event_active_type input_active)
        : callback(std::forward<FactorType>(fn)), active(input_active) {}
  };

  using event_callback_on_pull_range =
      std::function<void(logic_hpa_policy&, gsl::span<const std::unique_ptr<logic_hpa_pull_range_record>>)>;
  using event_callback_on_pull_instant =
      std::function<void(logic_hpa_policy&, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>>)>;
  using event_callback_on_pull_error = std::function<void(logic_hpa_policy&, gsl::string_view, gsl::string_view)>;
  using event_callback_on_pull_warning = std::function<void(logic_hpa_policy&, gsl::string_view)>;

  using event_callback_on_ready = std::function<void(logic_hpa_policy&)>;

  template <class CallbackDataType>
  struct UTIL_SYMBOL_VISIBLE event_callback_handle {
    typename std::list<event_callback_data<CallbackDataType>>::iterator iterator;
    int64_t version;

    UTIL_FORCEINLINE event_callback_handle(typename std::list<event_callback_data<CallbackDataType>>::iterator it,
                                           int64_t v)
        : iterator(it), version(v) {}

    event_callback_handle(const event_callback_handle&) = delete;
    UTIL_FORCEINLINE event_callback_handle(event_callback_handle&& other) noexcept
        : iterator(other.iterator), version(other.version) {
      other.version = 0;
    }

    event_callback_handle& operator=(const event_callback_handle&) = delete;
    UTIL_FORCEINLINE event_callback_handle& operator=(event_callback_handle&& other) noexcept {
      iterator = other.iterator;
      version = other.version;
      other.version = 0;
      return *this;
    }
  };

  using event_on_pull_range_callback_handle = event_callback_handle<event_callback_on_pull_range>;
  using event_on_pull_instant_callback_handle = event_callback_handle<event_callback_on_pull_instant>;
  using event_callback_on_ready_handle = event_callback_handle<event_callback_on_ready>;

  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_policy);
  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_policy);

 public:
  SERVER_FRAME_API logic_hpa_policy(
      logic_hpa_controller& controller, const std::shared_ptr<rpc::telemetry::group_type>& telemetry_group,
      const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
      const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg,
      const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& common_attributes_reference,
      const std::unordered_map<std::string, std::string>& common_selectors);

  SERVER_FRAME_API ~logic_hpa_policy();

  SERVER_FRAME_API const std::string& get_metrics_name() const noexcept;

  SERVER_FRAME_API const std::string& get_metrics_unit() const noexcept;

  SERVER_FRAME_API const std::string& get_metrics_description() const noexcept;

  SERVER_FRAME_API const std::string& get_query() const noexcept;

  SERVER_FRAME_API int tick();

  SERVER_FRAME_API void stop();

  SERVER_FRAME_API void reset_instrument();

  /**
   * @brief Add standard observer of int64
   *
   * @param fn callback to pick metric value
   * @param attributes (optional) private labels only for this observer
   * @return callback handle
   */
  SERVER_FRAME_API observable_callback_int64_handle
  add_observer_int64(observable_callback_int64 fn,
                     gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes = {});

  SERVER_FRAME_API bool remove_observer_int64(observable_callback_int64_handle& handle);

  /**
   * @brief Add standard observer of double
   *
   * @param fn callback to pick metric value
   * @param attributes (optional) private labels only for this observer
   * @return callback handle
   */
  SERVER_FRAME_API observable_callback_double_handle
  add_observer_double(observable_callback_double fn,
                      gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes = {});

  SERVER_FRAME_API bool remove_observer_double(observable_callback_double_handle& handle);

  /**
   * @brief Add custom observer, we need call logic_hpa_observer::observe to add value in callback
   *
   * @param register_type use int64 or double type to register this callback
   * @param fn callback in which we can call logic_hpa_observer::observe
   * @param attributes (optional) private labels only for this observer
   *
   * @note To minimize unnecessary CPU waste, deduplication and semantic conversation are no longer performed in
   *       logic_hpa_observer::observe. Please ensure that the additional labels in the attributes passed in meet the
   *       requirements of Prometheus.
   * @note The label key must use only uppercase or lowercase letters, digits, and underscores([a-zA-Z_][a-zA-Z0-9_]*)
   * @see https://prometheus.io/docs/concepts/data_model/
   *
   * @return callback handle
   */
  SERVER_FRAME_API observable_callback_custom_handle
  add_observer_custom(custom_observer_register_type register_type, observable_callback_custom fn,
                      gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes = {});

  SERVER_FRAME_API bool remove_observer_custom(observable_callback_custom_handle& handle);

  UTIL_FORCEINLINE bool is_stoping() const noexcept { return stoping_; }

  SERVER_FRAME_API bool is_stopped() const noexcept;

  UTIL_FORCEINLINE bool is_ready() const noexcept { return ready_; }

  /**
   * @brief 当前策略是否可拉取(配置了拉取规则)
   *
   * @return 可拉取返回 true
   */
  SERVER_FRAME_API bool can_pulling_available() const noexcept;

  /**
   * @brief 当前策略是否有有效监听(配置了拉取规则且设置了拉取回调)
   *
   * @return 配置了拉取规则且设置了拉取回调返回 true
   */
  SERVER_FRAME_API bool is_pulling_available() const noexcept;

  /**
   * @brief 是否设置了扩容或者缩容策略
   *
   * @return 设置了扩容或者缩容策略返回 true
   */
  SERVER_FRAME_API bool has_scaling_configure() const noexcept;

  /**
   * @brief 获取拉取到range数据转instant数据时的聚合策略
   *
   * @return 聚合策略
   */
  SERVER_FRAME_API logic_hpa_range_reduce_type get_pull_range_reduce_type() const noexcept;

  /**
   * @brief 设置拉取到range数据转instant数据时的聚合策略
   *
   * @param t 聚合策略
   */
  SERVER_FRAME_API void set_pull_range_reduce_type(logic_hpa_range_reduce_type t) noexcept;

  // =================== 事件监听接口 - begin ===================

  SERVER_FRAME_API event_on_pull_range_callback_handle add_event_on_pull_range(
      event_callback_on_pull_range fn, logic_hpa_event_active_type active = logic_hpa_event_active_type::kActive);
  SERVER_FRAME_API void remove_event_on_pull_range(event_on_pull_range_callback_handle& handle);
  SERVER_FRAME_API void set_event_on_pull_range_active(event_on_pull_range_callback_handle& handle,
                                                       logic_hpa_event_active_type active);
  SERVER_FRAME_API void clear_event_on_pull_range();
  SERVER_FRAME_API bool is_event_on_pull_range_handle_valid(const event_on_pull_range_callback_handle& handle);

  SERVER_FRAME_API event_on_pull_instant_callback_handle add_event_on_pull_instant(
      event_callback_on_pull_instant fn, logic_hpa_event_active_type active = logic_hpa_event_active_type::kActive);
  SERVER_FRAME_API void remove_event_on_pull_instant(event_on_pull_instant_callback_handle& handle);
  SERVER_FRAME_API void set_event_on_pull_instant_active(event_on_pull_instant_callback_handle& handle,
                                                         logic_hpa_event_active_type active);
  SERVER_FRAME_API void clear_event_on_pull_instant();
  SERVER_FRAME_API bool is_event_on_pull_instant_handle_valid(const event_on_pull_instant_callback_handle& handle);

  SERVER_FRAME_API void set_event_on_pull_error(event_callback_on_pull_error fn);
  SERVER_FRAME_API void clear_event_on_pull_error();

  SERVER_FRAME_API void set_event_on_pull_warning(event_callback_on_pull_warning fn);
  SERVER_FRAME_API void clear_event_on_pull_warning();

  SERVER_FRAME_API event_callback_on_ready_handle add_event_on_ready(
      event_callback_on_ready fn, logic_hpa_event_active_type active = logic_hpa_event_active_type::kActive);
  SERVER_FRAME_API void remove_event_on_ready(event_callback_on_ready_handle& handle);
  SERVER_FRAME_API void set_event_on_ready_active(event_callback_on_ready_handle& handle,
                                                  logic_hpa_event_active_type active);
  SERVER_FRAME_API void clear_event_on_ready();
  SERVER_FRAME_API bool is_event_on_ready_handle_valid(const event_callback_on_ready_handle& handle);

  // ------------------- 内置策略的事件监听接口 - end -------------------

  /**
   * @brief 设置私有数据
   *
   * @param priv_data 私有数据
   */
  SERVER_FRAME_API void set_private_data(void* priv_data) noexcept;

  /**
   * @brief 获取私有数据
   *
   * @param priv_data 私有数据
   */
  SERVER_FRAME_API void* get_private_data() const noexcept;

  /**
   * @brief 尽快触发策略数据拉取计算
   */
  SERVER_FRAME_API void schedule_pull() noexcept;

  UTIL_FORCEINLINE const logic_hpa_controller& get_controller() const noexcept { return *controller_; }
  UTIL_FORCEINLINE logic_hpa_controller& get_controller() noexcept { return *controller_; }

  /**
   * @brief 获取当前策略的扩容预期副本数
   *
   * @return 扩容策略预期的副本数
   */
  UTIL_FORCEINLINE int64_t get_configure_scaling_up_value() const noexcept { return scaling_up_value_; }

  /**
   * @brief 获取当前策略的缩容预期副本数
   *
   * @return 缩容策略预期的副本数
   */
  UTIL_FORCEINLINE int64_t get_configure_scaling_down_value() const noexcept { return scaling_down_value_; }

  SERVER_FRAME_API void trigger_event_on_pull_result(logic_hpa_pull_result& result) noexcept;

 private:
  void reset_instrument_int64();

  void reset_instrument_double();

  void reset_instrument_custom();

  void make_query(const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);

  void make_query_metrics_name(std::list<gsl::string_view>& query_slice);
  void make_query_selectors(std::list<gsl::string_view>& query_slice);
  void make_query_simple_function(std::list<gsl::string_view>& query_slice,
                                  const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void make_query_aggregation_operator(std::list<gsl::string_view>& query_slice,
                                       const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);

  void do_ready();

 private:
  gsl::not_null<logic_hpa_controller*> controller_;
  bool stoping_;
  bool ready_;
  void* private_data_;
  std::shared_ptr<rpc::telemetry::group_type> telemetry_group_;
  std::unordered_map<std::string, std::string> attributes_lifetime_;
  std::unordered_map<std::string, opentelemetry::common::AttributeValue> attributes_reference_;
  std::unordered_map<std::string, std::string> selectors_;

  int32_t pull_aggregation_type_;
  std::list<std::string> pull_aggregation_parameters_;
  logic_hpa_range_reduce_type pull_range_reduce_type_;

  std::string metrics_name_;
  std::string metrics_unit_;
  std::string metrics_description_;
  int32_t metrics_type_;
  std::list<std::string> simple_function_parameters_;
  std::string pull_metrics_name_;
  logic_hpa_pull_metrics_name_mode pull_metrics_name_mode_;
  std::string pull_query_;

  int64_t scaling_up_value_;
  int64_t scaling_down_value_;
  std::chrono::system_clock::time_point next_pull_timepoint_;
  std::chrono::system_clock::duration pull_interval_;
  std::chrono::system_clock::duration retry_interval_;

  std::recursive_mutex metrics_resource_lock_;
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> async_instruments_int64_;
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> async_instruments_double_;
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> async_instruments_custom_;
  opentelemetry::metrics::ObservableCallbackPtr async_instrument_callback_int64_;
  opentelemetry::metrics::ObservableCallbackPtr async_instrument_callback_double_;
  opentelemetry::metrics::ObservableCallbackPtr async_instrument_callback_custom_;

  template <class CallbackType>
  struct observable_callback_data {
    CallbackType callback;
    std::unordered_map<std::string, std::string> attributes;
  };

  template <class CallbackType>
  struct observable_callback_set {
    std::list<observable_callback_data<CallbackType>> observable;
    int64_t version = 0;

    UTIL_FORCEINLINE observable_callback_set() {}
  };

  observable_callback_set<observable_callback_int64> observable_callback_int64_;
  observable_callback_set<observable_callback_double> observable_callback_double_;
  observable_callback_set<observable_callback_custom> observable_callback_custom_;

  atfw::util::memory::strong_rc_ptr<logic_hpa_puller> puller_;

  template <class CallbackType>
  struct event_callback_set {
    std::list<event_callback_data<CallbackType>> callbacks;
    int64_t version = 0;

    UTIL_FORCEINLINE event_callback_set() {}
  };

  using event_on_pull_range_callback_set = event_callback_set<event_callback_on_pull_range>;
  using event_on_pull_instant_callback_set = event_callback_set<event_callback_on_pull_instant>;
  using event_on_ready_callback_set = event_callback_set<event_callback_on_ready>;

  event_on_pull_range_callback_set event_on_pull_range_callback_list_;
  event_on_pull_instant_callback_set event_on_pull_instant_callback_list_;
  event_callback_on_pull_error event_on_pull_error_callback_;
  event_callback_on_pull_warning event_on_pull_warning_callback_;
  event_on_ready_callback_set event_on_ready_callback_;
};

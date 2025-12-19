// Copyright 2024 atframework
// Created by owent
/**
 * @note Please add these processors below when using a PodMonitor of prometheus-adaptor
 *       to populate metrics into Kubernetes to prevent duplicate labels.
 * ```yaml
 *   resource/hpa:
 *    attributes:
 *      - action: upsert
 *        from_attribute: instance
 *        key: service.instance.id
 *      - action: delete
 *        key: job
 *      - action: delete
 *        key: instance
 *  attributes/hpa:
 *    - action: upsert
 *      from_attribute: instance
 *      key: service.instance.id
 *    - action: delete
 *      key: job
 *    - action: delete
 *      key: instance
 * ```
 */

// Windows头文件需要前置include，不然会有冲突
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

#  include <WinSock2.h>
#endif

#include "logic/hpa/logic_hpa_controller.h"

#include <uv.h>

#include <opentelemetry/sdk/resource/semantic_conventions.h>

#include <prometheus/gateway.h>

#include <atframe/atapp.h>
#include <atframe/etcdcli/etcd_discovery.h>
#include <atframe/modules/etcd_module.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <memory/object_allocator.h>

#include <config/logic_config.h>

#include <dispatcher/task_manager.h>
#include <utility/protobuf_mini_dumper.h>
#include <utility/rapid_json_helper.h>

#include <rpc/telemetry/exporter/prometheus_utility.h>
#include <rpc/telemetry/opentelemetry_utility.h>
#include <rpc/telemetry/rpc_global_service.h>
#include <rpc/telemetry/semantic_conventions.h>

#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "logic/hpa/logic_hpa_discovery.h"
#include "logic/hpa/logic_hpa_policy.h"

#ifdef max
#  undef max
#endif
#ifdef min
#  undef min
#endif

namespace {
// 直接使用prometheus兼容的标签名称，这样可以省一点点转换性能
constexpr const char* kLogicHpaMetricsTargetInfoJob = "job";
constexpr const char* kLogicHpaMetricsTargetInfoInstance = "instance";
constexpr const char* kLogicHpaMetricsTargetInfoServiceNamespace = "service_namespace";
constexpr const char* kLogicHpaMetricsTargetInfoServiceName = "service_name";

constexpr const char* kLogicHpaDiscoveryLabelReady = "hpa_scaling_ready";
constexpr const char* kLogicHpaDiscoveryLabelTarget = "hpa_scaling_target";
constexpr const char* kLogicHpaDiscoveryLabelWithPodIndex = "hpa_with_pod_index";

constexpr const char* kLogicHpaLabelTargetName = "hpa_target_name";
constexpr const char* kLogicHpaLabelTargetKind = "hpa_target_kind";
constexpr const char* kLogicHpaLabelTargetApiVersion = "hpa_target_api_version";

constexpr const char* kLogicHpaValueTrue = "1";
constexpr const char* kLogicHpaValueFalse = "0";

static constexpr const uint64_t kIgnoreInaccuracyBoundHrTime = 1000000000;
static constexpr const int64_t kIgnoreInaccuracyBoundMicroseconds = 1000000;

// 资源型指标的记录缓存, 用于计算增量和解决多puller多次拉取时的数据刷新问题
struct ATFW_UTIL_SYMBOL_LOCAL cpu_permillage_metrics_timeval_record {
  uv_timeval_t ru_utime; /* user CPU time used */
  uv_timeval_t ru_stime; /* system CPU time used */
  int64_t last_report_value;

  std::chrono::system_clock::time_point previous_report_time = std::chrono::system_clock::from_time_t(0);
};

// CPU hrtime记录缓存, 用于计算增量和解决多puller多次拉取时的数据刷新问题
struct ATFW_UTIL_SYMBOL_LOCAL cpu_permillage_metrics_hrtime_record {
  uint64_t idle_time;
  int64_t last_report_value;

  uint64_t previous_report_time = 0;
};

// 简单值类型记录缓存, 用于计算增量和解决多puller多次拉取时的数据刷新问题
struct ATFW_UTIL_SYMBOL_LOCAL simple_metrics_int64_record {
  int64_t last_report_value = 0;

  std::chrono::system_clock::time_point previous_report_time = std::chrono::system_clock::from_time_t(0);
};

/**
 * @brief 默认聚合策略
 *
 * @param target 自动填充目标配置
 * @param source 原有聚合策略标签
 * @param append_labels 附加聚合策略标签
 * @return true 有新增的标签
 */
static bool append_default_aggregation_by(
    PROJECT_NAMESPACE_ID::config::logic_hpa_policy_aggregation_parameter& target,
    const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& source,
    std::initializer_list<std::initializer_list<std::string>> append_labels) {
  bool ret = false;
  for (auto& label_set : append_labels) {
    const std::string* key = nullptr;

    for (auto& label : label_set) {
      if (label.empty()) {
        continue;
      }
      if (nullptr == key) {
        key = &label;
      }
      if (source.find(label) != source.end()) {
        target.mutable_by()->add_labels(
            rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(*key, true));
        ret = true;
        break;
      }
    }
  }

  return ret;
}

/**
 * @brief SSL版本名转换
 *
 * @param ssl_version SSL版本名，和otel-cpp一致
 * @return 转换后的选项
 */
static logic_hpa_ssl_version ssl_version_from_name(const std::string& ssl_version) {
  if (ssl_version.empty()) {
    return logic_hpa_ssl_version::kNone;
  }
  if (0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv1.3", 7) ||
      0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv13", 6)) {
    return logic_hpa_ssl_version::kTlsV13;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv1.2", 7) ||
             0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv12", 6)) {
    return logic_hpa_ssl_version::kTlsV12;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv1.1", 7) ||
             0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv11", 6)) {
    return logic_hpa_ssl_version::kTlsV11;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv1", 5) ||
             0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv1.0", 7) ||
             0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "TLSv10", 6)) {
    return logic_hpa_ssl_version::kTlsV10;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP(ssl_version.c_str(), "SSLv3", 5)) {
    return logic_hpa_ssl_version::kSsl3;
  } else {
    return logic_hpa_ssl_version::kNone;
  }
}

enum class main_controller_flag : int8_t {
  kUnset = 0,
  kYes = 1,
  kNo = 2,
};

enum class main_controller_mode : int8_t {
  kUnknown = 0,
  kCloudNative = 1,
  kNonNativeCloud = 2,
};

enum class controller_status_metrics_gauge : int64_t {
  kMainController = 0x0001,    /// 主控节点状态
  kCloudNative = 0x0002,       /// 云原生模式
  kLabelReady = 0x0010,        /// Ready标签(当前分布标签)
  kLabelTarget = 0x0020,       /// Target标签(目标分布标签)
  kReplicatePending = 0x0100,  /// 等待同步
  kReplicateRunning = 0x0200,  /// 正在同步
  kScalingPending = 0x0400,    /// 等待扩缩容
  kScalingDone = 0x0800,       /// 扩缩容完成(稳定状态)
};

struct ATFW_UTIL_SYMBOL_LOCAL hpa_discovery_schedule_scaling_data {
  bool with_action = false;     // 指令控制扩缩容
  time_t replicate_start = -1;  // 指令控制同步开始时间
  time_t replicate_end = -1;    // 指令控制同步结束时间
  time_t expect_scaling = -1;   // 指令控制扩缩容时间

  inline hpa_discovery_schedule_scaling_data() noexcept {}
};

struct ATFW_UTIL_SYMBOL_LOCAL logic_hpa_discovery_with_event {
  std::shared_ptr<logic_hpa_discovery> instance;
  std::function<void(logic_hpa_discovery&, logic_hpa_discovery_setup_policy_accessor&)> setup_callback;
};

}  // namespace

struct ATFW_UTIL_SYMBOL_LOCAL logic_hpa_controller::hpa_discovery_data {
  bool reload_controller;
  bool stoping;
  time_t stoping_hpa_label_ready_timepoint;
  time_t stoping_hpa_label_target_timepoint;
  uint64_t with_type_id;
  std::string with_type_name;
  std::string configure_key;
  std::shared_ptr<logic_hpa_discovery> default_hpa_discovery;
  time_t default_hpa_discovery_next_submit_timepoint;
  time_t default_hpa_discovery_scaling_up_stabilization_end_timepoint;
  time_t default_hpa_discovery_scaling_down_stabilization_end_timepoint;
  int32_t default_hpa_discovery_scaling_up_target_replicas;
  int32_t default_hpa_discovery_scaling_down_target_replicas;
  int32_t default_hpa_discovery_scaling_up_expect_replicas;
  int32_t default_hpa_discovery_scaling_down_expect_replicas;
  std::unordered_map<std::string, logic_hpa_discovery_with_event> custom_hpa_discovery;

  std::mutex event_loop_lock;
  uv_loop_t* event_loop_pointer;

  atfw::atapp::etcd_discovery_set::ptr_t discovery_set;
  atfw::atapp::protocol::atapp_metadata discovery_filter;
  atfw::atapp::etcd_module::node_event_callback_handle_t node_event_handle;
  bool node_event_has_handle;

  int64_t current_setting_data_version;
  int64_t current_setting_modify_revision;
  PROJECT_NAMESPACE_ID::config::logic_hpa_status current_setting;
  PROJECT_NAMESPACE_ID::config::logic_hpa_status expect_setting;
  bool current_hpa_label_ready;
  bool current_hpa_label_target;
  bool current_hpa_discovery_with_pod_index;
  time_t last_controller_tick_timepoint;
  std::atomic<bool> report_hpa_label_ready;
  std::atomic<bool> report_hpa_label_target;

  std::atomic<int32_t> controller_stateful_index;
  std::atomic<int32_t> main_controller_current_replicas;
  std::atomic<int32_t> main_controller_expect_replicas;
  std::atomic<time_t> main_controller_expect_scaling_timepoint;
  main_controller_flag main_controller_flag_cache;
  main_controller_mode main_controller_flag_mode;
  time_t main_controller_next_submit_timepoint;
  int32_t main_controller_debug_expect_replicas;
  std::atomic<int64_t> controller_metrics_status;
  std::atomic<int64_t> controller_debug_cpu_permillage_offset;
  std::atomic<int64_t> controller_debug_cpu_permillage_end_time;

  time_t schedule_shutdown_timepoint;
  hpa_discovery_schedule_scaling_data schedule_scaling;

  hpa_discovery_data()
      : reload_controller(true),
        stoping(false),
        stoping_hpa_label_ready_timepoint(0),
        stoping_hpa_label_target_timepoint(0),
        with_type_id(0),
        default_hpa_discovery_next_submit_timepoint(0),
        default_hpa_discovery_scaling_up_stabilization_end_timepoint(0),
        default_hpa_discovery_scaling_down_stabilization_end_timepoint(0),
        default_hpa_discovery_scaling_up_target_replicas(0),
        default_hpa_discovery_scaling_down_target_replicas(0),
        default_hpa_discovery_scaling_up_expect_replicas(0),
        default_hpa_discovery_scaling_down_expect_replicas(0),
        event_loop_pointer(nullptr),
        discovery_set(atfw::memory::stl::make_strong_rc<atfw::atapp::etcd_discovery_set>()),
        node_event_has_handle(false),
        current_setting_data_version(0),
        current_setting_modify_revision(0),
        current_hpa_label_ready(false),
        current_hpa_label_target(false),
        current_hpa_discovery_with_pod_index(false),
        last_controller_tick_timepoint(0),
        report_hpa_label_ready(false),
        report_hpa_label_target(false),
        controller_stateful_index(0),
        main_controller_current_replicas(0),
        main_controller_expect_replicas(0),
        main_controller_expect_scaling_timepoint(0),
        main_controller_flag_cache(main_controller_flag::kUnset),
        main_controller_flag_mode(main_controller_mode::kUnknown),
        main_controller_next_submit_timepoint(0),
        main_controller_debug_expect_replicas(0),
        controller_metrics_status(0),
        controller_debug_cpu_permillage_offset(0),
        controller_debug_cpu_permillage_end_time(0),
        schedule_shutdown_timepoint(0) {}
};

struct ATFW_UTIL_SYMBOL_LOCAL logic_hpa_controller::hpa_discovery_data_accessor {
  static void assign_main_controller_expect_from_status(logic_hpa_controller::hpa_discovery_data& output,
                                                        const PROJECT_NAMESPACE_ID::config::logic_hpa_status& input) {
    output.main_controller_expect_replicas.store(input.expect_replicas(), std::memory_order_release);
    output.main_controller_expect_scaling_timepoint.store(input.expect_scaling_timepoint().seconds(),
                                                          std::memory_order_release);
  }

  static void reset_controller_metrics_status(logic_hpa_controller::hpa_discovery_data& output) {
    auto now = util::time::time_utility::sys_now();

    // 状态指标，用于监控
    int64_t value = 0;
    if (output.main_controller_flag_cache == main_controller_flag::kYes) {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kMainController);
    }

    if (output.main_controller_flag_mode == main_controller_mode::kCloudNative) {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kCloudNative);
    }

    if (output.current_hpa_label_ready) {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kLabelReady);
    }

    if (output.current_hpa_label_target) {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kLabelTarget);
    }

    if (now >= protobuf_to_system_clock(output.current_setting.expect_scaling_timepoint())) {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kScalingDone);
    } else if (now >= protobuf_to_system_clock(output.current_setting.expect_replicate_end_timepoint())) {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kScalingPending);
    } else if (now >= protobuf_to_system_clock(output.current_setting.expect_replicate_start_timepoint())) {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kReplicateRunning);
    } else {
      value |= static_cast<int64_t>(controller_status_metrics_gauge::kReplicatePending);
    }

    output.controller_metrics_status.store(value, std::memory_order_relaxed);
  }

  static void command_show_hpa_controller_configure(
      util::cli::callback_param params,
      std::shared_ptr<logic_hpa_controller::hpa_discovery_data> hpa_discovery_data_ptr) {
    if (hpa_discovery_data_ptr->with_type_id != 0) {
      ::atfw::atapp::app::add_custom_command_rsp(
          params, util::log::format("HPA Controller Discovery with type id: {}", hpa_discovery_data_ptr->with_type_id));
    }

    if (!hpa_discovery_data_ptr->with_type_name.empty()) {
      ::atfw::atapp::app::add_custom_command_rsp(
          params,
          util::log::format("HPA Controller Discovery with type name: {}", hpa_discovery_data_ptr->with_type_name));
    }

    auto& hpa_configure = logic_config::me()->get_logic().hpa();
    ::atfw::atapp::app::add_custom_command_rsp(
        params,
        util::log::format("HPA metrics configure:\n{}", protobuf_mini_dumper_get_readable(hpa_configure.metrics())));

    ::atfw::atapp::app::add_custom_command_rsp(
        params, util::log::format("HPA Controller configure:\n{}",
                                  protobuf_mini_dumper_get_readable(hpa_configure.controller())));

    ::atfw::atapp::app::add_custom_command_rsp(
        params, util::log::format("HPA Controller Discovery filter:\n{}",
                                  protobuf_mini_dumper_get_readable(hpa_discovery_data_ptr->discovery_filter)));
  }

  static void command_show_hpa_controller_status(
      util::cli::callback_param params,
      std::shared_ptr<logic_hpa_controller::hpa_discovery_data> hpa_discovery_data_ptr) {
    ::atfw::atapp::app::add_custom_command_rsp(
        params, util::log::format("HPA Controller stateful index: {}",
                                  hpa_discovery_data_ptr->controller_stateful_index.load(std::memory_order_acquire)));

    ::atfw::atapp::app::add_custom_command_rsp(
        params,
        util::log::format("HPA Controller current replicas: {}",
                          hpa_discovery_data_ptr->main_controller_current_replicas.load(std::memory_order_acquire)));

    ::atfw::atapp::app::add_custom_command_rsp(
        params,
        util::log::format("HPA Controller expect replicas: {}",
                          hpa_discovery_data_ptr->main_controller_expect_replicas.load(std::memory_order_acquire)));

    {
      time_t expect_scaling_timepoint =
          hpa_discovery_data_ptr->main_controller_expect_scaling_timepoint.load(std::memory_order_acquire);
      std::tm c_tm;
      char local_time_str[32] = {0};
      UTIL_STRFUNC_LOCALTIME_S(&expect_scaling_timepoint, &c_tm);
      std::strftime(local_time_str, sizeof(local_time_str) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);

      ::atfw::atapp::app::add_custom_command_rsp(
          params, util::log::format("HPA Controller expect scaling timepoint: {}(local time: {})",
                                    expect_scaling_timepoint, local_time_str));
    }

    const char* is_main;
    if (hpa_discovery_data_ptr->main_controller_flag_cache == main_controller_flag::kYes) {
      is_main = "yes";
    } else if (hpa_discovery_data_ptr->main_controller_flag_cache == main_controller_flag::kNo) {
      is_main = "no";
    } else {
      is_main = "unset";
    }

    ::atfw::atapp::app::add_custom_command_rsp(params, util::log::format("HPA Controller is main: {}", is_main));
    ::atfw::atapp::app::add_custom_command_rsp(
        params, util::log::format("HPA Controller ready label: {}", hpa_discovery_data_ptr->current_hpa_label_ready));
    ::atfw::atapp::app::add_custom_command_rsp(
        params, util::log::format("HPA Controller target label: {}", hpa_discovery_data_ptr->current_hpa_label_target));

    // 主控节点额外输出提交信息
    if (hpa_discovery_data_ptr->main_controller_flag_cache == main_controller_flag::kYes) {
      time_t next_submit_timepoint = hpa_discovery_data_ptr->default_hpa_discovery_next_submit_timepoint;
      std::tm c_tm;
      char local_time_str[32] = {0};
      UTIL_STRFUNC_LOCALTIME_S(&next_submit_timepoint, &c_tm);
      std::strftime(local_time_str, sizeof(local_time_str) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);
      ::atfw::atapp::app::add_custom_command_rsp(
          params, util::log::format("HPA Controller next submit time: {}(local time: {})", next_submit_timepoint,
                                    local_time_str));

      time_t scaling_up_stabilization_end_timepoint =
          hpa_discovery_data_ptr->default_hpa_discovery_scaling_up_stabilization_end_timepoint;
      if (scaling_up_stabilization_end_timepoint > 0) {
        UTIL_STRFUNC_LOCALTIME_S(&scaling_up_stabilization_end_timepoint, &c_tm);
        std::strftime(local_time_str, sizeof(local_time_str) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);
        ::atfw::atapp::app::add_custom_command_rsp(
            params, util::log::format("HPA Controller scaling up stabilization end time: {}(local time: {})",
                                      scaling_up_stabilization_end_timepoint, local_time_str));
        ::atfw::atapp::app::add_custom_command_rsp(
            params, util::log::format("HPA Controller scaling up target replicas: {}",
                                      hpa_discovery_data_ptr->default_hpa_discovery_scaling_up_target_replicas));
      } else {
        ::atfw::atapp::app::add_custom_command_rsp(params, "HPA Controller scaling up stabilization end time: NA");
      }
      ::atfw::atapp::app::add_custom_command_rsp(
          params, util::log::format("HPA Controller scaling up expect replicas: {}",
                                    hpa_discovery_data_ptr->default_hpa_discovery_scaling_up_expect_replicas));

      time_t scaling_down_stabilization_end_timepoint =
          hpa_discovery_data_ptr->default_hpa_discovery_scaling_down_stabilization_end_timepoint;
      if (scaling_down_stabilization_end_timepoint > 0) {
        UTIL_STRFUNC_LOCALTIME_S(&scaling_down_stabilization_end_timepoint, &c_tm);
        std::strftime(local_time_str, sizeof(local_time_str) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);
        ::atfw::atapp::app::add_custom_command_rsp(
            params, util::log::format("HPA Controller scaling down stabilization end time: {}(local time: {})",
                                      scaling_down_stabilization_end_timepoint, local_time_str));
        ::atfw::atapp::app::add_custom_command_rsp(
            params, util::log::format("HPA Controller scaling down target replicas: {}",
                                      hpa_discovery_data_ptr->default_hpa_discovery_scaling_down_target_replicas));
      } else {
        ::atfw::atapp::app::add_custom_command_rsp(params, "HPA Controller scaling down stabilization end time: NA");
      }
      ::atfw::atapp::app::add_custom_command_rsp(
          params, util::log::format("HPA Controller scaling down expect replicas: {}",
                                    hpa_discovery_data_ptr->default_hpa_discovery_scaling_down_expect_replicas));

      // 输出所有策略的评估值
      if (hpa_discovery_data_ptr->default_hpa_discovery) {
        ::atfw::atapp::app::add_custom_command_rsp(params, "HPA Controller policy:");
        hpa_discovery_data_ptr->default_hpa_discovery->foreach_policy(
            [&params](const logic_hpa_policy& policy, int64_t last_value,
                      std::chrono::system_clock::time_point last_update_time) -> bool {
              time_t last_update_unix_timestamp = std::chrono::system_clock::to_time_t(last_update_time);
              char local_policy_time_str[32] = {0};
              if (last_update_unix_timestamp <= 0) {
                memcpy(local_policy_time_str, "Not pulled yet", 14);
              } else {
                std::tm policy_c_tm;
                UTIL_STRFUNC_LOCALTIME_S(&last_update_unix_timestamp, &policy_c_tm);
                std::strftime(local_policy_time_str, sizeof(local_policy_time_str) - 1, "%Y-%m-%d %H:%M:%S",
                              &policy_c_tm);
              }

              ::atfw::atapp::app::add_custom_command_rsp(
                  params,
                  util::log::format("\t Policy {} : Scaling up target: {}, , Scaling down target: {}, Last value: {}, "
                                    "Last update time: {}",
                                    policy.get_metrics_name(), policy.get_configure_scaling_up_value(),
                                    policy.get_configure_scaling_down_value(), last_value, local_policy_time_str));
              return true;
            });
      }
    } else {
      ::atfw::atapp::app::add_custom_command_rsp(params, "HPA Controller next submit time: NA");
      ::atfw::atapp::app::add_custom_command_rsp(params, "HPA Controller scaling up stabilization end time: NA");
      ::atfw::atapp::app::add_custom_command_rsp(params, "HPA Controller scaling down stabilization end time: NA");
    }

    ::atfw::atapp::app::add_custom_command_rsp(
        params, util::log::format("HPA Controller current setting:\n{}",
                                  protobuf_mini_dumper_get_readable(hpa_discovery_data_ptr->current_setting)));

    if (is_main) {
      ::atfw::atapp::app::add_custom_command_rsp(
          params, util::log::format("HPA Controller expect setting:\n{}",
                                    protobuf_mini_dumper_get_readable(hpa_discovery_data_ptr->expect_setting)));
    }
  }

  static void command_show_hpa_controller_discovery(
      util::cli::callback_param params,
      std::shared_ptr<logic_hpa_controller::hpa_discovery_data> hpa_discovery_data_ptr) {
    if (!hpa_discovery_data_ptr->discovery_set) {
      return;
    }

    auto& hpa_configure = logic_config::me()->get_logic().hpa();
    ::atfw::atapp::app::add_custom_command_rsp(
        params, util::log::format("HPA Controller Discovery configure: {}",
                                  protobuf_mini_dumper_get_readable(hpa_configure.discovery())));

    for (auto& node : hpa_discovery_data_ptr->discovery_set->get_sorted_nodes()) {
      if (!node) {
        continue;
      }

      ::atfw::atapp::app::add_custom_command_rsp(
          params, util::log::format("HPA Controller Discovery node: {}",
                                    protobuf_mini_dumper_get_readable(node->get_discovery_info())));
    }
  }

  static void command_schedule_hpa_node_shutdown(
      util::cli::callback_param params,
      std::shared_ptr<logic_hpa_controller::hpa_discovery_data> hpa_discovery_data_ptr) {
    if (!hpa_discovery_data_ptr->discovery_set) {
      return;
    }

    time_t offset = 0;
    if (params.get_params_number() > 0) {
      offset = params[0]->to_int32();
      if (offset < 0) {
        offset = 0;
      }
    }

    // 手动控制预期下线
    offset = util::time::time_utility::get_sys_now() + offset;
    hpa_discovery_data_ptr->schedule_shutdown_timepoint = offset;

    std::tm c_tm;
    char local_time_str[32] = {0};
    UTIL_STRFUNC_LOCALTIME_S(&offset, &c_tm);
    std::strftime(local_time_str, sizeof(local_time_str) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);

    std::string response_message =
        util::log::format("HPA Controller schedule shutdown at: {}(local time: {})", offset, local_time_str);
    ::atfw::atapp::app::add_custom_command_rsp(params, response_message);

    FWLOGINFO("[HPA]: Controller receive command: {}\n{}", "schedule-hpa-node-shutdown", response_message);
  }

  static void command_schedule_hpa_expect_scaling(
      util::cli::callback_param params,
      std::shared_ptr<logic_hpa_controller::hpa_discovery_data> hpa_discovery_data_ptr) {
    if (!hpa_discovery_data_ptr->discovery_set) {
      return;
    }

    // 手动控制预期扩缩容时间
    time_t sys_now = util::time::time_utility::get_sys_now();
    hpa_discovery_data_ptr->schedule_scaling.with_action = true;
    hpa_discovery_data_ptr->default_hpa_discovery_next_submit_timepoint = sys_now;
    hpa_discovery_data_ptr->default_hpa_discovery_scaling_up_stabilization_end_timepoint = sys_now;
    hpa_discovery_data_ptr->default_hpa_discovery_scaling_down_stabilization_end_timepoint = sys_now;

    if (params.get_params_number() > 0) {
      hpa_discovery_data_ptr->schedule_scaling.replicate_start = sys_now + params[0]->to_int32();
    } else {
      hpa_discovery_data_ptr->schedule_scaling.replicate_start = sys_now;
    }

    if (params.get_params_number() > 1) {
      hpa_discovery_data_ptr->schedule_scaling.replicate_end =
          hpa_discovery_data_ptr->schedule_scaling.replicate_start + params[1]->to_int32();
    } else {
      hpa_discovery_data_ptr->schedule_scaling.replicate_end = hpa_discovery_data_ptr->schedule_scaling.replicate_start;
    }

    if (params.get_params_number() > 2) {
      hpa_discovery_data_ptr->schedule_scaling.expect_scaling =
          hpa_discovery_data_ptr->schedule_scaling.replicate_end + params[2]->to_int32();
    } else {
      hpa_discovery_data_ptr->schedule_scaling.expect_scaling = hpa_discovery_data_ptr->schedule_scaling.replicate_end;
    }

    std::tm c_tm;
    char local_time_replicate_start[32] = {0};
    char local_time_replicate_end[32] = {0};
    char local_time_replicate_scaling[32] = {0};
    time_t tp_replicate_start = hpa_discovery_data_ptr->schedule_scaling.replicate_start;
    time_t tp_replicate_end = hpa_discovery_data_ptr->schedule_scaling.replicate_end;
    time_t tp_expect_scaling = hpa_discovery_data_ptr->schedule_scaling.expect_scaling;
    UTIL_STRFUNC_LOCALTIME_S(&tp_replicate_start, &c_tm);
    std::strftime(local_time_replicate_start, sizeof(local_time_replicate_start) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);
    UTIL_STRFUNC_LOCALTIME_S(&tp_replicate_end, &c_tm);
    std::strftime(local_time_replicate_end, sizeof(local_time_replicate_start) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);
    UTIL_STRFUNC_LOCALTIME_S(&tp_expect_scaling, &c_tm);
    std::strftime(local_time_replicate_scaling, sizeof(local_time_replicate_start) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);

    std::string response_message = util::log::format(
        "HPA Controller schedule scaling\n\treplicate start at: {}(local time: {})\n\treplicate "
        "end at: {}(local time: {})\n\treplicate scaling at: {}(local time: {})",
        tp_replicate_start, local_time_replicate_start, tp_replicate_end, local_time_replicate_end, tp_expect_scaling,
        local_time_replicate_scaling);

    ::atfw::atapp::app::add_custom_command_rsp(params, response_message);

    FWLOGINFO("[HPA]: Controller receive command: {}\n{}", "schedule-hpa-expect-scaling", response_message);
  }

  static void command_debug_hpa_set_expect_replicas(
      util::cli::callback_param params,
      std::shared_ptr<logic_hpa_controller::hpa_discovery_data> hpa_discovery_data_ptr) {
    if (!hpa_discovery_data_ptr->discovery_set) {
      return;
    }

    // 测试用，手动控制预期副本数
    if (params.get_params_number() > 0) {
      hpa_discovery_data_ptr->main_controller_debug_expect_replicas = params[0]->to_int32();
    } else {
      hpa_discovery_data_ptr->main_controller_debug_expect_replicas = 0;
    }

    std::string response_message = util::log::format("debug-hpa-set-expect-replicas {}",
                                                     hpa_discovery_data_ptr->main_controller_debug_expect_replicas);
    ::atfw::atapp::app::add_custom_command_rsp(params, response_message);

    FWLOGINFO("[HPA]: Controller receive command: {}", response_message);
  }

  static void command_debug_hpa_fake_cpu_permillage(
      util::cli::callback_param params,
      std::shared_ptr<logic_hpa_controller::hpa_discovery_data> hpa_discovery_data_ptr) {
    if (!hpa_discovery_data_ptr->discovery_set) {
      return;
    }

    // 测试用，虚拟控制CPU占用上报千分比
    if (params.get_params_number() > 0) {
      hpa_discovery_data_ptr->controller_debug_cpu_permillage_offset.store(params[0]->to_int64(),
                                                                           std::memory_order_relaxed);
      if (params.get_params_number() > 1) {
        hpa_discovery_data_ptr->controller_debug_cpu_permillage_end_time.store(
            static_cast<int64_t>(util::time::time_utility::get_sys_now()) + params[1]->to_int64(),
            std::memory_order_relaxed);
      } else {
        hpa_discovery_data_ptr->controller_debug_cpu_permillage_end_time.store(
            static_cast<int64_t>(util::time::time_utility::get_sys_now()) + 600, std::memory_order_relaxed);
      }
    } else {
      hpa_discovery_data_ptr->controller_debug_cpu_permillage_offset.store(0, std::memory_order_relaxed);
      hpa_discovery_data_ptr->controller_debug_cpu_permillage_end_time.store(
          static_cast<int64_t>(util::time::time_utility::get_sys_now() - 1), std::memory_order_relaxed);
    }

    std::tm c_tm;
    char local_time_end_time[32] = {0};
    time_t tp_end_time = static_cast<int64_t>(
        hpa_discovery_data_ptr->controller_debug_cpu_permillage_end_time.load(std::memory_order_relaxed));
    UTIL_STRFUNC_LOCALTIME_S(&tp_end_time, &c_tm);
    std::strftime(local_time_end_time, sizeof(local_time_end_time) - 1, "%Y-%m-%d %H:%M:%S", &c_tm);

    std::string response_message = util::log::format(
        "debug-hpa-fake-cpu-permillage set offset: {}, end time: {}(local time: {})",
        hpa_discovery_data_ptr->controller_debug_cpu_permillage_offset.load(std::memory_order_relaxed),
        hpa_discovery_data_ptr->controller_debug_cpu_permillage_end_time.load(std::memory_order_relaxed),
        local_time_end_time);
    ::atfw::atapp::app::add_custom_command_rsp(params, response_message);

    FWLOGINFO("[HPA]: Controller receive command: {}", response_message);
  }

  static void set_main_hpa_controller_flag(hpa_discovery_data& target, main_controller_flag flag_cache,
                                           main_controller_mode mode) noexcept {
    target.main_controller_flag_cache = flag_cache;
    target.main_controller_flag_mode = mode;

    if (flag_cache == main_controller_flag::kNo) {
      target.default_hpa_discovery_scaling_up_stabilization_end_timepoint = 0;
      target.default_hpa_discovery_scaling_up_target_replicas = 0;
      target.default_hpa_discovery_scaling_up_expect_replicas = 0;
      target.default_hpa_discovery_scaling_down_stabilization_end_timepoint = 0;
      target.default_hpa_discovery_scaling_down_target_replicas = 0;
      target.default_hpa_discovery_scaling_down_expect_replicas = 0;
    }
  }
};

SERVER_FRAME_API logic_hpa_controller::logic_hpa_controller(atfw::atapp::app& owner_app)
    : owner_app_(&owner_app),
      need_configure_(false),
      available_(false),
      last_tick_timepoint_(0),
      pull_ssl_version_(logic_hpa_ssl_version::kNone),
      pull_http_method_(util::network::http_request::method_t::EN_MT_GET),
      hpa_discovery_data_(atfw::memory::stl::make_shared<hpa_discovery_data>()) {
  // Setup commands
  std::shared_ptr<hpa_discovery_data> hpa_discovery_data_ptr = hpa_discovery_data_;
  util::cli::cmd_option_ci::ptr_type cmd_mgr = owner_app.get_command_manager();

  // 绑定HPA控制器相关的应用指令
  cmd_mgr
      ->bind_cmd("show-hpa-controller-configure", hpa_discovery_data_accessor::command_show_hpa_controller_configure,
                 hpa_discovery_data_ptr)
      ->set_help_msg("show-hpa-controller-configure          show hpa controller configure");

  cmd_mgr
      ->bind_cmd("show-hpa-controller-status", hpa_discovery_data_accessor::command_show_hpa_controller_status,
                 hpa_discovery_data_ptr)
      ->set_help_msg("show-hpa-controller-status             show hpa controller status");

  cmd_mgr
      ->bind_cmd("show-hpa-controller-discovery", hpa_discovery_data_accessor::command_show_hpa_controller_discovery,
                 hpa_discovery_data_ptr)
      ->set_help_msg("show-hpa-controller-discovery          show hpa controller discovery");

  cmd_mgr
      ->bind_cmd("schedule-hpa-node-shutdown", hpa_discovery_data_accessor::command_schedule_hpa_node_shutdown,
                 hpa_discovery_data_ptr)
      ->set_help_msg("schedule-hpa-node-shutdown [seconds]   schedule hpa controller discovery");

  cmd_mgr
      ->bind_cmd("schedule-hpa-expect-scaling", hpa_discovery_data_accessor::command_schedule_hpa_expect_scaling,
                 hpa_discovery_data_ptr)
      ->set_help_msg(
          "schedule-hpa-expect-scaling [start] [end] [scaling] schedule hpa expect scaling for main controller");

  cmd_mgr
      ->bind_cmd("debug-hpa-set-expect-replicas", hpa_discovery_data_accessor::command_debug_hpa_set_expect_replicas,
                 hpa_discovery_data_ptr)
      ->set_help_msg(
          "debug-hpa-set-expect-replicas [replicate]           Debug HPA main controller and set expect replicas");

  cmd_mgr
      ->bind_cmd("debug-hpa-fake-cpu-permillage", hpa_discovery_data_accessor::command_debug_hpa_fake_cpu_permillage,
                 hpa_discovery_data_ptr)
      ->set_help_msg("debug-hpa-fake-cpu-permillage [offset] [duration]   Debug HPA controller and set cpu permillage");
}

SERVER_FRAME_API logic_hpa_controller::~logic_hpa_controller() {
  if (multi_curl_handle_) {
    util::network::http_request::destroy_curl_multi(multi_curl_handle_);
  }
}

SERVER_FRAME_API void logic_hpa_controller::init() {
  util::network::http_request::curl_share_options curl_share_options;
  util::network::http_request::curl_multi_options curl_multi_options;
  {
    std::lock_guard<std::mutex> event_loop_lock_guard{hpa_discovery_data_->event_loop_lock};
    hpa_discovery_data_->event_loop_pointer = get_app()->get_evloop();
    curl_multi_options.ev_loop = hpa_discovery_data_->event_loop_pointer;
  }
  util::network::http_request::create_curl_share(curl_share_options, curl_multi_options.share_context);

  util::network::http_request::create_curl_multi(curl_multi_options, multi_curl_handle_);
}

SERVER_FRAME_API void logic_hpa_controller::reload() {
  // 因为HPA模块依赖telemetry模块的篇日志初始化，我们延迟实际的reload行为到下一次tick
  need_configure_ = true;

  auto& hpa_configure = logic_config::me()->get_logic().hpa();
  auto& hpa_target = hpa_configure.controller().target();

  time_t sys_now = util::time::time_utility::get_sys_now();

  // Guess stateful_index
  reload_runtime_stateful_pod_index();

  // 首次初始化，设置HPA策略路由标签
  if (owner_app_ != nullptr && !owner_app_->is_running()) {
    bool ready_now = hpa_configure.controller().configure_key().empty() || !hpa_configure.controller().enable();
    // 如果启动了HPA控制模块，需要等第一次数据拉取完毕后或者1分钟后才能决定初始是不是ready状态
    owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelReady, kLogicHpaValueFalse);
    owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelTarget, kLogicHpaValueFalse);

    if (hpa_discovery_data_) {
      hpa_discovery_data_->current_hpa_label_ready = false;
      hpa_discovery_data_->current_hpa_label_target = false;
      hpa_discovery_data_->report_hpa_label_ready.store(false, std::memory_order_relaxed);
      hpa_discovery_data_->report_hpa_label_target.store(false, std::memory_order_relaxed);

      int32_t pod_stateful_index = hpa_discovery_data_->controller_stateful_index.load(std::memory_order_acquire);

      hpa_discovery_data_->current_setting.set_current_replicas(pod_stateful_index);
      hpa_discovery_data_->current_setting.set_expect_replicas(pod_stateful_index);
      // 延迟1-2秒以便于消峰和触发业务层回调
      hpa_discovery_data_->current_setting.mutable_expect_replicate_start_timepoint()->set_seconds(sys_now + 1);
      if (ready_now) {
        hpa_discovery_data_->current_setting.mutable_expect_replicate_end_timepoint()->set_seconds(sys_now + 1);
      } else {
        hpa_discovery_data_->current_setting.mutable_expect_replicate_end_timepoint()->set_seconds(
            sys_now + util::time::time_utility::MINITE_SECONDS);
      }
      hpa_discovery_data_->current_setting.mutable_expect_scaling_timepoint()->set_seconds(
          hpa_discovery_data_->current_setting.expect_replicate_end_timepoint().seconds());
      hpa_discovery_data_->main_controller_current_replicas.store(pod_stateful_index);
      hpa_discovery_data_->main_controller_expect_replicas.store(pod_stateful_index);
    }
  } else if (owner_app_ != nullptr) {
    // Reload时直接恢复之前的设置
    if (hpa_discovery_data_) {
      owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelReady, hpa_discovery_data_->current_hpa_label_ready
                                                                       ? kLogicHpaValueTrue
                                                                       : kLogicHpaValueFalse);
      owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelTarget, hpa_discovery_data_->current_hpa_label_target
                                                                        ? kLogicHpaValueTrue
                                                                        : kLogicHpaValueFalse);
    }
  }

  // 固定标签总是要附加
  if (owner_app_ != nullptr) {
    if (!hpa_target.kind().empty()) {
      owner_app_->set_metadata_label(kLogicHpaLabelTargetKind, hpa_target.kind());
    }

    if (!hpa_target.api_version().empty()) {
      owner_app_->set_metadata_label(kLogicHpaLabelTargetApiVersion, hpa_target.api_version());
    }

    if (!hpa_target.name().empty()) {
      owner_app_->set_metadata_label(kLogicHpaLabelTargetName, hpa_target.name());
    }
  }

  // HPA控制器自动标签
  reload_hpa_controller_metadata_filter();

  // Patch补充HPA策略路由配置
  auto& ready_rules = *logic_config::me()
                           ->mutable_server_cfg()
                           ->mutable_logic()
                           ->mutable_hpa()
                           ->mutable_discovery()
                           ->mutable_scaling_ready();
  auto& target_rules = *logic_config::me()
                            ->mutable_server_cfg()
                            ->mutable_logic()
                            ->mutable_hpa()
                            ->mutable_discovery()
                            ->mutable_scaling_target();
  protobuf_copy_message(ready_rules, logic_config::me()->get_logic().discovery_selector());
  protobuf_copy_message(target_rules, ready_rules);

  do {
    auto rule_desc = ready_rules.GetDescriptor();
    auto rule_reflection = ready_rules.GetReflection();
    if (nullptr == rule_desc || nullptr == rule_reflection) {
      break;
    }

    for (int i = 0; i < rule_desc->field_count(); ++i) {
      auto fds = ready_rules.GetDescriptor()->field(i);
      if (fds == nullptr || fds->message_type() != atfw::atapp::protocol::atapp_metadata::descriptor()) {
        continue;
      }

      atfw::atapp::protocol::atapp_metadata* metadata =
          static_cast<atfw::atapp::protocol::atapp_metadata*>(rule_reflection->MutableMessage(&ready_rules, fds));
      if (nullptr == metadata) {
        FWLOGERROR("mutable atapp_metadata failed");
        continue;
      }

      (*metadata->mutable_labels())[kLogicHpaDiscoveryLabelReady] = kLogicHpaValueTrue;
      // 如果要控制服务发现层关联 hpa_target_name, hpa_target_kind, hpa_target_api_version ，必须通过全局配置
    }
  } while (false);

  do {
    auto rule_desc = target_rules.GetDescriptor();
    auto rule_reflection = target_rules.GetReflection();
    if (nullptr == rule_desc || nullptr == rule_reflection) {
      break;
    }

    for (int i = 0; i < rule_desc->field_count(); ++i) {
      auto fds = target_rules.GetDescriptor()->field(i);
      if (fds == nullptr || fds->message_type() != atfw::atapp::protocol::atapp_metadata::descriptor()) {
        continue;
      }

      atfw::atapp::protocol::atapp_metadata* metadata =
          static_cast<atfw::atapp::protocol::atapp_metadata*>(rule_reflection->MutableMessage(&target_rules, fds));
      if (nullptr == metadata) {
        FWLOGERROR("mutable atapp_metadata failed");
        continue;
      }

      (*metadata->mutable_labels())[kLogicHpaDiscoveryLabelTarget] = kLogicHpaValueTrue;
      // 如果要控制服务发现层关联 hpa_target_name, hpa_target_kind, hpa_target_api_version ，必须通过全局配置
    }
  } while (false);

  // 快速设置controller，可能要重建回调
  if (hpa_discovery_data_) {
    hpa_discovery_data_->last_controller_tick_timepoint = 0;
  }
}

SERVER_FRAME_API int logic_hpa_controller::tick() {
  int ret = 0;

  bool need_reload = need_configure_;
  if (need_reload) {
    need_configure_ = false;
    do_reload_hpa_configure();
    do_reload_hpa_metrics();
  }
  // 这一步依赖metrics policy和HPA配置，必须保证 do_reload_hpa_configure() 和 do_reload_hpa_metrics() 先执行
  do_reload_hpa_controller_tick(need_reload);

  if (last_tick_timepoint_ == util::time::time_utility::get_sys_now()) {
    return ret;
  }
  last_tick_timepoint_ = util::time::time_utility::get_sys_now();
  if (hpa_discovery_data_) {
    hpa_discovery_data_accessor::reset_controller_metrics_status(*hpa_discovery_data_);
  }

  // 内置策略Tick
  if (policy_cpu_permillage_) {
    policy_cpu_permillage_->tick();
  }
  if (policy_main_thread_cpu_permillage_) {
    policy_main_thread_cpu_permillage_->tick();
  }
  if (policy_memory_) {
    policy_memory_->tick();
  }
  if (policy_recent_max_task_count_) {
    policy_recent_max_task_count_->tick();
  }
  if (policy_controller_status_) {
    policy_controller_status_->tick();
  }
  for (auto& policy : policy_custom_) {
    if (policy.second) {
      policy.second->tick();
    }
  }

  if (controller_policy_stateful_index_) {
    controller_policy_stateful_index_->tick();
  }
  if (controller_policy_expect_replicas_) {
    controller_policy_expect_replicas_->tick();
  }

  // logic_hpa_discovery 依赖 logic_hpa_policy, 必须在 logic_hpa_policy 后执行tick
  if (hpa_discovery_data_ && hpa_discovery_data_->default_hpa_discovery) {
    hpa_discovery_data_->default_hpa_discovery->tick();
  }
  return ret;
}

SERVER_FRAME_API int logic_hpa_controller::stop(bool prestop, time_t target_label_offset, time_t ready_label_offset) {
  int ret = 0;
  if (hpa_discovery_data_) {
    auto sys_now = util::time::time_utility::get_sys_now();
    hpa_discovery_data_->stoping = true;

    // 启用HPA controller时（适用于需要状态转移），prestop 使用target和ready保护延迟
    // 不启用HPA controller时，仅仅使用服务发现和策略路由，两个标签都要提前缩短
    auto& controller_cfg = logic_config::me()->get_logic().hpa().controller();
    if (prestop && controller_cfg.enable()) {
      time_t expect_hpa_label_target_timepoint =
          sys_now + target_label_offset >= 0 ? target_label_offset
                                             : static_cast<time_t>(controller_cfg.replicate_start_delay().seconds());
      time_t expect_hpa_label_ready_timepoint = expect_hpa_label_target_timepoint + ready_label_offset >= 0
                                                    ? ready_label_offset
                                                    : static_cast<time_t>(controller_cfg.replicate_period().seconds());
      if (hpa_discovery_data_->stoping_hpa_label_target_timepoint <= 0 ||
          hpa_discovery_data_->stoping_hpa_label_target_timepoint > expect_hpa_label_target_timepoint) {
        hpa_discovery_data_->stoping_hpa_label_target_timepoint = expect_hpa_label_target_timepoint;
      }
      if (hpa_discovery_data_->stoping_hpa_label_ready_timepoint <= 0 ||
          hpa_discovery_data_->stoping_hpa_label_ready_timepoint > expect_hpa_label_ready_timepoint) {
        hpa_discovery_data_->stoping_hpa_label_ready_timepoint = expect_hpa_label_ready_timepoint;
      }
    } else {
      if (hpa_discovery_data_->stoping_hpa_label_target_timepoint <= 0 ||
          hpa_discovery_data_->stoping_hpa_label_target_timepoint > sys_now) {
        hpa_discovery_data_->stoping_hpa_label_target_timepoint = sys_now;
      }
      if (hpa_discovery_data_->stoping_hpa_label_ready_timepoint <= 0 ||
          hpa_discovery_data_->stoping_hpa_label_ready_timepoint > sys_now) {
        hpa_discovery_data_->stoping_hpa_label_ready_timepoint = sys_now;
      }
    }
  }

  // 内置策略清理
  if (policy_cpu_permillage_) {
    if (policy_callback_cpu_permillage_.on_cleanup) {
      policy_callback_cpu_permillage_.on_cleanup(*this, policy_cpu_permillage_);
    }
    FWLOGINFO("[HPA]: Controller remove cpu permillage policy {}", policy_cpu_permillage_->get_metrics_name());
    policy_cpu_permillage_->stop();
    if (policy_cpu_permillage_->is_stopped()) {
      policy_cpu_permillage_.reset();
    } else {
      ret = 1;
    }
  }
  remove_on_setup_cpu_permillage_policy();
  remove_on_cleanup_cpu_permillage_policy();

  if (policy_main_thread_cpu_permillage_) {
    if (policy_callback_main_thread_cpu_permillage_.on_cleanup) {
      policy_callback_main_thread_cpu_permillage_.on_cleanup(*this, policy_main_thread_cpu_permillage_);
    }
    FWLOGINFO("[HPA]: Controller remove cpu permillage policy {}",
              policy_main_thread_cpu_permillage_->get_metrics_name());
    policy_main_thread_cpu_permillage_->stop();
    if (policy_main_thread_cpu_permillage_->is_stopped()) {
      policy_main_thread_cpu_permillage_.reset();
    } else {
      ret = 1;
    }
  }
  remove_on_setup_main_thread_cpu_permillage_policy();
  remove_on_cleanup_main_thread_cpu_permillage_policy();

  if (policy_memory_) {
    if (policy_callback_memory_.on_cleanup) {
      policy_callback_memory_.on_cleanup(*this, policy_memory_);
    }
    FWLOGINFO("[HPA]: Controller remove memory policy {}", policy_memory_->get_metrics_name());
    policy_memory_->stop();
    if (policy_memory_->is_stopped()) {
      policy_memory_.reset();
    } else {
      ret = 1;
    }
  }

  remove_on_setup_memory_policy();
  remove_on_cleanup_memory_policy();

  if (policy_recent_max_task_count_) {
    if (policy_callback_recent_max_task_count_.on_cleanup) {
      policy_callback_recent_max_task_count_.on_cleanup(*this, policy_recent_max_task_count_);
    }
    FWLOGINFO("[HPA]: Controller remove recent max task count policy {}",
              policy_recent_max_task_count_->get_metrics_name());
    policy_recent_max_task_count_->stop();
    if (policy_recent_max_task_count_->is_stopped()) {
      policy_recent_max_task_count_.reset();
    } else {
      ret = 1;
    }
  }

  remove_on_setup_recent_max_task_count();
  remove_on_cleanup_recent_max_task_count();

  if (policy_controller_status_) {
    if (policy_callback_controller_status_.on_cleanup) {
      policy_callback_controller_status_.on_cleanup(*this, policy_controller_status_);
    }
    FWLOGINFO("[HPA]: Controller remove controller status policy {}", policy_controller_status_->get_metrics_name());
    policy_controller_status_->stop();
    if (policy_controller_status_->is_stopped()) {
      policy_controller_status_.reset();
    } else {
      ret = 1;
    }
  }

  remove_on_setup_controller_status();
  remove_on_cleanup_controller_status();

  std::unordered_map<std::string, std::shared_ptr<logic_hpa_policy>> custom_policies;
  std::unordered_map<std::string, policy_callback_data> custom_policy_callbacks;
  custom_policies = policy_custom_;
  custom_policy_callbacks.swap(policy_callback_custom_);

  for (auto& policy : custom_policies) {
    if (!policy.second) {
      continue;
    }

    auto iter_callback = custom_policy_callbacks.find(policy.second->get_metrics_name());
    if (iter_callback != custom_policy_callbacks.end()) {
      if (iter_callback->second.on_cleanup) {
        iter_callback->second.on_cleanup(*this, policy.second);
      }
    }

    policy.second->stop();
    if (policy.second->is_stopped()) {
      policy_custom_.erase(policy.first);
    } else {
      ret = 1;
    }
  }

  if (controller_policy_stateful_index_) {
    cleanup_stateful_index_policy();
    if (controller_policy_stateful_index_) {
      ret = 1;
    }
  }

  if (controller_policy_expect_replicas_) {
    cleanup_expect_replicas_policy();
    if (controller_policy_expect_replicas_) {
      ret = 1;
    }
  }

  if (hpa_discovery_data_) {
    if (hpa_discovery_data_->default_hpa_discovery) {
      cleanup_default_hpa_discovery();
    }

    // 状态未迁出，不能直接退出
    if (hpa_discovery_data_->current_hpa_label_ready) {
      ret = 1;
    }
  }

  return ret;
}

SERVER_FRAME_API void logic_hpa_controller::cleanup() {
  if (hpa_discovery_data_) {
    if (!hpa_discovery_data_->stoping) {
      stop(false);
    }
  }
  cleanup_hpa_controller();

  if (hpa_discovery_data_) {
    std::lock_guard<std::mutex> event_loop_lock_guard{hpa_discovery_data_->event_loop_lock};
    hpa_discovery_data_->event_loop_pointer = nullptr;
  }
}

SERVER_FRAME_API void logic_hpa_controller::set_on_setup_cpu_permillage_policy(on_setup_policy_callback fn) {
  policy_callback_cpu_permillage_.on_setup = std::move(fn);
  if (policy_callback_cpu_permillage_.on_setup && policy_cpu_permillage_) {
    policy_callback_cpu_permillage_.on_setup(*this, policy_cpu_permillage_);
  }
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_setup_cpu_permillage_policy() {
  policy_callback_cpu_permillage_.on_setup = on_setup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_cleanup_cpu_permillage_policy(on_cleanup_policy_callback fn) {
  policy_callback_cpu_permillage_.on_cleanup = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_cleanup_cpu_permillage_policy() {
  policy_callback_cpu_permillage_.on_cleanup = on_cleanup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_setup_main_thread_cpu_permillage_policy(
    on_setup_policy_callback fn) {
  policy_callback_main_thread_cpu_permillage_.on_setup = std::move(fn);
  if (policy_callback_main_thread_cpu_permillage_.on_setup && policy_main_thread_cpu_permillage_) {
    policy_callback_main_thread_cpu_permillage_.on_setup(*this, policy_main_thread_cpu_permillage_);
  }
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_setup_main_thread_cpu_permillage_policy() {
  policy_callback_main_thread_cpu_permillage_.on_setup = on_setup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_cleanup_main_thread_cpu_permillage_policy(
    on_cleanup_policy_callback fn) {
  policy_callback_main_thread_cpu_permillage_.on_cleanup = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_cleanup_main_thread_cpu_permillage_policy() {
  policy_callback_main_thread_cpu_permillage_.on_cleanup = on_cleanup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_setup_memory_policy(on_setup_policy_callback fn) {
  policy_callback_memory_.on_setup = std::move(fn);
  if (policy_callback_memory_.on_setup && policy_memory_) {
    policy_callback_memory_.on_setup(*this, policy_memory_);
  }
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_setup_memory_policy() {
  policy_callback_memory_.on_setup = on_setup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_cleanup_memory_policy(on_cleanup_policy_callback fn) {
  policy_callback_memory_.on_cleanup = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_cleanup_memory_policy() {
  policy_callback_memory_.on_cleanup = on_cleanup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_setup_recent_max_task_count(on_setup_policy_callback fn) {
  policy_callback_recent_max_task_count_.on_setup = std::move(fn);
  if (policy_callback_recent_max_task_count_.on_setup && policy_recent_max_task_count_) {
    policy_callback_recent_max_task_count_.on_setup(*this, policy_recent_max_task_count_);
  }
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_setup_recent_max_task_count() {
  policy_callback_recent_max_task_count_.on_setup = on_setup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_cleanup_recent_max_task_count(on_cleanup_policy_callback fn) {
  policy_callback_recent_max_task_count_.on_cleanup = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_cleanup_recent_max_task_count() {
  policy_callback_recent_max_task_count_.on_cleanup = on_cleanup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_setup_controller_status(on_setup_policy_callback fn) {
  policy_callback_controller_status_.on_setup = std::move(fn);
  if (policy_callback_controller_status_.on_setup && policy_controller_status_) {
    policy_callback_controller_status_.on_setup(*this, policy_controller_status_);
  }
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_setup_controller_status() {
  policy_callback_controller_status_.on_setup = on_setup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_cleanup_controller_status(on_cleanup_policy_callback fn) {
  policy_callback_controller_status_.on_cleanup = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_cleanup_controller_status() {
  policy_callback_controller_status_.on_cleanup = on_cleanup_policy_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_setup_custom_policy(const std::string& metrics_name,
                                                                       on_setup_policy_callback fn) {
  if (!fn) {
    remove_on_setup_custom_policy(metrics_name);
    return;
  }

  auto& callbacks = policy_callback_custom_[metrics_name];
  callbacks.on_setup = std::move(fn);

  auto iter = policy_custom_.find(metrics_name);
  if (iter != policy_custom_.end()) {
    if (iter->second) {
      callbacks.on_setup(*this, iter->second);
    }
  }
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_setup_custom_policy(const std::string& metrics_name) {
  auto iter = policy_callback_custom_.find(metrics_name);
  if (iter == policy_callback_custom_.end()) {
    return;
  }
  iter->second.on_setup = on_setup_policy_callback();

  if (!iter->second.on_setup && !iter->second.on_cleanup) {
    policy_callback_custom_.erase(iter);
  }
}

SERVER_FRAME_API void logic_hpa_controller::set_on_cleanup_custom_policy(const std::string& metrics_name,
                                                                         on_cleanup_policy_callback fn) {
  if (!fn) {
    remove_on_cleanup_custom_policy(metrics_name);
    return;
  }

  auto& callbacks = policy_callback_custom_[metrics_name];
  callbacks.on_cleanup = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_cleanup_custom_policy(const std::string& metrics_name) {
  auto iter = policy_callback_custom_.find(metrics_name);
  if (iter == policy_callback_custom_.end()) {
    return;
  }
  iter->second.on_cleanup = on_cleanup_policy_callback();

  if (!iter->second.on_setup && !iter->second.on_cleanup) {
    policy_callback_custom_.erase(iter);
  }
}

SERVER_FRAME_API void logic_hpa_controller::set_on_ready_checking(on_stateful_checking_callback fn) {
  hpa_ready_checking_callback_ = fn;
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_ready_checking() {
  hpa_ready_checking_callback_ = on_stateful_checking_callback();
}

SERVER_FRAME_API void logic_hpa_controller::set_on_stateful_checking(on_stateful_checking_callback fn) {
  hpa_stateful_checking_callback_ = fn;
}

SERVER_FRAME_API void logic_hpa_controller::remove_on_stateful_checking() {
  hpa_stateful_checking_callback_ = on_stateful_checking_callback();
}

SERVER_FRAME_API util::network::http_request::ptr_t logic_hpa_controller::create_http_request(gsl::string_view url) {
  if (!multi_curl_handle_) {
    return nullptr;
  }

  util::network::http_request::ptr_t ret =
      util::network::http_request::create(multi_curl_handle_.get(), static_cast<std::string>(url));
  if (!ret) {
    return ret;
  }

  auto& hpa_metrics_cfg = logic_config::me()->get_logic().hpa().metrics();
  auto& pull_request_cfg = hpa_metrics_cfg.pull_request();
  auto& pull_ssl_cfg = hpa_metrics_cfg.pull_ssl();

  // Settings from configure
  if (pull_request_cfg.has_dns_cache_timeout() && pull_request_cfg.dns_cache_timeout().seconds() > 0) {
    ret->set_opt_long(CURLOPT_DNS_CACHE_TIMEOUT, pull_request_cfg.dns_cache_timeout().seconds() * 1000 +
                                                     pull_request_cfg.dns_cache_timeout().nanos() / 1000000);
  }
  if (pull_request_cfg.has_connect_timeout() && pull_request_cfg.connect_timeout().seconds() > 0) {
    ret->set_opt_connect_timeout(pull_request_cfg.connect_timeout().seconds() * 1000 +
                                 pull_request_cfg.connect_timeout().nanos() / 1000000);
  } else {
    ret->set_opt_connect_timeout(10000);
  }

  ret->set_opt_follow_location(true);
  ret->set_opt_accept_encoding("");
  ret->set_opt_http_content_decoding(true);
  if (pull_request_cfg.has_timeout() && pull_request_cfg.timeout().seconds() > 0) {
    ret->set_opt_timeout(pull_request_cfg.timeout().seconds() * 1000 + pull_request_cfg.timeout().nanos() / 1000000);
  } else {
    ret->set_opt_timeout(10000);
  }
  if (!pull_request_cfg.user_agent().empty()) {
    ret->set_user_agent(pull_request_cfg.user_agent());
  }
#if LIBCURL_VERSION_NUM >= 0x075500
  ret->set_opt_string(CURLOPT_PROTOCOLS_STR, "http,https");
#else
  ret->set_opt_long(CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
  ret->set_opt_no_signal(true);

  if (!pull_request_cfg.proxy().empty()) {
    ret->set_opt_string(CURLOPT_PROXY, pull_request_cfg.proxy().c_str());
    if (0 == UTIL_STRFUNC_STRNCASE_CMP("http:", pull_request_cfg.proxy().c_str(), 5) ||
        0 == UTIL_STRFUNC_STRNCASE_CMP("https:", pull_request_cfg.proxy().c_str(), 6)) {
      ret->set_opt_bool(CURLOPT_HTTPPROXYTUNNEL, true);
    }
  }
#if LIBCURL_VERSION_NUM >= 0x071304
  if (!pull_request_cfg.no_proxy().empty()) {
    ret->set_opt_string(CURLOPT_NOPROXY, pull_request_cfg.no_proxy().c_str());
  }
#endif

#if LIBCURL_VERSION_NUM >= 0x071301
  if (!pull_request_cfg.proxy_user_name().empty()) {
    ret->set_opt_string(CURLOPT_PROXYUSERNAME, pull_request_cfg.proxy_user_name().c_str());
  }

  if (!pull_request_cfg.proxy_user_name().empty()) {
    ret->set_opt_string(CURLOPT_PROXYPASSWORD, pull_request_cfg.proxy_user_name().c_str());
  }
#endif

  // TLS settings
  bool verify_peer = pull_ssl_cfg.verify_peer();

  ret->set_opt_ssl_verify_peer(verify_peer);
  ret->set_opt_long(CURLOPT_SSL_VERIFYHOST, verify_peer ? 2L : 0L);
#if LIBCURL_VERSION_NUM >= 0x072900
  ret->set_opt_bool(CURLOPT_SSL_VERIFYSTATUS, verify_peer);
#endif
#if LIBCURL_VERSION_NUM >= 0x073400
  ret->set_opt_bool(CURLOPT_PROXY_SSL_VERIFYPEER, verify_peer);
  ret->set_opt_long(CURLOPT_PROXY_SSL_VERIFYHOST, verify_peer ? 2L : 0L);
#endif

  if (pull_ssl_version_ != logic_hpa_ssl_version::kNone) {
    switch (pull_ssl_version_) {
#if LIBCURL_VERSION_NUM >= 0x073400
      case logic_hpa_ssl_version::kTlsV13:
        ret->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_3);
        ret->set_opt_long(CURLOPT_PROXY_SSLVERSION, CURL_SSLVERSION_TLSv1_3);
        break;
#endif
#if LIBCURL_VERSION_NUM >= 0x072200
      case logic_hpa_ssl_version::kTlsV12:
        ret->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#  if LIBCURL_VERSION_NUM >= 0x073400
        ret->set_opt_long(CURLOPT_PROXY_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#  endif
        break;
      case logic_hpa_ssl_version::kTlsV11:
        ret->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_1);
#  if LIBCURL_VERSION_NUM >= 0x073400
        ret->set_opt_long(CURLOPT_PROXY_SSLVERSION, CURL_SSLVERSION_TLSv1_1);
#  endif
        break;
      case logic_hpa_ssl_version::kTlsV10:
        ret->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
#  if LIBCURL_VERSION_NUM >= 0x073400
        ret->set_opt_long(CURLOPT_PROXY_SSLVERSION, CURL_SSLVERSION_TLSv1);
#  endif
        break;
#endif
      case logic_hpa_ssl_version::kSsl3:
        ret->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_SSLv3);
        break;
      default:
#if LIBCURL_VERSION_NUM >= 0x072200
        ret->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#  if LIBCURL_VERSION_NUM >= 0x073400
        ret->set_opt_long(CURLOPT_PROXY_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#  endif
#else
        req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_SSLv3);
#endif
        break;
    }
  }
  // client cert and key
#if LIBCURL_VERSION_NUM >= 0x072400
  ret->set_opt_bool(CURLOPT_SSL_ENABLE_ALPN, pull_ssl_cfg.enable_alpn());
#endif
  if (!pull_ssl_cfg.ssl_client_cert().empty()) {
    ret->set_opt_string(CURLOPT_SSLCERT, pull_ssl_cfg.ssl_client_cert().c_str());
  }
#if LIBCURL_VERSION_NUM >= 0x070903
  if (!pull_ssl_cfg.ssl_client_cert().empty()) {
    ret->set_opt_string(CURLOPT_SSLCERTTYPE, pull_ssl_cfg.ssl_client_cert().c_str());
  }
#endif
  if (!pull_ssl_cfg.ssl_client_key().empty()) {
    ret->set_opt_string(CURLOPT_SSLKEY, pull_ssl_cfg.ssl_client_key().c_str());
  }
  if (!pull_ssl_cfg.ssl_client_key_type().empty()) {
    ret->set_opt_string(CURLOPT_SSLKEYTYPE, pull_ssl_cfg.ssl_client_key_type().c_str());
  }
#if LIBCURL_VERSION_NUM >= 0x071004
  if (!pull_ssl_cfg.ssl_client_key_passwd().empty()) {
    ret->set_opt_string(CURLOPT_SSLKEYPASSWD, pull_ssl_cfg.ssl_client_key_passwd().c_str());
  }
#elif LIBCURL_VERSION_NUM >= 0x070902
  if (!pull_ssl_cfg.ssl_client_key_passwd.empty()) {
    ret->set_opt_string(CURLOPT_SSLCERTPASSWD, pull_ssl_cfg.ssl_client_key_passwd().c_str());
  }
#endif
  if (!pull_ssl_cfg.ssl_ca_cert().empty()) {
    ret->set_opt_string(CURLOPT_CAINFO, pull_ssl_cfg.ssl_ca_cert().c_str());
  }

#if LIBCURL_VERSION_NUM >= 0x071504
  if (!pull_ssl_cfg.ssl_client_tlsauth_username().empty()) {
    ret->set_opt_string(CURLOPT_TLSAUTH_TYPE,
                        "SRP");  // @see https://curl.haxx.se/libcurl/c/CURLOPT_TLSAUTH_TYPE.html
    ret->set_opt_string(CURLOPT_TLSAUTH_USERNAME, pull_ssl_cfg.ssl_client_tlsauth_username().c_str());
    ret->set_opt_string(CURLOPT_TLSAUTH_PASSWORD, pull_ssl_cfg.ssl_client_tlsauth_password().c_str());
  }
#endif
  // proxy cert and key
#if LIBCURL_VERSION_NUM >= 0x073400
  if (!pull_ssl_cfg.ssl_proxy_cert().empty()) {
    ret->set_opt_string(CURLOPT_PROXY_SSLCERT, pull_ssl_cfg.ssl_proxy_cert().c_str());
  }

  if (!pull_ssl_cfg.ssl_proxy_cert_type().empty()) {
    ret->set_opt_string(CURLOPT_PROXY_SSLCERTTYPE, pull_ssl_cfg.ssl_proxy_cert_type().c_str());
  }

  if (!pull_ssl_cfg.ssl_proxy_key().empty()) {
    ret->set_opt_string(CURLOPT_PROXY_SSLKEY, pull_ssl_cfg.ssl_proxy_key().c_str());
  }

  if (!pull_ssl_cfg.ssl_proxy_key_type().empty()) {
    ret->set_opt_string(CURLOPT_PROXY_SSLKEYTYPE, pull_ssl_cfg.ssl_proxy_key_type().c_str());
  }

  if (!pull_ssl_cfg.ssl_proxy_key_passwd().empty()) {
    ret->set_opt_string(CURLOPT_PROXY_KEYPASSWD, pull_ssl_cfg.ssl_proxy_key_passwd().c_str());
  }

  if (!pull_ssl_cfg.ssl_proxy_ca_cert().empty()) {
    ret->set_opt_string(CURLOPT_PROXY_CAINFO, pull_ssl_cfg.ssl_proxy_ca_cert().c_str());
  }

  if (!pull_ssl_cfg.ssl_proxy_tlsauth_username().empty()) {
    ret->set_opt_string(CURLOPT_PROXY_TLSAUTH_TYPE,
                        "SRP");  // @see https://curl.haxx.se/libcurl/c/CURLOPT_PROXY_TLSAUTH_TYPE.html
    ret->set_opt_string(CURLOPT_PROXY_TLSAUTH_USERNAME, pull_ssl_cfg.ssl_proxy_tlsauth_username().c_str());
    ret->set_opt_string(CURLOPT_PROXY_TLSAUTH_PASSWORD, pull_ssl_cfg.ssl_proxy_tlsauth_password().c_str());
  }
#endif
  // ssl cipher
  if (!pull_ssl_cfg.ssl_cipher_list().empty()) {
    ret->set_opt_string(CURLOPT_SSL_CIPHER_LIST, pull_ssl_cfg.ssl_cipher_list().c_str());
  }

#if LIBCURL_VERSION_NUM >= 0x073d00
  if (!pull_ssl_cfg.ssl_cipher_list_tls13().empty()) {
    ret->set_opt_string(CURLOPT_TLS13_CIPHERS, pull_ssl_cfg.ssl_cipher_list_tls13().c_str());
  }
#endif

  // Debug settings(show verbose)
  if (pull_request_cfg.debug_mode()) {
    ret->set_on_verbose([](util::network::http_request&, curl_infotype type, char* data, size_t size) {
      const char* verbose_type = "Unknown Action";
      switch (type) {
        case CURLINFO_TEXT:
          verbose_type = "Text";
          break;
        case CURLINFO_HEADER_OUT:
          verbose_type = "Header Send";
          break;
        case CURLINFO_DATA_OUT:
          verbose_type = "Data Send";
          break;
        case CURLINFO_SSL_DATA_OUT:
          verbose_type = "SSL Data Send";
          break;
        case CURLINFO_HEADER_IN:
          verbose_type = "Header Received";
          break;
        case CURLINFO_DATA_IN:
          verbose_type = "Data Received";
          break;
        case CURLINFO_SSL_DATA_IN:
          verbose_type = "SSL Data Received";
          break;
        default: /* in case a new one is introduced to shock us */
          break;
      }

      FWLOGTRACE("[HPA]: curl verbose type: {}\n{}", verbose_type, gsl::string_view{data, size});
      return 0;
    });
  }

  return ret;
}

SERVER_FRAME_API bool logic_hpa_controller::get_discovery_ready_tag() noexcept {
  if (!hpa_discovery_data_) {
    return false;
  }

  return hpa_discovery_data_->current_hpa_label_ready;
}

SERVER_FRAME_API bool logic_hpa_controller::get_discovery_target_tag() noexcept {
  if (!hpa_discovery_data_) {
    return false;
  }

  return hpa_discovery_data_->current_hpa_label_target;
}

SERVER_FRAME_API std::string logic_hpa_controller::make_custom_discovery_path(gsl::string_view name,
                                                                              bool with_target_path) {
  if (hpa_discovery_data_ && with_target_path) {
    if (!hpa_discovery_data_->configure_key.empty() && '/' != *hpa_discovery_data_->configure_key.rbegin()) {
      return util::string::format("{}/{}", hpa_discovery_data_->configure_key, name);
    }
    return util::string::format("{}{}", hpa_discovery_data_->configure_key, name);
  } else {
    return std::string{name};
  }
}

SERVER_FRAME_API std::shared_ptr<logic_hpa_discovery> logic_hpa_controller::create_custom_discovery(
    gsl::string_view name,
    std::function<void(logic_hpa_discovery&, logic_hpa_discovery_setup_policy_accessor&)> setup_callback,
    gsl::string_view domain, bool with_target_path) {
  if (name.empty() || !hpa_discovery_data_) {
    return nullptr;
  }

  if (domain.empty()) {
    domain = logic_hpa_discovery_semantic_conventions::kLogicHpaDiscoveryDomainCustom;
  }

  if (find_custom_discovery(name, domain, with_target_path)) {
    return nullptr;
  }

  // 自定义策略数据的存放Key
  std::string etcd_path = make_custom_discovery_path(name, with_target_path);

  std::shared_ptr<logic_hpa_discovery> ret =
      atfw::memory::stl::make_shared<logic_hpa_discovery>(*this, hpa_discovery_data_->configure_key, domain);
  if (!ret) {
    return nullptr;
  }

  logic_hpa_discovery_setup_policy_accessor accessor;
  auto& storage_data = hpa_discovery_data_->custom_hpa_discovery[ret->get_etcd_path()];
  storage_data = logic_hpa_discovery_with_event{ret, std::move(setup_callback)};
  // reload状态后续会setup，不需要立即setup
  if (storage_data.setup_callback && !need_configure_) {
    storage_data.setup_callback(*ret, accessor);
  }
  return ret;
}

SERVER_FRAME_API std::shared_ptr<logic_hpa_discovery> logic_hpa_controller::find_custom_discovery(
    gsl::string_view name, gsl::string_view domain, bool with_target_path) {
  if (name.empty() || !hpa_discovery_data_) {
    return nullptr;
  }

  if (domain.empty()) {
    domain = logic_hpa_discovery_semantic_conventions::kLogicHpaDiscoveryDomainCustom;
  }

  std::string etcd_path = make_custom_discovery_path(name, with_target_path);
  etcd_path = logic_hpa_discovery::make_path(etcd_path, domain);

  auto iter = hpa_discovery_data_->custom_hpa_discovery.find(etcd_path);
  if (iter == hpa_discovery_data_->custom_hpa_discovery.end()) {
    return nullptr;
  }

  return iter->second.instance;
}

SERVER_FRAME_API bool logic_hpa_controller::remove_custom_discovery(
    const std::shared_ptr<logic_hpa_discovery>& discovery_instance) {
  if (!discovery_instance || !hpa_discovery_data_) {
    return false;
  }

  return hpa_discovery_data_->custom_hpa_discovery.erase(discovery_instance->get_etcd_path()) > 0;
}

void logic_hpa_controller::reload_runtime_stateful_pod_index() {
  if (nullptr == owner_app_ || !hpa_discovery_data_) {
    return;
  }

  // Stateful pod index in K8s start from 0
  int32_t pod_index = owner_app_->get_runtime_stateful_pod_index();
  if (pod_index < 0) {
    pod_index = 0;

    owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelWithPodIndex, kLogicHpaValueFalse);
    hpa_discovery_data_->current_hpa_discovery_with_pod_index = false;
  } else {
    owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelWithPodIndex, kLogicHpaValueTrue);
    hpa_discovery_data_->current_hpa_discovery_with_pod_index = true;
  }

  hpa_discovery_data_->controller_stateful_index.store(pod_index + 1, std::memory_order_release);
}

void logic_hpa_controller::reload_hpa_controller_metadata_filter() {
  if (nullptr == owner_app_ || !hpa_discovery_data_) {
    return;
  }

  // 内置模块拉取指标的筛选规则,和k8s相似的结构
  std::string auto_labels[] = {opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName,
                               opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace,
                               kLogicHpaLabelTargetKind, kLogicHpaLabelTargetApiVersion, kLogicHpaLabelTargetName};

  hpa_discovery_data_->discovery_filter.Clear();
  if (!owner_app_->get_metadata().kind().empty()) {
    hpa_discovery_data_->discovery_filter.set_kind(owner_app_->get_metadata().kind());
  }

  if (!owner_app_->get_metadata().api_version().empty()) {
    hpa_discovery_data_->discovery_filter.set_api_version(owner_app_->get_metadata().api_version());
  }

  if (!owner_app_->get_metadata().group().empty()) {
    hpa_discovery_data_->discovery_filter.set_group(owner_app_->get_metadata().group());
  }

  if (!owner_app_->get_metadata().namespace_name().empty()) {
    hpa_discovery_data_->discovery_filter.set_namespace_name(owner_app_->get_metadata().namespace_name());
  }

  if (!owner_app_->get_metadata().service_subset().empty()) {
    hpa_discovery_data_->discovery_filter.set_service_subset(owner_app_->get_metadata().service_subset());
  }

  auto& labels = owner_app_->get_metadata().labels();
  for (auto& label_key : auto_labels) {
    auto iter_label = owner_app_->get_metadata().labels().find(label_key);
    if (iter_label == labels.end()) {
      continue;
    }

    (*hpa_discovery_data_->discovery_filter.mutable_labels())[label_key] = iter_label->second;
  }

  // HPA控制器的额外标签
  for (auto& label_kv : logic_config::me()->get_logic().hpa().controller().discovery_labels()) {
    (*hpa_discovery_data_->discovery_filter.mutable_labels())[label_kv.first] = label_kv.second;

    owner_app_->set_metadata_label(label_kv.first, label_kv.second);
  }
}

void logic_hpa_controller::do_reload_hpa_metrics() {
  auto hpa_telemetry_group =
      rpc::telemetry::global_service::get_group(rpc::telemetry::semantic_conventions::kGroupNameHpa);
  auto& hpa_configure = logic_config::me()->get_logic().hpa();
  common_attributes_reference_.clear();
  common_attributes_lifetime_.clear();
  common_selectors_.clear();

  if (hpa_telemetry_group && hpa_configure.has_rule() &&
      (!hpa_configure.rule().cpu_permillage().metrics_name().empty() ||
       !hpa_configure.rule().main_thread_cpu_permillage().metrics_name().empty() ||
       !hpa_configure.rule().memory().metrics_name().empty() ||
       !hpa_configure.rule().recent_max_task_count().metrics_name().empty() ||
       !hpa_configure.rule().controller_status().metrics_name().empty() || hpa_configure.rule().custom_size() > 0)) {
    telemetry_group_ = hpa_telemetry_group;
    available_ = true;

    // 默认HTTP请求配置，有些代理层可能和Prometheus gateway不一样
    pull_ssl_version_ = ssl_version_from_name(hpa_configure.metrics().pull_ssl().ssl_min_version());
    if (hpa_configure.metrics().pull_request().method().empty()) {
      pull_http_method_ = util::network::http_request::method_t::EN_MT_GET;
    } else if (UTIL_STRFUNC_STRCASE_CMP(hpa_configure.metrics().pull_request().method().c_str(), "POST")) {
      pull_http_method_ = util::network::http_request::method_t::EN_MT_POST;
    } else if (UTIL_STRFUNC_STRCASE_CMP(hpa_configure.metrics().pull_request().method().c_str(), "PUT")) {
      pull_http_method_ = util::network::http_request::method_t::EN_MT_PUT;
    } else if (UTIL_STRFUNC_STRCASE_CMP(hpa_configure.metrics().pull_request().method().c_str(), "DELETE")) {
      pull_http_method_ = util::network::http_request::method_t::EN_MT_DELETE;
    } else if (UTIL_STRFUNC_STRCASE_CMP(hpa_configure.metrics().pull_request().method().c_str(), "TRACE")) {
      pull_http_method_ = util::network::http_request::method_t::EN_MT_TRACE;
    } else {
      pull_http_method_ = util::network::http_request::method_t::EN_MT_GET;
    }
  } else {
    available_ = false;

    pull_ssl_version_ = logic_hpa_ssl_version::kNone;
    pull_http_method_ = util::network::http_request::method_t::EN_MT_GET;

    // Cleanup policies
    setup_cpu_permillage_policy(hpa_configure, hpa_configure.rule().cpu_permillage());
    setup_main_thread_cpu_permillage_policy(hpa_configure, hpa_configure.rule().main_thread_cpu_permillage());
    setup_memory_policy(hpa_configure, hpa_configure.rule().memory());
    setup_recent_max_task_count_policy(hpa_configure, hpa_configure.rule().recent_max_task_count());
    setup_controller_status_policy(hpa_configure, hpa_configure.rule().controller_status());

    cleanup_custom_policies();

    telemetry_group_.reset();
    return;
  }

  // 忽略标签，某些自定义策略中，可以用来忽略自动生成的target相关的标签以便垮服务类型访问
  std::unordered_set<std::string> common_ignore_selectors;
  common_ignore_selectors.reserve(static_cast<size_t>(hpa_configure.metrics().without_auto_selectors_size()));
  for (auto& ignore_selector : hpa_configure.metrics().without_auto_selectors()) {
    common_ignore_selectors.insert(
        rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(ignore_selector, true));
  }

  // 需要管理属性数据的生命周期和提供容器给otel-cpp传入
  common_selectors_.reserve(static_cast<size_t>(hpa_configure.metrics().selectors_size() + 2));
  size_t common_attributes_cap = static_cast<size_t>(hpa_configure.metrics().labels_size() + 5) +
                                 rpc::telemetry::global_service::get_metrics_labels(hpa_telemetry_group).size();
  common_attributes_lifetime_.reserve(common_attributes_cap);
  common_attributes_reference_.reserve(common_attributes_cap);
  for (auto& kv : rpc::telemetry::global_service::get_metrics_labels(hpa_telemetry_group)) {
    auto& attribute_with_lifetime = common_attributes_lifetime_[kv.first];
    attribute_with_lifetime = rpc::telemetry::opentelemetry_utility::convert_attribute_value_to_string(kv.second);
    common_attributes_reference_[kv.first] = attribute_with_lifetime;
  }

  // 自动注入环境标签、进程信息标签。其中仅部分作为query聚合拉取规则
  do_reload_hpa_metrics_auto_inject_resource(common_ignore_selectors);

  // 自动注入HPA控制器的功能性标签
  do_reload_hpa_metrics_auto_inject_hpa_labels(common_ignore_selectors);

  // 自动注入公共标签
  do_reload_hpa_metrics_auto_inject_common_attributes(common_ignore_selectors);

  // Setup policies
  setup_cpu_permillage_policy(hpa_configure, hpa_configure.rule().cpu_permillage());
  setup_main_thread_cpu_permillage_policy(hpa_configure, hpa_configure.rule().main_thread_cpu_permillage());
  setup_memory_policy(hpa_configure, hpa_configure.rule().memory());
  setup_recent_max_task_count_policy(hpa_configure, hpa_configure.rule().recent_max_task_count());
  setup_controller_status_policy(hpa_configure, hpa_configure.rule().controller_status());

  cleanup_custom_policies();
  for (auto& custom_policy : hpa_configure.rule().custom()) {
    setup_custom_policy(hpa_configure, custom_policy);
  }
}

void logic_hpa_controller::do_reload_hpa_metrics_auto_inject_resource(
    std::unordered_set<std::string>& common_ignore_selectors) {
  if (nullptr == owner_app_) {
    return;
  }

  // 我们要确保经过Prometheus Exporter清洗后仍然没有重复的label，这里得跑和 otel-cpp 一样的清洗逻辑

  // @see https://opentelemetry.io/docs/specs/semconv/resource/deployment-environment/
  std::string deployment_environment_key = rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
      opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName, true);
  if (common_attributes_reference_.end() ==
          common_attributes_reference_.find(
              opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName) &&
      common_attributes_reference_.end() == common_attributes_reference_.find(deployment_environment_key)) {
    auto& label_value =
        common_attributes_lifetime_[opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName];
    label_value = static_cast<std::string>(logic_config::me()->get_deployment_environment_name());
    common_attributes_reference_[opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName] =
        label_value;
  }
  if (common_ignore_selectors.end() == common_ignore_selectors.find(deployment_environment_key) &&
      common_ignore_selectors.end() ==
          common_ignore_selectors.find(opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName)) {
    common_selectors_[deployment_environment_key] =
        static_cast<std::string>(logic_config::me()->get_deployment_environment_name());
  }

  // @see https://opentelemetry.io/docs/specs/semconv/resource/#service
  auto iter_namespace =
      common_attributes_reference_.find(opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace);
  if (common_attributes_reference_.end() == iter_namespace) {
    iter_namespace =
        common_attributes_reference_.find(rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
            opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace, true));
  }
  auto iter_service_name =
      common_attributes_reference_.find(opentelemetry::sdk::resource::SemanticConventions::kServiceName);
  if (common_attributes_reference_.end() == iter_service_name) {
    iter_service_name =
        common_attributes_reference_.find(rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
            opentelemetry::sdk::resource::SemanticConventions::kServiceName, true));
  }
  auto iter_service_instance_id =
      common_attributes_reference_.find(opentelemetry::sdk::resource::SemanticConventions::kServiceInstanceId);
  if (common_attributes_reference_.end() == iter_service_name) {
    iter_service_instance_id =
        common_attributes_reference_.find(rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
            opentelemetry::sdk::resource::SemanticConventions::kServiceInstanceId, true));
  }

  // Target Info转换规则
  std::string job;
  std::string service_namespace;
  std::string service_name;
  std::string service_instance_id;
  if (iter_namespace != common_attributes_reference_.end()) {
    service_namespace =
        rpc::telemetry::opentelemetry_utility::convert_attribute_value_to_string(iter_namespace->second);
  }
  if (iter_service_name != common_attributes_reference_.end()) {
    service_name = rpc::telemetry::opentelemetry_utility::convert_attribute_value_to_string(iter_service_name->second);
  } else if (owner_app_ != nullptr) {
    service_name = owner_app_->get_type_name();
  } else {
    service_name = "UNKNOWN";
  }
  if (iter_service_instance_id != common_attributes_reference_.end()) {
    service_instance_id =
        rpc::telemetry::opentelemetry_utility::convert_attribute_value_to_string(iter_service_instance_id->second);
  }
  //
  if (!service_namespace.empty()) {
    job = service_namespace + "-" + service_name;
  } else {
    job = service_name;
  }

  // Job可能被exporter重置，所以这里不使用job标签作为服务筛选
  // 规范详见:
  // https://opentelemetry.io/docs/specs/otel/compatibility/prometheus_and_openmetrics/#resource-attributes-1
  auto& job_value = common_attributes_lifetime_[kLogicHpaMetricsTargetInfoJob];
  job_value = job;
  common_attributes_reference_[kLogicHpaMetricsTargetInfoJob] = job_value;
  if (!service_namespace.empty()) {
    if (iter_namespace == common_attributes_reference_.end()) {
      auto& label_value =
          common_attributes_lifetime_[opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace];
      label_value = service_namespace;
      common_attributes_reference_[opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace] = label_value;
    }
    if (common_ignore_selectors.end() ==
        common_ignore_selectors.find(opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace)) {
      common_selectors_[rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
          opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace, true)] = service_namespace;
    }
  }

  if (iter_service_name == common_attributes_reference_.end()) {
    auto& label_value = common_attributes_lifetime_[opentelemetry::sdk::resource::SemanticConventions::kServiceName];
    label_value = service_name;
    common_attributes_reference_[opentelemetry::sdk::resource::SemanticConventions::kServiceName] = label_value;
  }
  if (common_ignore_selectors.end() ==
      common_ignore_selectors.find(opentelemetry::sdk::resource::SemanticConventions::kServiceName)) {
    common_selectors_[rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
        opentelemetry::sdk::resource::SemanticConventions::kServiceName, true)] = service_name;
  }

  // instance和service.instance.id不作为默认selector
  if (service_instance_id.empty()) {
    service_instance_id = owner_app_->get_app_name();
  }
  if (iter_service_instance_id == common_attributes_reference_.end()) {
    auto& label_value =
        common_attributes_lifetime_[opentelemetry::sdk::resource::SemanticConventions::kServiceInstanceId];
    label_value = service_instance_id;
    common_attributes_reference_[opentelemetry::sdk::resource::SemanticConventions::kServiceInstanceId] = label_value;
  }
  auto& instance_value = common_attributes_lifetime_[kLogicHpaMetricsTargetInfoInstance];
  instance_value = service_instance_id;
  common_attributes_reference_[kLogicHpaMetricsTargetInfoInstance] = instance_value;
}

void logic_hpa_controller::do_reload_hpa_metrics_auto_inject_hpa_labels(
    std::unordered_set<std::string>& common_ignore_selectors) {
  auto& hpa_configure = logic_config::me()->get_logic().hpa();

  // 自动注入的target相关标签
  if (!hpa_configure.controller().target().kind().empty()) {
    auto& label_value = common_attributes_lifetime_[kLogicHpaLabelTargetKind];
    label_value = hpa_configure.controller().target().kind();
    common_attributes_reference_[kLogicHpaLabelTargetKind] = label_value;
    if (common_ignore_selectors.end() == common_ignore_selectors.find(kLogicHpaLabelTargetKind)) {
      common_selectors_[kLogicHpaLabelTargetKind] = hpa_configure.controller().target().kind();
    }
  }
  if (!hpa_configure.controller().target().name().empty()) {
    auto& label_value = common_attributes_lifetime_[kLogicHpaLabelTargetName];
    label_value = hpa_configure.controller().target().name();
    common_attributes_reference_[kLogicHpaLabelTargetName] = label_value;
    if (common_ignore_selectors.end() == common_ignore_selectors.find(kLogicHpaLabelTargetName)) {
      common_selectors_[kLogicHpaLabelTargetName] = hpa_configure.controller().target().name();
    }
  }
  if (!hpa_configure.controller().target().api_version().empty()) {
    auto& label_value = common_attributes_lifetime_[kLogicHpaLabelTargetApiVersion];
    label_value = hpa_configure.controller().target().api_version();
    common_attributes_reference_[kLogicHpaLabelTargetApiVersion] = label_value;
    if (common_ignore_selectors.end() == common_ignore_selectors.find(kLogicHpaLabelTargetApiVersion)) {
      common_selectors_[kLogicHpaLabelTargetApiVersion] = hpa_configure.controller().target().api_version();
    }
  }
}

void logic_hpa_controller::do_reload_hpa_metrics_auto_inject_common_attributes(
    std::unordered_set<std::string>& common_ignore_selectors) {
  auto& hpa_configure = logic_config::me()->get_logic().hpa();

  // HPA控制器的额外上报标签
  for (auto& kv : hpa_configure.metrics().labels()) {
    auto& label_value = common_attributes_lifetime_[kv.first];
    label_value = kv.second;
    common_attributes_reference_[kv.first] = label_value;
  }

  // HPA控制器的额外聚合拉取标签
  for (auto& kv : hpa_configure.metrics().selectors()) {
    std::string sanitize_key =
        rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(kv.first, true);
    if (common_ignore_selectors.end() != common_ignore_selectors.find(sanitize_key)) {
      continue;
    }

    common_selectors_[sanitize_key] = kv.second;
  }
}

util::nostd::nonnull<std::shared_ptr<logic_hpa_policy>> logic_hpa_controller::internal_create_hpa_policy(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg, int64_t push_interval_seconds) {
  std::shared_ptr<logic_hpa_policy> policy_instance;
  int64_t pull_default_time_range =
      logic_config::me()->get_logic().hpa().metrics().pull_default_time_range_multiplying_factor();

  // 默认4.5倍的推送间隔，考虑 [业务service]->[本地otelcol]->[远程otelcol]->PodMonitor抓取每层都有可能有时间差
  if (pull_default_time_range <= 0) {
    pull_default_time_range = 4500;
  }
  pull_default_time_range = push_interval_seconds * pull_default_time_range / 1000;

  // 默认的聚合规则，走sum
  if (policy_cfg.aggregation() == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_NONE) {
    PROJECT_NAMESPACE_ID::config::logic_hpa_policy patch_policy_cfg = policy_cfg;
    if (push_interval_seconds > 0 && patch_policy_cfg.simple_function_size() == 0) {
      auto simple_function = patch_policy_cfg.add_simple_function();
      simple_function->mutable_last_over_time()->set_seconds(pull_default_time_range);
    }
    if (append_default_aggregation_by(
            *patch_policy_cfg.mutable_aggregation_parameter(), common_attributes_reference_,
            {{rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
                  opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName, true),
              opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName},
             // {kLogicHpaMetricsTargetInfoInstance}, // Prometheus直接计算好，省去二次聚合
             {kLogicHpaMetricsTargetInfoServiceNamespace,
              opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace},
             {kLogicHpaMetricsTargetInfoServiceName,
              opentelemetry::sdk::resource::SemanticConventions::kServiceName}})) {
      patch_policy_cfg.set_aggregation(PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_SUM);
      policy_instance = atfw::memory::stl::make_shared<logic_hpa_policy>(
          *this, telemetry_group_, hpa_cfg, patch_policy_cfg, common_attributes_reference_, common_selectors_);
    } else {
      policy_instance = atfw::memory::stl::make_shared<logic_hpa_policy>(
          *this, telemetry_group_, hpa_cfg, policy_cfg, common_attributes_reference_, common_selectors_);
    }
  } else {
    if (push_interval_seconds > 0 && policy_cfg.simple_function_size() == 0) {
      PROJECT_NAMESPACE_ID::config::logic_hpa_policy patch_policy_cfg = policy_cfg;
      auto simple_function = patch_policy_cfg.add_simple_function();
      simple_function->mutable_last_over_time()->set_seconds(pull_default_time_range);

      policy_instance = atfw::memory::stl::make_shared<logic_hpa_policy>(
          *this, telemetry_group_, hpa_cfg, patch_policy_cfg, common_attributes_reference_, common_selectors_);
    } else {
      policy_instance = atfw::memory::stl::make_shared<logic_hpa_policy>(
          *this, telemetry_group_, hpa_cfg, policy_cfg, common_attributes_reference_, common_selectors_);
    }
  }

  return policy_instance;
}

void logic_hpa_controller::setup_cpu_permillage_policy(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (policy_cpu_permillage_) {
    std::shared_ptr<logic_hpa_policy> policy;
    policy.swap(policy_cpu_permillage_);
    if (policy_callback_cpu_permillage_.on_cleanup) {
      policy_callback_cpu_permillage_.on_cleanup(*this, policy);
    }

    FWLOGINFO("[HPA]: Controller remove cpu permillage policy {}", policy->get_metrics_name());
  }

  if (!available_ || !hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty()) {
    return;
  }

  if (hpa_discovery_data_ && hpa_discovery_data_->stoping) {
    return;
  }

  // 创建内置指标的推送和拉取策略
  int64_t push_interval = 0;
  if (telemetry_group_) {
    auto metrics_configure = rpc::telemetry::global_service::get_metrics_configure(telemetry_group_);
    push_interval = metrics_configure.reader().export_interval().seconds();
  }

  policy_cpu_permillage_ = internal_create_hpa_policy(hpa_cfg, policy_cfg, push_interval);
  policy_cpu_permillage_->set_pull_range_reduce_type(logic_hpa_range_reduce_type::kSum);

  if (!policy_cpu_permillage_) {
    FWLOGERROR("[HPA]: Controller create cpu permillage policy {} failed", policy_cfg.metrics_name());
    return;
  }

  // 设置采集接口
  uv_rusage_t rusage;
  uv_getrusage(&rusage);
  auto shared_record = atfw::memory::stl::make_shared<cpu_permillage_metrics_timeval_record>();
  shared_record->ru_stime = rusage.ru_stime;
  shared_record->ru_utime = rusage.ru_utime;
  shared_record->last_report_value = 0;
  shared_record->previous_report_time = std::chrono::system_clock::now();

  std::weak_ptr<hpa_discovery_data> hpa_discovery_data_weak = hpa_discovery_data_;
  auto update_interval = std::chrono::seconds{push_interval};
  policy_cpu_permillage_->add_observer_int64([shared_record, hpa_discovery_data_weak,
                                              update_interval](logic_hpa_policy& report_policy) -> int64_t {
    std::shared_ptr<hpa_discovery_data> hpa_discovery_data_ptr = hpa_discovery_data_weak.lock();
    if (!hpa_discovery_data_ptr) {
      return logic_hpa_observable_value::kInt64NaN;
    }

    // 短期多源抓取，直接返回缓存
    auto now = std::chrono::system_clock::now();
    int64_t base = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - shared_record->previous_report_time).count());
    if (now - shared_record->previous_report_time + std::chrono::microseconds{kIgnoreInaccuracyBoundMicroseconds} <
        update_interval) {
      return shared_record->last_report_value;
    } else if (base <= kIgnoreInaccuracyBoundMicroseconds) {
      return shared_record->last_report_value;
    }

    uv_rusage_t update_rusage;
    uv_getrusage(&update_rusage);
    int64_t offset_us = update_rusage.ru_stime.tv_sec + update_rusage.ru_utime.tv_sec - shared_record->ru_stime.tv_sec -
                        shared_record->ru_utime.tv_sec;
    offset_us *= 1000000;
    offset_us += update_rusage.ru_stime.tv_usec + update_rusage.ru_utime.tv_usec - shared_record->ru_stime.tv_usec -
                 shared_record->ru_utime.tv_usec;
    shared_record->last_report_value = static_cast<int64_t>(offset_us) * 1000 / base;
    shared_record->ru_stime = update_rusage.ru_stime;
    shared_record->ru_utime = update_rusage.ru_utime;
    shared_record->previous_report_time = now;

    // 这里有误差没关系，只是为了消除 tsan
    int64_t debug_cpu_permillage_offset = 0;
    if (now < std::chrono::system_clock::from_time_t(
                  hpa_discovery_data_ptr->controller_debug_cpu_permillage_end_time.load(std::memory_order_relaxed))) {
      debug_cpu_permillage_offset =
          hpa_discovery_data_ptr->controller_debug_cpu_permillage_offset.load(std::memory_order_relaxed);
    }

    if (debug_cpu_permillage_offset > 0) {
      shared_record->last_report_value += debug_cpu_permillage_offset;
      FWLOGDEBUG(
          "[HPA]: Controller report cpu permillage policy {}, offset: {}, base: {}.ru_stime: {},{}, ru_utime: "
          "{},{}, value: {}({:+})",
          report_policy.get_metrics_name(), offset_us, base, update_rusage.ru_stime.tv_sec,
          update_rusage.ru_stime.tv_usec, update_rusage.ru_utime.tv_sec, update_rusage.ru_utime.tv_usec,
          shared_record->last_report_value, debug_cpu_permillage_offset);
    } else {
      FWLOGDEBUG(
          "[HPA]: Controller report cpu permillage policy {}, offset: {}, base: {}.ru_stime: {},{}, ru_utime: "
          "{},{}, value: {}",
          report_policy.get_metrics_name(), offset_us, base, update_rusage.ru_stime.tv_sec,
          update_rusage.ru_stime.tv_usec, update_rusage.ru_utime.tv_sec, update_rusage.ru_utime.tv_usec,
          shared_record->last_report_value);
    }
    return shared_record->last_report_value;
  });

  if (policy_callback_cpu_permillage_.on_setup) {
    policy_callback_cpu_permillage_.on_setup(*this, policy_cpu_permillage_);
  }

  // 聚合拉取回调
  policy_cpu_permillage_->add_event_on_pull_instant(
      [](logic_hpa_policy& policy, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> records) {
        if (records.size() == 1 && *records.begin()) {
          FWLOGDEBUG("[HPA]: Policy {} got record: timepoint: {}, {}", policy.get_metrics_name(),
                     std::chrono::system_clock::to_time_t((*records.begin())->get_time_point()),
                     (*records.begin())->get_value_as_int64());
        } else {
          FWLOGDEBUG("[HPA]: Policy {} got {} records", policy.get_metrics_name(), records.size());
          int32_t index = 0;
          for (auto& record : records) {
            ++index;
            FWLOGDEBUG("\tRecord {}/{}: timepoint: {}, {}", index, records.size(),
                       std::chrono::system_clock::to_time_t(record->get_time_point()), record->get_value_as_int64());
          }
        }
      },
      logic_hpa_event_active_type::kWatchOnly);

  FWLOGINFO("[HPA]: Controller create cpu permillage policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::setup_main_thread_cpu_permillage_policy(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (policy_main_thread_cpu_permillage_) {
    std::shared_ptr<logic_hpa_policy> policy;
    policy.swap(policy_main_thread_cpu_permillage_);
    if (policy_callback_main_thread_cpu_permillage_.on_cleanup) {
      policy_callback_main_thread_cpu_permillage_.on_cleanup(*this, policy);
    }

    FWLOGINFO("[HPA]: Controller remove cpu permillage of main thread policy {}", policy->get_metrics_name());
  }

  if (!available_ || !hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty()) {
    return;
  }

  if (hpa_discovery_data_ && hpa_discovery_data_->stoping) {
    return;
  }

  // 创建内置指标的推送和拉取策略
  int64_t push_interval = 0;
  if (telemetry_group_) {
    auto metrics_configure = rpc::telemetry::global_service::get_metrics_configure(telemetry_group_);
    push_interval = metrics_configure.reader().export_interval().seconds();
  }

  policy_main_thread_cpu_permillage_ = internal_create_hpa_policy(hpa_cfg, policy_cfg, push_interval);
  policy_main_thread_cpu_permillage_->set_pull_range_reduce_type(logic_hpa_range_reduce_type::kSum);

  if (!policy_main_thread_cpu_permillage_) {
    FWLOGERROR("[HPA]: Controller create cpu permillage of main thread policy {} failed", policy_cfg.metrics_name());
    return;
  }

  // 设置采集接口
  auto shared_record = atfw::memory::stl::make_shared<cpu_permillage_metrics_hrtime_record>();
  shared_record->idle_time = 0;
  shared_record->previous_report_time = 0;
  if (get_app() != nullptr) {
    shared_record->idle_time = uv_metrics_idle_time(get_app()->get_evloop());
    shared_record->previous_report_time = uv_hrtime();
  }
  shared_record->last_report_value = 0;

  std::weak_ptr<hpa_discovery_data> hpa_discovery_data_weak = hpa_discovery_data_;
  auto update_interval = std::chrono::seconds{push_interval};
  policy_main_thread_cpu_permillage_->add_observer_int64([shared_record, hpa_discovery_data_weak,
                                                          update_interval](logic_hpa_policy& report_policy) -> int64_t {
    std::shared_ptr<hpa_discovery_data> hpa_discovery_data_ptr = hpa_discovery_data_weak.lock();
    if (!hpa_discovery_data_ptr) {
      return logic_hpa_observable_value::kInt64NaN;
    }

    uint64_t now_hr = uv_hrtime();
    // 短期多源抓取，直接返回缓存
    if (std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(now_hr) -
                                 static_cast<std::chrono::nanoseconds::rep>(shared_record->previous_report_time)} +
            std::chrono::nanoseconds{kIgnoreInaccuracyBoundHrTime} <
        update_interval) {
      return shared_record->last_report_value;
    }

    uint64_t idle_time = 0;
    {
      std::lock_guard<std::mutex> event_loop_lock_guard{hpa_discovery_data_ptr->event_loop_lock};
      if (hpa_discovery_data_ptr->event_loop_pointer == nullptr) {
        return logic_hpa_observable_value::kInt64NaN;
      }

      idle_time = uv_metrics_idle_time(hpa_discovery_data_ptr->event_loop_pointer);
    }

    if (idle_time < shared_record->idle_time || now_hr <= shared_record->previous_report_time ||
        shared_record->idle_time <= 0 || shared_record->previous_report_time <= 0) {
      FWLOGINFO(
          "[HPA]: Controller ignore cpu permillage of main thread policy {} because data rotate or initialize(idle "
          "time: {} -> {}, report_time: {} -> {})",
          report_policy.get_metrics_name(), shared_record->idle_time, idle_time, shared_record->previous_report_time,
          now_hr);
      shared_record->idle_time = idle_time;
      shared_record->previous_report_time = now_hr;
      return logic_hpa_observable_value::kInt64NaN;
    }
    if (now_hr - shared_record->previous_report_time < kIgnoreInaccuracyBoundHrTime) {
      return shared_record->last_report_value;
    }

    uint64_t base = now_hr - shared_record->previous_report_time;
    uint64_t offset = idle_time - shared_record->idle_time;
    shared_record->last_report_value = static_cast<int64_t>(((base - offset) * 1000) / base);
    shared_record->idle_time = idle_time;
    shared_record->previous_report_time = now_hr;

    int64_t debug_cpu_permillage_offset = 0;
    if (std::chrono::system_clock::now() <
        std::chrono::system_clock::from_time_t(
            hpa_discovery_data_ptr->controller_debug_cpu_permillage_end_time.load(std::memory_order_relaxed))) {
      debug_cpu_permillage_offset =
          hpa_discovery_data_ptr->controller_debug_cpu_permillage_offset.load(std::memory_order_relaxed);
    }

    if (debug_cpu_permillage_offset > 0) {
      shared_record->last_report_value += debug_cpu_permillage_offset;
      FWLOGDEBUG(
          "[HPA]: Controller report cpu permillage of main thread policy {}, offset: {}, base: {}, value: {}({:+})",
          report_policy.get_metrics_name(), offset, base, shared_record->last_report_value,
          debug_cpu_permillage_offset);
    } else {
      FWLOGDEBUG("[HPA]: Controller report cpu permillage of main thread policy {}, offset: {}, base: {}, value: {}",
                 report_policy.get_metrics_name(), offset, base, shared_record->last_report_value);
    }
    return shared_record->last_report_value;
  });

  if (policy_callback_main_thread_cpu_permillage_.on_setup) {
    policy_callback_main_thread_cpu_permillage_.on_setup(*this, policy_main_thread_cpu_permillage_);
  }

  // 聚合拉取回调
  policy_main_thread_cpu_permillage_->add_event_on_pull_instant(
      [](logic_hpa_policy& policy, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> records) {
        if (records.size() == 1 && *records.begin()) {
          FWLOGDEBUG("[HPA]: Policy {} got record: timepoint: {}, {}", policy.get_metrics_name(),
                     std::chrono::system_clock::to_time_t((*records.begin())->get_time_point()),
                     (*records.begin())->get_value_as_int64());
        } else {
          FWLOGDEBUG("[HPA]: Policy {} got {} records", policy.get_metrics_name(), records.size());
          int32_t index = 0;
          for (auto& record : records) {
            ++index;
            FWLOGDEBUG("\tRecord {}/{}: timepoint: {}, {}", index, records.size(),
                       std::chrono::system_clock::to_time_t(record->get_time_point()), record->get_value_as_int64());
          }
        }
      },
      logic_hpa_event_active_type::kWatchOnly);

  FWLOGINFO("[HPA]: Controller create cpu permillage of main thread policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::setup_memory_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                               const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (policy_memory_) {
    std::shared_ptr<logic_hpa_policy> policy;
    policy.swap(policy_memory_);
    if (policy_callback_memory_.on_cleanup) {
      policy_callback_memory_.on_cleanup(*this, policy);
    }
    FWLOGINFO("[HPA]: Controller remove memory policy {}", policy->get_metrics_name());
  }

  if (!available_ || !hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty()) {
    return;
  }

  if (hpa_discovery_data_ && hpa_discovery_data_->stoping) {
    return;
  }

  // 创建内置指标的推送和拉取策略
  int64_t push_interval = 0;
  if (telemetry_group_) {
    auto metrics_configure = rpc::telemetry::global_service::get_metrics_configure(telemetry_group_);
    push_interval = metrics_configure.reader().export_interval().seconds();
  }

  policy_memory_ = internal_create_hpa_policy(hpa_cfg, policy_cfg, push_interval);
  policy_memory_->set_pull_range_reduce_type(logic_hpa_range_reduce_type::kSum);

  if (!policy_memory_) {
    FWLOGERROR("[HPA]: Controller create memory policy {} failed", policy_cfg.metrics_name());
    return;
  }

  // 设置采集接口
  auto shared_record = std::make_shared<simple_metrics_int64_record>();
  shared_record->last_report_value = logic_hpa_observable_value::kInt64NaN;
  shared_record->previous_report_time = util::time::time_utility::sys_now();
  auto update_interval = std::chrono::seconds{push_interval};
  policy_memory_->add_observer_int64([shared_record, update_interval](logic_hpa_policy& report_policy) -> int64_t {
    auto now = util::time::time_utility::sys_now();
    // 短期多源抓取，直接返回缓存
    if (now - shared_record->previous_report_time + std::chrono::microseconds{kIgnoreInaccuracyBoundMicroseconds} <
        update_interval) {
      return shared_record->last_report_value;
    } else if (now - shared_record->previous_report_time <
               std::chrono::microseconds{kIgnoreInaccuracyBoundMicroseconds}) {
      return shared_record->last_report_value;
    }
    shared_record->previous_report_time = now;

    size_t memory_rss = 0;
    uv_resident_set_memory(&memory_rss);
    FWLOGDEBUG("[HPA]: Controller report memory policy {}, total memory {}bytes", report_policy.get_metrics_name(),
               memory_rss);

    shared_record->last_report_value = static_cast<int64_t>(memory_rss);
    return shared_record->last_report_value;
  });

  if (policy_callback_memory_.on_setup) {
    policy_callback_memory_.on_setup(*this, policy_memory_);
  }

  // 聚合拉取回调
  policy_memory_->add_event_on_pull_instant(
      [](logic_hpa_policy& policy, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> records) {
        if (records.size() == 1 && *records.begin()) {
          FWLOGDEBUG("[HPA]: Policy {} got timepoint: {}, {}", policy.get_metrics_name(),
                     std::chrono::system_clock::to_time_t((*records.begin())->get_time_point()),
                     (*records.begin())->get_value_as_int64());
        } else {
          FWLOGDEBUG("[HPA]: Policy {} got {} records", policy.get_metrics_name(), records.size());
          int32_t index = 0;
          for (auto& record : records) {
            ++index;
            FWLOGDEBUG("\tRecord {}/{}: timepoint: {}, {}", index, records.size(),
                       std::chrono::system_clock::to_time_t(record->get_time_point()), record->get_value_as_int64());
          }
        }
      },
      logic_hpa_event_active_type::kWatchOnly);
  FWLOGINFO("[HPA]: Controller create memory policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::setup_recent_max_task_count_policy(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (policy_recent_max_task_count_) {
    std::shared_ptr<logic_hpa_policy> policy;
    policy.swap(policy_recent_max_task_count_);
    if (policy_callback_recent_max_task_count_.on_cleanup) {
      policy_callback_recent_max_task_count_.on_cleanup(*this, policy);
    }
    FWLOGINFO("[HPA]: Controller remove recent max task count policy {}", policy->get_metrics_name());
  }

  if (!available_ || !hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty()) {
    return;
  }

  if (hpa_discovery_data_ && hpa_discovery_data_->stoping) {
    return;
  }

  // 创建内置指标的推送和拉取策略
  int64_t push_interval = 0;
  if (telemetry_group_) {
    auto metrics_configure = rpc::telemetry::global_service::get_metrics_configure(telemetry_group_);
    push_interval = metrics_configure.reader().export_interval().seconds();
  }

  policy_recent_max_task_count_ = internal_create_hpa_policy(hpa_cfg, policy_cfg, push_interval);
  policy_recent_max_task_count_->set_pull_range_reduce_type(logic_hpa_range_reduce_type::kSum);

  if (!policy_recent_max_task_count_) {
    FWLOGERROR("[HPA]: Controller create recent max task count policy {} failed", policy_cfg.metrics_name());
    return;
  }

  // 设置采集接口
  auto shared_record = std::make_shared<simple_metrics_int64_record>();
  shared_record->last_report_value = logic_hpa_observable_value::kInt64NaN;
  shared_record->previous_report_time = util::time::time_utility::sys_now();
  policy_recent_max_task_count_->add_observer_int64([shared_record](logic_hpa_policy& report_policy) -> int64_t {
    auto now = util::time::time_utility::sys_now();
    shared_record->previous_report_time = now;

    auto max_task_size = task_manager::get_metrics_task_recently_max_size();
    FWLOGDEBUG("[HPA]: Controller report recent max task count policy {}, total max task count {}",
               report_policy.get_metrics_name(), max_task_size);

    shared_record->last_report_value = static_cast<int64_t>(max_task_size);
    return shared_record->last_report_value;
  });

  if (policy_callback_recent_max_task_count_.on_setup) {
    policy_callback_recent_max_task_count_.on_setup(*this, policy_recent_max_task_count_);
  }

  // 聚合拉取回调
  policy_recent_max_task_count_->add_event_on_pull_instant(
      [](logic_hpa_policy& policy, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> records) {
        if (records.size() == 1 && *records.begin()) {
          FWLOGDEBUG("[HPA]: Policy {} got timepoint: {}, {}", policy.get_metrics_name(),
                     std::chrono::system_clock::to_time_t((*records.begin())->get_time_point()),
                     (*records.begin())->get_value_as_int64());
        } else {
          FWLOGDEBUG("[HPA]: Policy {} got {} records", policy.get_metrics_name(), records.size());
          int32_t index = 0;
          for (auto& record : records) {
            ++index;
            FWLOGDEBUG("\tRecord {}/{}: timepoint: {}, {}", index, records.size(),
                       std::chrono::system_clock::to_time_t(record->get_time_point()), record->get_value_as_int64());
          }
        }
      },
      logic_hpa_event_active_type::kWatchOnly);
  FWLOGINFO("[HPA]: Controller create recent max task count policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::setup_controller_status_policy(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (policy_controller_status_) {
    std::shared_ptr<logic_hpa_policy> policy;
    policy.swap(policy_controller_status_);
    if (policy_callback_controller_status_.on_cleanup) {
      policy_callback_controller_status_.on_cleanup(*this, policy);
    }
    FWLOGINFO("[HPA]: Controller remove controller status policy {}", policy->get_metrics_name());
  }

  if (!available_ || !hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty()) {
    return;
  }

  if (hpa_discovery_data_ && hpa_discovery_data_->stoping) {
    return;
  }

  // 创建内置指标的推送和拉取策略
  if (policy_cfg.aggregation() == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_NONE) {
    policy_controller_status_ = atfw::memory::stl::make_shared<logic_hpa_policy>(
        *this, telemetry_group_, hpa_cfg, policy_cfg, common_attributes_reference_, common_selectors_);
  } else {
    policy_controller_status_ = atfw::memory::stl::make_shared<logic_hpa_policy>(
        *this, telemetry_group_, hpa_cfg, policy_cfg, common_attributes_reference_, common_selectors_);
  }
  policy_controller_status_->set_pull_range_reduce_type(logic_hpa_range_reduce_type::kSum);

  if (!policy_controller_status_) {
    FWLOGERROR("[HPA]: Controller create controller status policy {} failed", policy_cfg.metrics_name());
    return;
  }

  // 设置采集接口
  auto shared_record = std::make_shared<simple_metrics_int64_record>();
  shared_record->last_report_value = logic_hpa_observable_value::kInt64NaN;
  shared_record->previous_report_time = util::time::time_utility::sys_now();
  std::weak_ptr<hpa_discovery_data> hpa_discovery_data_weak = hpa_discovery_data_;
  policy_controller_status_->add_observer_int64(
      [shared_record, hpa_discovery_data_weak](logic_hpa_policy& report_policy) -> int64_t {
        auto now = util::time::time_utility::sys_now();
        shared_record->previous_report_time = now;

        std::shared_ptr<hpa_discovery_data> hpa_discovery_data_ptr = hpa_discovery_data_weak.lock();
        if (!hpa_discovery_data_ptr) {
          return logic_hpa_observable_value::kInt64NaN;
        }

        int64_t current_status = hpa_discovery_data_ptr->controller_metrics_status.load(std::memory_order_relaxed);
        FWLOGDEBUG("[HPA]: Controller report controller status policy {}, status: {}", report_policy.get_metrics_name(),
                   current_status);
        shared_record->last_report_value = current_status;
        return current_status;
      });

  if (policy_callback_controller_status_.on_setup) {
    policy_callback_controller_status_.on_setup(*this, policy_controller_status_);
  }

  // 聚合拉取回调
  policy_controller_status_->add_event_on_pull_instant(
      [](logic_hpa_policy& policy, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> records) {
        if (records.size() == 1 && *records.begin()) {
          FWLOGDEBUG("[HPA]: Policy {} got timepoint: {}, {}", policy.get_metrics_name(),
                     std::chrono::system_clock::to_time_t((*records.begin())->get_time_point()),
                     (*records.begin())->get_value_as_int64());
        } else {
          FWLOGDEBUG("[HPA]: Policy {} got {} records", policy.get_metrics_name(), records.size());
          int32_t index = 0;
          for (auto& record : records) {
            ++index;
            FWLOGDEBUG("\tRecord {}/{}: timepoint: {}, {}", index, records.size(),
                       std::chrono::system_clock::to_time_t(record->get_time_point()), record->get_value_as_int64());
          }
        }
      },
      logic_hpa_event_active_type::kWatchOnly);
  FWLOGINFO("[HPA]: Controller create controller status policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::cleanup_custom_policies() {
  std::unordered_map<std::string, std::shared_ptr<logic_hpa_policy>> policies;
  policies.swap(policy_custom_);

  for (auto& policy : policies) {
    if (!policy.second) {
      continue;
    }

    auto iter_callback = policy_callback_custom_.find(policy.second->get_metrics_name());
    if (iter_callback != policy_callback_custom_.end()) {
      if (iter_callback->second.on_cleanup) {
        iter_callback->second.on_cleanup(*this, policy.second);
      }
    }

    FWLOGINFO("[HPA]: Controller remove custom policy {}", policy.second->get_metrics_name());
  }
}

void logic_hpa_controller::setup_custom_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                               const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (!available_ || !hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty()) {
    return;
  }

  if (hpa_discovery_data_ && hpa_discovery_data_->stoping) {
    return;
  }

  std::shared_ptr<logic_hpa_policy> policy = atfw::memory::stl::make_shared<logic_hpa_policy>(
      *this, telemetry_group_, hpa_cfg, policy_cfg, common_attributes_reference_, common_selectors_);
  if (!policy) {
    FWLOGERROR("[HPA]: Controller create custom policy {} failed", policy_cfg.metrics_name());
    return;
  }

  auto iter_callback = policy_callback_custom_.find(policy_cfg.metrics_name());
  if (policy && iter_callback != policy_callback_custom_.end()) {
    if (iter_callback->second.on_setup) {
      iter_callback->second.on_setup(*this, policy);
    }
  }

  policy_custom_[policy->get_metrics_name()] = policy;
  FWLOGINFO("[HPA]: Controller create custom policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::setup_stateful_index_policy(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (controller_policy_stateful_index_) {
    std::shared_ptr<logic_hpa_policy> policy;
    policy.swap(controller_policy_stateful_index_);

    FWLOGINFO("[HPA]: Controller remove stateful index policy {}", policy->get_metrics_name());
  }

  if (!hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty() || !hpa_discovery_data_) {
    return;
  }
  if (hpa_discovery_data_->stoping) {
    return;
  }

  controller_policy_stateful_index_ = atfw::memory::stl::make_shared<logic_hpa_policy>(
      *this, telemetry_group_, hpa_cfg, policy_cfg, common_attributes_reference_, common_selectors_);

  if (!controller_policy_stateful_index_) {
    FWLOGERROR("[HPA]: Controller create stateful index policy {} failed", policy_cfg.metrics_name());
    return;
  }

  // 设置本节点的状态Index上报策略，ready时上报Pod Index，否则上报0
  auto shared_record = std::make_shared<simple_metrics_int64_record>();
  shared_record->last_report_value = logic_hpa_observable_value::kInt64NaN;
  shared_record->previous_report_time = util::time::time_utility::sys_now();
  std::weak_ptr<hpa_discovery_data> hpa_discovery_data_weak = hpa_discovery_data_;
  controller_policy_stateful_index_->add_observer_int64([shared_record, hpa_discovery_data_weak](
                                                            logic_hpa_policy& report_policy) -> int64_t {
    auto now = util::time::time_utility::sys_now();
    shared_record->previous_report_time = now;

    std::shared_ptr<hpa_discovery_data> hpa_discovery_data_ptr = hpa_discovery_data_weak.lock();
    if (!hpa_discovery_data_ptr) {
      return logic_hpa_observable_value::kInt64NaN;
    }

    int64_t value = 0;
    if (hpa_discovery_data_ptr->report_hpa_label_ready.load(std::memory_order_relaxed)) {
      value = hpa_discovery_data_ptr->controller_stateful_index.load(std::memory_order_acquire);
    }

    FWLOGDEBUG("[HPA]: Controller report stateful index policy {}, value: {}", report_policy.get_metrics_name(), value);
    shared_record->last_report_value = value;
    return value;
  });

  FWLOGINFO("[HPA]: Controller create stateful index policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::cleanup_stateful_index_policy() {
  if (!controller_policy_stateful_index_) {
    return;
  }

  if (!controller_policy_stateful_index_->is_stoping()) {
    FWLOGINFO("[HPA]: Controller remove stateful index policy {}",
              controller_policy_stateful_index_->get_metrics_name());
  }
  controller_policy_stateful_index_->stop();
  if (controller_policy_stateful_index_->is_stopped()) {
    controller_policy_stateful_index_.reset();
  }
}

void logic_hpa_controller::setup_expect_replicas_policy(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (controller_policy_expect_replicas_) {
    std::shared_ptr<logic_hpa_policy> policy;
    policy.swap(controller_policy_expect_replicas_);

    FWLOGINFO("[HPA]: Controller remove expect replicas policy {}", policy->get_metrics_name());
  }

  if (!hpa_cfg.metrics().enable() || policy_cfg.metrics_name().empty() || !hpa_discovery_data_) {
    return;
  }
  if (hpa_discovery_data_->stoping) {
    return;
  }

  controller_policy_expect_replicas_ = atfw::memory::stl::make_shared<logic_hpa_policy>(
      *this, telemetry_group_, hpa_cfg, policy_cfg, common_attributes_reference_, common_selectors_);

  if (!controller_policy_expect_replicas_) {
    FWLOGERROR("[HPA]: Controller create expect replicas policy {} failed", policy_cfg.metrics_name());
    return;
  }

  // 创建预期节点数的上报策略，主控节点设置
  // 因为otel某些代理组件如果设置为累计值模式不会删除series，所以只要上报过数据就要保持上报
  // 我们在HPA设置里使用max(指标),所以这里不会影响
  auto shared_record = std::make_shared<simple_metrics_int64_record>();
  shared_record->last_report_value = logic_hpa_observable_value::kInt64NaN;
  shared_record->previous_report_time = util::time::time_utility::sys_now();
  std::weak_ptr<hpa_discovery_data> hpa_discovery_data_weak = hpa_discovery_data_;
  controller_policy_expect_replicas_->add_observer_int64(
      [shared_record, hpa_discovery_data_weak](logic_hpa_policy& report_policy) -> int64_t {
        auto now = util::time::time_utility::sys_now();
        shared_record->previous_report_time = now;

        std::shared_ptr<hpa_discovery_data> hpa_discovery_data_ptr = hpa_discovery_data_weak.lock();
        if (!hpa_discovery_data_ptr) {
          return logic_hpa_observable_value::kInt64NaN;
        }

        // Change metrics_name_expect_replicas after expect_scaling_timepoint
        if (util::time::time_utility::get_sys_now() >
            hpa_discovery_data_ptr->main_controller_expect_scaling_timepoint.load(std::memory_order_acquire)) {
          shared_record->last_report_value =
              hpa_discovery_data_ptr->main_controller_expect_replicas.load(std::memory_order_acquire);

          FWLOGDEBUG("[HPA]: Controller report expect replicas policy {}, expect: {}", report_policy.get_metrics_name(),
                     shared_record->last_report_value);
        } else {
          shared_record->last_report_value =
              hpa_discovery_data_ptr->main_controller_current_replicas.load(std::memory_order_acquire);
          FWLOGDEBUG("[HPA]: Controller report expect replicas policy {}, current: {}",
                     report_policy.get_metrics_name(), shared_record->last_report_value);
        }

        return shared_record->last_report_value;
      });

  FWLOGINFO("[HPA]: Controller create replicas policy {}", policy_cfg.metrics_name());
}

void logic_hpa_controller::cleanup_expect_replicas_policy() {
  if (!controller_policy_expect_replicas_) {
    return;
  }

  if (!controller_policy_expect_replicas_->is_stoping()) {
    FWLOGINFO("[HPA]: Controller remove expect replicas policy {}",
              controller_policy_expect_replicas_->get_metrics_name());
  }
  controller_policy_expect_replicas_->stop();
  if (controller_policy_expect_replicas_->is_stopped()) {
    controller_policy_expect_replicas_.reset();
  }
}

void logic_hpa_controller::setup_default_hpa_discovery(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg) {
  if (!hpa_discovery_data_) {
    return;
  }

  if (hpa_discovery_data_->configure_key.empty() || !hpa_cfg.controller().enable() || hpa_discovery_data_->stoping) {
    return;
  }

  if (hpa_discovery_data_->default_hpa_discovery) {
    // 测试是否有变化，无变化则重置policy即可
    std::string etcd_path = logic_hpa_discovery::make_path(
        hpa_discovery_data_->configure_key, logic_hpa_discovery_semantic_conventions::kLogicHpaDiscoveryDomainDefault);
    if (etcd_path != hpa_discovery_data_->default_hpa_discovery->get_etcd_path()) {
      cleanup_default_hpa_discovery();
      return;
    }
    logic_hpa_discovery_setup_policy_accessor accessor;
    hpa_discovery_data_->default_hpa_discovery->reset_policy(accessor);
  } else {
    hpa_discovery_data_->default_hpa_discovery = atfw::memory::stl::make_shared<logic_hpa_discovery>(
        *this, hpa_discovery_data_->configure_key,
        logic_hpa_discovery_semantic_conventions::kLogicHpaDiscoveryDomainDefault);
    if (!hpa_discovery_data_->default_hpa_discovery) {
      FWLOGERROR("Create default_hpa_discovery {} failed", hpa_discovery_data_->configure_key);
      return;
    }

    // 指定Key watch，不用监听目录
    hpa_discovery_data_->default_hpa_discovery->watch(logic_hpa_discovery_watch_mode::kExactly);

    // 本地数据更新回调
    hpa_discovery_data_->default_hpa_discovery->add_event_on_changed([](logic_hpa_discovery& hpa_discovery,
                                                                        const logic_hpa_discovery::data_header& header,
                                                                        const std::string& value) {
      if (value.empty()) {
        return;
      }

      if (!hpa_discovery.get_controller().hpa_discovery_data_) {
        return;
      }

      // time_t sys_now = util::time::time_utility::get_sys_now();

      auto& hpa_discovery_data_ptr = hpa_discovery.get_controller().hpa_discovery_data_;

      rapidjson_helper_dump_options dump_options;
      rapidjson_helper_parse(hpa_discovery_data_ptr->current_setting, value, dump_options);
      hpa_discovery_data_ptr->current_setting_data_version = header.version;
      hpa_discovery_data_ptr->current_setting_modify_revision = header.mod_revision;

      if (hpa_discovery_data_ptr->current_setting.current_replicas() > 0) {
        hpa_discovery_data_ptr->main_controller_current_replicas.store(
            hpa_discovery_data_ptr->current_setting.current_replicas(), std::memory_order_release);

        if (!hpa_discovery.get_controller().is_main_hpa_controller()) {
          protobuf_copy_message(hpa_discovery_data_ptr->expect_setting, hpa_discovery_data_ptr->current_setting);
          hpa_discovery_data_accessor::assign_main_controller_expect_from_status(
              *hpa_discovery_data_ptr, hpa_discovery_data_ptr->current_setting);
        }

        FWLOGINFO(
            "[HPA]: Controller update default discovery current setting, current_replicas: {}, expect_replicas: {}, "
            "expect_replicate_start_timepoint: {}, expect_replicate_end_timepoint: {}, expect_scaling_timepoint: {}, "
            "data meta(revision: {}, subkey: {}, main controller: {}({}))",
            hpa_discovery_data_ptr->current_setting.current_replicas(),
            hpa_discovery_data_ptr->current_setting.expect_replicas(),
            hpa_discovery_data_ptr->current_setting.expect_replicate_start_timepoint().seconds(),
            hpa_discovery_data_ptr->current_setting.expect_replicate_end_timepoint().seconds(),
            hpa_discovery_data_ptr->current_setting.expect_scaling_timepoint().seconds(), header.mod_revision,
            header.subkey, hpa_discovery_data_ptr->current_setting.controller_node_id(),
            hpa_discovery_data_ptr->current_setting.controller_node_name());
      }

      // 获取到当前设置下发后可能要重设提交判定时间
      time_t sys_now = util::time::time_utility::get_sys_now();
      apply_default_hpa_discovery_next_submit_timepoint_from_current_setting(*hpa_discovery_data_ptr, sys_now);
    });

    // ready后开始main controller上报数据
    hpa_discovery_data_->default_hpa_discovery->add_event_on_ready(
        [](logic_hpa_discovery& hpa_discovery) { hpa_discovery.get_controller().do_report_default_hpa_discovery(); });
  }

  // 收集所有配置了HPA目标策略的policy
  logic_hpa_discovery_setup_policy_accessor accessor;
  if (policy_cpu_permillage_) {
    if (policy_cpu_permillage_->can_pulling_available() && policy_cpu_permillage_->has_scaling_configure()) {
      hpa_discovery_data_->default_hpa_discovery->add_pull_policy(policy_cpu_permillage_, accessor);
    }
  }
  if (policy_main_thread_cpu_permillage_) {
    if (policy_main_thread_cpu_permillage_->can_pulling_available() &&
        policy_main_thread_cpu_permillage_->has_scaling_configure()) {
      hpa_discovery_data_->default_hpa_discovery->add_pull_policy(policy_main_thread_cpu_permillage_, accessor);
    }
  }
  if (policy_memory_) {
    if (policy_memory_->can_pulling_available() && policy_memory_->has_scaling_configure()) {
      hpa_discovery_data_->default_hpa_discovery->add_pull_policy(policy_memory_, accessor);
    }
  }
  if (policy_recent_max_task_count_) {
    if (policy_recent_max_task_count_->can_pulling_available() &&
        policy_recent_max_task_count_->has_scaling_configure()) {
      hpa_discovery_data_->default_hpa_discovery->add_pull_policy(policy_recent_max_task_count_, accessor);
    }
  }
  if (policy_controller_status_) {
    if (policy_controller_status_->can_pulling_available() && policy_controller_status_->has_scaling_configure()) {
      hpa_discovery_data_->default_hpa_discovery->add_pull_policy(policy_controller_status_, accessor);
    }
  }
  for (auto& policy : policy_custom_) {
    if (policy.second) {
      if (policy.second->can_pulling_available() && policy.second->has_scaling_configure()) {
        hpa_discovery_data_->default_hpa_discovery->add_pull_policy(policy.second, accessor);
      }
    }
  }
}

void logic_hpa_controller::cleanup_default_hpa_discovery() {
  if (!hpa_discovery_data_) {
    return;
  }

  if (!hpa_discovery_data_->default_hpa_discovery) {
    return;
  }

  if (!hpa_discovery_data_->default_hpa_discovery->is_stoping()) {
    hpa_discovery_data_->default_hpa_discovery->stop();
  }

  if (hpa_discovery_data_->default_hpa_discovery->is_stopped()) {
    hpa_discovery_data_->default_hpa_discovery.reset();
  }
}

void logic_hpa_controller::do_reload_hpa_configure() {
  auto& hpa_configure = logic_config::me()->get_logic().hpa();
  auto& hpa_target = hpa_configure.controller().target();
  std::string hpa_configure_key;

  // 默认的key生成规则，包含target信息
  if (hpa_configure.controller().enable() && hpa_configure.metrics().enable()) {
    if (!hpa_target.kind().empty()) {
      if (!hpa_configure_key.empty() && hpa_configure_key[hpa_configure_key.size() - 1] != '/') {
        hpa_configure_key += '/';
      }
      hpa_configure_key += hpa_target.kind();
    }

    if (!hpa_target.api_version().empty()) {
      if (!hpa_configure_key.empty() && hpa_configure_key[hpa_configure_key.size() - 1] != '/') {
        hpa_configure_key += '/';
      }
      hpa_configure_key += hpa_target.api_version();
    }

    if (!hpa_target.name().empty()) {
      if (!hpa_configure_key.empty() && hpa_configure_key[hpa_configure_key.size() - 1] != '/') {
        hpa_configure_key += '/';
      }
      hpa_configure_key += hpa_target.name();
    }
  }

  if (hpa_discovery_data_) {
    if (hpa_discovery_data_->with_type_id != hpa_configure.controller().type_id() ||
        hpa_discovery_data_->with_type_name != hpa_configure.controller().type_name()) {
      hpa_discovery_data_->reload_controller = true;
    }

    if (hpa_discovery_data_->configure_key == hpa_configure_key) {
      return;
    }

    hpa_discovery_data_->configure_key = hpa_configure_key;
  }
}

void logic_hpa_controller::do_reload_hpa_controller_tick(bool need_reload) {
  auto& hpa_configure = logic_config::me()->get_logic().hpa();

  if (nullptr == owner_app_) {
    return;
  }

  if (!hpa_discovery_data_) {
    return;
  }

  time_t sys_now = util::time::time_utility::get_sys_now();
  time_t controller_tick_timepoint;
  // Stop阶段提升频率以减少等待延迟
  if (hpa_discovery_data_->stoping) {
    controller_tick_timepoint = sys_now * 10 + util::time::time_utility::get_now_usec() / 250000;
  } else {
    controller_tick_timepoint = sys_now * 10 + util::time::time_utility::get_now_usec() / 100000;
  }
  if (controller_tick_timepoint == hpa_discovery_data_->last_controller_tick_timepoint) {
    return;
  }
  hpa_discovery_data_->last_controller_tick_timepoint = controller_tick_timepoint;

  std::unique_ptr<gsl::final_action<std::function<void()>>> reload_custom_discoveries;

  if (hpa_discovery_data_->reload_controller) {
    hpa_discovery_data_->reload_controller = false;

    if (hpa_discovery_data_->stoping || hpa_discovery_data_->configure_key.empty() ||
        !hpa_configure.controller().enable()) {
      cleanup_hpa_controller();
    } else {
      setup_hpa_controller();
    }
  }

  // Reload时延迟重建策略，底层otel-cpp会重建provider，这里要重新注册上报策略和拉取策略
  if (need_reload) {
    cleanup_stateful_index_policy();
    cleanup_expect_replicas_policy();
    cleanup_default_hpa_discovery();

    reload_custom_discoveries.reset(
        new gsl::final_action<std::function<void()>>([hpa_discovery_data = hpa_discovery_data_]() {
          if (!hpa_discovery_data || hpa_discovery_data->stoping) {
            return;
          }

          logic_hpa_discovery_setup_policy_accessor accessor;
          for (auto& custom_discovery : hpa_discovery_data->custom_hpa_discovery) {
            if (!custom_discovery.second.instance) {
              continue;
            }

            custom_discovery.second.instance->reset_policy(accessor);
          }

          for (auto& custom_discovery : hpa_discovery_data->custom_hpa_discovery) {
            if (!custom_discovery.second.instance) {
              continue;
            }

            if (custom_discovery.second.setup_callback) {
              custom_discovery.second.setup_callback(*custom_discovery.second.instance, accessor);
            }
          }
        }));
  }
  // Stoping default_hpa_discovery
  if (hpa_discovery_data_->default_hpa_discovery && hpa_discovery_data_->default_hpa_discovery->is_stoping()) {
    cleanup_default_hpa_discovery();
  }

  if (!hpa_discovery_data_->stoping && hpa_discovery_data_->discovery_set &&
      !hpa_discovery_data_->default_hpa_discovery) {
    setup_default_hpa_discovery(hpa_configure);
  }

  // Reset kLogicHpaDiscoveryLabelTarget after expect_replicate_start_timepoint
  int32_t current_expect_replicas = hpa_discovery_data_->current_setting.expect_replicas();
  // Non-Native Cloud start instance index from 1, so we add one counter here.
  if (!hpa_discovery_data_->current_hpa_discovery_with_pod_index) {
    ++current_expect_replicas;
  }
  bool set_scaling_labels_by_rule = hpa_configure.rule().set_scaling_label();
  if (sys_now > hpa_discovery_data_->current_setting.expect_replicate_start_timepoint().seconds() ||
      (hpa_discovery_data_->stoping && sys_now > hpa_discovery_data_->stoping_hpa_label_target_timepoint)) {
    bool scaling_rule_target =
        !set_scaling_labels_by_rule ||
        current_expect_replicas >= hpa_discovery_data_->controller_stateful_index.load(std::memory_order_acquire);
    if (!hpa_discovery_data_->current_hpa_label_target && !hpa_discovery_data_->stoping &&
        // 如果已经计划退出，即便时间没到也不需要提升为ready了
        hpa_discovery_data_->schedule_shutdown_timepoint <= 0 && scaling_rule_target) {
      owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelTarget, kLogicHpaValueTrue);
      hpa_discovery_data_->current_hpa_label_target = true;
      hpa_discovery_data_->report_hpa_label_target.store(true, std::memory_order_relaxed);
    } else if (hpa_discovery_data_->current_hpa_label_target &&
               (  // 优雅退出
                   hpa_discovery_data_->stoping ||
                   // 手动控制目标路由下线
                   (hpa_discovery_data_->schedule_shutdown_timepoint > 0 &&
                    // 自动HPA控制目标路由下线
                    sys_now > hpa_discovery_data_->schedule_shutdown_timepoint) ||
                   !scaling_rule_target)) {
      owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelTarget, kLogicHpaValueFalse);
      hpa_discovery_data_->current_hpa_label_target = false;
      hpa_discovery_data_->report_hpa_label_target.store(false, std::memory_order_relaxed);
    }
  }

  // Reset kLogicHpaDiscoveryLabelReady after expect_replicate_end_timepoint and state checking callback got false
  if (sys_now > hpa_discovery_data_->current_setting.expect_replicate_end_timepoint().seconds() ||
      (hpa_discovery_data_->stoping && sys_now > hpa_discovery_data_->stoping_hpa_label_ready_timepoint)) {
    bool scaling_rule_ready =
        !set_scaling_labels_by_rule ||
        current_expect_replicas >= hpa_discovery_data_->controller_stateful_index.load(std::memory_order_acquire);
    if (!hpa_discovery_data_->current_hpa_label_ready && !hpa_discovery_data_->stoping && scaling_rule_ready) {
      if (!hpa_ready_checking_callback_ || hpa_ready_checking_callback_(*this)) {
        owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelReady, kLogicHpaValueTrue);
        hpa_discovery_data_->current_hpa_label_ready = true;
        hpa_discovery_data_->report_hpa_label_ready.store(true, std::memory_order_relaxed);
      }
    } else if (hpa_discovery_data_->current_hpa_label_ready && (  // 优雅退出
                                                                   hpa_discovery_data_->stoping ||
                                                                   // 自动HPA控制当前路由下线
                                                                   !scaling_rule_ready)) {
      // stateful checking callback
      if (!hpa_stateful_checking_callback_ || !hpa_stateful_checking_callback_(*this)) {
        owner_app_->set_metadata_label(kLogicHpaDiscoveryLabelReady, kLogicHpaValueFalse);
        hpa_discovery_data_->current_hpa_label_ready = false;
        hpa_discovery_data_->report_hpa_label_ready.store(false, std::memory_order_relaxed);
      }
    }
  }

  if (hpa_discovery_data_->configure_key.empty() || !hpa_configure.controller().enable()) {
    cleanup_stateful_index_policy();
    cleanup_expect_replicas_policy();
    return;
  }
  // Report metrics_name_stateful_index
  if (controller_policy_stateful_index_ && controller_policy_stateful_index_->is_stoping()) {
    cleanup_stateful_index_policy();
  } else if (!controller_policy_stateful_index_ && !hpa_configure.metrics().metrics_name_stateful_index().empty()) {
    PROJECT_NAMESPACE_ID::config::logic_hpa_policy policy_cfg;
    policy_cfg.set_metrics_name(hpa_configure.metrics().metrics_name_stateful_index());
    policy_cfg.set_metrics_unit("count");

    for (auto& label_kv : logic_config::me()->get_logic().hpa().controller().discovery_labels()) {
      (*policy_cfg.mutable_labels())[rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
          label_kv.first, true)] = label_kv.second;
    }
    setup_stateful_index_policy(hpa_configure, policy_cfg);
  }

  if (!is_main_hpa_controller()) {
    // 不能清空policy，继续以副本保持上报
    // v1版本不允许清空meter，如果配置成累计上报，这里重置掉会导致底层一直上报老数据
    // v2版本未稳定，目前清空逻辑有问题，会导致存储区和试图为清理干净，所以也不用走清理逻辑
    // TODO(owent): 等v2版本稳定且能清理干净之后在接入动态清理
    // cleanup_expect_replicas_policy();
    return;
  }

  if (hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint <= sys_now) {
    do_report_default_hpa_discovery();
  }

  // ready后main controller定时重试上报数据（如果有变化）

  // 至少要等所有的指标都拉取一次后才能设置预期副本上报策略
  // Setup metrics_name_expect_replicas reportor for main controller
  if (controller_policy_expect_replicas_ && controller_policy_expect_replicas_->is_stoping()) {
    cleanup_expect_replicas_policy();
  } else if (!controller_policy_expect_replicas_ && !hpa_configure.metrics().metrics_name_expect_replicas().empty() &&
             hpa_discovery_data_->default_hpa_discovery && hpa_discovery_data_->default_hpa_discovery->is_ready() &&
             hpa_discovery_data_->default_hpa_discovery->is_all_policies_pulled()) {
    PROJECT_NAMESPACE_ID::config::logic_hpa_policy policy_cfg;
    policy_cfg.set_metrics_name(hpa_configure.metrics().metrics_name_expect_replicas());
    policy_cfg.set_metrics_unit("count");

    for (auto& label_kv : logic_config::me()->get_logic().hpa().controller().discovery_labels()) {
      (*policy_cfg.mutable_labels())[rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
          label_kv.first, true)] = label_kv.second;
    }
    setup_expect_replicas_policy(hpa_configure, policy_cfg);
  }
}

void logic_hpa_controller::do_report_default_hpa_discovery() {
  if (!hpa_discovery_data_) {
    return;
  }

  if (!is_main_hpa_controller()) {
    return;
  }

  time_t sys_now = util::time::time_utility::get_sys_now();
  if (hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint > sys_now) {
    return;
  }

  auto& hpa_configure = logic_config::me()->get_logic().hpa();
  time_t pull_interval_sec = hpa_configure.metrics().pull_interval().seconds();
  if (pull_interval_sec <= 0) {
    pull_interval_sec = 60;
  }
  hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint = sys_now + pull_interval_sec;

  // 可能要提前重设 current_replicas
  apply_default_hpa_discovery_next_submit_timepoint_from_current_setting(*hpa_discovery_data_, sys_now);

  if (!hpa_discovery_data_->default_hpa_discovery) {
    return;
  }

  if (!hpa_discovery_data_->default_hpa_discovery->is_ready() ||
      !hpa_discovery_data_->default_hpa_discovery->is_all_policies_pulled()) {
    FWLOGINFO(
        "[HPA]: Controller will retry default discovery reporting at {} because it's not ready yet or any policy not "
        "pulled.",
        hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint);
    return;
  }

  do_update_default_hpa_settings(sys_now, hpa_configure);
}

void logic_hpa_controller::do_update_default_hpa_settings(
    time_t sys_now, const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure) {
  if (!hpa_discovery_data_) {
    return;
  }

  if (!hpa_discovery_data_->default_hpa_discovery) {
    return;
  }

  const int32_t current_replicates =
      sys_now >= hpa_discovery_data_->current_setting.expect_scaling_timepoint().seconds()
          ? hpa_discovery_data_->current_setting.expect_replicas()
          : hpa_discovery_data_->current_setting.current_replicas();

  // 云下模式的Index
  int32_t non_native_cloud_index = 0;
  if (hpa_discovery_data_->main_controller_flag_mode == main_controller_mode::kNonNativeCloud) {
    non_native_cloud_index = hpa_discovery_data_->controller_stateful_index.load(std::memory_order_acquire);
    non_native_cloud_index =
        hpa_discovery_data_->default_hpa_discovery->get_non_native_cloud_replicas(non_native_cloud_index);
  }

  int32_t expect_replicas = current_replicates;
  // 稳定预期副本数的计算
  if (hpa_discovery_data_->expect_setting.expect_replicas() > 0 &&
      (hpa_discovery_data_->expect_setting.controller_node_id() == get_app()->get_app_id() ||
       hpa_discovery_data_->expect_setting.controller_node_name() == get_app()->get_app_name())) {
    expect_replicas = hpa_discovery_data_->expect_setting.expect_replicas();
  } else if (hpa_discovery_data_->current_setting.expect_replicas() > 0) {
    // 迁移时如果未复制数据，使用当前值
    expect_replicas = hpa_discovery_data_->current_setting.expect_replicas();
  }

  // 应用扩容策略
  expect_replicas = apply_default_hpa_scaling_up_expect_replicas(sys_now, hpa_configure, non_native_cloud_index,
                                                                 current_replicates, expect_replicas);

  // 应用缩容策略
  expect_replicas = apply_default_hpa_scaling_down_expect_replicas(sys_now, hpa_configure, non_native_cloud_index,
                                                                   current_replicates, expect_replicas);

  bool apply_expect_replicas = sys_now >= hpa_discovery_data_->expect_setting.expect_scaling_timepoint().seconds();
  if (apply_expect_replicas) {
    // 重置冷静窗口
    reset_default_hpa_scaling_up_expect_replicas(sys_now);
    reset_default_hpa_scaling_down_expect_replicas(sys_now);
  }

  time_t expect_replicate_start = 0;
  time_t expect_replicate_end = 0;
  time_t expect_scaling = 0;
  // 预期副本数变更或未在etcd中查询到配置，则要更新数据
  // 上一轮HPA未完成要等上轮结束
  if (apply_expect_replicas && (expect_replicas != hpa_discovery_data_->expect_setting.expect_replicas() ||
                                hpa_discovery_data_->current_setting_modify_revision == 0)) {
    hpa_discovery_data_->expect_setting.set_current_replicas(current_replicates);
    hpa_discovery_data_->expect_setting.set_expect_replicas(expect_replicas);

    // 预期副本数变化，需要重设迁移冷静期，预期迁移时间和震荡期保护时间
    expect_replicate_start = sys_now + hpa_configure.controller().replicate_start_delay().seconds();
    expect_replicate_end = expect_replicate_start + hpa_configure.controller().replicate_period().seconds();
    expect_scaling = expect_replicate_end + hpa_configure.controller().scaling_delay().seconds();
  } else {
    // 当前副本数和预期副本数都没变化，则不需要刷新和下发配置
    if (!hpa_discovery_data_->schedule_scaling.with_action &&
        hpa_discovery_data_->expect_setting.current_replicas() == current_replicates) {
      return;
    }
    hpa_discovery_data_->expect_setting.set_current_replicas(current_replicates);

    expect_replicate_start = hpa_discovery_data_->expect_setting.expect_replicate_start_timepoint().seconds();
    expect_replicate_end = hpa_discovery_data_->expect_setting.expect_replicate_end_timepoint().seconds();
    expect_scaling = hpa_discovery_data_->expect_setting.expect_scaling_timepoint().seconds();
  }

  // 命令控制重设HPA流程启动时间
  if (hpa_discovery_data_->schedule_scaling.with_action) {
    if (hpa_discovery_data_->schedule_scaling.replicate_start >= 0 &&
        expect_replicate_start < hpa_discovery_data_->schedule_scaling.replicate_start) {
      expect_replicate_start = hpa_discovery_data_->schedule_scaling.replicate_start;
    }

    if (hpa_discovery_data_->schedule_scaling.replicate_end >= 0 &&
        expect_replicate_end < hpa_discovery_data_->schedule_scaling.replicate_end) {
      expect_replicate_end = hpa_discovery_data_->schedule_scaling.replicate_end;
    }

    if (hpa_discovery_data_->schedule_scaling.expect_scaling >= 0 &&
        expect_scaling < hpa_discovery_data_->schedule_scaling.expect_scaling) {
      expect_scaling = hpa_discovery_data_->schedule_scaling.expect_scaling;
    }
  }

  // 刷新预期副本设置
  hpa_discovery_data_->expect_setting.mutable_expect_replicate_start_timepoint()->set_seconds(expect_replicate_start);
  hpa_discovery_data_->expect_setting.mutable_expect_replicate_end_timepoint()->set_seconds(expect_replicate_end);
  hpa_discovery_data_->expect_setting.mutable_expect_scaling_timepoint()->set_seconds(expect_scaling);
  hpa_discovery_data_->expect_setting.set_controller_node_id(get_app()->get_app_id());
  if (hpa_discovery_data_->expect_setting.controller_node_name() != get_app()->get_app_name()) {
    hpa_discovery_data_->expect_setting.set_controller_node_name(get_app()->get_app_name());
  }

  bool has_update_to_etcd = false;
  if (hpa_discovery_data_->current_setting_modify_revision == 0 ||

      atfw::atapp::protobuf_equal(hpa_discovery_data_->current_setting, hpa_discovery_data_->expect_setting)) {
    rapidjson_helper_load_options load_options;
    hpa_discovery_data_->default_hpa_discovery->set_value(
        rapidjson_helper_stringify(hpa_discovery_data_->expect_setting, load_options));
    has_update_to_etcd = true;
  }

  hpa_discovery_data_accessor::assign_main_controller_expect_from_status(*hpa_discovery_data_,
                                                                         hpa_discovery_data_->expect_setting);

  // Reset command trigger
  hpa_discovery_data_->schedule_scaling.with_action = false;

  FWLOGINFO(
      "[HPA]: Controller update default discovery expect setting, current_replicas: {}, expect_replicas: {}, "
      "expect_replicate_start_timepoint: {}, expect_replicate_end_timepoint: {}, expect_scaling_timepoint: {}, upload: "
      "{}",
      hpa_discovery_data_->expect_setting.current_replicas(), hpa_discovery_data_->expect_setting.expect_replicas(),
      hpa_discovery_data_->expect_setting.expect_replicate_start_timepoint().seconds(),
      hpa_discovery_data_->expect_setting.expect_replicate_end_timepoint().seconds(),
      hpa_discovery_data_->expect_setting.expect_scaling_timepoint().seconds(), has_update_to_etcd ? "true" : "false");
}

int32_t logic_hpa_controller::apply_default_hpa_scaling_up_expect_replicas(
    time_t sys_now, const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure, int32_t non_native_cloud_index,
    int32_t current_replicates, int32_t expect_replicas) {
  int32_t scaling_up_target = calculate_default_hpa_scaling_up_replicas(hpa_configure, non_native_cloud_index,
                                                                        current_replicates, expect_replicas);
  if (scaling_up_target > expect_replicas) {
    // 稳定阶段，转冷静期
    if (hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint <= 0) {
      time_t start_time = std::max(
          sys_now, static_cast<time_t>(hpa_discovery_data_->current_setting.expect_scaling_timepoint().seconds()));
      start_time = std::max(
          start_time, static_cast<time_t>(hpa_discovery_data_->expect_setting.expect_scaling_timepoint().seconds()));
      hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint =
          start_time + hpa_configure.rule().scaling_up_configure().stabilization_window().seconds();
      hpa_discovery_data_->default_hpa_discovery_scaling_up_target_replicas = scaling_up_target;
    } else if (scaling_up_target > hpa_discovery_data_->default_hpa_discovery_scaling_up_target_replicas) {
      // 冷静窗口期间，可以继续放大
      hpa_discovery_data_->default_hpa_discovery_scaling_up_target_replicas = scaling_up_target;
    }
  }

  if (hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint > 0) {
    if (sys_now >= hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint) {
      if (hpa_discovery_data_->default_hpa_discovery_scaling_up_target_replicas > expect_replicas) {
        expect_replicas = hpa_discovery_data_->default_hpa_discovery_scaling_up_target_replicas;
      }

      // 生效时要保存一下，在scaling up冷静窗口期触发scaling down计算时使用缓存数据
      hpa_discovery_data_->default_hpa_discovery_scaling_up_expect_replicas =
          hpa_discovery_data_->default_hpa_discovery_scaling_up_target_replicas;
    } else {
      // 重设下一次重试时间到冷静窗口结束
      if (hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint >
          hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint) {
        hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint =
            hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint;
      }
    }
  } else {
    // 不在冷静窗口中则每次都刷新上报，否则会覆盖缩容结算
    hpa_discovery_data_->default_hpa_discovery_scaling_up_expect_replicas = scaling_up_target;
  }

  if (hpa_discovery_data_->default_hpa_discovery_scaling_up_expect_replicas > expect_replicas) {
    expect_replicas = hpa_discovery_data_->default_hpa_discovery_scaling_up_expect_replicas;
  }

  return expect_replicas;
}

void logic_hpa_controller::reset_default_hpa_scaling_up_expect_replicas(time_t sys_now) {
  if (sys_now < hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint) {
    return;
  }

  hpa_discovery_data_->default_hpa_discovery_scaling_up_stabilization_end_timepoint = 0;
  hpa_discovery_data_->default_hpa_discovery_scaling_up_target_replicas = 0;
}

int32_t logic_hpa_controller::calculate_default_hpa_scaling_up_replicas(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure, int32_t non_native_cloud_index,
    int32_t current_replicates, int32_t expect_replicas) {
  int32_t scaling_up_target = std::min(current_replicates, expect_replicas);
  int32_t max_scaling_count = hpa_configure.rule().scaling_up_configure().max_scaling_count();
  int32_t except_value = hpa_discovery_data_->default_hpa_discovery->get_scaling_up_expect_replicas();
  if (hpa_discovery_data_->main_controller_debug_expect_replicas > 0) {
    except_value = hpa_discovery_data_->main_controller_debug_expect_replicas;
  }
  if (except_value > scaling_up_target) {
    if (max_scaling_count > 0 && except_value > current_replicates &&
        except_value - current_replicates > max_scaling_count) {
      scaling_up_target = current_replicates + max_scaling_count;
    } else {
      scaling_up_target = except_value;
    }
  }

  // 云下模式至少要包含本地节点
  if (non_native_cloud_index > 0 && scaling_up_target < non_native_cloud_index) {
    scaling_up_target = non_native_cloud_index;
  }

  if (hpa_configure.controller().min_replicas() > 0 && scaling_up_target < hpa_configure.controller().min_replicas()) {
    scaling_up_target = hpa_configure.controller().min_replicas();
  }

  if (hpa_configure.controller().max_replicas() > 0 && scaling_up_target > hpa_configure.controller().max_replicas()) {
    scaling_up_target = hpa_configure.controller().max_replicas();
  }

  return scaling_up_target;
}

int32_t logic_hpa_controller::apply_default_hpa_scaling_down_expect_replicas(
    time_t sys_now, const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure, int32_t non_native_cloud_index,
    int32_t current_replicates, int32_t expect_replicas) {
  int32_t scaling_down_target = calculate_default_hpa_scaling_down_replicas(hpa_configure, non_native_cloud_index,
                                                                            current_replicates, expect_replicas);
  if (scaling_down_target < expect_replicas) {
    // 稳定阶段，转冷静期
    if (hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint <= 0) {
      time_t start_time = std::max(
          sys_now, static_cast<time_t>(hpa_discovery_data_->current_setting.expect_scaling_timepoint().seconds()));
      start_time = std::max(
          start_time, static_cast<time_t>(hpa_discovery_data_->expect_setting.expect_scaling_timepoint().seconds()));
      hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint =
          start_time + hpa_configure.rule().scaling_down_configure().stabilization_window().seconds();

      hpa_discovery_data_->default_hpa_discovery_scaling_down_target_replicas = scaling_down_target;
    } else if (scaling_down_target < hpa_discovery_data_->default_hpa_discovery_scaling_down_target_replicas) {
      // 冷静窗口期间，可以继续缩小
      hpa_discovery_data_->default_hpa_discovery_scaling_down_target_replicas = scaling_down_target;
    }
  }

  if (hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint > 0) {
    if (sys_now >= hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint) {
      if (hpa_discovery_data_->default_hpa_discovery_scaling_down_target_replicas < expect_replicas) {
        expect_replicas = hpa_discovery_data_->default_hpa_discovery_scaling_down_target_replicas;
      }

      // 生效时要保存一下，在scaling down冷静窗口期触发scaling up计算时使用缓存数据
      hpa_discovery_data_->default_hpa_discovery_scaling_down_expect_replicas =
          hpa_discovery_data_->default_hpa_discovery_scaling_down_target_replicas;
    } else {
      // 重设下一次重试时间到冷静窗口结束
      if (hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint >
          hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint) {
        hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint =
            hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint;
      }
    }
  } else {
    // 不在冷静窗口中则每次都刷新上报，否则会覆盖扩容结算
    hpa_discovery_data_->default_hpa_discovery_scaling_down_expect_replicas = scaling_down_target;
  }

  if (hpa_discovery_data_->default_hpa_discovery_scaling_down_expect_replicas > 0 &&
      hpa_discovery_data_->default_hpa_discovery_scaling_down_expect_replicas < expect_replicas) {
    expect_replicas = hpa_discovery_data_->default_hpa_discovery_scaling_down_expect_replicas;
  }

  return expect_replicas;
}

void logic_hpa_controller::reset_default_hpa_scaling_down_expect_replicas(time_t sys_now) {
  if (sys_now < hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint) {
    return;
  }

  hpa_discovery_data_->default_hpa_discovery_scaling_down_stabilization_end_timepoint = 0;
  hpa_discovery_data_->default_hpa_discovery_scaling_down_target_replicas = 0;
}

int32_t logic_hpa_controller::calculate_default_hpa_scaling_down_replicas(
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure, int32_t non_native_cloud_index,
    int32_t current_replicates, int32_t expect_replicas) {
  int32_t scaling_down_target = std::max(current_replicates, expect_replicas);

  int32_t max_scaling_count = hpa_configure.rule().scaling_down_configure().max_scaling_count();
  int32_t except_value = hpa_discovery_data_->default_hpa_discovery->get_scaling_down_expect_replicas();
  if (hpa_discovery_data_->main_controller_debug_expect_replicas > 0) {
    except_value = hpa_discovery_data_->main_controller_debug_expect_replicas;
  }
  if (except_value > 0 && except_value < scaling_down_target) {
    if (max_scaling_count > 0 && current_replicates > except_value &&
        current_replicates - except_value > max_scaling_count) {
      scaling_down_target = current_replicates - max_scaling_count;
    } else {
      scaling_down_target = except_value;
    }
  }

  // 云下模式至少要包含本地节点
  if (non_native_cloud_index > 0 && scaling_down_target < non_native_cloud_index) {
    scaling_down_target = non_native_cloud_index;
  }

  if (hpa_configure.controller().min_replicas() > 0 &&
      scaling_down_target < hpa_configure.controller().min_replicas()) {
    scaling_down_target = hpa_configure.controller().min_replicas();
  }

  if (hpa_configure.controller().max_replicas() > 0 &&
      scaling_down_target > hpa_configure.controller().max_replicas()) {
    scaling_down_target = hpa_configure.controller().max_replicas();
  }

  return scaling_down_target;
}

void logic_hpa_controller::apply_default_hpa_discovery_next_submit_timepoint_from_current_setting(
    hpa_discovery_data& hpa_discovery_data, time_t sys_now) {
  // 计算下一次进入定时器操作的时间
  if (hpa_discovery_data.current_setting.expect_scaling_timepoint().seconds() > sys_now &&
      hpa_discovery_data.current_setting.expect_replicas() != hpa_discovery_data.current_setting.current_replicas() &&
      hpa_discovery_data.default_hpa_discovery_next_submit_timepoint >
          hpa_discovery_data.current_setting.expect_scaling_timepoint().seconds()) {
    hpa_discovery_data.default_hpa_discovery_next_submit_timepoint =
        hpa_discovery_data.current_setting.expect_scaling_timepoint().seconds();
  }

  if (hpa_discovery_data.default_hpa_discovery_scaling_up_stabilization_end_timepoint > sys_now &&
      hpa_discovery_data.default_hpa_discovery_scaling_up_stabilization_end_timepoint >
          hpa_discovery_data.default_hpa_discovery_next_submit_timepoint) {
    hpa_discovery_data.default_hpa_discovery_next_submit_timepoint =
        hpa_discovery_data.default_hpa_discovery_scaling_up_stabilization_end_timepoint;
  }

  if (hpa_discovery_data.default_hpa_discovery_scaling_down_stabilization_end_timepoint > sys_now &&
      hpa_discovery_data.default_hpa_discovery_scaling_down_stabilization_end_timepoint >
          hpa_discovery_data.default_hpa_discovery_next_submit_timepoint) {
    hpa_discovery_data.default_hpa_discovery_next_submit_timepoint =
        hpa_discovery_data.default_hpa_discovery_scaling_down_stabilization_end_timepoint;
  }
}

void logic_hpa_controller::setup_hpa_controller() {
  if (nullptr == owner_app_) {
    return;
  }

  if (!hpa_discovery_data_) {
    return;
  }

  if (hpa_discovery_data_->stoping) {
    return;
  }

  auto& hpa_configure = logic_config::me()->get_logic().hpa();
  hpa_discovery_data_->with_type_id = hpa_configure.controller().type_id();
  hpa_discovery_data_->with_type_name = hpa_configure.controller().type_name();

  hpa_discovery_data_->discovery_set.reset();
  if (!hpa_discovery_data_->discovery_set) {
    hpa_discovery_data_->discovery_set = atfw::memory::stl::make_strong_rc<atfw::atapp::etcd_discovery_set>();
  }

  // 用已有的服务发现初始化节点分布
  for (auto& node : owner_app_->get_global_discovery().get_sorted_nodes()) {
    if (!node) {
      continue;
    }

    if (atfw::atapp::etcd_discovery_set::metadata_equal_type::filter(hpa_discovery_data_->discovery_filter,
                                                                     node->get_discovery_info().metadata())) {
      continue;
    }

    if (hpa_discovery_data_->with_type_id != 0 && node->get_discovery_info().type_id() != 0) {
      if (hpa_discovery_data_->with_type_id == node->get_discovery_info().type_id()) {
        hpa_discovery_data_->discovery_set->add_node(node);
        hpa_discovery_data_->main_controller_flag_cache = main_controller_flag::kUnset;
      }
    } else if (!hpa_discovery_data_->with_type_name.empty() && !node->get_discovery_info().type_name().empty()) {
      if (hpa_discovery_data_->with_type_name == node->get_discovery_info().type_name()) {
        hpa_discovery_data_->discovery_set->add_node(node);
        hpa_discovery_data_->main_controller_flag_cache = main_controller_flag::kUnset;
      }
    } else if (hpa_discovery_data_->with_type_id == 0 && hpa_discovery_data_->with_type_name.empty()) {
      hpa_discovery_data_->discovery_set->add_node(node);
      hpa_discovery_data_->main_controller_flag_cache = main_controller_flag::kUnset;
    }
  }

  // Setup/cleanup etcd watcher
  // 注册监听节点变化事件以便更新节点分布缓存
  if (!hpa_discovery_data_->node_event_has_handle && nullptr != owner_app_) {
    auto etcd_mod = owner_app_->get_etcd_module();
    if (etcd_mod) {
      std::weak_ptr<hpa_discovery_data> hpa_discovery_data_weak = hpa_discovery_data_;
      hpa_discovery_data_->node_event_handle = etcd_mod->add_on_node_discovery_event(
          [hpa_discovery_data_weak](atfw::atapp::etcd_module::node_action_t::type action,
                                    const atfw::atapp::etcd_discovery_node::ptr_t& node) {
            std::shared_ptr<hpa_discovery_data> hpa_discovery_data_ptr = hpa_discovery_data_weak.lock();
            if (!hpa_discovery_data_ptr || !hpa_discovery_data_ptr->discovery_set || !node) {
              return;
            }
            bool select = false;
            if (hpa_discovery_data_ptr->with_type_id != 0 && node->get_discovery_info().type_id() != 0) {
              if (hpa_discovery_data_ptr->with_type_id == node->get_discovery_info().type_id()) {
                select = true;
              }
            } else if (!hpa_discovery_data_ptr->with_type_name.empty() &&
                       !node->get_discovery_info().type_name().empty()) {
              if (hpa_discovery_data_ptr->with_type_name == node->get_discovery_info().type_name()) {
                select = true;
              }
            } else if (hpa_discovery_data_ptr->with_type_id == 0 && hpa_discovery_data_ptr->with_type_name.empty()) {
              select = true;
            }

            if (!select) {
              return;
            }

            // 新增要开绿筛选标签，移除不需要
            if (action == atfw::atapp::etcd_module::node_action_t::EN_NAT_PUT) {
              if (atfw::atapp::etcd_discovery_set::metadata_equal_type::filter(hpa_discovery_data_ptr->discovery_filter,
                                                                               node->get_discovery_info().metadata())) {
                hpa_discovery_data_ptr->discovery_set->add_node(node);
              }
            } else {
              hpa_discovery_data_ptr->discovery_set->remove_node(node);
            }
            hpa_discovery_data_ptr->main_controller_flag_cache = main_controller_flag::kUnset;
          });
      hpa_discovery_data_->node_event_has_handle = true;
    }
  }
}

void logic_hpa_controller::cleanup_hpa_controller() {
  if (!hpa_discovery_data_) {
    return;
  }

  if (hpa_discovery_data_->node_event_has_handle && nullptr != owner_app_) {
    auto etcd_mod = owner_app_->get_etcd_module();
    if (etcd_mod) {
      etcd_mod->remove_on_node_event(hpa_discovery_data_->node_event_handle);
      hpa_discovery_data_->node_event_has_handle = false;
    }
  }

  if (hpa_discovery_data_->discovery_set) {
    hpa_discovery_data_->discovery_set.reset();
  }

  cleanup_default_hpa_discovery();
}

bool logic_hpa_controller::is_main_hpa_controller() const noexcept {
  if (!hpa_discovery_data_ || nullptr == owner_app_) {
    return false;
  }

  // 如果已有缓存则跳过
  if (hpa_discovery_data_->main_controller_flag_cache == main_controller_flag::kNo) {
    return false;
  } else if (hpa_discovery_data_->main_controller_flag_cache == main_controller_flag::kYes) {
    return true;
  }

  // 进入重算主控节点的流程
  if (!hpa_discovery_data_->discovery_set) {
    return false;
  }

  auto& hpa_target_set = hpa_discovery_data_->discovery_set->get_sorted_nodes();
  if (hpa_target_set.empty()) {
    return false;
  }

  // 云下节点的index要更新为pod下标+1
  if (owner_app_->get_runtime_stateful_pod_index() < 0) {
    std::pair<atfw::atapp::app::app_id_t, const std::string&> self_info{owner_app_->get_app_id(),
                                                                        owner_app_->get_app_name()};

    auto iter = std::lower_bound(hpa_target_set.begin(), hpa_target_set.end(), self_info,
                                 [](const atfw::atapp::etcd_discovery_node::ptr_t& data,
                                    const std::pair<atfw::atapp::app::app_id_t, const std::string&>& info) {
                                   if (!data) {
                                     return false;
                                   }

                                   if (data->get_discovery_info().id() != info.first) {
                                     return data->get_discovery_info().id() < info.first;
                                   }

                                   return data->get_discovery_info().name() < info.second;
                                 });
    if (iter == hpa_target_set.end()) {
      hpa_discovery_data_->controller_stateful_index.store(static_cast<int32_t>(hpa_target_set.size() + 1),
                                                           std::memory_order_release);
    } else {
      hpa_discovery_data_->controller_stateful_index.store(static_cast<int32_t>((iter - hpa_target_set.begin()) + 1),
                                                           std::memory_order_release);
    }
  }

  atfw::atapp::etcd_discovery_node::ptr_t select_node;
  gsl::string_view select_mode;
  main_controller_mode select_controller_mode = main_controller_mode::kUnknown;
  for (auto iter = hpa_target_set.begin(); iter != hpa_target_set.end(); ++iter) {
    if (!*iter) {
      continue;
    }

    // Check pod index flag
    auto& labels = (*iter)->get_discovery_info().metadata().labels();
    auto label_iter = labels.find(kLogicHpaDiscoveryLabelWithPodIndex);
    if (label_iter == labels.end()) {
      // 云下采用最后一个节点
      select_node = *iter;
      select_mode = "Non-Native Cloud";
      select_controller_mode = main_controller_mode::kNonNativeCloud;
      continue;
    }

    // 云上采用第一个节点
    if (gsl::string_view(kLogicHpaValueTrue) == label_iter->second) {
      select_node = *iter;
      select_mode = "Cloud Native";
      select_controller_mode = main_controller_mode::kCloudNative;
      break;
    } else {
      // 云下采用最后一个节点
      select_node = *iter;
      select_mode = "Non-Native Cloud";
      select_controller_mode = main_controller_mode::kNonNativeCloud;
    }
  }

  if (!select_node) {
    hpa_discovery_data_accessor::set_main_hpa_controller_flag(*hpa_discovery_data_, main_controller_flag::kNo,
                                                              select_controller_mode);

    FWLOGINFO("[HPA]: Controller select as replicate controller(mode: {})", select_mode);

    if (hpa_discovery_data_->default_hpa_discovery) {
      hpa_discovery_data_->default_hpa_discovery->set_policy_active(logic_hpa_event_active_type::kUnactive);
    }
    return false;
  }

  time_t pull_interval_sec = logic_config::me()->get_logic().hpa().metrics().pull_interval().seconds();
  if (pull_interval_sec <= 0) {
    pull_interval_sec = 60;
  }

  time_t sys_now = util::time::time_utility::get_sys_now();
  // 停服阶段不允许提升为main controller
  if (!hpa_discovery_data_->stoping && owner_app_->get_app_id() == select_node->get_discovery_info().id()) {
    hpa_discovery_data_accessor::set_main_hpa_controller_flag(*hpa_discovery_data_, main_controller_flag::kYes,
                                                              select_controller_mode);
    // 转换为main controller后要等待稳定,首次启动时也需要等待一段时间可能要重算main controller再触发更新
    hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint = sys_now + pull_interval_sec;

    // 先复制现有数据，保证dump指令的输出是保持一致的
    protobuf_copy_message(hpa_discovery_data_->expect_setting, hpa_discovery_data_->current_setting);
    hpa_discovery_data_accessor::assign_main_controller_expect_from_status(*hpa_discovery_data_,
                                                                           hpa_discovery_data_->expect_setting);

    // 复原之前其他节点的 current_replicas 设置流程
    apply_default_hpa_discovery_next_submit_timepoint_from_current_setting(*hpa_discovery_data_, sys_now);

    FWLOGINFO("[HPA]: Controller select as main controller(mode: {})", select_mode);

    if (hpa_discovery_data_->default_hpa_discovery) {
      hpa_discovery_data_->default_hpa_discovery->set_policy_active(logic_hpa_event_active_type::kActive);
    }
    return true;
  }

  // 停服阶段不允许提升为main controller
  if (!hpa_discovery_data_->stoping && owner_app_->get_app_name() == select_node->get_discovery_info().name()) {
    hpa_discovery_data_accessor::set_main_hpa_controller_flag(*hpa_discovery_data_, main_controller_flag::kYes,
                                                              select_controller_mode);
    // 转换为main controller后要等待稳定,首次启动时也需要等待一段时间可能要重算main controller再触发更新
    hpa_discovery_data_->default_hpa_discovery_next_submit_timepoint = sys_now + pull_interval_sec;

    // 先复制现有数据，保证dump指令的输出是保持一致的
    protobuf_copy_message(hpa_discovery_data_->expect_setting, hpa_discovery_data_->current_setting);
    hpa_discovery_data_accessor::assign_main_controller_expect_from_status(*hpa_discovery_data_,
                                                                           hpa_discovery_data_->expect_setting);

    // 复原之前其他节点的 current_replicas 设置流程
    apply_default_hpa_discovery_next_submit_timepoint_from_current_setting(*hpa_discovery_data_, sys_now);

    FWLOGINFO("[HPA]: Controller select as main controller(mode: {})", select_mode);

    if (hpa_discovery_data_->default_hpa_discovery) {
      hpa_discovery_data_->default_hpa_discovery->set_policy_active(logic_hpa_event_active_type::kActive);
    }
    return true;
  }

  FWLOGINFO("[HPA]: Controller select as replicate controller(mode: {})", select_mode);
  hpa_discovery_data_accessor::set_main_hpa_controller_flag(*hpa_discovery_data_, main_controller_flag::kNo,
                                                            select_controller_mode);

  if (hpa_discovery_data_->default_hpa_discovery) {
    hpa_discovery_data_->default_hpa_discovery->set_policy_active(logic_hpa_event_active_type::kUnactive);
  }
  return false;
}

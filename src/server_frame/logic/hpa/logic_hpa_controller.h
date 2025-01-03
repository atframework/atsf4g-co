// Copyright 2024 atframework
// Created by owent

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>
#include <nostd/nullability.h>

#include <config/server_frame_build_feature.h>

#include <network/http_request.h>

#include <rpc/telemetry/rpc_global_service.h>

#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "logic/hpa/logic_hpa_data_type.h"

namespace atapp {
class app;
}

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_hpa_cfg;
class logic_hpa_policy;
}  // namespace config
PROJECT_NAMESPACE_END

class logic_hpa_policy;
class logic_hpa_discovery;
class logic_hpa_discovery_setup_policy_accessor;

class logic_hpa_controller {
 public:
  using on_setup_policy_callback = std::function<void(logic_hpa_controller&, std::shared_ptr<logic_hpa_policy>)>;
  using on_cleanup_policy_callback = std::function<void(logic_hpa_controller&, std::shared_ptr<logic_hpa_policy>)>;
  using on_stateful_checking_callback = std::function<bool(logic_hpa_controller&)>;

  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_controller);
  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_controller);

 public:
  SERVER_FRAME_API explicit logic_hpa_controller(atapp::app& owner_app);

  SERVER_FRAME_API ~logic_hpa_controller();

  SERVER_FRAME_API void init();

  SERVER_FRAME_API void reload();

  SERVER_FRAME_API int tick();

  SERVER_FRAME_API int stop(bool prestop, time_t target_label_offset = -1, time_t ready_label_offset = -1);

  SERVER_FRAME_API void cleanup();

  // =================== 内置策略的事件监听接口 - begin ===================

  SERVER_FRAME_API void set_on_setup_cpu_permillage_policy(on_setup_policy_callback fn);
  SERVER_FRAME_API void remove_on_setup_cpu_permillage_policy();
  SERVER_FRAME_API void set_on_cleanup_cpu_permillage_policy(on_cleanup_policy_callback fn);
  SERVER_FRAME_API void remove_on_cleanup_cpu_permillage_policy();

  SERVER_FRAME_API void set_on_setup_main_thread_cpu_permillage_policy(on_setup_policy_callback fn);
  SERVER_FRAME_API void remove_on_setup_main_thread_cpu_permillage_policy();
  SERVER_FRAME_API void set_on_cleanup_main_thread_cpu_permillage_policy(on_cleanup_policy_callback fn);
  SERVER_FRAME_API void remove_on_cleanup_main_thread_cpu_permillage_policy();

  SERVER_FRAME_API void set_on_setup_memory_policy(on_setup_policy_callback fn);
  SERVER_FRAME_API void remove_on_setup_memory_policy();
  SERVER_FRAME_API void set_on_cleanup_memory_policy(on_cleanup_policy_callback fn);
  SERVER_FRAME_API void remove_on_cleanup_memory_policy();

  SERVER_FRAME_API void set_on_setup_recent_max_task_count(on_setup_policy_callback fn);
  SERVER_FRAME_API void remove_on_setup_recent_max_task_count();
  SERVER_FRAME_API void set_on_cleanup_recent_max_task_count(on_cleanup_policy_callback fn);
  SERVER_FRAME_API void remove_on_cleanup_recent_max_task_count();

  SERVER_FRAME_API void set_on_setup_controller_status(on_setup_policy_callback fn);
  SERVER_FRAME_API void remove_on_setup_controller_status();
  SERVER_FRAME_API void set_on_cleanup_controller_status(on_cleanup_policy_callback fn);
  SERVER_FRAME_API void remove_on_cleanup_controller_status();

  SERVER_FRAME_API void set_on_setup_custom_policy(const std::string& metrics_name, on_setup_policy_callback fn);
  SERVER_FRAME_API void remove_on_setup_custom_policy(const std::string& metrics_name);
  SERVER_FRAME_API void set_on_cleanup_custom_policy(const std::string& metrics_name, on_cleanup_policy_callback fn);
  SERVER_FRAME_API void remove_on_cleanup_custom_policy(const std::string& metrics_name);

  // ------------------- 内置策略的事件监听接口 - end -------------------

  /**
   * @brief 设置业务层Ready检查函数
   *
   * @param fn 业务层检查函数,如果业务节点仍持有状态返回true，否则返回false
   */
  SERVER_FRAME_API void set_on_ready_checking(on_stateful_checking_callback fn);
  SERVER_FRAME_API void remove_on_ready_checking();

  /**
   * @brief 设置业务层状态检查函数
   *
   * @param fn 业务层检查函数,如果业务节点仍持有状态返回true，否则返回false
   */
  SERVER_FRAME_API void set_on_stateful_checking(on_stateful_checking_callback fn);
  SERVER_FRAME_API void remove_on_stateful_checking();

  /**
   * @brief 创建由HPA控制器统一管理的HTTP请求
   *
   * @param url URL
   * @return http_request 对象，可以后续继续进行其他设置
   */
  SERVER_FRAME_API util::network::http_request::ptr_t create_http_request(gsl::string_view url);

  /**
   * @brief 获取持有生命周期的atapp对象
   *
   * @return atapp对象
   */
  UTIL_FORCEINLINE const atapp::app* get_app() const noexcept { return owner_app_; }

  /**
   * @brief 获取持有生命周期的atapp对象
   *
   * @return atapp对象
   */
  UTIL_FORCEINLINE atapp::app* get_app() noexcept { return owner_app_; }

  /**
   * @brief 获取当前节点是否开启了Ready标签(就绪状态,当前分布)
   *
   * @return  有Ready标签返回true，否则返回false
   */
  SERVER_FRAME_API bool get_discovery_ready_tag() noexcept;

  /**
   * @brief 获取当前节点是否开启了Target标签(目标分布)
   *
   * @return  有Target标签返回true，否则返回false
   */
  SERVER_FRAME_API bool get_discovery_target_tag() noexcept;

  /**
   * @brief 生成自定义策略配置Key
   *
   * @param name discovery名
   * @param with_target_path 策略配置Key中是否包含Target Info信息
   * @return 自定义策略配置Key
   */
  SERVER_FRAME_API std::string make_custom_discovery_path(gsl::string_view name, bool with_target_path = true);

  /**
   * @brief 创建自定义discovery组件
   *
   * @param name discovery名
   * @param setup_callback 初始化/reload接口，请在这里面调用 reset_policy() 然后重新使用 add_pull_policy 添加所有策略。
   *                       reload后所有的policy会重建，如果不重新添加policy，discovery内的policy会指向老数据。
   * @param domain 自定义子域
   * @param with_target_path 生成的策略配置Key中是否包含Target Info信息
   * @return 自定义策略discovery
   */
  SERVER_FRAME_API std::shared_ptr<logic_hpa_discovery> create_custom_discovery(
      gsl::string_view name,
      std::function<void(logic_hpa_discovery&, logic_hpa_discovery_setup_policy_accessor&)> setup_callback,
      gsl::string_view domain = {}, bool with_target_path = true);

  /**
   * @brief 查找自定义discovery组件
   *
   * @param name discovery名
   * @param domain 自定义子域
   * @param with_target_path 策略配置Key中是否包含Target Info信息
   * @return 自定义策略discovery
   */
  SERVER_FRAME_API std::shared_ptr<logic_hpa_discovery> find_custom_discovery(gsl::string_view name,
                                                                              gsl::string_view domain = {},
                                                                              bool with_target_path = true);

  /**
   * @brief 移除自定义discovery组件
   *
   * @param discovery_instance discovery组件
   * @return 成功移除返回true
   */
  SERVER_FRAME_API bool remove_custom_discovery(const std::shared_ptr<logic_hpa_discovery>& discovery_instance);

  /**
   * @brief 获取默认的HTTP请求 Method
   *
   * @return HTTP Method
   */
  UTIL_FORCEINLINE util::network::http_request::method_t::type get_pull_http_method() const noexcept {
    return pull_http_method_;
  }

 private:
  struct hpa_discovery_data;
  struct hpa_discovery_data_accessor;
  friend struct hpa_discovery_data_accessor;

 private:
  void reload_runtime_stateful_pod_index();
  void reload_hpa_controller_metadata_filter();

  void do_reload_hpa_metrics();
  void do_reload_hpa_metrics_auto_inject_resource(std::unordered_set<std::string>& common_ignore_selectors);
  void do_reload_hpa_metrics_auto_inject_hpa_labels(std::unordered_set<std::string>& common_ignore_selectors);
  void do_reload_hpa_metrics_auto_inject_common_attributes(std::unordered_set<std::string>& common_ignore_selectors);

  util::nostd::nonnull<std::shared_ptr<logic_hpa_policy>> internal_create_hpa_policy(
      const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
      const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg, int64_t push_interval_seconds);

  void setup_cpu_permillage_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                   const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void setup_main_thread_cpu_permillage_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                               const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void setup_memory_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                           const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void setup_recent_max_task_count_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                          const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void setup_controller_status_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                      const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void cleanup_custom_policies();
  void setup_custom_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                           const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);

  void setup_stateful_index_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                   const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void cleanup_stateful_index_policy();

  void setup_expect_replicas_policy(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
  void cleanup_expect_replicas_policy();

  void setup_default_hpa_discovery(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg);
  void cleanup_default_hpa_discovery();

  void do_reload_hpa_configure();

  void do_reload_hpa_controller_tick(bool need_reload);

  void do_report_default_hpa_discovery();

  void do_update_default_hpa_settings(time_t sys_now, const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure);

  int32_t apply_default_hpa_scaling_up_expect_replicas(time_t sys_now,
                                                       const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure,
                                                       int32_t non_native_cloud_index, int32_t current_replicates,
                                                       int32_t expect_replicas);
  void reset_default_hpa_scaling_up_expect_replicas(time_t sys_now);

  int32_t calculate_default_hpa_scaling_up_replicas(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure,
                                                    int32_t non_native_cloud_index, int32_t current_replicates,
                                                    int32_t expect_replicas);

  int32_t apply_default_hpa_scaling_down_expect_replicas(
      time_t sys_now, const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure, int32_t non_native_cloud_index,
      int32_t current_replicates, int32_t expect_replicas);

  void reset_default_hpa_scaling_down_expect_replicas(time_t sys_now);

  int32_t calculate_default_hpa_scaling_down_replicas(const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_configure,
                                                      int32_t non_native_cloud_index, int32_t current_replicates,
                                                      int32_t expect_replicas);

  static void apply_default_hpa_discovery_next_submit_timepoint_from_current_setting(
      hpa_discovery_data& hpa_discovery_data, time_t sys_now);

  void setup_hpa_controller();
  void cleanup_hpa_controller();

  bool is_main_hpa_controller() const noexcept;

 private:
  atapp::app* owner_app_;
  bool need_configure_;
  bool available_;
  time_t last_tick_timepoint_;
  logic_hpa_ssl_version pull_ssl_version_;
  util::network::http_request::method_t::type pull_http_method_;
  util::network::http_request::curl_multi_context_ptr_type multi_curl_handle_;

  std::shared_ptr<rpc::telemetry::group_type> telemetry_group_;
  std::unordered_map<std::string, std::string> common_attributes_lifetime_;
  std::unordered_map<std::string, opentelemetry::common::AttributeValue> common_attributes_reference_;
  std::unordered_map<std::string, std::string> common_selectors_;

  struct policy_callback_data {
    on_setup_policy_callback on_setup;
    on_setup_policy_callback on_cleanup;
  };

  std::shared_ptr<logic_hpa_policy> policy_cpu_permillage_;
  std::shared_ptr<logic_hpa_policy> policy_main_thread_cpu_permillage_;
  std::shared_ptr<logic_hpa_policy> policy_memory_;
  std::shared_ptr<logic_hpa_policy> policy_recent_max_task_count_;
  std::shared_ptr<logic_hpa_policy> policy_controller_status_;
  std::unordered_map<std::string, std::shared_ptr<logic_hpa_policy>> policy_custom_;

  policy_callback_data policy_callback_cpu_permillage_;
  policy_callback_data policy_callback_main_thread_cpu_permillage_;
  policy_callback_data policy_callback_memory_;
  policy_callback_data policy_callback_recent_max_task_count_;
  policy_callback_data policy_callback_controller_status_;
  std::unordered_map<std::string, policy_callback_data> policy_callback_custom_;

  std::shared_ptr<hpa_discovery_data> hpa_discovery_data_;
  on_stateful_checking_callback hpa_ready_checking_callback_;
  on_stateful_checking_callback hpa_stateful_checking_callback_;
  std::shared_ptr<logic_hpa_policy> controller_policy_stateful_index_;
  std::shared_ptr<logic_hpa_policy> controller_policy_expect_replicas_;
};

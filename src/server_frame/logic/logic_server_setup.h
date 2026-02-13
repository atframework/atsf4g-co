// Copyright 2021 atframework
// Created by owent

#pragma once

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

#  include <WinSock2.h>
#endif

#include <uv.h>

#include <config/compiler_features.h>

#include <gsl/select-gsl.h>

#include <atframe/atapp_module_impl.h>
#include <atframe/etcdcli/etcd_discovery.h>
#include <atframe/modules/etcd_module.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/svr.global.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>
#include <config/server_frame_build_feature.h>

#include <opentelemetry/common/attribute_value.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>

class logic_server_common_module;
class logic_hpa_controller;

struct ATFW_UTIL_SYMBOL_VISIBLE logic_server_common_module_configure {
  inline logic_server_common_module_configure() noexcept {}
};

/**
 * @brief 初始化公共模块
 *
 * @param app 要插入的atapp实例
 * @param conf 配置选项
 * @return SERVER_FRAME_API
 */
SERVER_FRAME_API int logic_server_setup_common(atfw::atapp::app& app, const logic_server_common_module_configure& conf);

/**
 * @brief 获取公共模块
 * @note 使用前一定要判NULL,在进程stop过程中，模块将会被关闭．此时该函数返回 NULL
 * @return 初始化后,stop前,返回模块地址,否则返回NULL
 */
SERVER_FRAME_API logic_server_common_module* logic_server_last_common_module();

struct ATFW_UTIL_SYMBOL_VISIBLE logic_server_timer {
  std::chrono::system_clock::time_point timeout;
  uint64_t task_id;
  uintptr_t message_type;
  uint64_t sequence;

  friend inline bool operator<(const logic_server_timer& lhs, const logic_server_timer& rhs) noexcept {
    if (lhs.timeout != rhs.timeout) {
      return lhs.timeout < rhs.timeout;
    }

    return lhs.task_id < rhs.task_id;
  }
};

struct logic_server_type_discovery_set_t {
  atfw::atapp::etcd_discovery_set::ptr_t all_index;
  std::unordered_map<uint64_t, atfw::atapp::etcd_discovery_set::ptr_t> zone_index;
};

class logic_server_common_module : public atfw::atapp::module_impl {
 public:
  using etcd_keepalive_ptr_t = std::shared_ptr<atfw::atapp::etcd_keepalive>;
  using etcd_watcher_ptr_t = std::shared_ptr<atfw::atapp::etcd_watcher>;

  struct stats_data_t {
    // cross thread
    std::atomic<uint64_t> collect_sequence;
    std::atomic<int64_t> collect_max_tick_interval_us;
    std::atomic<int64_t> collect_cpu_sys;
    std::atomic<int64_t> collect_cpu_user;
    std::atomic<uint64_t> collect_memory_max_rss;
    std::atomic<size_t> collect_memory_rss;

    // main thread
    time_t last_update_usage_timepoint;
    uint64_t last_collect_sequence;
    uv_rusage_t last_checkpoint_usage;
    std::chrono::system_clock::time_point last_checkpoint;
    std::chrono::system_clock::time_point previous_tick_checkpoint;
  };

 public:
  SERVER_FRAME_API explicit logic_server_common_module(const logic_server_common_module_configure& static_conf);
  SERVER_FRAME_API ~logic_server_common_module();

  SERVER_FRAME_API int init() override;

  SERVER_FRAME_API void ready() override;

  SERVER_FRAME_API int reload() override;

  SERVER_FRAME_API int stop() override;

  SERVER_FRAME_API int timeout() override;

  SERVER_FRAME_API void cleanup() override;

  SERVER_FRAME_API const char* name() const override;

  SERVER_FRAME_API int tick() override;

  SERVER_FRAME_API int debug_stop_app();

  SERVER_FRAME_API bool is_closing() const noexcept;

  SERVER_FRAME_API bool is_runtime_active() const noexcept;

  SERVER_FRAME_API void setup_hpa_controller();

  SERVER_FRAME_API atfw::atapp::etcd_cluster* get_etcd_cluster();

  SERVER_FRAME_API std::shared_ptr<::atfw::atapp::etcd_module> get_etcd_module();

  /**
   * @brief 添加自定义的etcd keepalive 数据
   * @param path etcd可以的路径，请确保有权限
   * @param value 值，如果留空则会填入服务器信息，并传出
   * @return 成功返回keepalive对象，失败返回 nullptr
   */
  SERVER_FRAME_API etcd_keepalive_ptr_t add_keepalive(const std::string& path, std::string& value);

  /**
   * @brief 获取指定服务类型的服务发现版本号（本地）
   *
   * @param service_type_id 服务类型ID
   * @return （本地）服务发现版本号
   */
  SERVER_FRAME_API int64_t
  get_service_discovery_version(atframework::component::logic_service_type service_type_id) const noexcept;

  SERVER_FRAME_API void update_remote_server_configure(const std::string& global_conf, int32_t global_version,
                                                       const std::string& zone_conf, int32_t zone_version);

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::table_service_configure_data& get_remote_server_configure()
      const noexcept {
    return server_remote_conf_;
  }

  SERVER_FRAME_API void insert_timer(uint64_t task_id, std::chrono::system_clock::duration timeout,
                                     logic_server_timer& output);

  SERVER_FRAME_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type(uint64_t type_id) const;
  SERVER_FRAME_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type(
      const std::string& type_name) const;
  SERVER_FRAME_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type_zone(uint64_t type_id,
                                                                                           uint64_t zone_id) const;
  SERVER_FRAME_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type_zone(const std::string& type_name,
                                                                                           uint64_t zone_id) const;
  SERVER_FRAME_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_zone(uint64_t zone_id) const;
  ATFW_UTIL_FORCEINLINE const std::unordered_map<uint64_t, atfw::atapp::etcd_discovery_set::ptr_t>&
  get_origin_zone_index() const noexcept {
    return service_zone_index_;
  }

  SERVER_FRAME_API atfw::util::memory::strong_rc_ptr<atfw::atapp::etcd_discovery_node> get_discovery_by_id(
      uint64_t id) const;
  SERVER_FRAME_API atfw::util::memory::strong_rc_ptr<atfw::atapp::etcd_discovery_node> get_discovery_by_name(
      const std::string& name) const;

  /**
   * @brief 获取HPA控制器
   *
   * @return HPA控制器
   */
  ATFW_UTIL_FORCEINLINE const std::shared_ptr<logic_hpa_controller>& get_hpa_controller() { return hpa_controller_; }

 private:
  int setup_etcd_event_handle();

  int tick_update_remote_configures();
  void tick_stats();

  void setup_metrics();

  void add_service_type_id_index(const atfw::atapp::etcd_discovery_node::ptr_t& node);
  void remove_service_type_id_index(const atfw::atapp::etcd_discovery_node::ptr_t& node);
  void add_service_type_name_index(const atfw::atapp::etcd_discovery_node::ptr_t& node);
  void remove_service_type_name_index(const atfw::atapp::etcd_discovery_node::ptr_t& node);
  void add_service_zone_index(const atfw::atapp::etcd_discovery_node::ptr_t& node);
  void remove_service_zone_index(const atfw::atapp::etcd_discovery_node::ptr_t& node);

 private:
  logic_server_common_module_configure static_conf_;
  time_t stop_log_timepoint_;
  PROJECT_NAMESPACE_ID::config::logic_server_shared_component_cfg shared_component_;

  // stat
  std::shared_ptr<stats_data_t> stats_;

  mutable std::unordered_map<int32_t, int64_t> service_discovery_version_;

  std::unordered_map<uint64_t, logic_server_type_discovery_set_t> service_type_id_index_;
  std::unordered_map<std::string, logic_server_type_discovery_set_t> service_type_name_index_;
  std::unordered_map<uint64_t, atfw::atapp::etcd_discovery_set::ptr_t> service_zone_index_;
  std::unique_ptr<atfw::atapp::etcd_module::node_event_callback_handle_t> service_index_handle_;

  PROJECT_NAMESPACE_ID::table_service_configure_data server_remote_conf_;
  int32_t server_remote_conf_global_version_;
  int32_t server_remote_conf_zone_version_;
  time_t server_remote_conf_next_update_time_;

  std::priority_queue<logic_server_timer> task_timer_;
  std::shared_ptr<logic_hpa_controller> hpa_controller_;
};

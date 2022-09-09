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
#include <std/smart_ptr.h>

#include <atframe/atapp_module_impl.h>
#include <atframe/etcdcli/etcd_discovery.h>
#include <atframe/modules/etcd_module.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/svr.global.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <opentelemetry/common/attribute_value.h>

#include <stdint.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <functional>
#include <list>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class logic_server_common_module;

struct logic_server_common_module_configure {
  bool enable_watch_battlesvr;

  logic_server_common_module_configure();
};

int logic_server_setup_common(atapp::app& app, const logic_server_common_module_configure& conf);

/**
 * @brief 获取共用模块
 * @note 使用前一定要判NULL,在进程stop过程中，模块将会被关闭．此时该函数返回 NULL
 * @return 初始化后,stop前,返回模块地址,否则返回NULL
 */
logic_server_common_module* logic_server_last_common_module();

struct logic_server_timer {
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
  atapp::etcd_discovery_set::ptr_t all_index;
  std::unordered_map<uint64_t, atapp::etcd_discovery_set::ptr_t> zone_index;
};

class logic_server_common_module : public atapp::module_impl {
 public:
  using etcd_keepalive_ptr_t = std::shared_ptr<atapp::etcd_keepalive>;
  using etcd_watcher_ptr_t = std::shared_ptr<atapp::etcd_watcher>;

  struct battle_service_node_t {
    uint64_t server_id;
    std::string version;

    bool operator==(const battle_service_node_t& other) const;
  };

  struct battle_service_node_hash_t {
    size_t operator()(const battle_service_node_t& in) const;
  };

  using battle_service_set_t = std::unordered_set<battle_service_node_t, battle_service_node_hash_t>;
  using battle_service_version_map_t = std::unordered_map<std::string, battle_service_set_t>;
  using battle_service_id_map_t = std::unordered_map<uint64_t, battle_service_node_t>;

  struct stats_data_t {
    // cross thread
    std::atomic<bool> need_setup;
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
  explicit logic_server_common_module(const logic_server_common_module_configure& static_conf);
  ~logic_server_common_module();

  int init() override;

  void ready() override;

  int reload() override;

  int stop() override;

  int timeout() override;

  void cleanup() override;

  const char* name() const override;

  int tick() override;

  int debug_stop_app();

  bool is_closing() const noexcept;

  atapp::etcd_cluster* get_etcd_cluster();
  std::shared_ptr<::atapp::etcd_module> get_etcd_module();

  /**
   * @brief 添加自定义的etcd keepalive 数据
   * @param path etcd可以的路径，请确保有权限
   * @param value 值，如果留空则会填入服务器信息，并传出
   * @return 成功返回keepalive对象，失败返回 nullptr
   */
  etcd_keepalive_ptr_t add_keepalive(const std::string& path, std::string& value);

  std::string make_battle_etcd_version_path(const std::string& version) const;
  static bool parse_battle_etcd_version_path(const std::string& path, std::string& version, uint64_t& svr_id);

  void add_battlesvr_index(const battle_service_node_t& node);
  void remove_battlesvr_index(uint64_t server_id);
  const battle_service_set_t* get_battlesvr_set_by_version(const std::string& version) const;
  inline const battle_service_id_map_t& get_battlesvr_set_all() const { return battle_service_id_; }
  const battle_service_version_map_t& get_battlesvr_set_all_by_version() const { return battle_service_version_map_; }
  inline int64_t get_cachesvr_discovery_version() const { return cachesvr_discovery_version_; }

  void update_remote_server_configure(const std::string& global_conf, int32_t global_version,
                                      const std::string& zone_conf, int32_t zone_version);
  inline const PROJECT_NAMESPACE_ID::table_service_configure_data& get_remote_server_configure() const noexcept {
    return server_remote_conf_;
  }

  void insert_timer(uint64_t task_id, std::chrono::system_clock::duration timeout, logic_server_timer& output);

  atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type(uint64_t type_id) const;
  atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type(const std::string& type_name) const;
  atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type_zone(uint64_t type_id, uint64_t zone_id) const;
  atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type_zone(const std::string& type_name,
                                                                    uint64_t zone_id) const;
  atapp::etcd_discovery_set::ptr_t get_discovery_index_by_zone(uint64_t zone_id) const;
  inline const std::unordered_map<uint64_t, atapp::etcd_discovery_set::ptr_t>& get_origin_zone_index() const noexcept {
    return service_zone_index_;
  }

  std::shared_ptr<atapp::etcd_discovery_node> get_discovery_by_id(uint64_t id) const;
  std::shared_ptr<atapp::etcd_discovery_node> get_discovery_by_name(const std::string& name) const;

 private:
  int setup_battle_service_watcher();
  int setup_etcd_event_handle();

  int tick_update_remote_configures();
  void tick_stats();

  void setup_metrics_tick();
  void setup_metrics_cpu_sys();
  void setup_metrics_cpu_user();
  void setup_metrics_memory_maxrss();
  void setup_metrics_memory_rss();

  void add_service_type_id_index(const atapp::etcd_discovery_node::ptr_t& node);
  void remove_service_type_id_index(const atapp::etcd_discovery_node::ptr_t& node);
  void add_service_type_name_index(const atapp::etcd_discovery_node::ptr_t& node);
  void remove_service_type_name_index(const atapp::etcd_discovery_node::ptr_t& node);
  void add_service_zone_index(const atapp::etcd_discovery_node::ptr_t& node);
  void remove_service_zone_index(const atapp::etcd_discovery_node::ptr_t& node);

 private:
  logic_server_common_module_configure static_conf_;
  time_t stop_log_timepoint_;
  PROJECT_NAMESPACE_ID::config::logic_server_shared_component_cfg shared_component_;

  // stat
  std::shared_ptr<stats_data_t> stats_;

  battle_service_version_map_t battle_service_version_map_;
  battle_service_id_map_t battle_service_id_;
  etcd_watcher_ptr_t battle_service_watcher_;
  bool etcd_event_handle_registered_;
  int64_t cachesvr_discovery_version_;

  std::unordered_map<uint64_t, logic_server_type_discovery_set_t> service_type_id_index_;
  std::unordered_map<std::string, logic_server_type_discovery_set_t> service_type_name_index_;
  std::unordered_map<uint64_t, atapp::etcd_discovery_set::ptr_t> service_zone_index_;
  std::unique_ptr<atapp::etcd_module::node_event_callback_handle_t> service_index_handle_;

  PROJECT_NAMESPACE_ID::table_service_configure_data server_remote_conf_;
  int32_t server_remote_conf_global_version_;
  int32_t server_remote_conf_zone_version_;
  time_t server_remote_conf_next_update_time_;

  std::priority_queue<logic_server_timer> task_timer_;
};

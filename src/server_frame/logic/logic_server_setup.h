// Copyright 2021 atframework
// Created by owent

#ifndef LOGIC_SERVER_SETUP_H
#define LOGIC_SERVER_SETUP_H

#pragma once

#include <config/compiler_features.h>

#include <gsl/select-gsl.h>
#include <std/smart_ptr.h>

#include <atframe/atapp_module_impl.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.global.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <stdint.h>

#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace atapp {
class etcd_module;
class etcd_keepalive;
class etcd_watcher;
class etcd_cluster;
}  // namespace atapp

class logic_server_common_module;

struct logic_server_common_module_conf_t {
  bool enable_watch_battlesvr;

  inline logic_server_common_module_conf_t() : enable_watch_battlesvr(false) {}
};

int logic_server_setup_common(atapp::app& app, const logic_server_common_module_conf_t& conf);

/**
 * @brief 获取共用模块
 * @note 使用前一定要判NULL,在进程stop过程中，模块将会被关闭．此时该函数返回 NULL
 * @return 初始化后,stop前,返回模块地址,否则返回NULL
 */
logic_server_common_module* logic_server_last_common_module();

class logic_server_common_module : public atapp::module_impl {
 public:
  typedef std::shared_ptr<atapp::etcd_keepalive> etcd_keepalive_ptr_t;
  typedef std::shared_ptr<atapp::etcd_watcher> etcd_watcher_ptr_t;

  struct battle_service_node_t {
    uint64_t server_id;
    std::string version;

    bool operator==(const battle_service_node_t& other) const;
  };

  struct battle_service_node_hash_t {
    size_t operator()(const battle_service_node_t& in) const;
  };

  typedef std::unordered_set<battle_service_node_t, battle_service_node_hash_t> battle_service_set_t;
  typedef std::unordered_map<std::string, battle_service_set_t> battle_service_version_map_t;
  typedef std::unordered_map<uint64_t, battle_service_node_t> battle_service_id_map_t;

 public:
  explicit logic_server_common_module(const logic_server_common_module_conf_t& static_conf);
  ~logic_server_common_module();

  int init() override;

  void ready() override;

  int reload() override;

  int stop() override;

  int timeout() override;

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
  inline const PROJECT_SERVER_FRAME_NAMESPACE_ID::table_service_configure_data& get_remote_server_configure()
      const noexcept {
    return server_remote_conf_;
  }

 private:
  int setup_battle_service_watcher();
  int setup_etcd_event_handle();

  int tick_update_remote_configures();

 private:
  logic_server_common_module_conf_t static_conf_;

  battle_service_version_map_t battle_service_version_map_;
  battle_service_id_map_t battle_service_id_;
  etcd_watcher_ptr_t battle_service_watcher_;
  bool etcd_event_handle_registered_;
  int64_t cachesvr_discovery_version_;

  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_service_configure_data server_remote_conf_;
  int32_t server_remote_conf_global_version_;
  int32_t server_remote_conf_zone_version_;
  time_t server_remote_conf_next_update_time_;
};

#endif

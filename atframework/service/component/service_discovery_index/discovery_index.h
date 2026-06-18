// Copyright 2026 atframework.

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>
#include <nostd/nullability.h>

#include <atframe/etcdcli/etcd_discovery.h>
#include <atframe/modules/service_discovery_module.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "config/atframe_service_component_config.h"

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN

using service_discovery_index_by_type_id_t = std::unordered_map<uint64_t, atfw::atapp::etcd_discovery_set::ptr_t>;
using service_discovery_index_by_type_name_t = std::unordered_map<std::string, atfw::atapp::etcd_discovery_set::ptr_t>;

struct service_discovery_index_set_t {
  service_discovery_index_by_type_id_t by_type_id;
  service_discovery_index_by_type_name_t by_type_name;
};

struct service_discovery_index_realm_data_t {
  atfw::atapp::etcd_discovery_set::ptr_t node_set;
  service_discovery_index_set_t index;
};

using service_discovery_index_by_realm_t = std::unordered_map<uint64_t, service_discovery_index_realm_data_t>;

struct service_discovery_node_snapshot_t {
  uint64_t id = 0;
  std::string name;
  uint64_t type_id = 0;
  std::string type_name;
  uint64_t realm_id = 0;
};

using service_discovery_snapshot_by_id_t = std::unordered_map<uint64_t, service_discovery_node_snapshot_t>;
using service_discovery_snapshot_by_name_t = std::unordered_map<std::string, service_discovery_node_snapshot_t>;

class service_discovery_index {
 public:
  using ptr_t = std::shared_ptr<service_discovery_index>;
  using discovery_node_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::atapp::etcd_discovery_node>;

  using etcd_keepalive_ptr_t = std::shared_ptr<atfw::atapp::etcd_keepalive>;
  using etcd_watcher_ptr_t = std::shared_ptr<atfw::atapp::etcd_watcher>;

  UTIL_DESIGN_PATTERN_NOMOVABLE(service_discovery_index);
  UTIL_DESIGN_PATTERN_NOCOPYABLE(service_discovery_index);

  struct ctor_guard_t;

 public:
  service_discovery_index(ctor_guard_t&);

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API static ptr_t create(
      const std::shared_ptr<::atfw::atapp::service_discovery_module>& service_discovery_module);

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API ~service_discovery_index();

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API void initialize();

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API void reload();

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API void cleanup();

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type(
      uint64_t type_id) const noexcept;

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_type(
      const std::string& type_name) const noexcept;

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_realm_type(
      uint64_t realm_id, uint64_t type_id) const noexcept;

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_realm_type(
      uint64_t realm_id, const std::string& type_name) const noexcept;

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t get_discovery_index_by_realm(
      uint64_t realm_id) const noexcept;

  ATFW_UTIL_FORCEINLINE const service_discovery_index_by_realm_t& get_origin_zone_index() const noexcept {
    return index_by_realm_;
  }

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API discovery_node_ptr_t get_discovery_by_id(uint64_t id) const noexcept;

  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API discovery_node_ptr_t
  get_discovery_by_name(const std::string& name) const noexcept;

  /**
   * @brief 添加自定义的etcd keepalive 数据
   * @param path etcd可以的路径，请确保有权限
   * @param value 值，如果留空则会填入服务器信息，并传出
   * @return 成功返回keepalive对象，失败返回 nullptr
   */
  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API etcd_keepalive_ptr_t add_keepalive(const std::string& path,
                                                                             std::string& value);

  /**
   * @brief 获取指定服务类型的服务发现版本号（本地）
   *        仅在收到真实的服务发现 PUT/DELETE 变更时递增，initialize/reload 的本地回放不会递增
   *
   * @param service_type_id 服务类型ID
   * @return （本地）服务发现版本号
   */
  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API int64_t
  get_service_discovery_version(uint64_t service_type_id) const noexcept;

  /**
   * @brief 获取指定服务类型的服务发现版本号（本地）
   *        仅在收到真实的服务发现 PUT/DELETE 变更时递增，initialize/reload 的本地回放不会递增
   *
   * @param service_type_name 服务类型名称
   * @return （本地）服务发现版本号
   */
  ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API int64_t
  get_service_discovery_version(const std::string& service_type_name) const noexcept;

 private:
  void setup_etcd_event_handle();
  void reset_local_cache(bool reset_version) noexcept;
  void apply_node_event(atfw::atapp::service_discovery_module::node_action_t action_type,
                        const atfw::atapp::etcd_discovery_node::ptr_t& node,
                        std::unordered_set<uint64_t>& bump_service_discovery_version_by_type_id,
                        std::unordered_set<std::string>& bump_service_discovery_version_by_type_name);
  void bump_service_discovery_version(uint64_t service_type_id, bool update_version) const noexcept;
  void bump_service_discovery_version(const std::string& service_type_name, bool update_version) const noexcept;

  static service_discovery_node_snapshot_t make_node_snapshot(const atfw::atapp::etcd_discovery_node::ptr_t& node);
  bool get_node_snapshot(uint64_t id, const std::string& name,
                         service_discovery_node_snapshot_t& output) const noexcept;
  void set_node_snapshot(const service_discovery_node_snapshot_t& snapshot);
  void remove_node_snapshot(const service_discovery_node_snapshot_t& snapshot);
  void remove_node_snapshot(uint64_t id, const std::string& name);

  static void add_service_type_id_index(service_discovery_index_set_t& index_set,
                                        const atfw::atapp::etcd_discovery_node::ptr_t& node);
  static void remove_service_type_id_index(service_discovery_index_set_t& index_set,
                                           const atfw::atapp::etcd_discovery_node::ptr_t& node);
  static void remove_service_type_id_index(service_discovery_index_set_t& index_set, uint64_t type_id, uint64_t node_id,
                                           const std::string& node_name);
  static void add_service_type_name_index(service_discovery_index_set_t& index_set,
                                          const atfw::atapp::etcd_discovery_node::ptr_t& node);
  static void remove_service_type_name_index(service_discovery_index_set_t& index_set,
                                             const atfw::atapp::etcd_discovery_node::ptr_t& node);
  static void remove_service_type_name_index(service_discovery_index_set_t& index_set, const std::string& type_name,
                                             uint64_t node_id, const std::string& node_name);

  static void add_service_realm_index(service_discovery_index_by_realm_t& index_set, uint64_t realm_id,
                                      const atfw::atapp::etcd_discovery_node::ptr_t& node);
  static void remove_service_realm_index(service_discovery_index_by_realm_t& index_set, uint64_t realm_id,
                                         const atfw::atapp::etcd_discovery_node::ptr_t& node);
  static void remove_service_realm_index(service_discovery_index_by_realm_t& index_set, uint64_t realm_id,
                                         uint64_t type_id, const std::string& type_name, uint64_t node_id,
                                         const std::string& node_name);

 private:
  bool initialized_;
  atfw::util::nostd::nonnull<std::shared_ptr<::atfw::atapp::service_discovery_module>> service_discovery_module_;

  mutable std::unordered_map<uint64_t, int64_t> service_discovery_version_by_type_id_;
  mutable std::unordered_map<std::string, int64_t> service_discovery_version_by_type_name_;
  mutable std::unordered_set<uint64_t> service_discovery_dirty_by_type_id_;
  mutable std::unordered_set<std::string> service_discovery_dirty_by_type_name_;

  atfw::util::nostd::nonnull<atfw::atapp::etcd_discovery_set::ptr_t> all_nodes_;
  service_discovery_snapshot_by_id_t snapshot_by_id_;
  service_discovery_snapshot_by_name_t snapshot_by_name_;
  service_discovery_index_set_t index_all_;
  service_discovery_index_by_realm_t index_by_realm_;

  atfw::util::nostd::nullable<std::unique_ptr<atfw::atapp::service_discovery_module::node_event_callback_handle_t>>
      service_index_handle_;
};

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END

// Copyright 2026 atframework.

#include "service_discovery_index/discovery_index.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>
#include <cstdint>
#include "memory/rc_ptr.h"

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN

struct service_discovery_index::ctor_guard_t {
  std::shared_ptr<::atfw::atapp::etcd_module> etcd_module;
};

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API service_discovery_index::ptr_t service_discovery_index::create(
    const std::shared_ptr<::atfw::atapp::etcd_module>& etcd_module) {
  if (!etcd_module) {
    return {};
  }

  ctor_guard_t guard;
  guard.etcd_module = etcd_module;
  return std::make_shared<service_discovery_index>(guard);
}

service_discovery_index::service_discovery_index(ctor_guard_t& ctor_guard)
    : initialized_(false),
      etcd_module_(std::move(ctor_guard.etcd_module)),
      all_nodes_(atfw::util::memory::make_strong_rc<atfw::atapp::etcd_discovery_set>()) {}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API service_discovery_index::~service_discovery_index() { cleanup(); }

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API void service_discovery_index::initialize() {
  setup_etcd_event_handle();

  initialized_ = true;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API void service_discovery_index::reload() {
  if (!initialized_) {
    return;
  }

  setup_etcd_event_handle();
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API void service_discovery_index::cleanup() {
  initialized_ = false;

  if (service_index_handle_) {
    etcd_module_->remove_on_node_event(*service_index_handle_);
    service_index_handle_.reset();
  }

  reset_local_cache(true);
}

void service_discovery_index::reset_local_cache(bool reset_version) noexcept {
  index_all_.by_type_id.clear();
  index_all_.by_type_name.clear();
  index_by_realm_.clear();
  all_nodes_ = atfw::util::memory::make_strong_rc<atfw::atapp::etcd_discovery_set>();
  snapshot_by_id_.clear();
  snapshot_by_name_.clear();
  if (reset_version) {
    service_discovery_version_by_type_id_.clear();
    service_discovery_version_by_type_name_.clear();
    service_discovery_dirty_by_type_id_.clear();
    service_discovery_dirty_by_type_name_.clear();
  }
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t
service_discovery_index::get_discovery_index_by_type(uint64_t type_id) const noexcept {
  if (type_id == 0) {
    return nullptr;
  }

  auto iter = index_all_.by_type_id.find(type_id);
  if (iter != index_all_.by_type_id.end()) {
    return iter->second;
  }

  return nullptr;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t
service_discovery_index::get_discovery_index_by_type(const std::string& type_name) const noexcept {
  if (type_name.empty()) {
    return nullptr;
  }

  auto iter = index_all_.by_type_name.find(type_name);
  if (iter != index_all_.by_type_name.end()) {
    return iter->second;
  }

  return nullptr;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t
service_discovery_index::get_discovery_index_by_realm_type(uint64_t realm_id, uint64_t type_id) const noexcept {
  if (type_id == 0) {
    return nullptr;
  }

  auto realm_iter = index_by_realm_.find(realm_id);
  if (realm_iter == index_by_realm_.end()) {
    return nullptr;
  }

  auto type_iter = realm_iter->second.index.by_type_id.find(type_id);
  if (type_iter != realm_iter->second.index.by_type_id.end()) {
    return type_iter->second;
  }

  return nullptr;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t
service_discovery_index::get_discovery_index_by_realm_type(uint64_t realm_id,
                                                           const std::string& type_name) const noexcept {
  if (type_name.empty()) {
    return nullptr;
  }

  auto realm_iter = index_by_realm_.find(realm_id);
  if (realm_iter == index_by_realm_.end()) {
    return nullptr;
  }

  auto type_iter = realm_iter->second.index.by_type_name.find(type_name);
  if (type_iter != realm_iter->second.index.by_type_name.end()) {
    return type_iter->second;
  }

  return nullptr;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::atapp::etcd_discovery_set::ptr_t
service_discovery_index::get_discovery_index_by_realm(uint64_t realm_id) const noexcept {
  auto realm_iter = index_by_realm_.find(realm_id);
  if (realm_iter == index_by_realm_.end()) {
    return nullptr;
  }

  return realm_iter->second.node_set;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API service_discovery_index::discovery_node_ptr_t
service_discovery_index::get_discovery_by_id(uint64_t id) const noexcept {
  return all_nodes_->get_node_by_id(id);
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API service_discovery_index::discovery_node_ptr_t
service_discovery_index::get_discovery_by_name(const std::string& name) const noexcept {
  return all_nodes_->get_node_by_name(name);
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API service_discovery_index::etcd_keepalive_ptr_t
service_discovery_index::add_keepalive(const std::string& path, std::string& value) {
  if (path.empty()) {
    return nullptr;
  }

  etcd_keepalive_ptr_t ret = etcd_module_->add_keepalive_actor(value, path);
  if (!ret) {
    FWLOGERROR("add keepalive {}={} failed", path, value);
  }

  return ret;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API int64_t
service_discovery_index::get_service_discovery_version(uint64_t service_type_id) const noexcept {
  if (service_discovery_dirty_by_type_id_.end() != service_discovery_dirty_by_type_id_.find(service_type_id)) {
    bump_service_discovery_version(service_type_id, true);
  }

  auto iter = service_discovery_version_by_type_id_.find(service_type_id);
  if (iter != service_discovery_version_by_type_id_.end()) {
    return iter->second;
  }

  int64_t init_version =
      (atfw::util::time::time_utility::get_sys_now() * 1000) + (atfw::util::time::time_utility::get_now_usec() / 1000);
  service_discovery_version_by_type_id_.emplace(service_type_id, init_version);

  return init_version;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API int64_t
service_discovery_index::get_service_discovery_version(const std::string& service_type_name) const noexcept {
  if (service_discovery_dirty_by_type_name_.end() != service_discovery_dirty_by_type_name_.find(service_type_name)) {
    bump_service_discovery_version(service_type_name, true);
  }

  auto iter = service_discovery_version_by_type_name_.find(service_type_name);
  if (iter != service_discovery_version_by_type_name_.end()) {
    return iter->second;
  }

  int64_t init_version =
      (atfw::util::time::time_utility::get_sys_now() * 1000) + (atfw::util::time::time_utility::get_now_usec() / 1000);
  service_discovery_version_by_type_name_.emplace(service_type_name, init_version);

  return init_version;
}

service_discovery_node_snapshot_t service_discovery_index::make_node_snapshot(
    const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  service_discovery_node_snapshot_t ret;
  if (!node) {
    return ret;
  }

  ret.id = node->get_discovery_info().id();
  ret.name = node->get_discovery_info().name();
  ret.type_id = node->get_discovery_info().type_id();
  ret.type_name = node->get_discovery_info().type_name();
  ret.realm_id = node->get_discovery_info().area().zone_id();
  return ret;
}

bool service_discovery_index::get_node_snapshot(uint64_t id, const std::string& name,
                                                service_discovery_node_snapshot_t& output) const noexcept {
  if (id != 0) {
    auto iter = snapshot_by_id_.find(id);
    if (iter != snapshot_by_id_.end()) {
      output = iter->second;
      return true;
    }
  }

  if (!name.empty()) {
    auto iter = snapshot_by_name_.find(name);
    if (iter != snapshot_by_name_.end()) {
      output = iter->second;
      return true;
    }
  }

  return false;
}

void service_discovery_index::set_node_snapshot(const service_discovery_node_snapshot_t& snapshot) {
  remove_node_snapshot(snapshot.id, snapshot.name);

  if (snapshot.id != 0) {
    snapshot_by_id_[snapshot.id] = snapshot;
  }
  if (!snapshot.name.empty()) {
    snapshot_by_name_[snapshot.name] = snapshot;
  }
}

void service_discovery_index::remove_node_snapshot(const service_discovery_node_snapshot_t& snapshot) {
  if (snapshot.id != 0) {
    snapshot_by_id_.erase(snapshot.id);
  }
  if (!snapshot.name.empty()) {
    snapshot_by_name_.erase(snapshot.name);
  }
}

void service_discovery_index::remove_node_snapshot(uint64_t id, const std::string& name) {
  service_discovery_node_snapshot_t snapshot;
  if (get_node_snapshot(id, name, snapshot)) {
    remove_node_snapshot(snapshot);
    return;
  }

  if (id != 0) {
    snapshot_by_id_.erase(id);
  }
  if (!name.empty()) {
    snapshot_by_name_.erase(name);
  }
}

void service_discovery_index::bump_service_discovery_version(uint64_t service_type_id,
                                                             bool update_version) const noexcept {
  if (service_type_id == 0) {
    return;
  }

  auto iter = service_discovery_version_by_type_id_.find(service_type_id);
  if (iter == service_discovery_version_by_type_id_.end()) {
    int64_t init_version = (atfw::util::time::time_utility::get_sys_now() * 1000) +
                           (atfw::util::time::time_utility::get_now_usec() / 1000);
    iter = service_discovery_version_by_type_id_.emplace(service_type_id, init_version).first;
  } else if (update_version) {
    ++iter->second;
  }

  service_discovery_dirty_by_type_id_.erase(service_type_id);
}

void service_discovery_index::bump_service_discovery_version(const std::string& service_type_name,
                                                             bool update_version) const noexcept {
  if (service_type_name.empty()) {
    return;
  }

  auto iter = service_discovery_version_by_type_name_.find(service_type_name);
  if (iter == service_discovery_version_by_type_name_.end()) {
    int64_t init_version = (atfw::util::time::time_utility::get_sys_now() * 1000) +
                           (atfw::util::time::time_utility::get_now_usec() / 1000);
    iter = service_discovery_version_by_type_name_.emplace(service_type_name, init_version).first;
  } else if (update_version) {
    ++iter->second;
  }

  service_discovery_dirty_by_type_name_.erase(service_type_name);
}

void service_discovery_index::apply_node_event(
    atfw::atapp::etcd_module::node_action_t action_type, const atfw::atapp::etcd_discovery_node::ptr_t& node,
    std::unordered_set<uint64_t>& bump_service_discovery_version_by_type_id,
    std::unordered_set<std::string>& bump_service_discovery_version_by_type_name) {
  if (!node) {
    return;
  }

  switch (action_type) {
    case atfw::atapp::etcd_module::node_action_t::kPut: {
      service_discovery_node_snapshot_t old_snapshot;
      bool has_old_snapshot =
          get_node_snapshot(node->get_discovery_info().id(), node->get_discovery_info().name(), old_snapshot);
      service_discovery_node_snapshot_t new_snapshot = make_node_snapshot(node);

      bool identity_changed =
          has_old_snapshot && (old_snapshot.id != new_snapshot.id || old_snapshot.name != new_snapshot.name);
      bool type_id_changed = has_old_snapshot && old_snapshot.type_id != new_snapshot.type_id;
      bool type_name_changed = has_old_snapshot && old_snapshot.type_name != new_snapshot.type_name;
      bool realm_changed = has_old_snapshot && old_snapshot.realm_id != new_snapshot.realm_id;

      if (has_old_snapshot && (identity_changed || type_id_changed) && old_snapshot.type_id != 0) {
        remove_service_type_id_index(index_all_, old_snapshot.type_id, old_snapshot.id, old_snapshot.name);
      }
      if (has_old_snapshot && (identity_changed || type_name_changed) && !old_snapshot.type_name.empty()) {
        remove_service_type_name_index(index_all_, old_snapshot.type_name, old_snapshot.id, old_snapshot.name);
      }

      if (has_old_snapshot && identity_changed) {
        if (old_snapshot.id != 0) {
          all_nodes_->remove_node(old_snapshot.id);
        } else if (!old_snapshot.name.empty()) {
          all_nodes_->remove_node(old_snapshot.name);
        }
      }

      if (0 != node->get_discovery_info().type_id()) {
        add_service_type_id_index(index_all_, node);
      }
      if (!node->get_discovery_info().type_name().empty()) {
        add_service_type_name_index(index_all_, node);
      }

      if (has_old_snapshot && (identity_changed || realm_changed || type_id_changed || type_name_changed) &&
          old_snapshot.realm_id != 0 && (old_snapshot.type_id != 0 || !old_snapshot.type_name.empty())) {
        remove_service_realm_index(index_by_realm_, old_snapshot.realm_id, old_snapshot.type_id, old_snapshot.type_name,
                                   old_snapshot.id, old_snapshot.name);
      }

      if (0 != new_snapshot.realm_id) {
        if (0 != node->get_discovery_info().type_id() || !node->get_discovery_info().type_name().empty()) {
          add_service_realm_index(index_by_realm_, new_snapshot.realm_id, node);
        }
      }

      all_nodes_->add_node(node);
      set_node_snapshot(new_snapshot);

      if (has_old_snapshot && old_snapshot.type_id != 0 && old_snapshot.type_id != new_snapshot.type_id) {
        bump_service_discovery_version_by_type_id.insert(old_snapshot.type_id);
      }
      if (new_snapshot.type_id != 0) {
        bump_service_discovery_version_by_type_id.insert(new_snapshot.type_id);
      }

      if (has_old_snapshot && !old_snapshot.type_name.empty() && old_snapshot.type_name != new_snapshot.type_name) {
        bump_service_discovery_version_by_type_name.insert(old_snapshot.type_name);
      }
      if (!new_snapshot.type_name.empty()) {
        bump_service_discovery_version_by_type_name.insert(new_snapshot.type_name);
      }
      break;
    }
    case atfw::atapp::etcd_module::node_action_t::kDelete: {
      service_discovery_node_snapshot_t old_snapshot;
      bool has_old_snapshot =
          get_node_snapshot(node->get_discovery_info().id(), node->get_discovery_info().name(), old_snapshot);
      if (has_old_snapshot) {
        if (old_snapshot.id != 0) {
          all_nodes_->remove_node(old_snapshot.id);
        } else if (!old_snapshot.name.empty()) {
          all_nodes_->remove_node(old_snapshot.name);
        }
        remove_node_snapshot(old_snapshot);
      } else {
        all_nodes_->remove_node(node);
        old_snapshot = make_node_snapshot(node);
      }

      if (old_snapshot.type_id != 0) {
        remove_service_type_id_index(index_all_, old_snapshot.type_id, old_snapshot.id, old_snapshot.name);
      }
      if (!old_snapshot.type_name.empty()) {
        remove_service_type_name_index(index_all_, old_snapshot.type_name, old_snapshot.id, old_snapshot.name);
      }
      if (old_snapshot.realm_id != 0 && (old_snapshot.type_id != 0 || !old_snapshot.type_name.empty())) {
        remove_service_realm_index(index_by_realm_, old_snapshot.realm_id, old_snapshot.type_id, old_snapshot.type_name,
                                   old_snapshot.id, old_snapshot.name);
      }

      if (old_snapshot.type_id != 0) {
        bump_service_discovery_version_by_type_id.insert(old_snapshot.type_id);
      }
      if (!old_snapshot.type_name.empty()) {
        bump_service_discovery_version_by_type_name.insert(old_snapshot.type_name);
      }
      break;
    }
    default:
      break;
  }
}

void service_discovery_index::setup_etcd_event_handle() {
  if (service_index_handle_) {
    etcd_module_->remove_on_node_event(*service_index_handle_);
  } else {
    service_index_handle_ = gsl::make_unique<atfw::atapp::etcd_module::node_event_callback_handle_t>();
  }
  reset_local_cache(false);
  if (service_index_handle_) {
    auto event_handle = [this](atfw::atapp::etcd_module::node_action_t action_type,
                               const atfw::atapp::etcd_discovery_node::ptr_t& node) {
      apply_node_event(action_type, node, service_discovery_dirty_by_type_id_, service_discovery_dirty_by_type_name_);
    };
    *service_index_handle_ = etcd_module_->add_on_node_discovery_event(event_handle);

    // Initialize existing nodes
    for (const auto& node : etcd_module_->get_global_discovery().get_sorted_nodes()) {
      if (!node) {
        continue;
      }

      std::unordered_set<uint64_t> bump_service_discovery_version_by_type_id;
      std::unordered_set<std::string> bump_service_discovery_version_by_type_name;
      apply_node_event(atfw::atapp::etcd_module::node_action_t::kPut, node, bump_service_discovery_version_by_type_id,
                       bump_service_discovery_version_by_type_name);

      for (auto type_id : bump_service_discovery_version_by_type_id) {
        bump_service_discovery_version(type_id, false);
      }
      for (const auto& type_name : bump_service_discovery_version_by_type_name) {
        bump_service_discovery_version(type_name, false);
      }
    }
  }
}

void service_discovery_index::add_service_type_id_index(service_discovery_index_set_t& index_set,
                                                        const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  if (!node) {
    return;
  }

  uint64_t node_id = node->get_discovery_info().id();
  uint64_t type_id = node->get_discovery_info().type_id();

  if (type_id == 0) {
    return;
  }

  auto& type_index = index_set.by_type_id[type_id];
  if (!type_index) {
    type_index = atfw::util::memory::make_strong_rc<atfw::atapp::etcd_discovery_set>();
  }

  if (node_id != 0) {
    auto old_node = type_index->get_node_by_id(node_id);
    if (old_node == node) {
      return;
    }
  } else {
    auto old_node = type_index->get_node_by_name(node->get_discovery_info().name());
    if (old_node == node) {
      return;
    }
  }

  type_index->add_node(node);
}

void service_discovery_index::remove_service_type_id_index(service_discovery_index_set_t& index_set,
                                                           const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  if (!node) {
    return;
  }

  remove_service_type_id_index(index_set, node->get_discovery_info().type_id(), node->get_discovery_info().id(),
                               node->get_discovery_info().name());
}

void service_discovery_index::remove_service_type_id_index(service_discovery_index_set_t& index_set, uint64_t type_id,
                                                           uint64_t node_id, const std::string& node_name) {
  if (type_id == 0) {
    return;
  }

  auto type_iter = index_set.by_type_id.find(type_id);
  if (type_iter == index_set.by_type_id.end()) {
    return;
  }
  auto& type_index = type_iter->second;
  if (!type_index) {
    index_set.by_type_id.erase(type_iter);
    return;
  }

  if (node_id != 0) {
    type_index->remove_node(node_id);
  } else {
    type_index->remove_node(node_name);
  }

  if (type_index->empty()) {
    index_set.by_type_id.erase(type_iter);
  }
}

void service_discovery_index::add_service_type_name_index(service_discovery_index_set_t& index_set,
                                                          const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  if (!node) {
    return;
  }

  uint64_t node_id = node->get_discovery_info().id();
  const auto& type_name = node->get_discovery_info().type_name();

  if (type_name.empty()) {
    return;
  }

  auto& type_index = index_set.by_type_name[type_name];
  if (!type_index) {
    type_index = atfw::util::memory::make_strong_rc<atfw::atapp::etcd_discovery_set>();
  }

  if (node_id != 0) {
    auto old_node = type_index->get_node_by_id(node_id);
    if (old_node == node) {
      return;
    }
  } else {
    auto old_node = type_index->get_node_by_name(node->get_discovery_info().name());
    if (old_node == node) {
      return;
    }
  }

  type_index->add_node(node);
}

void service_discovery_index::remove_service_type_name_index(service_discovery_index_set_t& index_set,
                                                             const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  if (!node) {
    return;
  }

  remove_service_type_name_index(index_set, node->get_discovery_info().type_name(), node->get_discovery_info().id(),
                                 node->get_discovery_info().name());
}

void service_discovery_index::remove_service_type_name_index(service_discovery_index_set_t& index_set,
                                                             const std::string& type_name, uint64_t node_id,
                                                             const std::string& node_name) {
  if (type_name.empty()) {
    return;
  }

  auto type_iter = index_set.by_type_name.find(type_name);
  if (type_iter == index_set.by_type_name.end()) {
    return;
  }
  auto& type_index = type_iter->second;
  if (!type_index) {
    index_set.by_type_name.erase(type_iter);
    return;
  }

  if (node_id != 0) {
    type_index->remove_node(node_id);
  } else {
    type_index->remove_node(node_name);
  }
  if (type_index->empty()) {
    index_set.by_type_name.erase(type_iter);
  }
}

void service_discovery_index::add_service_realm_index(service_discovery_index_by_realm_t& index_set, uint64_t realm_id,
                                                      const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  if (realm_id == 0 || !node) {
    return;
  }

  if (0 == node->get_discovery_info().type_id() && node->get_discovery_info().type_name().empty()) {
    return;
  }

  auto& realm_index = index_set[realm_id];
  if (0 != node->get_discovery_info().type_id()) {
    add_service_type_id_index(realm_index.index, node);
  }
  if (!node->get_discovery_info().type_name().empty()) {
    add_service_type_name_index(realm_index.index, node);
  }

  if (!realm_index.node_set) {
    realm_index.node_set = atfw::util::memory::make_strong_rc<atfw::atapp::etcd_discovery_set>();
  }
  realm_index.node_set->add_node(node);
}

void service_discovery_index::remove_service_realm_index(service_discovery_index_by_realm_t& index_set,
                                                         uint64_t realm_id,
                                                         const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  if (realm_id == 0 || !node) {
    return;
  }

  remove_service_realm_index(index_set, realm_id, node->get_discovery_info().type_id(),
                             node->get_discovery_info().type_name(), node->get_discovery_info().id(),
                             node->get_discovery_info().name());
}

void service_discovery_index::remove_service_realm_index(service_discovery_index_by_realm_t& index_set,
                                                         uint64_t realm_id, uint64_t type_id,
                                                         const std::string& type_name, uint64_t node_id,
                                                         const std::string& node_name) {
  if (realm_id == 0) {
    return;
  }

  auto iter = index_set.find(realm_id);
  if (iter == index_set.end()) {
    return;
  }
  auto& realm_index = iter->second;
  if (0 != type_id) {
    remove_service_type_id_index(realm_index.index, type_id, node_id, node_name);
  }
  if (!type_name.empty()) {
    remove_service_type_name_index(realm_index.index, type_name, node_id, node_name);
  }

  if (realm_index.node_set) {
    if (node_id != 0) {
      realm_index.node_set->remove_node(node_id);
    } else {
      realm_index.node_set->remove_node(node_name);
    }
  }

  if (realm_index.index.by_type_id.empty() && realm_index.index.by_type_name.empty() &&
      (!realm_index.node_set || realm_index.node_set->empty())) {
    index_set.erase(iter);
  }
}

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END

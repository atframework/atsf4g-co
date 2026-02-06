// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include <time/time_utility.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "atproxy_manager.h"  // NOLINT: build/include_subdir

namespace atframework {
namespace proxy {

namespace {
static const atapp::protocol::atapp_gateway *next_listen_address(atproxy_manager::node_info_t &node_info) {
  if (node_info.round_robin_index < 0) {
    node_info.round_robin_index = 0;
  }
  if (node_info.etcd_node.node_discovery.gateways_size() > 0) {
    if (node_info.round_robin_index >= node_info.etcd_node.node_discovery.gateways_size()) {
      node_info.round_robin_index %= node_info.etcd_node.node_discovery.gateways_size();
      return &node_info.etcd_node.node_discovery.gateways(node_info.round_robin_index++);
    }
  }

  if (node_info.etcd_node.node_discovery.listen_size() > 0) {
    if (node_info.round_robin_index >= node_info.etcd_node.node_discovery.listen_size()) {
      node_info.round_robin_index %= node_info.etcd_node.node_discovery.listen_size();
      node_info.ingress_for_listen.set_address(
          node_info.etcd_node.node_discovery.listen(node_info.round_robin_index++));
      return &node_info.ingress_for_listen;
    }
  }

  return nullptr;
}

static int listen_address_size(atproxy_manager::node_info_t &node_info) {
  if (node_info.etcd_node.node_discovery.gateways_size() > 0) {
    return node_info.etcd_node.node_discovery.gateways_size();
  }

  return node_info.etcd_node.node_discovery.listen_size();
}
}  // namespace

atproxy_manager::atproxy_manager() {}

void atproxy_manager::prereload(atapp::app_conf &conf) {
  static constexpr const char *kAtproxyTopologyLabelTypeKey = "atapp_type";
  static constexpr const char *kAtproxyTopologyLabelTypeValue = "atproxy";
  static constexpr const char *kAtproxyTopologyLabelRegionKey = "atproxy_region";
  static constexpr const char *kAtproxyTopologyLabelRegionDefaultValue = "local";

  auto *rule = conf.origin.mutable_bus()->mutable_topology()->mutable_rule();
  auto *data = conf.origin.mutable_bus()->mutable_topology()->mutable_data();

  // 强制开启允许拓扑直连
  rule->set_allow_direct_connection(true);

  // 注入拓扑标签
  bool inject_region = false;
  conf.bus_conf.topology_labels[kAtproxyTopologyLabelTypeKey] = kAtproxyTopologyLabelTypeValue;
  (*data->mutable_label())[kAtproxyTopologyLabelTypeKey] = kAtproxyTopologyLabelTypeValue;
  auto &region_value = (*data->mutable_label())[kAtproxyTopologyLabelRegionKey];
  if (region_value.empty()) {
    region_value = kAtproxyTopologyLabelRegionDefaultValue;
    conf.bus_conf.topology_labels[kAtproxyTopologyLabelRegionKey] = kAtproxyTopologyLabelRegionDefaultValue;
    inject_region = true;
  }

  bool has_label_value_type = false;
  bool has_label_value_region = false;
  for (const auto &item : *rule->mutable_match_label()) {
    if (item.first == kAtproxyTopologyLabelTypeKey) {
      for (const auto &v : item.second.value()) {
        if (v == kAtproxyTopologyLabelTypeValue) {
          has_label_value_type = true;
          break;
        }
      }
    }
    if (inject_region && item.first == kAtproxyTopologyLabelRegionKey) {
      for (const auto &v : item.second.value()) {
        if (v == kAtproxyTopologyLabelRegionDefaultValue) {
          has_label_value_region = true;
          break;
        }
      }
    }
  }
  if (!has_label_value_type) {
    (*rule->mutable_match_label())[kAtproxyTopologyLabelTypeKey].add_value(kAtproxyTopologyLabelTypeValue);
  }
  if (inject_region && !has_label_value_region) {
    (*rule->mutable_match_label())[kAtproxyTopologyLabelRegionKey].add_value(kAtproxyTopologyLabelRegionDefaultValue);
  }
}

int atproxy_manager::init() {
  std::shared_ptr<::atfw::atapp::etcd_module> etcd_mod = get_app()->get_etcd_module();
  if (!etcd_mod) {
    FWLOGERROR("etcd mod not found");
    return -1;
  }

  int ret =
      etcd_mod->add_discovery_watcher_by_name([this](atapp::etcd_module::discovery_watcher_sender_list_t &sender) {
        if (this->get_app()->get_type_name() == sender.node.get().node_discovery.type_name()) {
          this->on_watcher_notify(sender);
        }
      });

  if (ret < 0) {
    FWLOGERROR("add watcher by type name {} failed, res: {}", get_app()->get_type_name(), ret);
    return ret;
  }

  FWLOGINFO("watch atproxy discovery watcher by_name path: {}", etcd_mod->get_discovery_by_name_watcher_path());

  return 0;
}

int atproxy_manager::tick() {
  time_t now = atfw::util::time::time_utility::get_sys_now();

  int ret = 0;
  do {
    if (check_list_.empty()) {
      break;
    }

    check_info_t ci = check_list_.front();
    if (now <= ci.timeout_sec) {
      break;
    }
    check_list_.pop_front();

    // skip self
    if (ci.proxy_id == get_app()->get_id()) {
      continue;
    }

    std::map<::atfw::atapp::app::app_id_t, node_info_t>::iterator iter = proxy_set_.find(ci.proxy_id);
    // already removed, skip
    if (iter == proxy_set_.end()) {
      continue;
    }

    // if has no listen addrs, skip
    if (listen_address_size(iter->second) <= 0) {
      continue;
    }

    // has another pending check info
    if (iter->second.next_action_time > ci.timeout_sec) {
      continue;
    }

    if (get_app()->get_bus_node()) {
      // set next_action_time first
      iter->second.next_action_time = 0;

      // already connected, skip
      if (nullptr != get_app()->get_bus_node()->get_endpoint(ci.proxy_id)) {
        continue;
      }

      if (false == iter->second.is_available) {
        continue;
      }

      // TODO(owent): 拓扑关系允许直连才发起连接

      const std::string *select_address = nullptr;
      {
        int check_size = listen_address_size(iter->second);
        for (int i = 0; nullptr == select_address && i < check_size; ++i) {
          // support more protocols
          const atapp::protocol::atapp_gateway *try_gateway = next_listen_address(iter->second);
          if (nullptr == try_gateway) {
            continue;
          }
          if (!get_app()->match_gateway(*try_gateway)) {
            continue;
          }
          uint32_t address_type = get_app()->get_address_type(try_gateway->address());
          if (0 != (address_type & static_cast<uint32_t>(atfw::atapp::app::address_type_t::kLocalHost))) {
            continue;
          }
          if (0 != (address_type & static_cast<uint32_t>(atfw::atapp::app::address_type_t::kSimplex))) {
            continue;
          }

          select_address = &try_gateway->address();
        }
      }

      if (nullptr == select_address) {
        continue;
      }

      // try to connect to brother proxy
      int res = get_app()->get_bus_node()->connect(select_address);
      if (res >= 0) {
        ++ret;
      } else {
        FWLOGERROR("try to connect to proxy: {:#x}, address: {} failed, res: {}",
                   iter->second.etcd_node.node_discovery.id(), *select_address, res);
      }

      // recheck some time later
      ci.timeout_sec = now + get_app()->get_bus_node()->get_conf().retry_interval;
      if (ci.timeout_sec <= now) {
        ci.timeout_sec = now + 1;
      }
      // try to reconnect later
      iter->second.next_action_time = ci.timeout_sec;
      check_list_.push_back(ci);

    } else {
      ci.timeout_sec = now + 1;
      // try to reconnect later
      iter->second.next_action_time = ci.timeout_sec;
      check_list_.push_back(ci);
    }
  } while (true);

  return ret;
}

const char *atproxy_manager::name() const { return "atproxy manager"; }

int atproxy_manager::set(atapp::etcd_module::node_info_t &etcd_node) {
  // TODO Support name only node
  check_info_t ci;
  ci.timeout_sec = atfw::util::time::time_utility::get_sys_now();
  ci.proxy_id = etcd_node.node_discovery.id();

  proxy_set_t::iterator iter = proxy_set_.find(etcd_node.node_discovery.id());
  if (iter != proxy_set_.end()) {
    // already has pending action, just skipped
    if (iter->second.next_action_time >= ci.timeout_sec) {
      return 0;
    }

    iter->second.next_action_time = ci.timeout_sec;
    iter->second.etcd_node = etcd_node;
  } else {
    node_info_t &proxy_info = proxy_set_[etcd_node.node_discovery.id()];
    proxy_info.next_action_time = ci.timeout_sec;
    proxy_info.etcd_node = etcd_node;
    proxy_info.is_available = check_available(etcd_node);
    proxy_info.round_robin_index = 0;
    FWLOGINFO("new atproxy {:#x} found", etcd_node.node_discovery.id());
  }

  // push front and check it on next loop
  check_list_.push_front(ci);
  return 0;
}

int atproxy_manager::remove(::atfw::atapp::app::app_id_t id) {
  proxy_set_t::iterator iter = proxy_set_.find(id);
  if (iter != proxy_set_.end()) {
    FWLOGINFO("lost atproxy {:#x}", id);
    proxy_set_.erase(iter);
  }
  return 0;
}

int atproxy_manager::reset(node_list_t &all_proxys) {
  proxy_set_.clear();
  check_list_.clear();

  for (std::list<node_info_t>::iterator iter = all_proxys.nodes.begin(); iter != all_proxys.nodes.end(); ++iter) {
    // skip all empty
    // TODO Support gateway
    if (iter->etcd_node.node_discovery.listen_size() == 0) {
      continue;
    }

    check_info_t ci;
    ci.timeout_sec = atfw::util::time::time_utility::get_sys_now();
    ci.proxy_id = iter->etcd_node.node_discovery.id();
    (*iter).next_action_time = ci.timeout_sec;
    (*iter).is_available = check_available((*iter).etcd_node);

    // copy proxy info
    proxy_set_[ci.proxy_id] = *iter;

    // push front and check it on next loop
    check_list_.push_front(ci);
  }

  return 0;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
int atproxy_manager::on_connected(const ::atfw::atapp::app &, ::atfw::atapp::app::app_id_t) { return 0; }

int atproxy_manager::on_disconnected(const ::atfw::atapp::app &app, ::atfw::atapp::app::app_id_t id) {
  proxy_set_t::iterator iter = proxy_set_.find(id);
  if (proxy_set_.end() != iter) {
    check_info_t ci;

    // when stoping bus noe may be unavailable
    if (!app.check_flag(::atfw::atapp::app::flag_t::type::kStoping)) {
      if (app.get_bus_node() && app.get_bus_node()->get_conf().retry_interval > 0) {
        ci.timeout_sec = atfw::util::time::time_utility::get_sys_now() + app.get_bus_node()->get_conf().retry_interval;
      } else {
        ci.timeout_sec = atfw::util::time::time_utility::get_sys_now() + 1;
      }
    } else {
      ci.timeout_sec = atfw::util::time::time_utility::get_sys_now() - 1;
    }

    if (iter->second.next_action_time < ci.timeout_sec) {
      iter->second.next_action_time = ci.timeout_sec;
      ci.proxy_id = id;
      check_list_.push_back(ci);
    }
  }

  return 0;
}

void atproxy_manager::swap(node_info_t &l, node_info_t &r) {
  using std::swap;
  l.etcd_node.node_discovery.Swap(&r.etcd_node.node_discovery);
  swap(l.etcd_node.action, r.etcd_node.action);

  swap(l.next_action_time, r.next_action_time);
  swap(l.is_available, r.is_available);
  swap(l.round_robin_index, r.round_robin_index);
}

void atproxy_manager::on_watcher_notify(atapp::etcd_module::discovery_watcher_sender_list_t &sender) {
  if (sender.node.get().action == node_action_t::kDelete) {
    // trigger manager
    remove(sender.node.get().node_discovery.id());
  } else {
    // trigger manager
    set(sender.node);
  }
}

bool atproxy_manager::check_available(const atapp::etcd_module::node_info_t &node_event) const {
  uint64_t atbus_protocol_version = 0;
  uint64_t atbus_protocol_min_version = 0;
  if (nullptr != get_app() && get_app()->get_bus_node()) {
    atbus_protocol_version = static_cast<uint64_t>(get_app()->get_bus_node()->get_protocol_version());
    atbus_protocol_min_version = static_cast<uint64_t>(get_app()->get_bus_node()->get_protocol_minimal_version());
  } else {
    atbus_protocol_version = atbus::protocol::ATBUS_PROTOCOL_VERSION;
    atbus_protocol_min_version = atbus::protocol::ATBUS_PROTOCOL_MINIMAL_VERSION;
  }

  return node_event.node_discovery.atbus_protocol_version() >= atbus_protocol_min_version &&
         atbus_protocol_version >= node_event.node_discovery.atbus_protocol_min_version();
}
}  // namespace proxy
}  // namespace atframework

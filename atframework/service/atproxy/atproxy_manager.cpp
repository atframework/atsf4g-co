// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include <time/time_utility.h>

#include <atbus_topology.h>

#include <opentelemetry/semconv/incubating/deployment_attributes.h>
#include <opentelemetry/semconv/incubating/service_attributes.h>

#include <chrono>
#include <memory>
#include <utility>
#include "atframe/atapp_common_types.h"
#include "atframe/atapp_conf.h"
#include "atframe/connectors/atapp_connector_atbus.h"
#include "detail/libatbus_channel_export.h"

#include "atproxy_manager.h"  // NOLINT: build/include_subdir

namespace atframework {
namespace proxy {

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

  // 注入可观测性标签
  auto *metadata_labels = conf.origin.mutable_metadata()->mutable_labels();
  if (metadata_labels->end() == metadata_labels->find(opentelemetry::semconv::service::kServiceName)) {
    (*metadata_labels)[opentelemetry::semconv::service::kServiceName] = "atproxy";
  }
  if (metadata_labels->end() == metadata_labels->find(opentelemetry::semconv::service::kServiceInstanceId)) {
    (*metadata_labels)[opentelemetry::semconv::service::kServiceInstanceId] = get_app()->get_app_name();
  }
  if (metadata_labels->end() == metadata_labels->find(opentelemetry::semconv::service::kServiceVersion)) {
    (*metadata_labels)[opentelemetry::semconv::service::kServiceVersion] = get_app()->get_app_version();
  }
  (*metadata_labels)["service.identity"] = get_app()->get_app_identity();
}

int atproxy_manager::reload() {
  get_app()->parse_configures_into(atproxy_configure_, "atproxy", "ATPROXY");
  return 0;
}

int atproxy_manager::init() {
  std::shared_ptr<::atfw::atapp::etcd_module> etcd_mod = get_app()->get_etcd_module();
  if (!etcd_mod) {
    FWLOGERROR("etcd mod not found");
    return -1;
  }

  atbus::node::ptr_t bus_node = get_app()->get_bus_node();
  if (!bus_node) {
    FWLOGERROR("bus node not found");
    return -1;
  }

  etcd_mod->add_on_topology_info_event([this](atapp::etcd_module::topology_action_t action,
                                              const atapp::etcd_module::atapp_topology_info_ptr_t &info,
                                              const atapp::etcd_data_version &) {
    if (!info) {
      return;
    }
    if (info->id() == 0) {
      return;
    }

    if (action == atapp::etcd_module::topology_action_t::kDelete) {
      this->remove_topology_info_ready(info->id());
    } else {
      this->set_topology_info_ready(info->id());
    }
  });

  etcd_mod->add_on_node_discovery_event(
      [this](atapp::etcd_module::node_action_t action, const atapp::etcd_discovery_node::ptr_t &node) {
        if (!node) {
          return;
        }

        if (node->get_discovery_info().id() == 0) {
          return;
        }

        if (action == atapp::etcd_module::node_action_t::kDelete) {
          this->remove_discovery_info_ready(node->get_discovery_info().id());
        } else if (action == atapp::etcd_module::node_action_t::kPut) {
          this->set_discovery_info_ready(node->get_discovery_info().id());
        }
      });

  bus_node->set_on_add_endpoint_handle([this](const atbus::node &, atbus::endpoint *ep, ATBUS_ERROR_TYPE result) {
    if (ep == nullptr || result != EN_ATBUS_ERR_SUCCESS) {
      return 0;
    }

    auto iter = this->proxy_set_.find(ep->get_id());
    if (iter == this->proxy_set_.end()) {
      return 0;
    }

    if (!iter->second) {
      this->proxy_set_.erase(iter);
      return 0;
    }

    atapp::protobuf_to_chrono_set_duration(iter->second->next_retry_duration,
                                           atproxy_configure_.activity_connecttion().retry_interval_min());
    if (iter->second->next_retry_duration <= std::chrono::system_clock::duration::zero()) {
      iter->second->next_retry_duration =
          std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(8));
    }
    if (!iter->second->timer_handle.expired()) {
      auto timer_inst = iter->second->timer_handle.lock();
      iter->second->timer_handle.reset();
      if (timer_inst) {
        atapp::jiffies_timer_t::remove_timer(*timer_inst);
      }
    }
    return 0;
  });

  FWLOGINFO("atproxy setup event listeners done");

  return 0;
}

const char *atproxy_manager::name() const { return "atproxy manager"; }

atproxy_manager::node_info_ptr_t atproxy_manager::mutable_node_info(::atfw::atapp::app::app_id_t id) {
  atproxy_manager::node_info_ptr_t &ret = proxy_set_[id];
  if (!ret) {
    ret = atfw::util::memory::make_strong_rc<node_info_t>();
    ret->has_discovery_info = false;
    ret->has_topology_info = false;
    ret->is_available = false;
    atapp::protobuf_to_chrono_set_duration(ret->next_retry_duration,
                                           atproxy_configure_.activity_connecttion().retry_interval_min());
    if (ret->next_retry_duration <= std::chrono::system_clock::duration::zero()) {
      ret->next_retry_duration =
          std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(8));
    }
  }

  return ret;
}

void atproxy_manager::remove_node_info(::atfw::atapp::app::app_id_t id) {
  auto iter = proxy_set_.find(id);
  if (iter == proxy_set_.end()) {
    return;
  }

  if (!iter->second) {
    proxy_set_.erase(iter);
    return;
  }

  if (!iter->second->timer_handle.expired()) {
    auto timer_inst = iter->second->timer_handle.lock();
    iter->second->timer_handle.reset();
    if (timer_inst) {
      atapp::jiffies_timer_t::remove_timer(*timer_inst);
    }
  }

  proxy_set_.erase(iter);
}

void atproxy_manager::try_activity_connect_to_node(::atfw::atapp::app::app_id_t id) {
  auto iter = proxy_set_.find(id);
  if (iter == proxy_set_.end()) {
    return;
  }

  if (!iter->second) {
    proxy_set_.erase(iter);
    return;
  }

  if (!iter->second->is_available) {
    return;
  }

  auto atbus_connector = get_app()->get_atbus_connector();
  if (!atbus_connector) {
    return;
  }
  if (!get_app()->get_bus_node()) {
    return;
  }

  // 有其他正在等待重试的定时器，则不重复设置
  if (iter->second->timer_handle.lock()) {
    return;
  }

  get_app()->add_custom_timer(
      iter->second->next_retry_duration,
      [this, id](time_t /*tick*/, const atapp::jiffies_timer_t::timer_t & /*timer*/) {
        auto inner_iter = this->proxy_set_.find(id);
        if (inner_iter == this->proxy_set_.end()) {
          return;
        }

        if (!inner_iter->second) {
          this->proxy_set_.erase(inner_iter);
          return;
        }

        inner_iter->second->timer_handle.reset();
        this->try_activity_connect_to_node(id);
      },
      nullptr, &iter->second->timer_handle);

  std::chrono::system_clock::duration max_retry_interval;
  atapp::protobuf_to_chrono_set_duration(max_retry_interval,
                                         atproxy_configure_.activity_connecttion().retry_interval_max());
  if (max_retry_interval <= std::chrono::system_clock::duration::zero()) {
    max_retry_interval = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(300));
  }

  if (iter->second->next_retry_duration < max_retry_interval) {
    iter->second->next_retry_duration *= 2;
    if (iter->second->next_retry_duration > max_retry_interval) {
      iter->second->next_retry_duration = max_retry_interval;
    }
  }

  atapp::etcd_discovery_node::ptr_t discovery = get_app()->get_discovery_node_by_id(id);
  if (!discovery) {
    remove_discovery_info_ready(id);
    return;
  }

  // 选取合适的地址进行连接尝试
  discovery->reset_ingress_index();
  int32_t ingress_size = discovery->get_ingress_size();
  bool has_sucess_connection = false;
  for (int32_t i = 0; i < ingress_size; ++i) {
    const atapp::protocol::atapp_gateway &ingress = discovery->next_ingress_gateway();
    gsl::string_view addr = gsl::string_view{ingress.address().data(), ingress.address().size()};
    // atproxy 仅仅支持atbus的协议
    atbus::channel::channel_address_t parsed_addr;
    if (!atbus::channel::make_address(addr, parsed_addr)) {
      continue;
    }
    if (atbus_connector->get_support_protocols().end() ==
        atbus_connector->get_support_protocols().find(parsed_addr.scheme)) {
      continue;
    }
    // 单工协议不允许作为控制通道协议
    if (atbus::channel::is_simplex_address(addr)) {
      continue;
    }
    if (atbus::channel::is_local_process_address(addr) &&
        discovery->get_discovery_info().pid() != atbus::node::get_pid()) {
      continue;
    }
    if (atbus::channel::is_local_host_address(addr) &&
        discovery->get_discovery_info().hostname() != atbus::node::get_hostname()) {
      continue;
    }

    int res = get_app()->get_bus_node()->connect(addr);
    if (res == 0) {
      has_sucess_connection = true;
      break;
    }
  }

  if (!has_sucess_connection) {
    FWLOGERROR("try connect to node {} failed, no available address, discovery info:\n{}", id,
               discovery->get_discovery_info().ShortDebugString());
    iter->second->is_available = false;
  }
}

void atproxy_manager::set_discovery_info_ready(::atfw::atapp::app::app_id_t id) {
  node_info_ptr_t node_info = mutable_node_info(id);

  node_info->has_discovery_info = true;
  if (node_info->has_topology_info) {
    node_info->is_available = check_available(id);
    try_activity_connect_to_node(id);
  }
}

void atproxy_manager::remove_discovery_info_ready(::atfw::atapp::app::app_id_t id) {
  auto iter = proxy_set_.find(id);
  if (iter == proxy_set_.end()) {
    return;
  }

  if (!iter->second) {
    proxy_set_.erase(iter);
    return;
  }

  iter->second->has_discovery_info = false;
  if (!iter->second->has_topology_info) {
    remove_node_info(id);
    return;
  }

  iter->second->is_available = false;
}

void atproxy_manager::set_topology_info_ready(::atfw::atapp::app::app_id_t id) {
  node_info_ptr_t node_info = mutable_node_info(id);

  node_info->has_topology_info = true;
  if (node_info->has_discovery_info) {
    node_info->is_available = check_available(id);
    try_activity_connect_to_node(id);
  }
}

void atproxy_manager::remove_topology_info_ready(::atfw::atapp::app::app_id_t id) {
  auto iter = proxy_set_.find(id);
  if (iter == proxy_set_.end()) {
    return;
  }

  if (!iter->second) {
    proxy_set_.erase(iter);
    return;
  }

  iter->second->has_topology_info = false;
  if (!iter->second->has_discovery_info) {
    remove_node_info(id);
    return;
  }

  iter->second->is_available = false;
}

bool atproxy_manager::check_available(::atfw::atapp::app::app_id_t id) const {
  atapp::etcd_discovery_node::ptr_t discovery = get_app()->get_discovery_node_by_id(id);
  if (!discovery) {
    return false;
  }
  const atbus::node::ptr_t &bus_node = get_app()->get_bus_node();
  if (!bus_node) {
    return false;
  }

  uint64_t atbus_protocol_version = static_cast<uint64_t>(bus_node->get_protocol_version());
  uint64_t atbus_protocol_min_version = static_cast<uint64_t>(bus_node->get_protocol_minimal_version());

  // 最小协议版本号判定
  if (discovery->get_discovery_info().atbus_protocol_version() < atbus_protocol_min_version) {
    return false;
  }
  if (atbus_protocol_version < discovery->get_discovery_info().atbus_protocol_min_version()) {
    return false;
  }

  atbus::topology_peer::ptr_t other_topology_info = get_app()->get_topology_peer(id);
  if (!other_topology_info) {
    return false;
  }
  atbus::topology_peer::ptr_t self_topology_info = get_app()->get_topology_peer(get_app()->get_id());
  if (!self_topology_info) {
    return false;
  }

  if (!atbus::topology_registry::check_policy(get_app()->get_topology_policy_rule(),
                                              self_topology_info->get_topology_data(),
                                              other_topology_info->get_topology_data())) {
    return false;
  }

  if (atproxy_configure_.activity_connecttion().has_metadata() &&
      !atapp::etcd_discovery_set::metadata_equal_type::filter(atproxy_configure_.activity_connecttion().metadata(),
                                                              discovery->get_discovery_info().metadata())) {
    return false;
  }

  return true;
}
}  // namespace proxy
}  // namespace atframework

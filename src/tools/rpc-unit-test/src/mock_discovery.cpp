// Copyright 2026 atframework

#include <atframework/testing/mock_discovery.h>

#include <atframe/atapp.h>
#include <atframe/modules/service_discovery_module.h>

#include <memory/rc_ptr.h>

#include <memory/object_allocator.h>

namespace atframework {
namespace testing {

mock_discovery::mock_discovery() = default;

mock_discovery::~mock_discovery() { unbind(); }

atfw::atapp::etcd_discovery_node::ptr_t mock_discovery::add_node(const mock_node &node) {
  if (nullptr == owner_) {
    return nullptr;
  }
  auto discovery_module = owner_->get_service_discovery_module();
  if (!discovery_module) {
    return nullptr;
  }

  const atfw::atapp::protocol::atapp_discovery &source_info = node.get_discovery_info();
  if (0 == source_info.id()) {
    return nullptr;
  }

  atfw::atapp::protocol::atapp_discovery info;
  info.CopyFrom(source_info);
  if (info.name().empty()) {
    info.set_name("mock-node-" + std::to_string(info.id()));
  }
  if (0 == info.gateways_size()) {
    auto *gateway = info.add_gateways();
    if (nullptr != gateway) {
      gateway->set_address("mock://" + info.name());
    }
  }

  auto new_node = atfw::component::memory::stl::make_strong_rc<atfw::atapp::etcd_discovery_node>();
  if (!new_node) {
    return nullptr;
  }
  new_node->copy_from(info, atfw::atapp::etcd_discovery_node::node_version(), 0);

  discovery_module->get_global_discovery().add_node(new_node);
  injected_nodes_.push_back(new_node);
  return new_node;
}

void mock_discovery::remove_node(uint64_t id) {
  if (nullptr == owner_) {
    return;
  }
  auto discovery_module = owner_->get_service_discovery_module();
  if (!discovery_module) {
    return;
  }
  discovery_module->get_global_discovery().remove_node(id);
  for (auto iter = injected_nodes_.begin(); iter != injected_nodes_.end(); ++iter) {
    if (*iter && (*iter)->get_discovery_info().id() == id) {
      injected_nodes_.erase(iter);
      break;
    }
  }
}

void mock_discovery::remove_node(gsl::string_view name) {
  if (nullptr == owner_) {
    return;
  }
  auto discovery_module = owner_->get_service_discovery_module();
  if (!discovery_module) {
    return;
  }
  std::string name_str{name.data(), name.size()};
  discovery_module->get_global_discovery().remove_node(name_str);
  for (auto iter = injected_nodes_.begin(); iter != injected_nodes_.end(); ++iter) {
    if (*iter && (*iter)->get_discovery_info().name() == name_str) {
      injected_nodes_.erase(iter);
      break;
    }
  }
}

atfw::atapp::etcd_discovery_node::ptr_t mock_discovery::get_node_by_id(uint64_t id) const {
  if (nullptr == owner_) {
    return nullptr;
  }
  auto discovery_module = owner_->get_service_discovery_module();
  if (!discovery_module) {
    return nullptr;
  }
  return discovery_module->get_global_discovery().get_node_by_id(id);
}

atfw::atapp::etcd_discovery_node::ptr_t mock_discovery::get_node_by_name(gsl::string_view name) const {
  if (nullptr == owner_) {
    return nullptr;
  }
  auto discovery_module = owner_->get_service_discovery_module();
  if (!discovery_module) {
    return nullptr;
  }
  return discovery_module->get_global_discovery().get_node_by_name(std::string{name.data(), name.size()});
}

void mock_discovery::bind(atfw::atapp::app *owner) { owner_ = owner; }

void mock_discovery::unbind() {
  if (nullptr != owner_) {
    auto discovery_module = owner_->get_service_discovery_module();
    if (discovery_module) {
      for (const auto &node : injected_nodes_) {
        if (node) {
          discovery_module->get_global_discovery().remove_node(node);
        }
      }
    }
  }
  injected_nodes_.clear();
  owner_ = nullptr;
}

}  // namespace testing
}  // namespace atframework

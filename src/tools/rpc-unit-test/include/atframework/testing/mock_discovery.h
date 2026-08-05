// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframe/atapp_conf.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp_config.h>
#include <atframe/etcdcli/etcd_discovery.h>

#include <cstdint>
#include <string>
#include <vector>

#include <atframework/testing/runtime.h>

LIBATAPP_MACRO_NAMESPACE_BEGIN
class app;
LIBATAPP_MACRO_NAMESPACE_END

namespace atframework {
namespace testing {

class mock_discovery;

// Builder of a fake discovery node. All setters return *this for chaining.
class ATFW_UTIL_SYMBOL_VISIBLE mock_node {
 public:
  mock_node() = default;

  mock_node &set_id(uint64_t input) noexcept {
    info_.set_id(input);
    return *this;
  }
  mock_node &set_name(gsl::string_view input) {
    info_.set_name(input.data(), input.size());
    return *this;
  }
  mock_node &set_type_id(uint64_t input) noexcept {
    info_.set_type_id(input);
    return *this;
  }
  mock_node &set_type_name(gsl::string_view input) {
    info_.set_type_name(input.data(), input.size());
    return *this;
  }
  mock_node &set_zone_id(uint64_t input) noexcept {
    info_.mutable_area()->set_zone_id(input);
    return *this;
  }
  mock_node &set_region(gsl::string_view input) {
    info_.mutable_area()->set_region(input.data(), input.size());
    return *this;
  }
  mock_node &set_district(gsl::string_view input) {
    info_.mutable_area()->set_district(input.data(), input.size());
    return *this;
  }
  mock_node &set_identity(gsl::string_view input) {
    info_.set_identity(input.data(), input.size());
    return *this;
  }
  mock_node &set_version(gsl::string_view input) {
    info_.set_version(input.data(), input.size());
    return *this;
  }
  mock_node &add_ingress(gsl::string_view address) {
    auto *gateway = info_.add_gateways();
    if (nullptr != gateway) {
      gateway->set_address(address.data(), address.size());
    }
    return *this;
  }
  mock_node &add_label(gsl::string_view key, gsl::string_view value) {
    (*info_.mutable_metadata()->mutable_labels())[std::string{key.data(), key.size()}] =
        std::string{value.data(), value.size()};
    return *this;
  }

  ATFW_UTIL_FORCEINLINE const atfw::atapp::protocol::atapp_discovery &get_discovery_info() const noexcept {
    return info_;
  }

 private:
  atfw::atapp::protocol::atapp_discovery info_;
};

// Discovery injection facade bound to one runtime. Nodes added here are removed from the app global
// discovery set at runtime teardown.
class RPC_UNIT_TEST_API mock_discovery {
 public:
  mock_discovery();
  ~mock_discovery();

  mock_discovery(const mock_discovery &) = delete;
  mock_discovery &operator=(const mock_discovery &) = delete;

  // Add a node into the app global discovery. When the node has no ingress gateway, a default
  // "mock://<name>" gateway is appended so the mock connector can pick it up.
  // Returns the inserted node, or nullptr when the runtime is not running or parameters are invalid.
  atfw::atapp::etcd_discovery_node::ptr_t add_node(const mock_node &node);

  void remove_node(uint64_t id);
  void remove_node(gsl::string_view name);

  atfw::atapp::etcd_discovery_node::ptr_t get_node_by_id(uint64_t id) const;
  atfw::atapp::etcd_discovery_node::ptr_t get_node_by_name(gsl::string_view name) const;

 private:
  friend class runtime;

  // Called by runtime: bind to the app, and unbind (removing all injected nodes) at teardown.
  void bind(atfw::atapp::app *owner);
  void unbind();

  atfw::atapp::app *owner_ = nullptr;
  std::vector<atfw::atapp::etcd_discovery_node::ptr_t> injected_nodes_;
};

}  // namespace testing
}  // namespace atframework

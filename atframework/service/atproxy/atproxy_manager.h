// Copyright 2026 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <memory/rc_ptr.h>

#include <atframe/atapp.h>
#include <atframe/atapp_module_impl.h>

#include <atframe/atapp_common_types.h>
#include <atframe/modules/etcd_module.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atproxy/atproxy_protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>
#include <unordered_map>

namespace atframework {
namespace proxy {
class atproxy_manager : public ::atfw::atapp::module_impl {
 public:
  using node_action_t = atapp::etcd_module::node_action_t;
  struct node_info_t {
    bool has_topology_info;
    bool has_discovery_info;
    bool is_available;
    std::chrono::system_clock::duration next_retry_duration;
    atapp::jiffies_timer_watcher_t timer_handle;
  };
  using node_info_ptr_t = ::atfw::util::memory::strong_rc_ptr<node_info_t>;

 public:
  atproxy_manager();

  void prereload(atapp::app_conf &conf) override;

  int reload() override;

  int init() override;

  const char *name() const override;

 private:
  node_info_ptr_t mutable_node_info(::atfw::atapp::app::app_id_t id);
  void remove_node_info(::atfw::atapp::app::app_id_t id);
  void try_activity_connect_to_node(::atfw::atapp::app::app_id_t id);

  void set_discovery_info_ready(::atfw::atapp::app::app_id_t id);
  void remove_discovery_info_ready(::atfw::atapp::app::app_id_t id);
  void set_topology_info_ready(::atfw::atapp::app::app_id_t id);
  void remove_topology_info_ready(::atfw::atapp::app::app_id_t id);

  bool check_available(atapp::app::app_id_t id) const;

 private:
  using proxy_set_t = std::unordered_map< ::atfw::atapp::app::app_id_t, node_info_ptr_t>;
  proxy_set_t proxy_set_;

  atframework::atproxy::protocol::atproxy_configure atproxy_configure_;
};
}  // namespace proxy
}  // namespace atframework

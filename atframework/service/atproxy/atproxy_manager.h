// Copyright 2026 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <atframe/atapp.h>
#include <atframe/atapp_module_impl.h>

#include <atframe/modules/etcd_module.h>

#include <ctime>
#include <list>
#include <map>

namespace atframework {
namespace proxy {
class atproxy_manager : public ::atfw::atapp::module_impl {
 public:
  using node_action_t = atapp::etcd_module::node_action_t;
  struct node_info_t {
    atapp::etcd_module::node_info_t etcd_node;
    atapp::protocol::atapp_gateway ingress_for_listen;
    time_t next_action_time;
    bool is_available;
    int round_robin_index;
  };

  struct node_list_t {
    std::list<node_info_t> nodes;
  };

 private:
  struct check_info_t {
    time_t timeout_sec;
    ::atfw::atapp::app::app_id_t proxy_id;
  };

 public:
  atproxy_manager();

  void prereload(atapp::app_conf &conf) override;

  int init() override;

  int tick() override;

  const char *name() const override;

  int set(atapp::etcd_module::node_info_t &proxy_info);

  int remove(::atfw::atapp::app::app_id_t id);

  int reset(node_list_t &all_proxys);

  int on_connected(const ::atfw::atapp::app &app, ::atfw::atapp::app::app_id_t id);

  int on_disconnected(const ::atfw::atapp::app &app, ::atfw::atapp::app::app_id_t id);

 private:
  void swap(node_info_t &l, node_info_t &r);
  void on_watcher_notify(atapp::etcd_module::discovery_watcher_sender_list_t &sender);
  bool check_available(const atapp::etcd_module::node_info_t &node_event) const;

 private:
  std::list<check_info_t> check_list_;
  using proxy_set_t = std::map< ::atfw::atapp::app::app_id_t, node_info_t>;
  proxy_set_t proxy_set_;
};
}  // namespace proxy
}  // namespace atframework

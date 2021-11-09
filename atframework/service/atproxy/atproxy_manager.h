// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <atframe/atapp.h>
#include <atframe/atapp_module_impl.h>

#include <atframe/modules/etcd_module.h>

#include <ctime>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace atframe {
namespace proxy {
class atproxy_manager : public ::atapp::module_impl {
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
    ::atapp::app::app_id_t proxy_id;
  };

 public:
  atproxy_manager();

  int init() override;

  int tick() override;

  const char *name() const override;

  int set(atapp::etcd_module::node_info_t &proxy_info);

  int remove(::atapp::app::app_id_t id);

  int reset(node_list_t &all_proxys);

  int on_connected(const ::atapp::app &app, ::atapp::app::app_id_t id);

  int on_disconnected(const ::atapp::app &app, ::atapp::app::app_id_t id);

 private:
  void swap(node_info_t &l, node_info_t &r);
  void on_watcher_notify(atapp::etcd_module::watcher_sender_one_t &sender);
  bool check_available(const atapp::etcd_module::node_info_t &node_event) const;

 private:
  std::list<check_info_t> check_list_;
  using proxy_set_t = std::map< ::atapp::app::app_id_t, node_info_t>;
  proxy_set_t proxy_set_;
};
}  // namespace proxy
}  // namespace atframe

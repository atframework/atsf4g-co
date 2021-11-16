// Copyright 2021 atframework
// Created by owent on 2018/05/07.
//

#ifndef ROUTER_ROUTER_PLAYER_MANAGER_H
#define ROUTER_ROUTER_PLAYER_MANAGER_H

#pragma once

#include <design_pattern/singleton.h>
#include <std/functional.h>

#include <memory>
#include <string>

#include "router/router_manager.h"
#include "router/router_player_cache.h"

class router_player_manager : public router_manager<router_player_cache, player_cache, router_player_private_type *>,
                              public util::design_pattern::singleton<router_player_manager> {
 public:
  using base_type = router_manager<router_player_cache, player_cache, router_player_private_type *>;
  using cache_t = base_type::cache_t;
  using priv_data_t = base_type::priv_data_t;
  using key_t = base_type::key_t;
  using flag_t = base_type::flag_t;
  using object_ptr_t = base_type::object_ptr_t;
  using ptr_t = base_type::ptr_t;
  using store_ptr_t = base_type::store_ptr_t;
  using self_type = router_player_manager;

  using create_object_fn_t = std::function<router_player_cache::object_ptr_t(uint64_t, uint32_t, const std::string &)>;

 public:
  router_player_manager();
  const char *name() const override;

  bool remove_player_object(uint64_t user_id, uint32_t zone_id, priv_data_t priv_data);

  bool remove_player_object(uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache,
                            priv_data_t priv_data);

  bool remove_player_cache(uint64_t user_id, uint32_t zone_id, priv_data_t priv_data);

  bool remove_player_cache(uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache,
                           priv_data_t priv_data);

  void set_create_object_fn(create_object_fn_t fn);

  router_player_cache::object_ptr_t create_player_object(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  int pull_online_server(const key_t &key, uint64_t &router_svr_id, uint64_t &router_svr_ver) override;

 private:
  void on_evt_remove_object(const key_t &key, const ptr_t &cache, priv_data_t priv_data) override;

 private:
  create_object_fn_t create_fn_;
};

#endif  // ROUTER_ROUTER_PLAYER_MANAGER_H
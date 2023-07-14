// Copyright 2021 atframework
// Created by owent on 2018-05-07.
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

class router_player_manager : public router_manager<router_player_cache, player_cache, router_player_private_type *> {
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

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(router_player_manager)
#  else
  UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(router_player_manager)
#  endif
#else
  UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(router_player_manager)
#endif

 private:
  SERVER_FRAME_CONFIG_API router_player_manager();

 public:
  SERVER_FRAME_CONFIG_API ~router_player_manager();

  SERVER_FRAME_CONFIG_API const char *name() const override;

  SERVER_FRAME_CONFIG_API EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_player_object(rpc::context &ctx, uint64_t user_id,
                                                                     uint32_t zone_id, priv_data_t priv_data);

  SERVER_FRAME_CONFIG_API EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_player_object(rpc::context &ctx, uint64_t user_id,
                                                                     uint32_t zone_id,
                                                                     std::shared_ptr<router_object_base> cache,
                                                                     priv_data_t priv_data);

  SERVER_FRAME_CONFIG_API EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_player_cache(rpc::context &ctx, uint64_t user_id,
                                                                    uint32_t zone_id, priv_data_t priv_data);

  SERVER_FRAME_CONFIG_API EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_player_cache(rpc::context &ctx, uint64_t user_id,
                                                                    uint32_t zone_id,
                                                                    std::shared_ptr<router_object_base> cache,
                                                                    priv_data_t priv_data);

  SERVER_FRAME_CONFIG_API void set_create_object_fn(create_object_fn_t fn);

  SERVER_FRAME_CONFIG_API router_player_cache::object_ptr_t create_player_object(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  SERVER_FRAME_CONFIG_API EXPLICIT_NODISCARD_ATTR rpc::result_code_type pull_online_server(rpc::context &ctx, const key_t &key,
                                                                   uint64_t &router_svr_id,
                                                                   uint64_t &router_svr_ver) override;

 private:
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type on_evt_remove_object(rpc::context &ctx, const key_t &key,
                                                                     const ptr_t &cache,
                                                                     priv_data_t priv_data) override;

 private:
  create_object_fn_t create_fn_;
};

#endif  // ROUTER_ROUTER_PLAYER_MANAGER_H

// Copyright 2021 atframework
// Created by owent on 2018/05/07.
//

#pragma once

#include <data/player_cache.h>

#include <string>

#include "router/router_object.h"

struct router_player_private_type {
  router_player_private_type();
  router_player_private_type(PROJECT_NAMESPACE_ID::table_login *tb, std::string *ver);

  PROJECT_NAMESPACE_ID::table_login *login_tb;
  std::string *login_ver;
};

class router_player_cache : public router_object<player_cache, router_player_cache> {
 public:
  using base_type = router_object<player_cache, router_player_cache>;
  using key_t = base_type::key_t;
  using flag_t = base_type::flag_t;
  using object_ptr_t = base_type::object_ptr_t;
  using ptr_t = base_type::ptr_t;
  using self_type = router_player_cache;
  using flag_guard = base_type::flag_guard;

 public:
  explicit router_player_cache(uint64_t user_id, uint32_t zone_id, const std::string &openid);
  explicit router_player_cache(const key_t &key);

  const char *name() const override;

  rpc::result_code_type pull_cache(rpc::context &ctx, void *priv_data) override;
  rpc::result_code_type pull_cache(rpc::context &ctx, router_player_private_type &priv_data);
  rpc::result_code_type pull_object(rpc::context &ctx, void *priv_data) override;
  rpc::result_code_type pull_object(rpc::context &ctx, router_player_private_type &priv_data);

  rpc::result_code_type save_object(rpc::context &ctx, void *priv_data) override;
};

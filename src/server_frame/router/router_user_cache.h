// Copyright 2021 atframework
// Created by owent on 2018-05-07.
//

#pragma once

#include <data/user_cache.h>

#include <nostd/string_view.h>
#include <rpc/rpc_shared_message.h>


#include <string>

#include "router/router_object.h"

struct router_user_private_type {
  SERVER_FRAME_API router_user_private_type();
  SERVER_FRAME_API router_user_private_type(
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> *login_lock_tb, uint64_t login_lock_cas_ver,
      const std::string &openid);
  SERVER_FRAME_API ~router_user_private_type();

  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> *login_lock_tb;
  uint64_t login_lock_cas_ver;
  std::string openid;
};

class ATFW_UTIL_SYMBOL_VISIBLE router_user_cache : public router_object<user_cache, router_user_cache> {
 public:
  using base_type = router_object<user_cache, router_user_cache>;
  using key_t = base_type::key_t;
  using flag_t = base_type::flag_t;
  using object_ptr_t = base_type::object_ptr_t;
  using ptr_t = base_type::ptr_t;
  using self_type = router_user_cache;
  using flag_guard = base_type::flag_guard;

 public:
  SERVER_FRAME_API explicit router_user_cache(uint64_t user_id, uint32_t zone_id, const std::string &openid);
  SERVER_FRAME_API explicit router_user_cache(const key_t &key);

  SERVER_FRAME_API const char *name() const override;

  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type pull_cache(rpc::context &ctx,
                                                                                 void *priv_data) override;
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type pull_cache(rpc::context &ctx,
                                                                                 router_user_private_type &priv_data);
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type pull_object(rpc::context &ctx,
                                                                                  void *priv_data) override;
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type pull_object(
      rpc::context &ctx, router_user_private_type &priv_data);

  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type save_object(rpc::context &ctx,
                                                                                  void *priv_data) override;
};

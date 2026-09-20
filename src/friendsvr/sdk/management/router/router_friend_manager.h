// Copyright 2026 atframework
// Created by owent on 2026-09-18

#pragma once

#include <router/router_manager.h>

#include <memory>

#include "router/router_friend_cache.h"

namespace atframework {
namespace friend_api {

class router_friend_manager : public router_manager<router_friend_cache, friend_cache, router_friend_private_type *> {
#if defined(FRIEND_SDK_MANAGEMENT_DLL) && FRIEND_SDK_MANAGEMENT_DLL
#  if defined(FRIEND_SDK_MANAGEMENT_NATIVE) && FRIEND_SDK_MANAGEMENT_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(router_friend_manager)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(router_friend_manager)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(router_friend_manager)
#endif

 public:
  using base_type = router_manager<router_friend_cache, friend_cache, router_friend_private_type *>;
  using base_type::cache_t;
  using base_type::flag_t;
  using base_type::key_t;
  using base_type::object_ptr_t;
  using base_type::priv_data_t;
  using base_type::ptr_t;
  using base_type::store_ptr_t;
  using self_type = router_friend_manager;

  using create_object_fn_t = std::function<router_friend_cache::object_ptr_t(rpc::context &, uint32_t, uint64_t)>;

 public:
  FRIEND_SDK_MANAGEMENT_API router_friend_manager();
  FRIEND_SDK_MANAGEMENT_API const char *name() const override;

  FRIEND_SDK_MANAGEMENT_API bool is_auto_mutable_object() const override;
  FRIEND_SDK_MANAGEMENT_API uint64_t get_default_router_server_id(const key_t &key) const override;

  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type remove_friend_object(
      rpc::context &ctx, uint32_t zone_id, uint64_t user_id, priv_data_t priv_data);

  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type remove_friend_object(
      rpc::context &ctx, uint32_t zone_id, uint64_t user_id, std::shared_ptr<router_object_base> cache,
      priv_data_t priv_data);

  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type remove_friend_cache(
      rpc::context &ctx, uint32_t zone_id, uint64_t user_id, priv_data_t priv_data);

  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type remove_friend_cache(
      rpc::context &ctx, uint32_t zone_id, uint64_t user_id, std::shared_ptr<router_object_base> cache,
      priv_data_t priv_data);

  FRIEND_SDK_MANAGEMENT_API void set_create_object_fn(create_object_fn_t fn);

  FRIEND_SDK_MANAGEMENT_API bool has_custom_create_object_fn() const;

  FRIEND_SDK_MANAGEMENT_API router_friend_cache::object_ptr_t create_friend_object(rpc::context &ctx, uint32_t zone_id,
                                                                                   uint64_t user_id);

  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type pull_online_server(
      rpc::context &ctx, const key_t &key, uint64_t &router_svr_id, uint64_t &router_svr_ver) override;

 private:
  create_object_fn_t create_fn_;
};

}  // namespace friend_api
}  // namespace atframework

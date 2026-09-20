// Copyright 2026 atframework
// Created by owent on 2026-09-18

#pragma once

#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <data/friend_cache.h>
#include <rpc/rpc_shared_message.h>

#include <router/router_object.h>

namespace atframework {
namespace friend_api {
struct ATFW_UTIL_SYMBOL_VISIBLE router_friend_private_type {
  FRIEND_SDK_MANAGEMENT_API router_friend_private_type();
  FRIEND_SDK_MANAGEMENT_API router_friend_private_type(rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> *tb,
                                                       uint64_t *ver);

  rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> *friend_tb;
  uint64_t *friend_ver;
};

class ATFW_UTIL_SYMBOL_VISIBLE router_friend_cache : public router_object<friend_cache, router_friend_cache> {
 public:
  using base_type = router_object<friend_cache, router_friend_cache>;
  using base_type::flag_guard;
  using base_type::flag_t;
  using base_type::key_t;
  using base_type::object_ptr_t;
  using base_type::ptr_t;
  using self_type = router_friend_cache;

 public:
  FRIEND_SDK_MANAGEMENT_API explicit router_friend_cache(rpc::context &ctx, uint32_t zone_id, uint64_t user_id);
  FRIEND_SDK_MANAGEMENT_API explicit router_friend_cache(rpc::context &ctx, const key_t &key);

  FRIEND_SDK_MANAGEMENT_API const char *name() const override;

  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type pull_cache(rpc::context &ctx,
                                                                                          void *priv_data) override;
  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type pull_cache(
      rpc::context &ctx, router_friend_private_type &priv_data);
  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type pull_object(rpc::context &ctx,
                                                                                           void *priv_data) override;
  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type pull_object(
      rpc::context &ctx, router_friend_private_type &priv_data);

  ATFW_EXPLICIT_NODISCARD_ATTR FRIEND_SDK_MANAGEMENT_API rpc::result_code_type save_object(rpc::context &ctx,
                                                                                           void *priv_data) override;

 private:
  void fix_router_timeout(rpc::context &ctx, PROJECT_NAMESPACE_ID::table_friend &table);
};

}  // namespace friend_api
}  // namespace atframework

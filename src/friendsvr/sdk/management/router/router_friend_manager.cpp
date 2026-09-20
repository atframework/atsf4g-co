// Copyright 2026 atframework
// Created by owent on 2026-09-18

#include "router/router_friend_manager.h"

#include <config/extern_service_types.h>

#include <rpc/db/local_db_interface.atfw.gen.h>

#include <logic/hpa/logic_hpa_easy_api.h>
#include <logic/logic_server_setup.h>

#include <memory>
#include <utility>

#include "data/friend_cache.h"

namespace atframework {
namespace friend_api {

#if defined(FRIEND_SDK_MANAGEMENT_DLL) && FRIEND_SDK_MANAGEMENT_DLL
#  if defined(FRIEND_SDK_MANAGEMENT_NATIVE) && FRIEND_SDK_MANAGEMENT_NATIVE
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(router_friend_manager);
#  else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(router_friend_manager);
#  endif
#else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(router_friend_manager);
#endif

FRIEND_SDK_MANAGEMENT_API router_friend_manager::router_friend_manager()
    : base_type(PROJECT_NAMESPACE_ID::EN_ROT_FRIEND) {}

FRIEND_SDK_MANAGEMENT_API const char *router_friend_manager::name() const { return "[friend router manager]"; }

FRIEND_SDK_MANAGEMENT_API bool router_friend_manager::is_auto_mutable_object() const { return true; }

FRIEND_SDK_MANAGEMENT_API uint64_t
router_friend_manager::get_default_router_server_id(EXPLICIT_UNUSED_ATTR const key_t &key) const {
  auto *mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return 0;
  }

  unsigned char buffer[sizeof(router_object_base::key_t)] = {0};
  memcpy(buffer, &key.object_id, sizeof(key.object_id));
  memcpy(buffer + sizeof(uint64_t), &key.zone_id, sizeof(key.zone_id));
  memcpy(buffer + sizeof(uint64_t) + sizeof(uint32_t), &key.type_id, sizeof(key.type_id));

  atapp::etcd_discovery_node::ptr_t discovery;

  auto discovery_set = mod->get_discovery_index_by_type_zone(
      static_cast<uint64_t>(atfw::component::logic_service_type::kFriendManagementSvr), key.zone_id);
  if (discovery_set) {
    discovery = discovery_set->get_node_by_consistent_hash(
        buffer,
        logic_hpa_discovery_select(static_cast<uint64_t>(atfw::component::logic_service_type::kFriendManagementSvr),
                                   logic_hpa_discovery_select_mode::kReady));
  }

  // 全局全服和跨区合服支持
  if (!discovery) {
    discovery_set = mod->get_discovery_index_by_type(
        static_cast<uint64_t>(atfw::component::logic_service_type::kFriendManagementSvr));
    if (discovery_set) {
      discovery = discovery_set->get_node_by_consistent_hash(
          buffer,
          logic_hpa_discovery_select(static_cast<uint64_t>(atfw::component::logic_service_type::kFriendManagementSvr),
                                     logic_hpa_discovery_select_mode::kReady));
    }
  }

  if (!discovery) {
    FWLOGWARNING("router_friend_manager::get_default_router_server_id for {}:{} but not found", key.object_id,
                 key.zone_id);
    return 0;
  }
  return discovery->get_discovery_info().id();
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_manager::remove_friend_object(rpc::context &ctx,
                                                                                            uint32_t zone_id,
                                                                                            uint64_t user_id,
                                                                                            priv_data_t priv_data) {
  return remove_friend_object(ctx, zone_id, user_id, nullptr, priv_data);
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_manager::remove_friend_object(
    rpc::context &ctx, uint32_t zone_id, uint64_t user_id, std::shared_ptr<router_object_base> cache,
    priv_data_t priv_data) {
  key_t key(get_type_id(), zone_id, user_id);
  return remove_object(ctx, key, std::move(cache), priv_data);
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_manager::remove_friend_cache(rpc::context &ctx,
                                                                                           uint32_t zone_id,
                                                                                           uint64_t user_id,
                                                                                           priv_data_t priv_data) {
  return remove_friend_cache(ctx, zone_id, user_id, nullptr, priv_data);
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_manager::remove_friend_cache(
    rpc::context &ctx, uint32_t zone_id, uint64_t user_id, std::shared_ptr<router_object_base> cache,
    priv_data_t priv_data) {
  key_t key(get_type_id(), zone_id, user_id);
  return remove_cache(ctx, key, std::move(cache), priv_data);
}

FRIEND_SDK_MANAGEMENT_API void router_friend_manager::set_create_object_fn(create_object_fn_t fn) {
  create_fn_ = std::move(fn);
}

FRIEND_SDK_MANAGEMENT_API bool router_friend_manager::has_custom_create_object_fn() const { return !!create_fn_; }

FRIEND_SDK_MANAGEMENT_API router_friend_cache::object_ptr_t router_friend_manager::create_friend_object(
    rpc::context &ctx, uint32_t zone_id, uint64_t user_id) {
  router_friend_cache::object_ptr_t ret;
  if (create_fn_) {
    ret = create_fn_(ctx, zone_id, user_id);
  }

  if (!ret) {
    ret = friend_cache::create(ctx, zone_id, user_id);
  }

  return ret;
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_manager::pull_online_server(rpc::context &ctx,
                                                                                          const key_t &key,
                                                                                          uint64_t &router_svr_id,
                                                                                          uint64_t &router_svr_ver) {
  router_svr_id = 0;
  router_svr_ver = 0;

  rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> local_db_tb_ptr{ctx};

  uint64_t local_db_ver = 0;

  auto ret = RPC_AWAIT_CODE_RESULT(
      rpc::db::user_friend::partly_get_basic_info(ctx, key.zone_id, key.object_id, *local_db_tb_ptr, local_db_ver));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  router_svr_id = local_db_tb_ptr->router_server_id();
  router_svr_ver = local_db_tb_ptr->router_version();

  ptr_t cache = get_cache(key);
  if (cache && !cache->is_writable()) {
    cache->set_router_server_id(router_svr_id, router_svr_ver);
  }

  RPC_RETURN_CODE(ret);
}

}  // namespace friend_api
}  // namespace atframework

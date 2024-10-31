// Copyright 2021 atframework
// Created by owent on 2018-05-07.
//

#include "router/router_player_manager.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <logic/session_manager.h>
#include <proto_base.h>

#include <rpc/db/login.h>
#include <rpc/db/player.h>
#include <rpc/rpc_utils.h>

#if defined(DS_BATTLE_SDK_DLL) && DS_BATTLE_SDK_DLL
#  if defined(DS_BATTLE_SDK_NATIVE) && DS_BATTLE_SDK_NATIVE
UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(router_player_manager);
#  else
UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(router_player_manager);
#  endif
#else
UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(router_player_manager);
#endif

SERVER_FRAME_CONFIG_API router_player_manager::router_player_manager()
    : base_type(PROJECT_NAMESPACE_ID::EN_ROT_PLAYER) {}

SERVER_FRAME_CONFIG_API router_player_manager::~router_player_manager() {}

SERVER_FRAME_CONFIG_API const char *router_player_manager::name() const { return "[player_cache router manager]"; }

SERVER_FRAME_CONFIG_API rpc::result_code_type router_player_manager::remove_player_object(rpc::context &ctx,
                                                                                          uint64_t user_id,
                                                                                          uint32_t zone_id,
                                                                                          priv_data_t priv_data) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(remove_player_object(ctx, user_id, zone_id, nullptr, priv_data)));
}

SERVER_FRAME_CONFIG_API rpc::result_code_type router_player_manager::remove_player_object(
    rpc::context &ctx, uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache,
    priv_data_t priv_data) {
  key_t key(get_type_id(), zone_id, user_id);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(remove_object(ctx, key, cache, priv_data)));
}

SERVER_FRAME_CONFIG_API rpc::result_code_type router_player_manager::remove_player_cache(rpc::context &ctx,
                                                                                         uint64_t user_id,
                                                                                         uint32_t zone_id,
                                                                                         priv_data_t priv_data) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(remove_player_cache(ctx, user_id, zone_id, nullptr, priv_data)));
}

SERVER_FRAME_CONFIG_API rpc::result_code_type router_player_manager::remove_player_cache(
    rpc::context &ctx, uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache,
    priv_data_t priv_data) {
  key_t key(get_type_id(), zone_id, user_id);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(remove_cache(ctx, key, cache, priv_data)));
}

SERVER_FRAME_CONFIG_API void router_player_manager::set_create_object_fn(create_object_fn_t fn) { create_fn_ = fn; }

SERVER_FRAME_CONFIG_API router_player_cache::object_ptr_t router_player_manager::create_player_object(
    uint64_t user_id, uint32_t zone_id, const std::string &openid) {
  router_player_cache::object_ptr_t ret;
  if (create_fn_) {
    ret = create_fn_(user_id, zone_id, openid);
  }

  if (!ret) {
    ret = player_cache::create(user_id, zone_id, openid);
  }

  return ret;
}

rpc::result_code_type router_player_manager::on_evt_remove_object(rpc::context &ctx, const key_t &key,
                                                                  const ptr_t &cache, priv_data_t priv_data) {
  player_cache::ptr_t obj = cache->get_object();
  // 释放本地数据, 下线相关Session
  session::ptr_t s = obj->get_session();
  if (s) {
    obj->set_session(ctx, nullptr);
    std::shared_ptr<player_cache> check_binded_user = s->get_player();
    if (!check_binded_user || check_binded_user == obj) {
      s->set_player(nullptr);
      session_manager::me()->remove(s, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    }
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(base_type::on_evt_remove_object(ctx, key, cache, priv_data)));
}

SERVER_FRAME_CONFIG_API rpc::result_code_type router_player_manager::pull_online_server(rpc::context &, const key_t &,
                                                                                        uint64_t &router_svr_id,
                                                                                        uint64_t &router_svr_ver) {
  router_svr_id = 0;
  router_svr_ver = 0;

  /**
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login> local_login_tb{ctx};
  std::string        local_login_ver;
  PROJECT_NAMESPACE_ID::table_user  tbu;

  // ** 如果login表和user表的jey保持一致的话也可以直接从login表取
  int ret = RPC_AWAIT_CODE_RESULT(rpc::db::player::get_basic(key.object_id, key.zone_id, tbu));
  if (ret < 0) {
      return ret;
  }

  ret = RPC_AWAIT_CODE_RESULT(rpc::db::login::get(tbu.open_id().c_str(), key.zone_id, local_login_tb, local_login_ver));
  if (ret < 0) {
      return ret;
  }

  router_svr_id  = local_login_tb.router_server_id();
  router_svr_ver = local_login_tb.router_version();

  ptr_t cache = get_cache(key);
  if (cache && !cache->is_writable()) {
      cache->set_router_server_id(router_svr_id, router_svr_ver);
  }

  return ret;
  */

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

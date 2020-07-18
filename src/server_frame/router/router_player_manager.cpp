//
// Created by owt50 on 2018/05/07.
//

#include <protocol/pbdesc/svr.const.pb.h>

#include <logic/session_manager.h>
#include <proto_base.h>

#include <rpc/db/login.h>
#include <rpc/db/player.h>

#include "router_player_manager.h"

router_player_manager::router_player_manager() : base_type(hello::EN_ROT_PLAYER) {}

const char *router_player_manager::name() const {
    return "[player_cache router manager]";
    ;
}

bool router_player_manager::remove_player_object(uint64_t user_id, uint32_t zone_id, priv_data_t priv_data) {
#if defined(UTIL_CONFIG_COMPILER_CXX_NULLPTR) && UTIL_CONFIG_COMPILER_CXX_NULLPTR
    return remove_player_object(user_id, zone_id, nullptr, priv_data);
#else
    return remove_player_object(user_id, zone_id, NULL, priv_data);
#endif
}

bool router_player_manager::remove_player_object(uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache, priv_data_t priv_data) {
    key_t key(get_type_id(), zone_id, user_id);
    return remove_object(key, cache, priv_data);
}

bool router_player_manager::remove_player_cache(uint64_t user_id, uint32_t zone_id, priv_data_t priv_data) {
#if defined(UTIL_CONFIG_COMPILER_CXX_NULLPTR) && UTIL_CONFIG_COMPILER_CXX_NULLPTR
    return remove_player_cache(user_id, zone_id, nullptr, priv_data);
#else
    return remove_player_cache(user_id, zone_id, NULL, priv_data);
#endif
}

bool router_player_manager::remove_player_cache(uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache, priv_data_t priv_data) {
    key_t key(get_type_id(), zone_id, user_id);
    return remove_cache(key, cache, priv_data);
}

void router_player_manager::set_create_object_fn(create_object_fn_t fn) { create_fn_ = fn; }

router_player_cache::object_ptr_t router_player_manager::create_player_object(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
    router_player_cache::object_ptr_t ret;
    if (create_fn_) {
        ret = create_fn_(user_id, zone_id, openid);
    }

    if (!ret) {
        ret = player_cache::create(user_id, zone_id, openid);
    }

    return ret;
}

void router_player_manager::on_evt_remove_object(const key_t &key, const ptr_t &cache, priv_data_t priv_data) {
    player_cache::ptr_t obj = cache->get_object();
    // 释放本地数据, 下线相关Session
    session::ptr_t s = obj->get_session();
    if (s) {
        obj->set_session(NULL);
        s->set_player(NULL);
        session_manager::me()->remove(s, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    }

    base_type::on_evt_remove_object(key, cache, priv_data);
}

int router_player_manager::pull_online_server(const key_t &key, uint64_t &router_svr_id, uint64_t &router_svr_ver) {
    router_svr_id  = 0;
    router_svr_ver = 0;

    /**
    hello::table_login local_login_tb;
    std::string        local_login_ver;
    hello::table_user  tbu;

    // ** 如果login表和user表的jey保持一致的话也可以直接从login表取
    int ret = rpc::db::player::get_basic(key.object_id, key.zone_id, tbu);
    if (ret < 0) {
        return ret;
    }

    ret = rpc::db::login::get(tbu.open_id().c_str(), key.zone_id, local_login_tb, local_login_ver);
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

    return 0;
}

//
// Created by owt50 on 2018/05/07.
//

#ifndef ROUTER_ROUTER_PLAYER_CACHE_H
#define ROUTER_ROUTER_PLAYER_CACHE_H

#pragma once

#include <data/player_cache.h>

#include "router_object.h"

struct router_player_private_type {
    router_player_private_type();
    router_player_private_type(hello::table_login *tb, std::string *ver);

    hello::table_login *login_tb;
    std::string *       login_ver;
};

class router_player_cache : public router_object<player_cache, router_player_cache> {
public:
    typedef router_object<player_cache, router_player_cache> base_type;
    typedef base_type::key_t                                 key_t;
    typedef base_type::flag_t                                flag_t;
    typedef base_type::object_ptr_t                          object_ptr_t;
    typedef base_type::ptr_t                                 ptr_t;
    typedef router_player_cache                              self_type;
    typedef base_type::flag_guard                            flag_guard;

public:
    router_player_cache(uint64_t user_id, uint32_t zone_id, const std::string &openid);
    explicit router_player_cache(const key_t &key);

    virtual const char *name() const UTIL_CONFIG_OVERRIDE;

    virtual int pull_cache(void *priv_data) UTIL_CONFIG_OVERRIDE;
    int         pull_cache(router_player_private_type &priv_data);
    virtual int pull_object(void *priv_data) UTIL_CONFIG_OVERRIDE;
    int         pull_object(router_player_private_type &priv_data);

    virtual int save_object(void *priv_data) UTIL_CONFIG_OVERRIDE;
};


#endif // ROUTER_ROUTER_PLAYER_CACHE_H

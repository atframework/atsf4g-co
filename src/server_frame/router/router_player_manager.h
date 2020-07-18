//
// Created by owt50 on 2018/05/07.
//

#ifndef ROUTER_ROUTER_PLAYER_MANAGER_H
#define ROUTER_ROUTER_PLAYER_MANAGER_H

#pragma once

#include <design_pattern/singleton.h>
#include <std/functional.h>

#include "router_manager.h"
#include "router_player_cache.h"

class router_player_manager : public router_manager<router_player_cache, player_cache, router_player_private_type *>,
                              public util::design_pattern::singleton<router_player_manager> {
public:
    typedef router_manager<router_player_cache, player_cache, router_player_private_type *> base_type;
    typedef base_type::cache_t                                                              cache_t;
    typedef base_type::priv_data_t                                                          priv_data_t;
    typedef base_type::key_t                                                                key_t;
    typedef base_type::flag_t                                                               flag_t;
    typedef base_type::object_ptr_t                                                         object_ptr_t;
    typedef base_type::ptr_t                                                                ptr_t;
    typedef base_type::store_ptr_t                                                          store_ptr_t;
    typedef router_player_manager                                                           self_type;

    typedef std::function<router_player_cache::object_ptr_t(uint64_t, uint32_t, const std::string &)> create_object_fn_t;

public:
    router_player_manager();
    virtual const char *name() const UTIL_CONFIG_OVERRIDE;

    bool remove_player_object(uint64_t user_id, uint32_t zone_id, priv_data_t priv_data);

    bool remove_player_object(uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache, priv_data_t priv_data);

    bool remove_player_cache(uint64_t user_id, uint32_t zone_id, priv_data_t priv_data);

    bool remove_player_cache(uint64_t user_id, uint32_t zone_id, std::shared_ptr<router_object_base> cache, priv_data_t priv_data);

    void set_create_object_fn(create_object_fn_t fn);

    router_player_cache::object_ptr_t create_player_object(uint64_t user_id, uint32_t zone_id, const std::string &openid);

    virtual int pull_online_server(const key_t &key, uint64_t &router_svr_id, uint64_t &router_svr_ver) UTIL_CONFIG_OVERRIDE;

private:
    virtual void on_evt_remove_object(const key_t &key, const ptr_t &cache, priv_data_t priv_data) UTIL_CONFIG_OVERRIDE;

private:
    create_object_fn_t create_fn_;
};


#endif // ROUTER_ROUTER_PLAYER_MANAGER_H

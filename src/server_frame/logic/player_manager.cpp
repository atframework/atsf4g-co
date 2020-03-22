#include <log/log_wrapper.h>
#include <proto_base.h>
#include <time/time_utility.h>


#include "protocol/pbdesc/svr.const.err.pb.h"

#include <config/logic_config.h>
#include <dispatcher/task_manager.h>
#include <rpc/db/login.h>
#include <rpc/db/player.h>


#include <router/router_player_manager.h>

#include "player_manager.h"
#include "session_manager.h"

bool player_manager::remove(player_manager::player_ptr_t u, bool force_kickoff) {
    if (!u) {
        return false;
    }

    return remove(u->get_user_id(), u->get_zone_id(), force_kickoff);
}

bool player_manager::remove(uint64_t user_id, uint32_t zone_id, bool force_kickoff) {
    if (0 == user_id) {
        return false;
    }

    router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);

    router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);
    // 先保存用户数据，防止重复保存
    if (!cache) {
        return true;
    }

    if (!force_kickoff && !cache->is_writable()) {
        return true;
    }

    // 这里会触发保存
    if (force_kickoff) {
        return router_player_manager::me()->remove_player_cache(user_id, zone_id, cache, NULL);
    } else {
        return router_player_manager::me()->remove_player_object(user_id, zone_id, NULL);
    }
}

bool player_manager::save(uint64_t user_id, uint32_t zone_id) {
    router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
    router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

    if (!cache || !cache->is_writable()) {
        return false;
    }

    int res = cache->save(NULL);
    if (res < 0) {
        WLOGERROR("save player_cache %u:%llu failed, res: %d", zone_id, static_cast<unsigned long long>(user_id), res);
        return false;
    }

    return true;
}

player_manager::player_ptr_t player_manager::load(uint64_t user_id, uint32_t zone_id, bool force) {
    router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
    router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

    if (force || !cache) {
        int res = router_player_manager::me()->mutable_object(cache, key, NULL);
        if (res < 0) {
            return NULL;
        }
    }

    if (cache) {
        return cache->get_object();
    }

    return NULL;
}

size_t player_manager::size() const { return router_player_manager::me()->size(); }

player_manager::player_ptr_t player_manager::create(uint64_t user_id, uint32_t zone_id, const std::string &openid, hello::table_login &login_tb,
                                                    std::string &login_ver) {
    if (0 == user_id || openid.empty()) {
        WLOGERROR("can not create player_cache without user id or open id");
        return NULL;
    }

    if (find(user_id, zone_id)) {
        WLOGERROR("player_cache %u:%llu already exists, can not create again", zone_id, static_cast<unsigned long long>(user_id));
        return NULL;
    }

    // online user number limit
    if (size() > logic_config::me()->get_cfg_logic().player_max_online_number) {
        WLOGERROR("online number extended");
        return NULL;
    }

    router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
    router_player_cache::ptr_t cache;
    router_player_private_type priv_data(&login_tb, &login_ver);

    int res = router_player_manager::me()->mutable_object(cache, key, &priv_data);
    if (res < 0 || !cache) {
        WLOGERROR("pull player_cache %s(%u:%llu) object failed, res: %d", openid.c_str(), zone_id, static_cast<unsigned long long>(user_id), res);
        return NULL;
    }

    player_ptr_t ret = cache->get_object();
    if (!ret) {
        WLOGERROR("player_cache %s(%u:%llu) already exists(data version=%u), can not create again", openid.c_str(), zone_id,
                  static_cast<unsigned long long>(user_id), ret->get_data_version());
        return NULL;
    }

    // 新用户，数据版本号为0，启动创建初始化
    if (0 == ret->get_data_version()) {
        // manager 创建初始化
        if (ret->get_login_info().account().version_type() >= hello::EN_VERSION_INNER) {
            ret->create_init(hello::EN_VERSION_DEFAULT);
        } else {
            ret->create_init(ret->get_login_info().account().version_type());
        }

        // 初始化完成，保存一次
        res = cache->save(NULL);
        if (res < 0) {
            WLOGERROR("save player_cache %s(%u:%llu) object failed, res: %d", openid.c_str(), zone_id, static_cast<unsigned long long>(user_id), res);
            return NULL;
        }

        WLOGINFO("create player %s(%u:%llu) success", openid.c_str(), zone_id, static_cast<unsigned long long>(user_id));
    }

    return ret;
}

player_manager::player_ptr_t player_manager::find(uint64_t user_id, uint32_t zone_id) const {
    router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
    router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

    if (cache && cache->is_writable()) {
        return cache->get_object();
    }

    return NULL;
}

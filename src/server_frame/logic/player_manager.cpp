// Copyright 2021 atframework

#include "logic/player_manager.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>
#include <proto_base.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <dispatcher/task_manager.h>
#include <rpc/db/login.h>
#include <rpc/db/player.h>
#include <utility/protobuf_mini_dumper.h>

#include <router/router_manager_set.h>
#include <router/router_player_manager.h>

#include "logic/session_manager.h"

bool player_manager::remove(player_manager::player_ptr_t u, bool force_kickoff) {
  if (!u) {
    return false;
  }

  return remove(u->get_user_id(), u->get_zone_id(), force_kickoff, u.get());
}

bool player_manager::remove(uint64_t user_id, uint32_t zone_id, bool force_kickoff, player_cache *check_user) {
  if (0 == user_id) {
    return false;
  }

  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);

  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);
  // 先保存用户数据，防止重复保存
  if (!cache) {
    return true;
  }

  if (check_user != nullptr && false == cache->is_object_equal(*check_user)) {
    auto check_sess = check_user->get_session();
    check_user->set_session(nullptr);
    if (check_sess && check_sess->get_player().get() == check_user) {
      check_sess->set_player(nullptr);
      session_manager::me()->remove(check_sess, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    }
    return true;
  }

  if (!force_kickoff && !cache->is_writable()) {
    return true;
  }

  // 这里会触发保存
  if (force_kickoff) {
    return router_player_manager::me()->remove_player_cache(user_id, zone_id, cache, nullptr);
  } else {
    return router_player_manager::me()->remove_player_object(user_id, zone_id, nullptr);
  }
}

bool player_manager::save(uint64_t user_id, uint32_t zone_id, const player_cache *check_user) {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  if (!cache || !cache->is_writable()) {
    return false;
  }

  if (check_user != nullptr && false == cache->is_object_equal(*check_user)) {
    return false;
  }

  int res = cache->save(nullptr);
  if (res < 0) {
    FWLOGERROR("save player_cache {}:{} failed, res: {}({})", zone_id, user_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    return false;
  }

  return true;
}

bool player_manager::add_save_schedule(uint64_t user_id, uint32_t zone_id) {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  if (!cache || !cache->is_writable()) {
    return false;
  }

  return router_manager_set::me()->add_save_schedule(std::static_pointer_cast<router_object_base>(cache));
}

player_manager::player_ptr_t player_manager::load(uint64_t user_id, uint32_t zone_id, bool force) {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  if (force || !cache) {
    int res = router_player_manager::me()->mutable_object(cache, key, nullptr);
    if (res < 0) {
      return nullptr;
    }
  }

  if (cache) {
    return cache->get_object();
  }

  return nullptr;
}

size_t player_manager::size() const { return router_player_manager::me()->size(); }

player_manager::player_ptr_t player_manager::create(uint64_t user_id, uint32_t zone_id, const std::string &openid,
                                                    hello::table_login &login_tb, std::string &login_ver) {
  if (0 == user_id || openid.empty()) {
    FWLOGERROR("can not create player_cache without user id or open id");
    return nullptr;
  }

  if (find(user_id, zone_id)) {
    FWLOGERROR("player_cache {}:{} already exists, can not create again", zone_id, user_id);
    return nullptr;
  }

  // online user number limit
  if (size() > logic_config::me()->get_logic().user().max_online()) {
    FWLOGERROR("online number extended");
    return nullptr;
  }

  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache;
  router_player_private_type priv_data(&login_tb, &login_ver);

  int res = router_player_manager::me()->mutable_object(cache, key, &priv_data);
  if (res < 0 || !cache) {
    FWLOGERROR("pull player_cache {}:{} object failed, res: {}({})", zone_id, user_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    return nullptr;
  }

  player_ptr_t ret = cache->get_object();
  if (!ret) {
    FWLOGERROR("player_cache {}:{} already exists(data version={}), can not create again", zone_id, user_id,
               ret->get_data_version());
    return nullptr;
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
    res = cache->save(nullptr);
    if (res < 0) {
      FWLOGERROR("save player_cache {}:{} object failed, res: {}({})", zone_id, user_id, res,
                 protobuf_mini_dumper_get_error_msg(res));
      return nullptr;
    }

    FWLOGINFO("create player {}:{} success", zone_id, user_id);
  }

  return ret;
}

player_manager::player_ptr_t player_manager::find(uint64_t user_id, uint32_t zone_id) const {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  if (cache && cache->is_writable()) {
    return cache->get_object();
  }

  return nullptr;
}

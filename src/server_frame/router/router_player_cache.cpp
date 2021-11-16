// Copyright 2021 atframework
// Created by owent on 2018/05/07.
//

#include "router/router_player_cache.h"

#include <config/logic_config.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <proto_base.h>
#include <rpc/db/login.h>

#include <logic/session_manager.h>
#include <rpc/db/player.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "router/router_player_manager.h"

router_player_private_type::router_player_private_type() : login_tb(nullptr), login_ver(nullptr) {}
router_player_private_type::router_player_private_type(PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login *tb,
                                                       std::string *ver)
    : login_tb(tb), login_ver(ver) {}

router_player_cache::router_player_cache(uint64_t user_id, uint32_t zone_id, const std::string &openid)
    : base_type(router_player_manager::me()->create_player_object(user_id, zone_id, openid),
                key_t(router_player_manager::me()->get_type_id(), zone_id, user_id)) {}

// 这个时候openid无效，后面需要再init一次
router_player_cache::router_player_cache(const key_t &key)
    : base_type(router_player_manager::me()->create_player_object(key.object_id, key.zone_id, ""), key) {}

const char *router_player_cache::name() const { return "[player  router cache]"; }

int router_player_cache::pull_cache(void *priv_data) {
  if (nullptr == priv_data) {
    router_player_private_type local_priv_data;
    return pull_cache(local_priv_data);
  }

  return pull_cache(*reinterpret_cast<router_player_private_type *>(priv_data));
}

int router_player_cache::pull_cache(router_player_private_type &priv_data) {
  ::rpc::context ctx;
  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login local_login_tb;
  std::string local_login_ver;
  if (nullptr == priv_data.login_ver) {
    priv_data.login_ver = &local_login_ver;
  }

  // 先尝试从数据库读数据
  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user tbu;
  int res = rpc::db::player::get_basic(ctx, get_key().object_id, get_key().zone_id, tbu);
  if (res < 0) {
    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FWLOGERROR("load player_cache data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
                 res);
    }
    return res;
  }

  player_cache::ptr_t obj = get_object();
  if (obj->get_open_id().empty()) {
    obj->init(get_key().object_id, get_key().zone_id, tbu.open_id());
  }

  if (nullptr == priv_data.login_tb) {
    priv_data.login_tb = &local_login_tb;
    int ret = rpc::db::login::get(ctx, obj->get_open_id().c_str(), get_key().zone_id, *priv_data.login_tb,
                                  *priv_data.login_ver);
    if (ret < 0) {
      return ret;
    }
  }

  // 设置路由ID
  set_router_server_id(priv_data.login_tb->router_server_id(), priv_data.login_tb->router_version());

  obj->load_and_move_login_info(COPP_MACRO_STD_MOVE(*priv_data.login_tb), *priv_data.login_ver);

  // table_login内的平台信息复制到player里
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
    obj->init_from_table_data(tbu);
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int router_player_cache::pull_object(void *priv_data) {
  if (nullptr == priv_data) {
    router_player_private_type local_priv_data;
    return pull_object(local_priv_data);
  }

  return pull_object(*reinterpret_cast<router_player_private_type *>(priv_data));
}

int router_player_cache::pull_object(router_player_private_type &priv_data) {
  ::rpc::context ctx;

  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login local_login_tb;
  std::string local_login_ver;
  if (nullptr == priv_data.login_ver) {
    priv_data.login_ver = &local_login_ver;
  }

  player_cache::ptr_t obj = get_object();
  if (!obj || !obj->can_be_writable()) {
    FWLOGERROR("pull_object for {}:{}:{} failed, error code: {}", get_key().type_id, get_key().zone_id,
               get_key().object_id, static_cast<int>(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY));
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY;
  }

  // 先尝试从数据库读数据
  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user tbu;
  int res = rpc::db::player::get_basic(ctx, get_key().object_id, get_key().zone_id, tbu);
  if (res < 0) {
    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FWLOGERROR("load player_cache data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
                 res);
      return res;
    } else if (nullptr != priv_data.login_tb) {
      // 创建用户走这里的流程
      tbu.set_open_id(priv_data.login_tb->open_id());
      tbu.set_user_id(priv_data.login_tb->user_id());
      protobuf_copy_message(*tbu.mutable_account(), priv_data.login_tb->account());
      res = 0;
    } else {
      return res;
    }
  }

  if (obj->get_open_id().empty()) {
    obj->init(get_key().object_id, get_key().zone_id, tbu.open_id());
  }

  if (nullptr == priv_data.login_tb) {
    priv_data.login_tb = &local_login_tb;
    int ret = rpc::db::login::get(ctx, obj->get_open_id().c_str(), obj->get_zone_id(), *priv_data.login_tb,
                                  *priv_data.login_ver);
    if (ret < 0) {
      return ret;
    }
  }

  // 拉取玩家数据
  // 设置路由ID
  set_router_server_id(priv_data.login_tb->router_server_id(), priv_data.login_tb->router_version());
  obj->load_and_move_login_info(COPP_MACRO_STD_MOVE(*priv_data.login_tb), *priv_data.login_ver);

  // table_login内的平台信息复制到player里
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
    obj->init_from_table_data(tbu);
  }

  uint64_t self_bus_id = logic_config::me()->get_local_server_id();
  // 如果router server id是0则设置为本地的登入地址
  if (0 == get_router_server_id()) {
    uint64_t old_router_server_id = obj->get_login_info().router_server_id();
    uint64_t old_router_ver = obj->get_login_info().router_version();

    obj->get_login_info().set_router_server_id(self_bus_id);
    obj->get_login_info().set_router_version(old_router_ver + 1);

    // 新登入则设置登入时间
    obj->get_login_info().set_login_time(util::time::time_utility::get_now());

    int ret = rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(), obj->get_login_info(),
                                  obj->get_login_version());
    if (ret < 0) {
      FWPLOGERROR(*obj, "save login data failed, msg:\n{}", obj->get_login_info().DebugString());
      // 失败则恢复路由信息
      obj->get_login_info().set_router_server_id(old_router_server_id);
      obj->get_login_info().set_router_version(old_router_ver);
      return ret;
    }

    set_router_server_id(obj->get_login_info().router_server_id(), obj->get_login_info().router_version());
  } else if (self_bus_id != get_router_server_id()) {
    // 不在这个进程上
    FWPLOGERROR(*obj, "is in server {:#x} but try to pull in server {:#x}", get_router_server_id(), self_bus_id);

    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_ROUTER_IN_OTHER_SERVER;
  }

  return 0;
}

int router_player_cache::save_object(void *priv_data) {
  ::rpc::context ctx;

  // 保存数据
  player_cache::ptr_t obj = object();
  if (!obj || !obj->can_be_writable()) {
    FWLOGERROR("save_object for {}:{}:{} failed, error code: {}", get_key().type_id, get_key().zone_id,
               get_key().object_id, static_cast<int>(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY));
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY;
  }

  uint64_t self_bus_id = logic_config::me()->get_local_server_id();
  // RPC read from DB(以后可以优化掉)
  int res = 0;
  // 异常的玩家数据记录，自动修复一下
  bool bad_data_kickoff = false;
  int try_times = 2;  // 其实并不需要重试，这里只是处理table_login过期后走更新流程
  while (try_times-- > 0) {
    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
      res = rpc::db::login::get(ctx, obj->get_open_id().c_str(), obj->get_zone_id(), obj->get_login_info(),
                                obj->get_login_version());
      if (res < 0) {
        FWPLOGERROR(*obj, "try load login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        return res;
      }
    }

    if (0 != get_router_server_id() && 0 != obj->get_login_info().router_server_id() &&
        obj->get_login_info().router_server_id() != self_bus_id) {
      bad_data_kickoff = true;
    }

    if (0 == obj->get_login_info().router_server_id() && 0 != get_router_server_id()) {
      FWPLOGERROR(*obj, "login bus id error(expected: {:#x}, real: {:#x})", get_router_server_id(),
                  obj->get_login_info().router_server_id());

      uint64_t old_router_server_id = obj->get_login_info().router_server_id();
      uint64_t old_router_ver = obj->get_login_info().router_version();

      obj->get_login_info().set_router_server_id(get_router_server_id());
      obj->get_login_info().set_router_version(old_router_ver + 1);
      // RPC save to db
      res = rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(), obj->get_login_info(),
                                obj->get_login_version());
      if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        continue;
      }

      if (res < 0) {
        FWPLOGERROR(*obj, "try set login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        return res;
      } else {
        set_router_server_id(obj->get_login_info().router_server_id(), obj->get_login_info().router_version());
        break;
      }
    }

    // 登出流程
    if (0 == get_router_server_id()) {
      uint64_t old_router_server_id = obj->get_login_info().router_server_id();
      uint64_t old_router_ver = obj->get_login_info().router_version();

      obj->get_login_info().set_router_server_id(0);
      obj->get_login_info().set_router_version(old_router_ver + 1);
      obj->get_login_info().set_logout_time(util::time::time_utility::get_now());  // 登出时间

      // RPC save to db
      res = rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(), obj->get_login_info(),
                                obj->get_login_version());
      if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        continue;
      }

      if (res < 0) {
        FWPLOGERROR(*obj, "try set login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        return res;
      } else {
        set_router_server_id(obj->get_login_info().router_server_id(), obj->get_login_info().router_version());
      }
    } else if (obj->get_session()) {  // 续期login code
      uint64_t old_router_server_id = obj->get_login_info().router_server_id();
      uint64_t old_router_ver = obj->get_login_info().router_version();

      if (get_router_server_id() != old_router_server_id) {
        obj->get_login_info().set_router_server_id(get_router_server_id());
        obj->get_login_info().set_router_version(old_router_ver + 1);
      }

      // 鉴权登入码续期
      obj->get_login_info().set_login_code_expired(
          util::time::time_utility::get_now() +
          logic_config::me()->get_logic().session().login_code_valid_sec().seconds());

      res = rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(), obj->get_login_info(),
                                obj->get_login_version());
      if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        continue;
      }

      if (res < 0) {
        FWPLOGERROR(*obj, "call login rpc method set failed, res: {}, msg: {}", res,
                    obj->get_login_info().DebugString());
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        return res;
      } else {
        set_router_server_id(obj->get_login_info().router_server_id(), obj->get_login_info().router_version());
      }
    }

    break;
  }

  if (bad_data_kickoff) {
    FWPLOGERROR(*obj, "login pd error(expected: {:#x}, real: {:#x})", self_bus_id,
                obj->get_login_info().router_server_id());

    // 在其他设备登入的要把这里的Session踢下线
    if (obj->get_session()) {
      obj->get_session()->send_kickoff(::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    }

    router_player_manager::me()->remove_object(get_key(), nullptr, nullptr);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE;
  }

  // 尝试保存用户数据
  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user user_tb;
  obj->dump(user_tb, true);

  FWPLOGDEBUG(*obj, "save curr data version: {}", obj->get_version());

  // RPC save to DB
  res = rpc::db::player::set(ctx, obj->get_user_id(), obj->get_zone_id(), user_tb, obj->get_version());

  // CAS 序号错误（可能是先超时再返回成功）,重试一次
  // 前面已经确认了当前用户在此处登入并且已经更新了版本号到版本信息
  // RPC save to DB again
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
    res = rpc::db::player::set(ctx, obj->get_user_id(), obj->get_zone_id(), user_tb, obj->get_version());
  }

  if (res < 0) {
    FWPLOGERROR(*obj, "try save db failed. res: {}, version: {}", res, obj->get_version());
  }

  if (res >= 0) {
    obj->on_saved();
  }

  return res;
}
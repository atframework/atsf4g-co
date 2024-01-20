// Copyright 2021 atframework
// Created by owent on 2018-05-07.
//

#include "router/router_player_cache.h"

#include <config/logic_config.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <proto_base.h>
#include <rpc/db/login.h>

#include <logic/session_manager.h>
#include <rpc/db/player.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "router/router_player_manager.h"

router_player_private_type::router_player_private_type() : login_tb(nullptr), login_ver(nullptr) {}
router_player_private_type::router_player_private_type(PROJECT_NAMESPACE_ID::table_login *tb, std::string *ver)
    : login_tb(tb), login_ver(ver) {}

router_player_cache::router_player_cache(uint64_t user_id, uint32_t zone_id, const std::string &openid)
    : base_type(router_player_manager::me()->create_player_object(user_id, zone_id, openid),
                key_t(router_player_manager::me()->get_type_id(), zone_id, user_id)) {}

// 这个时候openid无效，后面需要再init一次
router_player_cache::router_player_cache(const key_t &key)
    : base_type(router_player_manager::me()->create_player_object(key.object_id, key.zone_id, ""), key) {}

const char *router_player_cache::name() const { return "[player  router cache]"; }

rpc::result_code_type router_player_cache::pull_cache(rpc::context &ctx, void *priv_data) {
  if (nullptr == priv_data) {
    router_player_private_type local_priv_data;
    return pull_cache(ctx, local_priv_data);
  }

  return pull_cache(ctx, *reinterpret_cast<router_player_private_type *>(priv_data));
}

rpc::result_code_type router_player_cache::pull_cache(rpc::context &ctx, router_player_private_type &priv_data) {
  PROJECT_NAMESPACE_ID::table_login *login_table_ptr = priv_data.login_tb;
  if (nullptr == login_table_ptr) {
    login_table_ptr = ctx.create<PROJECT_NAMESPACE_ID::table_login>();
  }

  std::string local_login_ver;
  std::string *local_login_ver_ptr = priv_data.login_ver;
  if (nullptr == local_login_ver_ptr) {
    local_login_ver_ptr = &local_login_ver;
  }

  // 先尝试从数据库读数据
  PROJECT_NAMESPACE_ID::table_user tbu;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::player::get_basic(ctx, get_key().object_id, get_key().zone_id, tbu));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FWLOGERROR("load player_cache data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
                 res);
    }
    RPC_RETURN_CODE(res);
  }

  player_cache::ptr_t obj = get_object();
  if (obj->get_open_id().empty()) {
    obj->init(get_key().object_id, get_key().zone_id, tbu.open_id());
  }

  if (0 == login_table_ptr->user_id() || 0 == login_table_ptr->zone_id()) {
    auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::login::get(ctx, obj->get_open_id().c_str(), get_key().zone_id,
                                                         *login_table_ptr, *local_login_ver_ptr));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  // 设置路由ID
  // 注意: 如果要改成同账号跨大区也不能多处登入（Login表跨大区），这里要判定当前zone_id是否和login表一致
  set_router_server_id(login_table_ptr->router_server_id(), login_table_ptr->router_version());

  obj->load_and_move_login_info(COPP_MACRO_STD_MOVE(*login_table_ptr), *local_login_ver_ptr);
  login_table_ptr->set_user_id(0);
  login_table_ptr->set_zone_id(0);

  // table_login内的平台信息复制到player里
  if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
    obj->init_from_table_data(ctx, tbu);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type router_player_cache::pull_object(rpc::context &ctx, void *priv_data) {
  if (nullptr == priv_data) {
    router_player_private_type local_priv_data;
    return pull_object(ctx, local_priv_data);
  }

  return pull_object(ctx, *reinterpret_cast<router_player_private_type *>(priv_data));
}

rpc::result_code_type router_player_cache::pull_object(rpc::context &ctx, router_player_private_type &priv_data) {
  PROJECT_NAMESPACE_ID::table_login *login_table_ptr = priv_data.login_tb;
  if (nullptr == login_table_ptr) {
    login_table_ptr = ctx.create<PROJECT_NAMESPACE_ID::table_login>();
  }

  std::string local_login_ver;
  std::string *local_login_ver_ptr = priv_data.login_ver;
  if (nullptr == local_login_ver_ptr) {
    local_login_ver_ptr = &local_login_ver;
  }

  player_cache::ptr_t obj = get_object();
  if (!obj || !obj->can_be_writable()) {
    FWLOGERROR("pull_object for {}:{}:{} failed, error code: {}", get_key().type_id, get_key().zone_id,
               get_key().object_id, static_cast<int>(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  // 异常冲突保护，如果需要处理合服逻辑，这里要允许mapping的服务大区
  if (get_key().zone_id != logic_config::me()->get_local_zone_id()) {
    FWLOGERROR("pull_object for {}:{} failed, should not pulled on local zone {}", get_key().zone_id,
               get_key().object_id, logic_config::me()->get_local_zone_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  // 先尝试从数据库读数据
  PROJECT_NAMESPACE_ID::table_user tbu;
  std::string tbu_version;
  auto res =
      RPC_AWAIT_CODE_RESULT(rpc::db::player::get_basic(ctx, get_key().object_id, get_key().zone_id, tbu, &tbu_version));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FWLOGERROR("load player_cache data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
                 res);
      RPC_RETURN_CODE(res);
    } else if (nullptr != priv_data.login_tb) {
      // 创建用户走这里的流程
      if (!priv_data.login_tb->open_id().empty()) {
        tbu.set_open_id(priv_data.login_tb->open_id());
      } else if (!obj->get_open_id().empty()) {
        // 重试流程
        tbu.set_open_id(obj->get_open_id());
      } else {
        // Fallback
        tbu.set_open_id(util::log::format("{}", get_key().object_id));
      }
      tbu.set_user_id(get_key().object_id);
      tbu.set_zone_id(get_key().zone_id);
      protobuf_copy_message(*tbu.mutable_account(), login_table_ptr->account());
      res = 0;
    } else {
      RPC_RETURN_CODE(res);
    }
  } else if (nullptr != login_table_ptr) {
    // 修复数据
    tbu.set_open_id(login_table_ptr->open_id());
    tbu.set_user_id(login_table_ptr->user_id());
    protobuf_copy_message(*tbu.mutable_account(), login_table_ptr->account());
  }

  if (obj->get_open_id().empty()) {
    obj->init(get_key().object_id, get_key().zone_id, tbu.open_id());
  }

  if (0 == login_table_ptr->user_id() || 0 == login_table_ptr->zone_id()) {
    auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::login::get(ctx, obj->get_open_id().c_str(), obj->get_zone_id(),
                                                         *login_table_ptr, *local_login_ver_ptr));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  // 冲突检测
  {
    int64_t expect_table_user_version =
        util::string::to_int<int64_t>(login_table_ptr->expect_table_user_db_version().c_str());
    int64_t real_table_user_version = util::string::to_int<int64_t>(tbu_version.c_str());
    if (expect_table_user_version > 0 && real_table_user_version > 0 &&
        expect_table_user_version >= real_table_user_version) {
      // Check timeout
      auto sys_now = util::time::time_utility::sys_now();
      auto timeout = std::chrono::system_clock::from_time_t(login_table_ptr->expect_table_user_db_timeout().seconds()) +
                     std::chrono::nanoseconds(login_table_ptr->expect_table_user_db_timeout().nanos());
      if (timeout >= sys_now) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_EAGAIN);
      }
    }
  }

  // 拉取玩家数据
  // 设置路由ID
  set_router_server_id(login_table_ptr->router_server_id(), login_table_ptr->router_version());
  obj->load_and_move_login_info(COPP_MACRO_STD_MOVE(*login_table_ptr), *local_login_ver_ptr);
  login_table_ptr->set_user_id(0);
  login_table_ptr->set_zone_id(0);

  // table_login内的平台信息复制到player里
  if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
    obj->init_from_table_data(ctx, tbu);
  }

  uint64_t self_node_id = logic_config::me()->get_local_server_id();
  // 如果router server id是0则设置为本地的登入地址
  if (0 == get_router_server_id()) {
    PROJECT_NAMESPACE_ID::table_login &login_blob_data = obj->get_login_info();

    uint64_t old_router_server_id = login_blob_data.router_server_id();
    uint64_t old_router_ver = login_blob_data.router_version();

    login_blob_data.set_router_server_id(self_node_id);
    login_blob_data.set_router_version(old_router_ver + 1);

    // 新登入则设置登入时间
    auto old_logout_time = login_blob_data.logout_time();
    auto old_business_logout_time = login_blob_data.business_logout_time();
    login_blob_data.set_login_time(util::time::time_utility::get_sys_now());
    login_blob_data.set_business_login_time(util::time::time_utility::get_now());

    auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(),
                                                         login_blob_data, obj->get_login_version()));
    if (ret < 0) {
      FWPLOGERROR(*obj, "save login data failed, msg:\n{}", login_blob_data.DebugString());
      // 失败则恢复路由信息
      login_blob_data.set_logout_time(old_logout_time);                    // 恢复登出时间
      login_blob_data.set_business_logout_time(old_business_logout_time);  // 恢复登出时间
      login_blob_data.set_router_server_id(old_router_server_id);
      login_blob_data.set_router_version(old_router_ver);
      RPC_RETURN_CODE(ret);
    }

    set_router_server_id(login_blob_data.router_server_id(), login_blob_data.router_version());
  } else if (self_node_id != get_router_server_id()) {
    // 不在这个进程上
    FWPLOGERROR(*obj, "is in server {:#x} but try to pull in server {:#x}", get_router_server_id(), self_node_id);

    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_IN_OTHER_SERVER);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type router_player_cache::save_object(rpc::context &ctx, void * /*priv_data*/) {
  // 保存数据
  player_cache::ptr_t obj = object();
  if (!obj || !obj->can_be_writable()) {
    FWLOGERROR("save_object for {}:{}:{} failed, error code: {}", get_key().type_id, get_key().zone_id,
               get_key().object_id, static_cast<int>(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  uint64_t self_node_id = logic_config::me()->get_local_server_id();
  // RPC read from DB(以后可以优化掉)
  int res = 0;
  // 异常的玩家数据记录，自动修复一下
  bool bad_data_kickoff = false;
  int try_times = 2;  // 其实并不需要重试，这里只是处理table_login过期后走更新流程
  while (try_times-- > 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
      res = RPC_AWAIT_CODE_RESULT(rpc::db::login::get(ctx, obj->get_open_id().c_str(), obj->get_zone_id(),
                                                      obj->get_login_info(), obj->get_login_version()));
      if (res < 0) {
        FWPLOGERROR(*obj, "try load login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        RPC_RETURN_CODE(res);
      }
    }

    if (0 != get_router_server_id() && 0 != obj->get_login_info().router_server_id() &&
        obj->get_login_info().router_server_id() != self_node_id) {
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
      res = RPC_AWAIT_CODE_RESULT(rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(),
                                                      obj->get_login_info(), obj->get_login_version()));
      if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        continue;
      }

      if (res < 0) {
        FWPLOGERROR(*obj, "try set login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        RPC_RETURN_CODE(res);
      } else {
        set_router_server_id(obj->get_login_info().router_server_id(), obj->get_login_info().router_version());
        break;
      }
    }

    // 冲突检测的版本号设置
    {
      obj->get_login_info().set_expect_table_user_db_version(obj->get_version());
      time_t timeout_sec = util::time::time_utility::get_sys_now();
      int32_t timeout_nano = static_cast<int32_t>(util::time::time_utility::get_now_usec() * 1000);
      timeout_sec += logic_config::me()->get_cfg_task().csmsg().timeout().seconds();
      timeout_nano += logic_config::me()->get_cfg_task().csmsg().timeout().nanos();
      if (timeout_nano >= 1000000000) {
        timeout_sec += 1;
        timeout_nano -= 1000000000;
      }
      obj->get_login_info().mutable_expect_table_user_db_timeout()->set_seconds(timeout_sec);
      obj->get_login_info().mutable_expect_table_user_db_timeout()->set_nanos(timeout_nano);
    }

    // 登出流程
    if (0 == get_router_server_id()) {
      uint64_t old_router_server_id = obj->get_login_info().router_server_id();
      uint64_t old_router_ver = obj->get_login_info().router_version();

      obj->get_login_info().set_router_server_id(0);
      obj->get_login_info().set_router_version(old_router_ver + 1);
      obj->get_login_info().set_logout_time(util::time::time_utility::get_sys_now());       // 登出时间
      obj->get_login_info().set_business_logout_time(util::time::time_utility::get_now());  // 登出时间

      // RPC save to db
      res = RPC_AWAIT_CODE_RESULT(rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(),
                                                      obj->get_login_info(), obj->get_login_version()));
      if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        continue;
      }

      if (res < 0) {
        FWPLOGERROR(*obj, "try set login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        RPC_RETURN_CODE(res);
      } else {
        set_router_server_id(obj->get_login_info().router_server_id(), obj->get_login_info().router_version());
      }
    } else {  // 续期login code
      uint64_t old_router_server_id = obj->get_login_info().router_server_id();
      uint64_t old_router_ver = obj->get_login_info().router_version();

      if (get_router_server_id() != old_router_server_id) {
        obj->get_login_info().set_router_server_id(get_router_server_id());
        obj->get_login_info().set_router_version(old_router_ver + 1);
      }

      // 鉴权登入码续期
      if (obj->get_session()) {
        obj->get_login_info().set_login_code_expired(
            util::time::time_utility::get_sys_now() +
            logic_config::me()->get_logic().session().login_code_valid_sec().seconds());
      }

      res = RPC_AWAIT_CODE_RESULT(rpc::db::login::set(ctx, obj->get_open_id().c_str(), obj->get_zone_id(),
                                                      obj->get_login_info(), obj->get_login_version()));
      if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        continue;
      }

      if (res < 0) {
        FWPLOGERROR(*obj, "call login rpc method set failed, res: {}, msg: {}", res,
                    obj->get_login_info().DebugString());
        obj->get_login_info().set_router_server_id(old_router_server_id);
        obj->get_login_info().set_router_version(old_router_ver);
        RPC_RETURN_CODE(res);
      } else {
        set_router_server_id(obj->get_login_info().router_server_id(), obj->get_login_info().router_version());
      }
    }

    break;
  }

  if (bad_data_kickoff) {
    FWPLOGERROR(*obj, "login pd error(expected: {:#x}, real: {:#x})", self_node_id,
                obj->get_login_info().router_server_id());

    // 在其他设备登入的要把这里的Session踢下线
    if (obj->get_session()) {
      obj->get_session()->send_kickoff(::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    }

    RPC_AWAIT_IGNORE_RESULT(router_player_manager::me()->remove_object(ctx, get_key(), nullptr, nullptr));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
  }

  // 尝试保存用户数据
  PROJECT_NAMESPACE_ID::table_user user_tb;
  obj->dump(ctx, user_tb, true);

  FWPLOGDEBUG(*obj, "save curr data version: {}", obj->get_version());

  // RPC save to DB
  res = RPC_AWAIT_CODE_RESULT(
      rpc::db::player::set(ctx, obj->get_user_id(), obj->get_zone_id(), user_tb, obj->get_version()));

  // CAS 序号错误（可能是先超时再返回成功）,重试一次
  // 前面已经确认了当前用户在此处登入并且已经更新了版本号到版本信息
  // RPC save to DB again
  if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::player::set(ctx, obj->get_user_id(), obj->get_zone_id(), user_tb, obj->get_version()));
  }

  if (res < 0) {
    FWPLOGERROR(*obj, "try save db failed. res: {}, version: {}", res, obj->get_version());
  }

  if (res >= 0) {
    obj->on_saved(ctx);
  }

  RPC_RETURN_CODE(res);
}

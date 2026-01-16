// Copyright 2021 atframework
// Created by owent on 2018-05-07.
//

#include "router/router_player_cache.h"

#include <config/logic_config.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atgateway/protocols/libatgw_protocol_api.h>

#include <logic/session_manager.h>
#include <rpc/db/local_db_interface.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "router/router_player_manager.h"

SERVER_FRAME_API router_player_private_type::router_player_private_type()
    : login_lock_tb(nullptr), login_lock_cas_ver(0) {}
SERVER_FRAME_API router_player_private_type::router_player_private_type(
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> *login_lock_tb, uint64_t login_lock_cas_ver,
    const std::string &openid)
    : login_lock_tb(login_lock_tb), login_lock_cas_ver(login_lock_cas_ver), openid(openid) {}
SERVER_FRAME_API router_player_private_type::~router_player_private_type() {}

SERVER_FRAME_API router_player_cache::router_player_cache(uint64_t user_id, uint32_t zone_id, const std::string &openid)
    : base_type(router_player_manager::me()->create_player_object(user_id, zone_id, openid),
                key_t(router_player_manager::me()->get_type_id(), zone_id, user_id)) {}

// 这个时候openid无效，后面需要再init一次
SERVER_FRAME_API router_player_cache::router_player_cache(const key_t &key)
    : base_type(router_player_manager::me()->create_player_object(key.object_id, key.zone_id, ""), key) {}

SERVER_FRAME_API const char *router_player_cache::name() const { return "[player  router cache]"; }

SERVER_FRAME_API rpc::result_code_type router_player_cache::pull_cache(rpc::context &ctx, void *priv_data) {
  if (nullptr == priv_data) {
    router_player_private_type local_priv_data;
    return pull_cache(ctx, local_priv_data);
  }

  return pull_cache(ctx, *reinterpret_cast<router_player_private_type *>(priv_data));
}

SERVER_FRAME_API rpc::result_code_type router_player_cache::pull_cache(rpc::context &ctx,
                                                                       router_player_private_type &priv_data) {
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> login_lock_table_ptr{ctx};
  if (nullptr != priv_data.login_lock_tb) {
    login_lock_table_ptr = *priv_data.login_lock_tb;
  }

  uint64_t local_login_lock_ver = priv_data.login_lock_cas_ver;

  // 先尝试从数据库读数据
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_user> tbu{ctx};
  uint64_t tbu_version = 0;
  auto res = RPC_AWAIT_CODE_RESULT(
      rpc::db::user::partly_get_basic_info(ctx, get_key().zone_id, get_key().object_id, tbu, tbu_version));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FWLOGERROR("load player_cache data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
                 res);
    }
    RPC_RETURN_CODE(res);
  }

  player_cache::ptr_t obj = get_object();
  if (obj->get_open_id().empty()) {
    obj->init(get_key().object_id, get_key().zone_id, tbu->open_id());
  }

  if (0 == login_lock_table_ptr->user_id()) {
    auto ret = RPC_AWAIT_CODE_RESULT(
        rpc::db::login_lock::get_all(ctx, get_key().object_id, login_lock_table_ptr, local_login_lock_ver));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  // 设置路由ID
  if (login_lock_table_ptr->login_zone_id() == get_key().zone_id) {
    // Zone 匹配
    set_router_server_id(login_lock_table_ptr->router_server_id(), login_lock_table_ptr->router_version());
  }

  obj->load_and_move_login_lock(std::move(*login_lock_table_ptr), local_login_lock_ver);
  login_lock_table_ptr->set_user_id(0);

  // table_login内的平台信息复制到player里
  if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
    obj->init_from_table_data(ctx, *tbu);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API rpc::result_code_type router_player_cache::pull_object(rpc::context &ctx, void *priv_data) {
  if (nullptr == priv_data) {
    router_player_private_type local_priv_data;
    return pull_object(ctx, local_priv_data);
  }

  return pull_object(ctx, *reinterpret_cast<router_player_private_type *>(priv_data));
}

SERVER_FRAME_API rpc::result_code_type router_player_cache::pull_object(rpc::context &ctx,
                                                                        router_player_private_type &priv_data) {
  if (priv_data.login_lock_tb == nullptr) {
    FWLOGERROR("pull_object for {}:{}:{} failed, priv_data.login_lock_tb is nullptr", get_key().type_id,
               get_key().zone_id, get_key().object_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
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
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_user> tbu{ctx};
  uint64_t tbu_version = 0;
  auto res = RPC_AWAIT_CODE_RESULT(
      rpc::db::user::partly_get_basic_info(ctx, get_key().zone_id, get_key().object_id, tbu, tbu_version));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FWLOGERROR("load player_cache data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
                 res);
      RPC_RETURN_CODE(res);
    } else {
      tbu->set_open_id(priv_data.openid);
      tbu->set_user_id(get_key().object_id);
      tbu->set_zone_id(get_key().zone_id);
    }
  }

  // 补全OpenId
  if (obj->get_open_id().empty()) {
    obj->init(get_key().object_id, get_key().zone_id, tbu->open_id());
  }

  // 冲突检测
  {
    uint64_t expect_table_user_version = (*priv_data.login_lock_tb)->expect_table_user_db_version();
    uint64_t real_table_user_version = tbu_version;
    if ((*priv_data.login_lock_tb)->login_zone_id() == get_key().zone_id &&
        real_table_user_version < expect_table_user_version) {
      // Check timeout
      auto sys_now = atfw::util::time::time_utility::sys_now();
      auto timeout =
          std::chrono::system_clock::from_time_t((*priv_data.login_lock_tb)->expect_table_user_db_timeout().seconds()) +
          std::chrono::nanoseconds((*priv_data.login_lock_tb)->expect_table_user_db_timeout().nanos());
      if (timeout >= sys_now) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_EAGAIN);
      }
    }
  }

  // 拉取玩家数据
  // 设置路由ID
  set_router_server_id((*priv_data.login_lock_tb)->router_server_id(), (*priv_data.login_lock_tb)->router_version());
  obj->load_and_move_login_lock(std::move(**priv_data.login_lock_tb), priv_data.login_lock_cas_ver);
  (*priv_data.login_lock_tb)->set_user_id(0);

  // table_login内的平台信息复制到player里
  if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
    obj->init_from_table_data(ctx, *tbu);
  }
  obj->set_user_cas_version(tbu_version);

  uint64_t self_node_id = logic_config::me()->get_local_server_id();
  PROJECT_NAMESPACE_ID::table_login_lock &login_blob_data = obj->get_login_lock();

  // 更新登录锁信息
  login_blob_data.set_login_expired(atfw::util::time::time_utility::get_sys_now() +
                                    logic_config::me()->get_server_cfg().session().login_code_valid_sec().seconds());
  login_blob_data.set_login_zone_id(get_key().zone_id);

  uint64_t old_router_server_id = login_blob_data.router_server_id();
  uint64_t old_router_ver = login_blob_data.router_version();

  login_blob_data.set_router_server_id(self_node_id);
  login_blob_data.set_router_version(old_router_ver + 1);

  auto save_login_blob_data = rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_login_lock>(ctx, login_blob_data);
  auto ret = RPC_AWAIT_CODE_RESULT(
      rpc::db::login_lock::replace(ctx, std::move(save_login_blob_data), obj->get_login_lock_cas_version()));
  if (ret < 0) {
    FWPLOGERROR(*obj, "save login data failed, msg:\n{}", login_blob_data.DebugString());
    // 失败则恢复路由信息
    login_blob_data.set_router_server_id(old_router_server_id);
    login_blob_data.set_router_version(old_router_ver);
    RPC_RETURN_CODE(ret);
  }
  set_router_server_id(login_blob_data.router_server_id(), login_blob_data.router_version());

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API rpc::result_code_type router_player_cache::save_object(rpc::context &ctx, void * /*priv_data*/) {
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
  int try_times = 2;  // 其实并不需要重试，这里只是处理table_login过期后走更新流程
  while (try_times-- > 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
      auto save_login_blob_data =
          rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_login_lock>(ctx, obj->get_login_lock());
      res = RPC_AWAIT_CODE_RESULT(rpc::db::login_lock::get_all(ctx, get_key().object_id, save_login_blob_data,
                                                               obj->get_login_lock_cas_version()));
      if (res < 0) {
        FWPLOGERROR(*obj, "try load login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        RPC_RETURN_CODE(res);
      }
    }

    if (obj->get_login_lock().router_server_id() != self_node_id) {
      // 别的地方登录成功 尝试下线
      FWPLOGERROR(*obj, "login pd error(expected: {:#x}, real: {:#x})", self_node_id,
                  obj->get_login_lock().router_server_id());

      // 在其他设备登入的要把这里的Session踢下线
      if (obj->get_session()) {
        obj->get_session()->send_kickoff(::atframework::gateway::close_reason_t::EN_CRT_KICKOFF);
      }

      // 强制降级，删除缓存数据
      downgrade();
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
    }

    // 冲突检测的版本号设置
    {
      obj->get_login_lock().set_expect_table_user_db_version(obj->get_user_cas_version() + 1);
      time_t timeout_sec = atfw::util::time::time_utility::get_sys_now();
      int32_t timeout_nano = static_cast<int32_t>(util::time::time_utility::get_now_usec() * 1000);
      timeout_sec += logic_config::me()->get_cfg_task().csmsg().timeout().seconds();
      timeout_nano += logic_config::me()->get_cfg_task().csmsg().timeout().nanos();
      if (timeout_nano >= 1000000000) {
        timeout_sec += 1;
        timeout_nano -= 1000000000;
      }
      obj->get_login_lock().mutable_expect_table_user_db_timeout()->set_seconds(timeout_sec);
      obj->get_login_lock().mutable_expect_table_user_db_timeout()->set_nanos(timeout_nano);
    }

    // 登出流程
    if (0 == get_router_server_id()) {
      uint64_t old_router_server_id = obj->get_login_lock().router_server_id();
      uint64_t old_router_ver = obj->get_login_lock().router_version();

      obj->get_login_lock().set_router_server_id(0);
      obj->get_login_lock().set_router_version(old_router_ver + 1);

      // 登出时间由上层逻辑设置
      // RPC save to db
      auto save_login_blob_data =
          rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_login_lock>(ctx, obj->get_login_lock());
      res = RPC_AWAIT_CODE_RESULT(
          rpc::db::login_lock::replace(ctx, std::move(save_login_blob_data), obj->get_login_lock_cas_version()));
      if (res != 0) {
        obj->get_login_lock().set_router_server_id(old_router_server_id);
        obj->get_login_lock().set_router_version(old_router_ver);
        if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
          continue;
        }
        FWPLOGERROR(*obj, "try set login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        RPC_RETURN_CODE(res);
      }
      set_router_server_id(obj->get_login_lock().router_server_id(), obj->get_login_lock().router_version());
      // Logout
      obj->on_logout(ctx);
    } else {  // 续期login code
      uint64_t old_router_server_id = obj->get_login_lock().router_server_id();
      uint64_t old_router_ver = obj->get_login_lock().router_version();

      if (get_router_server_id() != old_router_server_id) {
        obj->get_login_lock().set_router_server_id(get_router_server_id());
        obj->get_login_lock().set_router_version(old_router_ver + 1);
      }

      auto save_login_blob_data =
          rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_login_lock>(ctx, obj->get_login_lock());
      res = RPC_AWAIT_CODE_RESULT(
          rpc::db::login_lock::replace(ctx, std::move(save_login_blob_data), obj->get_login_lock_cas_version()));
      if (res != 0) {
        obj->get_login_lock().set_router_server_id(old_router_server_id);
        obj->get_login_lock().set_router_version(old_router_ver);
        if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
          continue;
        }
        FWPLOGERROR(*obj, "try set login data failed, result: {}({}).", res, protobuf_mini_dumper_get_error_msg(res));
        RPC_RETURN_CODE(res);
      }
      set_router_server_id(obj->get_login_lock().router_server_id(), obj->get_login_lock().router_version());
    }

    break;
  }

  // 尝试保存用户数据
  {
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_user> user_tb{ctx};
    obj->dump(ctx, *user_tb, true);
    FWPLOGDEBUG(*obj, "save curr cas version: {}", obj->get_user_cas_version());

    // RPC save to DB
    res = RPC_AWAIT_CODE_RESULT(rpc::db::user::replace(ctx, std::move(user_tb), obj->get_user_cas_version()));
  }

  // CAS 序号错误（可能是先超时再返回成功）,重试一次
  // 前面已经确认了当前用户在此处登入并且已经更新了版本号到版本信息
  // RPC save to DB again
  if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_user> user_tb{ctx};
    obj->dump(ctx, *user_tb, true);
    FWPLOGINFO(*obj, "force save curr cas version: {}", obj->get_user_cas_version());

    res = RPC_AWAIT_CODE_RESULT(rpc::db::user::replace(ctx, std::move(user_tb), obj->get_user_cas_version()));
  }

  if (res < 0) {
    FWPLOGERROR(*obj, "try save db failed. res: {}, cas version: {}", res, obj->get_user_cas_version());
  }

  if (res >= 0) {
    obj->on_saved(ctx);
  }

  RPC_RETURN_CODE(res);
}

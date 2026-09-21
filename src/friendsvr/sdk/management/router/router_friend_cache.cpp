// Copyright 2026 atframework
// Created by owent on 2026-09-18

#include "router/router_friend_cache.h"

#include <config/logic_config.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <utility/protobuf_mini_dumper.h>

#include <logic/session_manager.h>

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_utils.h>

#include <memory>
#include <utility>

#include "router/router_friend_manager.h"

namespace atframework {
namespace friend_api {
FRIEND_SDK_MANAGEMENT_API router_friend_private_type::router_friend_private_type() : friend_tb(NULL), friend_ver(0) {}

FRIEND_SDK_MANAGEMENT_API router_friend_private_type::router_friend_private_type(
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> *tb, uint64_t *ver)
    : friend_tb(tb), friend_ver(ver) {}

FRIEND_SDK_MANAGEMENT_API router_friend_cache::router_friend_cache(rpc::context &ctx, uint32_t zone_id,
                                                                   uint64_t user_id)
    : base_type(router_friend_manager::me()->create_friend_object(ctx, zone_id, user_id),
                key_t(router_friend_manager::me()->get_type_id(), zone_id, user_id)) {}

// 这个时候openid无效，后面需要再init一次
FRIEND_SDK_MANAGEMENT_API router_friend_cache::router_friend_cache(rpc::context &ctx, const key_t &key)
    : base_type(router_friend_manager::me()->create_friend_object(ctx, key.zone_id, key.object_id), key) {}

FRIEND_SDK_MANAGEMENT_API const char *router_friend_cache::name() const { return "[friend router cache]"; }

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_cache::pull_cache(rpc::context &ctx, void *priv_data) {
  if (NULL == priv_data) {
    router_friend_private_type local_priv_data;
    return pull_cache(ctx, local_priv_data);
  }

  return pull_cache(ctx, *reinterpret_cast<router_friend_private_type *>(priv_data));
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_cache::pull_cache(rpc::context &ctx,
                                                                                router_friend_private_type &priv_data) {
  // 先尝试从数据库读数据
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> friend_tb_ptr{ctx};
  if (nullptr != priv_data.friend_tb) {
    friend_tb_ptr = *priv_data.friend_tb;
  }
  uint64_t local_dbdata_ver = 0;
  uint64_t *friend_tb_ver_ptr = priv_data.friend_ver;
  if (nullptr == friend_tb_ver_ptr) {
    friend_tb_ver_ptr = &local_dbdata_ver;
  }

  rpc::result_code_type::value_type res = 0;
  if (0 == friend_tb_ptr->user_id() || 0 == friend_tb_ptr->zone_id()) {
    int left_retry_times = 2;
    while (left_retry_times-- > 0) {
      if (0 != friend_tb_ptr->user_id() && 0 != friend_tb_ptr->zone_id()) {
        break;
      }

      if (!router_friend_manager::me()->has_custom_create_object_fn()) {
        res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::partly_get_basic_info(
            ctx, get_key().zone_id, get_key().object_id, *friend_tb_ptr, *friend_tb_ver_ptr));
      } else {
        // guildsvr上还是要全量拉。不然 guild_object 数据缺失
        res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::get_all(ctx, get_key().zone_id, get_key().object_id,
                                                                  *friend_tb_ptr, *friend_tb_ver_ptr));
      }

      if (res < 0) {
        if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
          FWLOGERROR("load friend data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id, res);
          left_retry_times = 0;
          break;
        }

        // try to create and retry
        friend_tb_ptr->set_user_id(get_key().object_id);
        friend_tb_ptr->set_zone_id(get_key().zone_id);
        *friend_tb_ptr->mutable_router_save_timepoint() = protobuf_from_system_clock(ctx.logical_now());
        *friend_tb_ver_ptr = 0;

        auto clone_friebd_tb = rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_friend>(ctx, friend_tb_ptr);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::insert(ctx, clone_friebd_tb, friend_tb_ver_ptr));
        if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS) {
          FWLOGERROR("create friend data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
                     res);
        }
      }
    }
    if (res < 0) {
      if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
        FWLOGERROR("load friend data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id, res);
      }
      RPC_RETURN_CODE(res);
    }
  }

  fix_router_timeout(ctx, *friend_tb_ptr);
  uint64_t router_server_id = friend_tb_ptr->router_server_id();
  uint64_t router_version = friend_tb_ptr->router_version();

  // 设置路由ID
  set_router_server_id(router_server_id, router_version);

  friend_cache::ptr_t obj = get_object();
  if (obj) {
    obj->load_and_move_db(ctx, std::move(*friend_tb_ptr), *friend_tb_ver_ptr);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_cache::pull_object(rpc::context &ctx, void *priv_data) {
  if (NULL == priv_data) {
    router_friend_private_type local_priv_data;
    return pull_object(ctx, local_priv_data);
  }

  return pull_object(ctx, *reinterpret_cast<router_friend_private_type *>(priv_data));
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_cache::pull_object(
    rpc::context &ctx, router_friend_private_type &priv_data) {
  // 先尝试从数据库读数据
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> friend_tb_ptr{ctx};
  if (nullptr != priv_data.friend_tb) {
    friend_tb_ptr = *priv_data.friend_tb;
  }
  uint64_t local_dbdata_ver = 0;
  uint64_t *friend_tb_ver_ptr = priv_data.friend_ver;
  if (nullptr == friend_tb_ver_ptr) {
    friend_tb_ver_ptr = &local_dbdata_ver;
  }

  rpc::result_code_type::value_type res = 0;
  int left_retry_times = 0;
  if (router_friend_manager::me()->is_auto_mutable_object()) {
    left_retry_times = 2;
  }

  while (left_retry_times-- >= 0) {
    if (0 != friend_tb_ptr->user_id() && 0 != friend_tb_ptr->zone_id()) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::user_friend::get_all(ctx, get_key().zone_id, get_key().object_id, *friend_tb_ptr, *friend_tb_ver_ptr));
    if (res < 0) {
      if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
        FWLOGERROR("load friend data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id, res);
        left_retry_times = 0;
        break;
      }

      // check if it's allowed to create new object
      if (left_retry_times < 0) {
        break;
      }

      // try to create and retry
      friend_tb_ptr->set_user_id(get_key().object_id);
      friend_tb_ptr->set_zone_id(get_key().zone_id);
      *friend_tb_ptr->mutable_router_save_timepoint() = protobuf_from_system_clock(ctx.logical_now());
      *friend_tb_ver_ptr = 0;
      auto clone_friebd_tb = rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_friend>(ctx, friend_tb_ptr);
      res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::insert(ctx, clone_friebd_tb, friend_tb_ver_ptr));
      if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS) {
        FWLOGERROR("create friend data for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id, res);
      }
    }
  }

  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  friend_cache::ptr_t obj = get_object();
  assert(!!obj);
  if (!obj) {
    FWLOGERROR("router object should not be null");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
  }

  uint64_t self_node_id = logic_config::me()->get_local_server_id();
  fix_router_timeout(ctx, *friend_tb_ptr);
  // 刷新路由ID
  set_router_server_id(friend_tb_ptr->router_server_id(), friend_tb_ptr->router_version());

  obj->load_and_move_db(ctx, std::move(*friend_tb_ptr), *friend_tb_ver_ptr);

  if (0 == get_router_server_id()) {
    uint64_t old_router_server_id = obj->get_router_server_id();
    uint64_t old_router_ver = obj->get_router_server_version();
    auto old_router_save_timepoint = obj->get_router_server_save_timepoint();

    obj->set_router_server(self_node_id, old_router_ver + 1, ctx.logical_now());

    *friend_tb_ver_ptr = obj->get_db_version();
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> db_data{ctx};
    obj->dump(ctx, *db_data);

    FWLOGDEBUG("friend router object {}:{} save curr data version: {}", get_key().zone_id, get_key().object_id,
               obj->get_db_version());
    res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::replace(ctx, db_data, *friend_tb_ver_ptr));
    obj->set_db_version(*friend_tb_ver_ptr);
    if (res < 0) {
      FWLOGERROR("save friend data for {}:{} failed, msg:\n{}", get_key().zone_id, get_key().object_id,
                 obj->mutable_db_data().DebugString());
      // 失败则恢复路由信息
      obj->set_router_server(old_router_server_id, old_router_ver, old_router_save_timepoint);
      RPC_RETURN_CODE(res);
    }

    set_router_server_id(obj->get_router_server_id(), obj->get_router_server_version());
  } else if (self_node_id != get_router_server_id()) {
    // 不在这个进程上
    FWLOGERROR("friend router object {}:{} is in server {:#x} but try to pull in server {:#x}", get_key().zone_id,
               get_key().object_id, get_router_server_id(), self_node_id);

    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_IN_OTHER_SERVER);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

FRIEND_SDK_MANAGEMENT_API rpc::result_code_type router_friend_cache::save_object(rpc::context &ctx,
                                                                                 void * /*priv_data*/) {
  // 保存数据
  friend_cache::ptr_t obj = object();
  if (!obj) {
    FWLOGERROR("save_object for {}:{} failed, error code: {}", get_key().zone_id, get_key().object_id,
               static_cast<int>(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  // RPC read from DB(以后可以优化掉)
  rpc::result_code_type::value_type res = 0;

  // 尝试保存用户数据
  uint64_t db_version = obj->get_db_version();
  uint64_t old_router_server_id = get_router_server_id();
  uint64_t old_router_version = get_router_version();
  auto old_router_save_timepoint = obj->get_router_server_save_timepoint();

  uint64_t self_node_id = logic_config::me()->get_local_server_id();
  obj->set_router_server(self_node_id, old_router_version + 1, ctx.logical_now());

  {
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> db_data{ctx};
    obj->dump(ctx, *db_data);

    FWLOGDEBUG("friend router object {}:{} save curr data version: {}", get_key().zone_id, get_key().object_id,
               obj->get_db_version());

    // RPC save to DB
    res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::replace(ctx, db_data, db_version));
  }

  // 开发环境中如果用户删除账号可能导致保存失败，这时候直接覆盖即可
  if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> db_data{ctx};
    obj->dump(ctx, *db_data);
    FWLOGWARNING("friend router object {}:{} may be deleted, try to add new one", get_key().zone_id,
                 get_key().object_id);
    db_version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::insert(ctx, db_data, &db_version));
  }

  // CAS 序号错误（可能是先超时再返回成功）,重试一次
  // 前面已经确认了当前用户在此处登入并且已经更新了版本号到版本信息
  // RPC save to DB again
  if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res) {
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> fake_db_data{ctx};

    db_version = 0;
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::user_friend::get_all(ctx, get_key().zone_id, get_key().object_id, *fake_db_data, db_version));
    if (res >= 0) {
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> db_data{ctx};
      obj->dump(ctx, *db_data);
      if (self_node_id == fake_db_data->router_server_id() || 0 == fake_db_data->router_server_id()) {
        // 数据发生了严重的不一致问题，发生了覆盖
        FWLOGERROR("friend router object {}:{} data old data will be overwrite.\nold data: {}\nnew data: {}",
                   get_key().zone_id, get_key().object_id, fake_db_data->DebugString(), db_data->DebugString());
      } else {
        // 路由实体异常，且在其他节点上。本节点刷新缓存并且要降级
        FWLOGERROR("friend router object {}:{} data old data will be discard.\nold data: {}\nnew data: {}",
                   get_key().zone_id, get_key().object_id, db_data->DebugString().c_str(),
                   fake_db_data->DebugString().c_str());
        obj->load_and_move_db(ctx, std::move(*fake_db_data), db_version);

        set_router_server_id(obj->get_router_server_id(), obj->get_router_server_version());
        // 降级为缓存
        downgrade();
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
      }

      res = RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::replace(ctx, db_data, db_version));
    }
  }

  if (res < 0) {
    obj->set_router_server(old_router_server_id, old_router_version, old_router_save_timepoint);
    FWLOGERROR("friend router object {}:{} try save db failed. res: {}, version: {}", get_key().zone_id,
               get_key().object_id, res, obj->get_db_version());
  } else {
    obj->set_db_version(db_version);
    obj->on_saved(ctx, get_router_server_id());
  }

  RPC_RETURN_CODE(res);
}

void router_friend_cache::fix_router_timeout(rpc::context &ctx, PROJECT_NAMESPACE_ID::table_friend &table) {
  // 路由信息的自动修复流程
  if (0 != table.router_server_id() && logic_config::me()->get_local_server_id() != table.router_server_id() &&
      protobuf_to_system_clock(table.router_save_timepoint()) +
              protobuf_to_system_clock(logic_config::me()->get_cfg_router().object_free_timeout()) <
          ctx.logical_now()) {
    FWLOGERROR("friend router object for {}:{} has expired router server id {:#x}", get_key().zone_id,
               get_key().object_id, table.router_server_id());
    table.set_router_server_id(0);
  }
}

}  // namespace friend_api
}  // namespace atframework

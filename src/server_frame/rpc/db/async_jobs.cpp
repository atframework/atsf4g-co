// Copyright 2021 atframework
// Created by owent on 2019-06-17.
//

#include "rpc/db/async_jobs.h"

#include <algorithm/murmur_hash.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include <unordered_map>

#include "rpc/db/db_macros.h"
#include "rpc/db/db_utils.h"
#include "rpc/game/gamesvrservice.h"
#include "rpc/game/player.h"
#include "rpc/rpc_macros.h"
#include "rpc/rpc_utils.h"

#include "rpc/db/login.h"
#include "rpc/db/uuid.h"

namespace rpc {
namespace db {
namespace async_jobs {

namespace detail {
struct player_key_hash_t {
  size_t operator()(const PROJECT_NAMESPACE_ID::DPlayerIDKey& key) const {
    uint64_t out[2] = {0};
    uint64_t val = key.user_id();
    util::hash::murmur_hash3_x64_128(&val, static_cast<int>(sizeof(val)), key.zone_id(), out);
    return out[0];
  }
};

struct player_key_equal_t {
  bool operator()(const PROJECT_NAMESPACE_ID::DPlayerIDKey& l, const PROJECT_NAMESPACE_ID::DPlayerIDKey& r) const {
    return l.zone_id() == r.zone_id() && l.user_id() == r.user_id();
  }
};

// 如果短期内发生太多次针对同一玩家得在线表拉取，则直接用缓存。这可以优化短期频繁拉取login表，并且异步任务就算过期也只是回延后触发，不影响逻辑
static rpc::result_code_type fetch_user_login_cache(rpc::context& ctx, uint64_t user_id, uint32_t zone_id,
                                                    PROJECT_NAMESPACE_ID::table_login& rsp) {
  static std::unordered_map<PROJECT_NAMESPACE_ID::DPlayerIDKey, PROJECT_NAMESPACE_ID::table_login, player_key_hash_t,
                            player_key_equal_t>
      local_cache;
  static time_t local_cache_timepoint = 0;
  time_t now = util::time::time_utility::get_now();
  if (now != local_cache_timepoint) {
    local_cache_timepoint = now;
    local_cache.clear();
  }

  PROJECT_NAMESPACE_ID::DPlayerIDKey key;
  key.set_user_id(user_id);
  key.set_zone_id(zone_id);

  auto iter_cache = local_cache.find(key);
  if (iter_cache != local_cache.end()) {
    protobuf_copy_message(rsp, iter_cache->second);
    RPC_RETURN_CODE(0);
  }

  std::string version;
  int ret = RPC_AWAIT_CODE_RESULT(rpc::db::login::get(ctx, std::to_string(user_id).c_str(), zone_id, rsp, version));
  if (0 == ret) {
    protobuf_copy_message(local_cache[key], rsp);
  }
  RPC_RETURN_CODE(ret);
}
}  // namespace detail

result_type get_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                     std::vector<async_jobs_record>& out) {
  if (0 == jobs_type || 0 == user_id) {
    FWLOGERROR("{} be called with invalid paronlineameters.(jobs_type={}, user_id={})", __FUNCTION__, jobs_type,
               user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (NULL ==
      PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor()->FindValueByNumber(static_cast<int>(jobs_type))) {
    FWLOGERROR("{} be called with unsupported type.(jobs_type={}, user_id={})", __FUNCTION__, jobs_type, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // TODO db operation
  // return RPC_AWAIT_CODE_RESULT(RPC_AWAIT_CODE_RESULT(rpc::db::TABLE_USER_ASYNC_JOBS_DEF::get_all(ctx, jobs_type,
  // user_id, zone_id, out)));
  RPC_DB_RETURN_CODE(0);
}

result_type del_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                     const std::vector<int64_t>& in) {
  if (0 == jobs_type || 0 == user_id) {
    FWLOGERROR("{} be called with invalid parameters.(jobs_type={}, user_id={})", __FUNCTION__, jobs_type, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (NULL ==
      PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor()->FindValueByNumber(static_cast<int>(jobs_type))) {
    FWLOGERROR("{} be called with unsupported type.(jobs_type={}, user_id={})", __FUNCTION__, jobs_type, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (in.empty()) {
    RPC_DB_RETURN_CODE(0);
  }

  // TODO db operation
  // return RPC_AWAIT_CODE_RESULT(rpc::db::TABLE_USER_ASYNC_JOBS_DEF::remove(ctx, jobs_type, user_id, zone_id, in));
  RPC_DB_RETURN_CODE(0);
}

result_type add_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                     PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data& in) {
  if (0 == jobs_type || 0 == user_id) {
    FWLOGERROR("{} be called with invalid parameters.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type,
               user_id, zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (NULL ==
      PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor()->FindValueByNumber(static_cast<int>(jobs_type))) {
    FWLOGERROR("{} be called with unsupported type.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type,
               user_id, zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data::ACTION_NOT_SET == in.action_case()) {
    FWLOGERROR("{} be called without a action.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type, user_id,
               zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (in.action_uuid().empty()) {
    in.set_action_uuid(rpc::db::uuid::generate_short_uuid());
  }
  in.set_timepoint_ms(util::time::time_utility::get_now() * 1000 + util::time::time_utility::get_now_usec() / 1000);

  // TODO db operation
  // auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::TABLE_USER_ASYNC_JOBS_DEF::add(ctx, in, nullptr, nullptr));
  // if (0 != ret) {
  //   return ret;
  // }
  int ret = 0;

  // 尝试通知在线玩家, 失败则放弃。只是会延迟到账，不影响逻辑。
  do {
    // 不走路由系统，异步任务允许任意节点发送，但是有些服务不需要拉缓存对象
    PROJECT_NAMESPACE_ID::table_login* login_table = ctx.create<PROJECT_NAMESPACE_ID::table_login>();
    PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync* req_body = ctx.create<PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync>();
    if (nullptr == login_table || nullptr == req_body) {
      FWLOGERROR("rpc::db::login::get({}, {}) create table_login or SSPlayerAsyncJobsSync failed, ignore notify",
                 user_id, zone_id);
      break;
    }

    auto res = RPC_AWAIT_CODE_RESULT(detail::fetch_user_login_cache(ctx, user_id, zone_id, *login_table));
    if (res < 0) {
      if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
        FWLOGWARNING("rpc::db::login::get({}, {}) but not found, maybe not created yet", user_id, zone_id);
      } else {
        FWLOGERROR("rpc::db::login::get({}, {}) failed, res: {}", user_id, zone_id, res);
      }
      break;
    }

    // 不在线则不用通知
    if (0 == login_table->router_server_id() ||
        login_table->login_code_expired() <= ::util::time::time_utility::get_now()) {
      break;
    }

    RPC_AWAIT_IGNORE_RESULT(rpc::game::player_async_jobs_sync(ctx, login_table->router_server_id(), zone_id, user_id,
                                                              LOG_WRAPPER_FWAPI_FORMAT("{}", user_id), *req_body));
  } while (false);
  RPC_DB_RETURN_CODE(ret);
}

result_type remove_all_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id) {
  if (0 == jobs_type || 0 == user_id) {
    FWLOGERROR("{} be called with invalid parameters.(jobs_type={}, user_id={})", __FUNCTION__, jobs_type, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (NULL ==
      PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor()->FindValueByNumber(static_cast<int>(jobs_type))) {
    FWLOGERROR("{} be called with unsupported type.(jobs_type={}, user_id={})", __FUNCTION__, jobs_type, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // TODO db operation
  // return RPC_AWAIT_CODE_RESULT(rpc::db::TABLE_USER_ASYNC_JOBS_DEF::remove_all(ctx, jobs_type, user_id, zone_id));
  RPC_DB_RETURN_CODE(0);
}

result_type update_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                        PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data& inout, int64_t record_index,
                        int64_t* version) {
  if (0 == jobs_type || 0 == user_id) {
    FWLOGERROR("{} be called with invalid parameters.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type,
               user_id, zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (NULL ==
      PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor()->FindValueByNumber(static_cast<int>(jobs_type))) {
    FWLOGERROR("{} be called with unsupported type.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type,
               user_id, zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data::ACTION_NOT_SET == inout.action_case()) {
    FWLOGERROR("{} be called without a action.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type, user_id,
               zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (record_index < 0) {
    FWLOGERROR("{} be called with invalid index {}.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, record_index,
               jobs_type, user_id, zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (inout.action_uuid().empty()) {
    inout.set_action_uuid(rpc::db::uuid::generate_short_uuid());
  }

  inout.set_timepoint_ms(util::time::time_utility::get_now() * 1000 + util::time::time_utility::get_now_usec() / 1000);

  // TODO db operation
  // auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::TABLE_USER_ASYNC_JOBS_DEF::replace(ctx, inout, record_index, version));
  // if (0 != ret) {
  //   return ret;
  // }
  int ret = 0;

  // 尝试通知在线玩家, 失败则放弃。只是会延迟到账，不影响逻辑。
  do {
    // 不走路由系统，异步任务允许任意节点发送，但是有些服务不需要拉缓存对象
    PROJECT_NAMESPACE_ID::table_login* login_table = ctx.create<PROJECT_NAMESPACE_ID::table_login>();
    PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync* req_body = ctx.create<PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync>();
    if (nullptr == login_table || nullptr == req_body) {
      FWLOGERROR("rpc::db::login::get({}, {}) create TABLE_LOGIN_DEF or SSPlayerAsyncJobsSync failed, ignore notify",
                 user_id, zone_id);
      break;
    }

    auto res = RPC_AWAIT_CODE_RESULT(detail::fetch_user_login_cache(ctx, user_id, zone_id, *login_table));
    if (res < 0) {
      if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
        FWLOGWARNING("rpc::db::login::get({}, {}) but not found, maybe not created yet", user_id, zone_id);
      } else {
        FWLOGERROR("rpc::db::login::get({}, {}) failed, res: {}", user_id, zone_id, res);
      }
      break;
    }

    // 不在线则不用通知
    if (0 == login_table->router_server_id() ||
        login_table->login_code_expired() <= ::util::time::time_utility::get_now()) {
      break;
    }

    RPC_AWAIT_IGNORE_RESULT(rpc::game::player_async_jobs_sync(ctx, login_table->router_server_id(), zone_id, user_id,
                                                              LOG_WRAPPER_FWAPI_FORMAT("{}", user_id), *req_body));
  } while (false);

  RPC_DB_RETURN_CODE(ret);
}

}  // namespace async_jobs
}  // namespace db
}  // namespace rpc

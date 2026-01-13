// Copyright 2021 atframework
// Created by owent on 2019-06-17.
//

#include "rpc/async_jobs/async_jobs.h"

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

#include "rpc/db/db_utils.h"
#include "rpc/game/gamesvrservice.h"
#include "rpc/game/player.h"
#include "rpc/rpc_macros.h"
#include "rpc/rpc_utils.h"

#include "rpc/db/uuid.h"

namespace rpc {
namespace async_jobs {

namespace detail {
struct player_key_hash_t {
  size_t operator()(const PROJECT_NAMESPACE_ID::DPlayerIDKey& key) const {
    uint64_t out[2] = {0};
    uint64_t val = key.user_id();
    atfw::util::hash::murmur_hash3_x64_128(&val, static_cast<int>(sizeof(val)), key.zone_id(), out);
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
                                                    shared_message<PROJECT_NAMESPACE_ID::table_login>& rsp,
                                                    bool ignore_cache) {
  static std::unordered_map<PROJECT_NAMESPACE_ID::DPlayerIDKey,
                            atfw::util::memory::strong_rc_ptr<shared_message<PROJECT_NAMESPACE_ID::table_login>>,
                            player_key_hash_t, player_key_equal_t>
      local_cache;
  static time_t local_cache_timepoint = 0;
  time_t now = atfw::util::time::time_utility::get_now();
  if (now != local_cache_timepoint) {
    local_cache_timepoint = now;
    local_cache.clear();
  }

  PROJECT_NAMESPACE_ID::DPlayerIDKey key;
  key.set_user_id(user_id);
  key.set_zone_id(zone_id);

  if (!ignore_cache) {
    auto iter_cache = local_cache.find(key);
    if (iter_cache != local_cache.end() && iter_cache->second) {
      protobuf_copy_message(*rsp, **iter_cache->second);
      RPC_RETURN_CODE(0);
    }
  }

  uint64_t version = 0;
  int ret = RPC_AWAIT_CODE_RESULT(rpc::db::login::get_all(ctx, user_id, zone_id, rsp, version));
  if (0 == ret) {
    local_cache[key] = atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::table_login>>(rsp);
  }
  RPC_RETURN_CODE(ret);
}
}  // namespace detail

GAME_RPC_API ::rpc::db::result_type get_jobs(
    rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
    std::vector<rpc::db::async_jobs::table_user_async_jobs_list_message>& out) {
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

  RPC_DB_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::async_jobs::get_all(ctx, jobs_type, user_id, zone_id, out)));
}

GAME_RPC_API ::rpc::db::result_type del_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                                             const std::vector<uint64_t>& in) {
  if (0 == jobs_type || 0 == user_id) {
    FWLOGERROR("{} be called with invalid parameters.(jobs_type={}, zone_id={}, user_id={})", __FUNCTION__, jobs_type,
               zone_id, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (NULL ==
      PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor()->FindValueByNumber(static_cast<int>(jobs_type))) {
    FWLOGERROR("{} be called with unsupported type.(jobs_type={}, zone_id={}, user_id={})", __FUNCTION__, jobs_type,
               zone_id, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (in.empty()) {
    RPC_DB_RETURN_CODE(0);
  }

  RPC_DB_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      rpc::db::async_jobs::remove_by_index(ctx, jobs_type, user_id, zone_id, gsl::make_span(in))));
}

GAME_RPC_API ::rpc::db::result_type add_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                                             shared_message<PROJECT_NAMESPACE_ID::user_async_jobs_blob_data>& in,
                                             action_options options) {
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

  if (PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::ACTION_NOT_SET == in->action_case()) {
    FWLOGERROR("{} be called without a action.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type, user_id,
               zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (in->action_uuid().empty()) {
    in->set_action_uuid(rpc::db::uuid::generate_short_uuid());
  }
  in->set_timepoint_ms(util::time::time_utility::get_now() * 1000 +
                       atfw::util::time::time_utility::get_now_usec() / 1000);

  rpc::shared_message<hello::table_user_async_jobs> input{ctx};
  input->set_job_type(jobs_type);
  input->set_user_id(user_id);
  input->set_zone_id(zone_id);
  protobuf_copy_message(*input->mutable_job_data(), *in);

  // 生成RecordId
  int64_t record_index = RPC_AWAIT_TYPE_RESULT(rpc::db::uuid::generate_global_unique_id(
      ctx, static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_DB_LIST_RECORD_ID),
      static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MIT_DB_LIST_RECORD_ID_ASYNC_JOBS), 0));
  if (record_index < 0) {
    RPC_RETURN_CODE(static_cast<int>(record_index));
  }

  uint64_t record_version = 0;
  int32_t ret =
      RPC_AWAIT_CODE_RESULT(rpc::db::async_jobs::replace(ctx, record_index, std::move(input), record_version));
  if (0 != ret) {
    RPC_DB_RETURN_CODE(ret);
  }

  // 尝试通知在线玩家, 失败则放弃。只是会延迟到账，不影响逻辑。
  do {
    if (!options.notify_player) {
      break;
    }
    // 不走路由系统，异步任务允许任意节点发送，但是有些服务不需要拉缓存对象
    shared_message<PROJECT_NAMESPACE_ID::table_login> login_table{ctx};
    shared_message<PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync> req_body{ctx};

    auto res = RPC_AWAIT_CODE_RESULT(
        detail::fetch_user_login_cache(ctx, user_id, zone_id, login_table, options.ignore_router_cache));
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
        login_table->login_code_expired() <= atfw::util::time::time_utility::get_sys_now()) {
      break;
    }

    RPC_AWAIT_IGNORE_RESULT(rpc::game::player_async_jobs_sync(ctx, login_table->router_server_id(), zone_id, user_id,
                                                              atfw::util::log::format("{}", user_id), *req_body));
  } while (false);
  RPC_DB_RETURN_CODE(ret);
}

GAME_RPC_API result_code_type
add_jobs_with_retry(rpc::context& ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                    shared_message<PROJECT_NAMESPACE_ID::user_async_jobs_blob_data>& inout, action_options options) {
  if (inout->left_retry_times() <= 0) {
    inout->set_left_retry_times(logic_config::me()->get_server_cfg().user().async_job().default_retry_times());
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(add_jobs(ctx, jobs_type, user_id, zone_id, inout, options)));
}

GAME_RPC_API ::rpc::db::result_type remove_all_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id,
                                                    uint32_t zone_id) {
  if (0 == jobs_type || 0 == user_id) {
    FWLOGERROR("{} be called with invalid parameters.(jobs_type={}, zone_id={}, user_id={})", __FUNCTION__, jobs_type,
               zone_id, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (NULL ==
      PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor()->FindValueByNumber(static_cast<int>(jobs_type))) {
    FWLOGERROR("{} be called with unsupported type.(jobs_type={}, zone_id={}, user_id={})", __FUNCTION__, jobs_type,
               zone_id, user_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  RPC_DB_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::async_jobs::remove_all(ctx, jobs_type, user_id, zone_id)));
}

GAME_RPC_API ::rpc::db::result_type update_jobs(rpc::context& ctx, int32_t jobs_type, uint64_t user_id,
                                                uint32_t zone_id,
                                                shared_message<PROJECT_NAMESPACE_ID::table_user_async_jobs>& input,
                                                int64_t record_index, uint64_t& version, action_options options) {
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

  if (PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::ACTION_NOT_SET == input->job_data().action_case()) {
    FWLOGERROR("{} be called without a action.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, jobs_type, user_id,
               zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (record_index < 0) {
    FWLOGERROR("{} be called with invalid index {}.(jobs_type={}, user_id={}, zone_id={})", __FUNCTION__, record_index,
               jobs_type, user_id, zone_id);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (input->job_data().action_uuid().empty()) {
    input->mutable_job_data()->set_action_uuid(rpc::db::uuid::generate_short_uuid());
  }

  input->mutable_job_data()->set_timepoint_ms(util::time::time_utility::get_now() * 1000 +
                                              atfw::util::time::time_utility::get_now_usec() / 1000);

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::async_jobs::replace(ctx, record_index, std::move(input), version));
  if (0 != ret) {
    RPC_DB_RETURN_CODE(ret);
  }

  // 尝试通知在线玩家, 失败则放弃。只是会延迟到账，不影响逻辑。
  do {
    if (!options.notify_player) {
      break;
    }
    // 不走路由系统，异步任务允许任意节点发送，但是有些服务不需要拉缓存对象
    shared_message<PROJECT_NAMESPACE_ID::table_login> login_table{ctx};
    shared_message<PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync> req_body{ctx};

    auto res = RPC_AWAIT_CODE_RESULT(
        detail::fetch_user_login_cache(ctx, user_id, zone_id, login_table, options.ignore_router_cache));
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
        login_table->login_code_expired() <= atfw::util::time::time_utility::get_sys_now()) {
      break;
    }

    RPC_AWAIT_IGNORE_RESULT(rpc::game::player_async_jobs_sync(ctx, login_table->router_server_id(), zone_id, user_id,
                                                              atfw::util::log::format("{}", user_id), *req_body));
  } while (false);

  RPC_DB_RETURN_CODE(ret);
}

}  // namespace async_jobs
}  // namespace rpc

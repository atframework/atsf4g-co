// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#include "rpc/db/player.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <hiredis_happ.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include "rpc/db/db_macros.h"
#include "rpc/db/db_utils.h"
#include "rpc/db/hash_table.h"

#include "rpc/rpc_common_types.h"
#include "rpc/rpc_utils.h"

#define RPC_DB_TABLE_NAME "player"

namespace rpc {
namespace db {
namespace player {

namespace detail {
static int32_t unpack_user(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FWLOGDEBUG("data mot found.");
    // 数据找不到，直接成功结束，外层会判为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return rpc::db::unpack_message(*msg.mutable_user(), reply, msg.mutable_version());
}
}  // namespace detail

result_type get_all(rpc::context &ctx, uint64_t user_id, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_user &rsp,
                    std::string &version) {
  table_key_type user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, user_id, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
  }

  PROJECT_NAMESPACE_ID::table_all_message output;
  // 协程操作
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::get_all(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{user_key, user_key_len}, output,
                                                                detail::unpack_user));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }

  version.assign(output.version());
  rsp.Swap(output.mutable_user());

  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

result_type get_basic(rpc::context &ctx, uint64_t user_id, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_user &rsp) {
  std::string version;
  return get_all(ctx, user_id, zone_id, rsp, version);
}

result_type set(rpc::context &ctx, uint64_t user_id, uint32_t zone_id, const PROJECT_NAMESPACE_ID::table_user &store,
                std::string &version) {
  table_key_type user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, user_id, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
  }

  // args unavailable now
  PROJECT_NAMESPACE_ID::table_all_message output;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                            gsl::string_view{user_key, user_key_len}, store, version,
                                                            output, detail::unpack_user));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }

  return rpc::db::result_type(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace player
}  // namespace db
}  // namespace rpc
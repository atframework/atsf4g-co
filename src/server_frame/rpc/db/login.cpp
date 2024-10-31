// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#include "rpc/db/login.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

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

#define RPC_DB_TABLE_NAME "login"

namespace rpc {
namespace db {
namespace login {

namespace detail {
static int32_t unpack_login(PROJECT_NAMESPACE_ID::table_all_message &table_msg, const redisReply *reply) {
  if (nullptr == reply) {
    FWLOGDEBUG("{}", "data not found.");
    // 数据找不到，直接成功结束，外层会判断为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return rpc::db::unpack_message(*table_msg.mutable_login(), reply, table_msg.mutable_version());
}
}  // namespace detail

result_type get(rpc::context &ctx, const char *openid, uint32_t zone_id,
                shared_message<PROJECT_NAMESPACE_ID::table_login> &rsp, std::string &version) {
  table_key_type user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, openid, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
  }

  shared_message<PROJECT_NAMESPACE_ID::table_all_message> output{ctx};
  // 协程操作
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::get_all(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{user_key, user_key_len}, output,
                                                                detail::unpack_login));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }

  version.assign(output->version());
  rsp->Swap(output->mutable_login());

  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

result_type set(rpc::context &ctx, const char *openid, uint32_t zone_id,
                const shared_message<PROJECT_NAMESPACE_ID::table_login> &store, std::string &version) {
  table_key_type user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, openid, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
  }

  // args unavailable now
  shared_message<PROJECT_NAMESPACE_ID::table_all_message> output{ctx};
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                            gsl::string_view{user_key, user_key_len}, store, version,
                                                            output, detail::unpack_login));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }

  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
}  // namespace login
}  // namespace db
}  // namespace rpc
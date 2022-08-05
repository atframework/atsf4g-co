// Copyright 2021 atframework
// Created by owent on 2022-03-29.
//

#include "rpc/db/distribute_transaction.h"

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

#define RPC_DB_TABLE_NAME "distribute_transaction"

namespace rpc {
namespace db {
namespace distribute_transaction {

namespace detail {
static int32_t unpack_table_distribute_transaction(PROJECT_NAMESPACE_ID::table_all_message &table_msg,
                                                   const redisReply *reply) {
  if (nullptr == reply) {
    FWLOGDEBUG("{}", "data not found.");
    //数据找不到，直接成功结束，外层会判断为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return rpc::db::unpack_message(*table_msg.mutable_distribute_transaction(), reply, table_msg.mutable_version());
}
}  // namespace detail

result_type get(rpc::context &ctx, uint32_t zone_id, gsl::string_view transaction_uuid,
                PROJECT_NAMESPACE_ID::table_distribute_transaction &output, std::string &version) {
  table_key_type user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, transaction_uuid, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
  }

  PROJECT_NAMESPACE_ID::table_all_message table_container;
  // 协程操作
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::get_all(
      ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, gsl::string_view{user_key, user_key_len}, table_container,
      detail::unpack_table_distribute_transaction));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }

  version.assign(table_container.version());
  output.Swap(table_container.mutable_distribute_transaction());

  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

result_type set(rpc::context &ctx, uint32_t zone_id, gsl::string_view transaction_uuid,
                const PROJECT_NAMESPACE_ID::table_distribute_transaction &store, std::string &version) {
  table_key_type user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, transaction_uuid, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
  }

  // args unavailable now
  PROJECT_NAMESPACE_ID::table_all_message output;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                            gsl::string_view{user_key, user_key_len}, store, version,
                                                            output, detail::unpack_table_distribute_transaction));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }

  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

result_type remove(rpc::context &ctx, uint32_t zone_id, gsl::string_view transaction_uuid) {
  table_key_type user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, transaction_uuid, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
  }

  // args unavailable now
  PROJECT_NAMESPACE_ID::table_all_message output;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::remove_all(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                   gsl::string_view{user_key, user_key_len}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }

  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace distribute_transaction
}  // namespace db
}  // namespace rpc
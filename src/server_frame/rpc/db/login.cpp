// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include "rpc/db/login.h"

#include <log/log_wrapper.h>

#include <hiredis_happ.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include "rpc/db/db_macros.h"
#include "rpc/db/db_utils.h"
#include "rpc/rpc_macros.h"
#include "rpc/rpc_utils.h"

#define RPC_DB_TABLE_NAME "login"

namespace rpc {
namespace db {
namespace login {

namespace detail {
static int32_t unpack_login(PROJECT_NAMESPACE_ID::table_all_message &table_msg, const redisReply *reply) {
  if (nullptr == reply) {
    WLOGDEBUG("data not found.");
    //数据找不到，直接成功结束，外层会判断为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return ::rpc::db::unpack_message(*table_msg.mutable_login(), reply, table_msg.mutable_version());
}
}  // namespace detail

result_type get(::rpc::context &ctx, const char *openid, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_login &rsp,
                std::string &version) {
  rpc::context __child_ctx(ctx);
  rpc::context::tracer __tracer;
  ::rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, "rpc.db.login.get", std::move(__trace_option));

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    WLOGERROR("current not in a task");
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK));
  }

  user_table_key_t user_key;
  size_t writen_len = format_user_key(user_key, RPC_DB_TABLE_NAME, openid, zone_id);
  if (writen_len <= 0) {
    WLOGERROR("format db cmd failed, cmd %s", user_key);
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED));
  }

  redis_args args(2);
  {
    args.push("HGETALL");
    args.push(user_key);
  }

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, writen_len,
                                              task->get_id(), logic_config::me()->get_local_server_id(),
                                              detail::unpack_login, rpc_sequence, static_cast<int>(args.size()),
                                              args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  PROJECT_NAMESPACE_ID::table_all_message table_msg;
  // 协程操作
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(table_msg, rpc_sequence));
  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  if (!table_msg.has_login() || table_msg.version().empty()) {
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND));
  }

  version.assign(table_msg.version());
  rsp.Swap(table_msg.mutable_login());
  return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

result_type set(::rpc::context &ctx, const char *openid, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_login &rsp,
                std::string &version) {
  rpc::context __child_ctx(ctx);
  rpc::context::tracer __tracer;
  ::rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, "rpc.db.login.set", std::move(__trace_option));

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    WLOGERROR("current not in a task");
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK));
  }

  if (version.empty()) {
    version = "0";
  }

  user_table_key_t user_key;
  size_t writen_len = format_user_key(user_key, RPC_DB_TABLE_NAME, openid, zone_id);
  if (writen_len <= 0) {
    WLOGERROR("format db cmd failed, cmd %s", user_key);
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED));
  }

  std::vector<const ::google::protobuf::FieldDescriptor *> fds;
  const ::google::protobuf::Descriptor *desc = PROJECT_NAMESPACE_ID::table_login::descriptor();
  fds.reserve(static_cast<size_t>(desc->field_count()));

  for (int i = 0; i < desc->field_count(); ++i) {
    fds.push_back(desc->field(i));
  }

  // version will take two segments
  // each fd will take key segment and value segment
  redis_args args(fds.size() * 2 + 6);
  {
    args.push("EVALSHA");
    args.push(db_msg_dispatcher::me()->get_db_script_sha1(PROJECT_NAMESPACE_ID::EN_DBSST_LOGIN));
    args.push(1);
    args.push(user_key);
  }

  std::stringstream segs_debug_info;
  int res = ::rpc::db::pack_message(rsp, args, fds, &version, &segs_debug_info);
  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  uint64_t rpc_sequence = 0;
  res = db_msg_dispatcher::me()->send_msg(
      db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, static_cast<size_t>(writen_len), task->get_id(),
      logic_config::me()->get_local_server_id(), detail::unpack_login, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  // args unavailable now

  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  PROJECT_NAMESPACE_ID::table_all_message table_msg;
  // 协程操作
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(table_msg, rpc_sequence));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res && !table_msg.version().empty()) {
      version.assign(table_msg.version());
    }

    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  if (table_msg.version().empty()) {
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND));
  }

  version.assign(table_msg.version());

  WLOGINFO("table_login [openid=%s] all saved, new version: %s, detail: %s", openid, version.c_str(),
           segs_debug_info.str().c_str());
  return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}
}  // namespace login
}  // namespace db
}  // namespace rpc
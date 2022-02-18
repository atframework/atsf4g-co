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
#include "rpc/rpc_macros.h"
#include "rpc/rpc_utils.h"

#define RPC_DB_TABLE_NAME "player"

namespace rpc {
namespace db {
namespace player {

namespace detail {
static int32_t unpack_user(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *reply) {
  if (nullptr == reply) {
    WLOGDEBUG("data mot found.");
    // 数据找不到，直接成功结束，外层会判为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return ::rpc::db::unpack_message(*msg.mutable_user(), reply, msg.mutable_version());
}
}  // namespace detail

result_type get_all(::rpc::context &ctx, uint64_t user_id, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_user &rsp,
                    std::string &version) {
  rpc::context __child_ctx(ctx);
  rpc::context::tracer __tracer;
  ::rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, "rpc.db.player.get_all", std::move(__trace_option));

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK));
  }

  user_table_key_t user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, user_id, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED));
  }

  redis_args args(2);
  {
    args.push("HGETALL");
    args.push(user_key);
  }

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, user_key_len,
                                              task->get_id(), logic_config::me()->get_local_server_id(),
                                              detail::unpack_user, rpc_sequence, static_cast<int>(args.size()),
                                              args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  PROJECT_NAMESPACE_ID::table_all_message msg;
  // 协程操作
  res = rpc::wait(msg, rpc_sequence);
  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  if (!msg.has_user() || msg.version().empty()) {
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND));
  }

  version.assign(msg.version());
  rsp.Swap(msg.mutable_user());

  FWLOGINFO("table_user[user_id={}] get all data version: {}", user_id, version);
  return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

result_type get_basic(::rpc::context &ctx, uint64_t user_id, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_user &rsp) {
  std::string version;
  return get_all(ctx, user_id, zone_id, rsp, version);
}

result_type set(::rpc::context &ctx, uint64_t user_id, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_user &store,
                std::string &version) {
  rpc::context __child_ctx(ctx);
  rpc::context::tracer __tracer;
  ::rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, "rpc.db.player.set", std::move(__trace_option));

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK));
  }

  if (version.empty()) {
    version = "0";
  }

  std::stringstream segs_debug_info;

  user_table_key_t user_key;
  size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, user_id, zone_id);
  if (user_key_len <= 0) {
    FWLOGERROR("format db cmd failed, cmd {}", user_key);
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED));
  }

  std::vector<const ::google::protobuf::FieldDescriptor *> fds;
  const google::protobuf::Reflection *reflect = store.GetReflection();
  if (nullptr == reflect) {
    FWLOGERROR("pack message {} failed, get reflection failed", store.GetDescriptor()->full_name());
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK));
  }
  reflect->ListFields(store, &fds);
  // version will take two segments
  // each fd will take key segment and value segment
  redis_args args(fds.size() * 2 + 6);

  args.push("EVALSHA");
  args.push(db_msg_dispatcher::me()->get_db_script_sha1(PROJECT_NAMESPACE_ID::EN_DBSST_USER));
  args.push(1);
  args.push(user_key);

  int res = ::rpc::db::pack_message(store, args, fds, &version, &segs_debug_info);
  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  FWLOGDEBUG("user {} save curr data version: {}", user_id, version);

  uint64_t rpc_sequence = 0;
  res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, user_key_len,
                                          task->get_id(), logic_config::me()->get_local_server_id(),
                                          detail::unpack_user, rpc_sequence, static_cast<int>(args.size()),
                                          args.get_args_values(), args.get_args_lengths());

  // args unavailable now

  if (res < 0) {
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  PROJECT_NAMESPACE_ID::table_all_message msg;
  // 协程操作
  res = rpc::wait(msg, rpc_sequence);
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res && !msg.version().empty()) {
      version.assign(msg.version());
    }
    return ::rpc::db::result_type(__tracer.return_code(res));
  }

  if (msg.version().empty()) {
    return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND));
  }

  version.assign(msg.version());

  FWLOGINFO("table_user [user_id={}] all saved, new version: {}, detail: {}", user_id, version.c_str(),
            segs_debug_info.str().c_str());

  return ::rpc::db::result_type(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

}  // namespace player
}  // namespace db
}  // namespace rpc
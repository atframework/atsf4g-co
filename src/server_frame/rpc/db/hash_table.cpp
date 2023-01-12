// Copyright 2021 atframework
// Created by owent on 2022-03-17.
//

#include "rpc/db/hash_table.h"

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
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_utils.h"

namespace rpc {
namespace db {
namespace hash_table {

result_type get_all(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                    PROJECT_NAMESPACE_ID::table_all_message &output,
                    int32_t (*unpack_fn)(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *reply)) {
  rpc::context __child_ctx(ctx);
  rpc::context::tracer __tracer;
  rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, "rpc.db.hash_table.get_all", std::move(__trace_option));

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK));
  }

  redis_args args(2);
  args.push("HGETALL");
  args.push(key.data(), key.size());

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(),
                                              key.size(), task->get_id(), logic_config::me()->get_local_server_id(),
                                              unpack_fn, rpc_sequence, static_cast<int>(args.size()),
                                              args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.return_code(res));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(output, await_options));
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.return_code(res));
  }

  if (output.version().empty()) {
    RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND));
  }

  FWLOGINFO("table [key={}] get all data version: {}", key, output.version());
  RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

result_type set(rpc::context &ctx, uint32_t channel, gsl::string_view key, const google::protobuf::Message &store,
                std::string &version, PROJECT_NAMESPACE_ID::table_all_message &output,
                int32_t (*unpack_fn)(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *reply)) {
  rpc::context __child_ctx(ctx);
  rpc::context::tracer __tracer;
  rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, "rpc.db.hash_table.set", std::move(__trace_option));

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK));
  }

  if (version.empty()) {
    version = "0";
  }

  std::stringstream segs_debug_info;

  std::vector<const ::google::protobuf::FieldDescriptor *> fds;
  const google::protobuf::Reflection *reflect = store.GetReflection();
  if (nullptr == reflect) {
    FWLOGERROR("pack message {} failed, get reflection failed", store.GetDescriptor()->full_name());
    RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK));
  }
  reflect->ListFields(store, &fds);
  // version will take two segments
  // each fd will take key segment and value segment
  redis_args args(fds.size() * 2 + 6);

  args.push("EVALSHA");
  args.push(db_msg_dispatcher::me()->get_db_script_sha1(db_msg_dispatcher::script_type::kCompareAndSetHashTable));
  args.push(1);
  args.push(key.data(), key.size());

  int res = rpc::db::pack_message(store, args, fds, &version, &segs_debug_info);
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.return_code(res));
  }

  FWLOGDEBUG("table [key={}] start to save data, expect version: {}", key, version);

  uint64_t rpc_sequence = 0;
  res = db_msg_dispatcher::me()->send_msg(static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(),
                                          key.size(), task->get_id(), logic_config::me()->get_local_server_id(),
                                          unpack_fn, rpc_sequence, static_cast<int>(args.size()),
                                          args.get_args_values(), args.get_args_lengths());

  // args unavailable now

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.return_code(res));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(output, await_options));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res && !output.version().empty()) {
      version.assign(output.version());
    }
    RPC_DB_RETURN_CODE(__tracer.return_code(res));
  }

  if (output.version().empty()) {
    RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND));
  }

  version.assign(output.version());

  FWLOGINFO("table [key={}] data saved, new version: {}, detail: {}", key, output.version(), segs_debug_info.str());
  RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

result_type remove_all(rpc::context &ctx, uint32_t channel, gsl::string_view key) {
  rpc::context __child_ctx(ctx);
  rpc::context::tracer __tracer;
  rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, "rpc.db.hash_table.remove_all", std::move(__trace_option));

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK));
  }

  redis_args args(4);

  args.push("DEL");
  args.push(key.data(), key.size());

  FWLOGDEBUG("table [key={}] start to remove all data", key);

  uint64_t rpc_sequence = 0;
  result_type::value_type res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), task->get_id(),
      logic_config::me()->get_local_server_id(), nullptr, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  // args unavailable now

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.return_code(res));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  PROJECT_NAMESPACE_ID::table_all_message output;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(output, await_options));
  FWLOGINFO("table [key={}] all data removed", key);

  RPC_DB_RETURN_CODE(__tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

}  // namespace hash_table
}  // namespace db
}  // namespace rpc
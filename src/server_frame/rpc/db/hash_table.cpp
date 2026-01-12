// Copyright 2021 atframework
// Created by owent on 2022-03-17.
//

#include "rpc/db/hash_table.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <opentelemetry/semconv/incubating/db_attributes.h>
#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <log/log_wrapper.h>

#include <hiredis_happ.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include "rpc/db/db_utils.h"
#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_utils.h"

namespace rpc {
namespace db {
namespace hash_table {
namespace key_value {
SERVER_FRAME_API result_type get_all(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                     atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t> output,
                                     db_msg_dispatcher::unpack_fn_t unpack_fn) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_value.get"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemNameValues::kRedis}};

  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_value.get_all", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  redis_args args(2);
  args.push("HGETALL");
  args.push(key.data(), key.size());

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), unpack_fn, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  if (!db_message.body_message) {
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, __trace_attributes}));
  }
  output->message.swap(db_message.body_message);
  output->version = db_message.head_message.cas_version();

  if (output->version != 0) {
    FWLOGINFO("table [key={}] get all cas_version: {}", key, output->version);
  } else {
    FWLOGINFO("table [key={}] get all", key);
  }

  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type partly_get(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                        gsl::string_view *partly_get_fields, int32_t partly_get_field_count,
                                        atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t> output,
                                        db_msg_dispatcher::unpack_fn_t unpack_fn) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_value.partly_get"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};
  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_value.partly_get", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  int32_t args_size = 2 + partly_get_field_count;
  redis_args args(args_size);
  args.push("HMGet");
  args.push(key.data(), key.size());

  for (int32_t index = 0; index < partly_get_field_count; ++index) {
    args.push(partly_get_fields[index].data(), partly_get_fields[index].size());
  }

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), unpack_fn, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  if (!db_message.body_message) {
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, __trace_attributes}));
  }
  output->version = db_message.head_message.cas_version();
  output->message.swap(db_message.body_message);

  if (output->version != 0) {
    FWLOGINFO("table [key={}] partly_get cas_version: {}", key, output->version);
  } else {
    FWLOGINFO("table [key={}] partly_get", key);
  }

  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type
batch_get_all(rpc::context &ctx, uint32_t channel, gsl::span<std::string> key,
              std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> &output,
              db_msg_dispatcher::unpack_fn_t unpack_fn) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_value.batch_get_all"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemNameValues::kRedis}};

  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_value.get_all", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  std::vector<task_type_trait::task_type> pending_action_batch_tasks;
  const size_t pending_action_batch_count = 20;
  pending_action_batch_tasks.reserve(pending_action_batch_count);

  for (int32_t index = 0; index < key.size(); ++index) {
    atfw::util::time::time_utility::update();

    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_status);
    int32_t ret = task_manager::convert_task_status_to_error_code(current_status);
    if (ret < 0) {
      break;
    }

    auto current_key = key[index];
    auto output_ptr = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
    output_ptr->result = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
    output[index] = output_ptr;
    auto invoke_task = rpc::async_invoke(
        __child_ctx, "batch_get_all get_all sub task",
        [current_key, output_ptr, channel, unpack_fn](rpc::context &sub_ctx) -> rpc::result_code_type {
          auto result = RPC_AWAIT_CODE_RESULT(get_all(sub_ctx, channel, current_key, output_ptr, unpack_fn));
          output_ptr->result = result;
          RPC_RETURN_CODE(0);
        });
    bool need_wait = false;
    if (invoke_task.is_success()) {
      if (!task_type_trait::is_exiting(*invoke_task.get_success())) {
        pending_action_batch_tasks.emplace_back(std::move(*invoke_task.get_success()));
      }
    } else {
      need_wait = true;
    }
    if (pending_action_batch_tasks.size() >= pending_action_batch_count) {
      need_wait = true;
    }

    if (need_wait) {
      auto wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(__child_ctx, pending_action_batch_tasks));
      pending_action_batch_tasks.clear();
      if (wait_result < 0) {
        FWLOGERROR("Wait sub tasks to failed, result: {}({})", wait_result,
                   protobuf_mini_dumper_get_error_msg(wait_result));
      }
    }
  }

  if (!pending_action_batch_tasks.empty()) {
    auto wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(__child_ctx, pending_action_batch_tasks));
    pending_action_batch_tasks.clear();
    if (wait_result < 0) {
      FWLOGERROR("Wait sub tasks to remove_object failed, result: {}({})", wait_result,
                 protobuf_mini_dumper_get_error_msg(wait_result));
    }
  }

  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type
batch_partly_get(rpc::context &ctx, uint32_t channel, gsl::span<std::string> key, gsl::string_view *partly_get_fields,
                 int32_t partly_get_field_count,
                 std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> &output,
                 db_msg_dispatcher::unpack_fn_t unpack_fn) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_value.batch_partly_get"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};
  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_value.batch_partly_get", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  std::vector<task_type_trait::task_type> pending_action_batch_tasks;
  const size_t pending_action_batch_count = 20;
  pending_action_batch_tasks.reserve(pending_action_batch_count);

  for (int32_t index = 0; index < key.size(); ++index) {
    atfw::util::time::time_utility::update();

    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_status);
    int32_t ret = task_manager::convert_task_status_to_error_code(current_status);
    if (ret < 0) {
      break;
    }

    auto current_key = key[index];
    auto output_ptr = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
    output_ptr->result = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
    output[index] = output_ptr;
    auto invoke_task = rpc::async_invoke(
        __child_ctx, "batch_get_all get_all sub task",
        [current_key, output_ptr, channel, unpack_fn, partly_get_fields,
         partly_get_field_count](rpc::context &sub_ctx) -> rpc::result_code_type {
          auto result = RPC_AWAIT_CODE_RESULT(partly_get(sub_ctx, channel, current_key, partly_get_fields,
                                                         partly_get_field_count, output_ptr, unpack_fn));
          output_ptr->result = result;
          RPC_RETURN_CODE(0);
        });
    bool need_wait = false;
    if (invoke_task.is_success()) {
      if (!task_type_trait::is_exiting(*invoke_task.get_success())) {
        pending_action_batch_tasks.emplace_back(std::move(*invoke_task.get_success()));
      }
    } else {
      need_wait = true;
    }
    if (pending_action_batch_tasks.size() >= pending_action_batch_count) {
      need_wait = true;
    }

    if (need_wait) {
      auto wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(__child_ctx, pending_action_batch_tasks));
      pending_action_batch_tasks.clear();
      if (wait_result < 0) {
        FWLOGERROR("Wait sub tasks to failed, result: {}({})", wait_result,
                   protobuf_mini_dumper_get_error_msg(wait_result));
      }
    }
  }

  if (!pending_action_batch_tasks.empty()) {
    auto wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(__child_ctx, pending_action_batch_tasks));
    pending_action_batch_tasks.clear();
    if (wait_result < 0) {
      FWLOGERROR("Wait sub tasks to remove_object failed, result: {}({})", wait_result,
                 protobuf_mini_dumper_get_error_msg(wait_result));
    }
  }

  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type set(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                 shared_abstract_message<google::protobuf::Message> &&store, uint64_t *version) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_value.set"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};
  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_value.set", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  std::stringstream segs_debug_info;

  std::vector<const ::google::protobuf::FieldDescriptor *> fds;
  const google::protobuf::Reflection *reflect = store->GetReflection();
  if (nullptr == reflect) {
    FWLOGERROR("pack message {} failed, get reflection failed", store->GetDescriptor()->full_name());
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_PACK, __trace_attributes}));
  }
  reflect->ListFields(*store, &fds);

  int32_t args_size = static_cast<int32_t>(fds.size()) * 2;
  if (version != nullptr) {
    // EVALSHA
    // sha1
    // numkeys
    // key
    // version field name + version field value
    args_size += 6;
  } else {
    // HSET
    args_size += 2;
  }
  redis_args args(args_size);
  if (version != nullptr) {
    args.push("EVALSHA");
    args.push(db_msg_dispatcher::me()->get_db_script_sha1(db_msg_dispatcher::script_type::kCompareAndSetHashTable));
    args.push(1);
    args.push(key.data(), key.size());
  } else {
    args.push("HSET");
    args.push(key.data(), key.size());
  }

  int res = rpc::db::pack_message(*store, args, fds, version, &segs_debug_info);
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  if (version != nullptr) {
    FWLOGDEBUG("table [key={}] start to save data, expect version: {}", key, *version);
  } else {
    FWLOGDEBUG("table [key={}] start to save data", key);
  }

  uint64_t rpc_sequence = 0;
  res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), nullptr, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  // args unavailable now

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res && db_message.head_message.cas_version() != 0 &&
        version != nullptr) {
      *version = db_message.head_message.cas_version();
    }
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  if (version != nullptr) {
    *version = db_message.head_message.cas_version();
    FWLOGINFO("table [key={}] data saved, new cas_version: {}, detail: {}", key, db_message.head_message.cas_version(),
              segs_debug_info.str());
  } else {
    FWLOGINFO("table [key={}] data saved, detail: {}", key, segs_debug_info.str());
  }
  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type inc_field(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                       gsl::string_view inc_field,
                                       shared_abstract_message<google::protobuf::Message> &message,
                                       db_msg_dispatcher::unpack_fn_t unpack_fn) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_value.inc_field"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};
  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_value.inc_field", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  std::stringstream segs_debug_info;

  std::vector<const ::google::protobuf::FieldDescriptor *> fds;
  auto fd = message->GetDescriptor()->FindFieldByName(inc_field.data());
  if (nullptr == fd) {
    FWLOGERROR("field {} not found in message {}", inc_field, message->GetDescriptor()->full_name());
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_PACK, __trace_attributes}));
  }
  fds.push_back(fd);

  // 命名名 + Key 两个参数 + 需要操作的字段两个参数
  redis_args args(2 + 2);

  args.push("HINCRBY");
  args.push(key.data(), key.size());

  int res = rpc::db::pack_message(*message, args, fds, nullptr, &segs_debug_info);
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  FWLOGDEBUG("table [key={}] start to inc data", key);

  uint64_t rpc_sequence = 0;
  res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), unpack_fn, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  // args unavailable now
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }
  db_message.body_message->swap(message);

  FWLOGINFO("table [key={}] inc_field success, detail: {}", key, segs_debug_info.str());
  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}
}  // namespace key_value

namespace key_list {
SERVER_FRAME_API result_type get_all(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                     std::vector<db_key_list_message_result_t> &output,
                                     db_msg_dispatcher::unpack_fn_t unpack_fn) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_list.get_all"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};

  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_list.get_all", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  redis_args args(2);
  args.push("HGETALL");
  args.push(key.data(), key.size());

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), unpack_fn, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }
  if (db_message.body_message_list.empty()) {
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, __trace_attributes}));
  }
  output.swap(db_message.body_message_list);

  FWLOGINFO("table [key={}] key_list get all, count: {}", key, output.size());

  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type get_by_indexs(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                           gsl::span<uint64_t> list_index, bool enable_cas,
                                           std::vector<db_key_list_message_result_t> &output,
                                           db_msg_dispatcher::unpack_fn_t unpack_fn) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_list.get_by_index"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};

  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_list.get_by_index", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  if (list_index.empty()) {
    FWLOGERROR("list_index is empty");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, __trace_attributes}));
  }

  size_t args_size = 2 + list_index.size() * (enable_cas ? 2 : 1);
  redis_args args(args_size);
  args.push("HMGET");
  args.push(key.data(), key.size());
  for (auto index : list_index) {
    args.push(::rpc::db::get_list_value_field(index));
    if (enable_cas) {
      args.push(::rpc::db::get_list_version_field(index));
    }
  }

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), unpack_fn, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  if (db_message.body_message_list.size() != list_index.size()) {
    FWLOGERROR("returned message count {} not match request count {}", db_message.body_message_list.size(),
               list_index.size());
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, __trace_attributes}));
  }
  output.swap(db_message.body_message_list);
  for (size_t i = 0; i < list_index.size(); ++i) {
    output[i].list_index = list_index[i];
  }
  FWLOGINFO("table [key={}] key_list get_by_indexs {}", key, list_index);

  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type set_by_index(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                          uint64_t list_index,
                                          shared_abstract_message<google::protobuf::Message> &&store,
                                          uint64_t *version) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_list.set_by_index"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};

  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_list.set_by_index", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  std::string value_field = ::rpc::db::get_list_value_field(list_index);
  std::string version_field = ::rpc::db::get_list_version_field(list_index);
  redis_args args(version != nullptr ? 8 : 4);
  if (version != nullptr) {
    args.push("EVALSHA");
    args.push(db_msg_dispatcher::me()->get_db_script_sha1(db_msg_dispatcher::script_type::kCompareAndSetHashTable));
    args.push(1);
    args.push(key.data(), key.size());
    args.push(version_field);
    args.push(*version);
  } else {
    args.push("HSET");
    args.push(key.data(), key.size());
  }

  size_t dump_len = store->ByteSizeLong();
  char *data_allocated = args.alloc(dump_len + 1);
  if (nullptr == data_allocated) {
    FWLOGERROR("pack message {} failed", store->GetDescriptor()->full_name());
    args.dealloc();
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC, __trace_attributes}));
  }
  memcpy(data_allocated, "&", 1);
  data_allocated += 1;
  // 再dump 字段内容
  store->SerializeWithCachedSizesToArray(reinterpret_cast<::google::protobuf::uint8 *>(data_allocated));

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), nullptr, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));

  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION == res && db_message.head_message.cas_version() != 0 &&
        version != nullptr) {
      *version = db_message.head_message.cas_version();
    }
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  if (version != nullptr) {
    *version = db_message.head_message.cas_version();
    FWLOGINFO("table [key={}] data saved, new cas_version: {}", key, db_message.head_message.cas_version());
  } else {
    FWLOGINFO("table [key={}] data saved", key);
  }

  FWLOGINFO("table [key={}] key_list set_by_index {}", key, list_index);
  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                             gsl::span<uint64_t> list_index, bool enable_cas) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_list.remove_by_index"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};

  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_list.remove_by_index", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  size_t args_size = 2 + list_index.size();
  if (enable_cas) {
    args_size += list_index.size();
  }
  redis_args args(enable_cas ? 4 : 3);  // Note: CAS delete not fully supported yet, falling back to HDEL
  args.push("HDEL");
  args.push(key.data(), key.size());
  for (auto index : list_index) {
    args.push(::rpc::db::get_list_value_field(index));
    if (enable_cas) {
      args.push(::rpc::db::get_list_version_field(index));
    }
  }

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), nullptr, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  FWLOGINFO("table [key={}] key_list remove_by_index {}", key, list_index);
  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                             gsl::span<const uint64_t> list_index, bool enable_cas) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.key_list.remove_by_index"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};

  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer =
      __child_ctx.make_tracer("rpc.db.hash_table.key_list.remove_by_index", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  size_t args_size = 2 + list_index.size();
  if (enable_cas) {
    args_size += list_index.size();
  }
  redis_args args(enable_cas ? 4 : 3);  // Note: CAS delete not fully supported yet, falling back to HDEL
  args.push("HDEL");
  args.push(key.data(), key.size());
  for (auto index : list_index) {
    args.push(::rpc::db::get_list_value_field(index));
    if (enable_cas) {
      args.push(::rpc::db::get_list_version_field(index));
    }
  }

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), nullptr, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));
  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  FWLOGINFO("table [key={}] key_list remove_by_index {}", key, list_index);
  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}
}  // namespace key_list

SERVER_FRAME_API result_type remove_all(rpc::context &ctx, uint32_t channel, gsl::string_view key) {
  rpc::context __child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.db"},
      {opentelemetry::semconv::rpc::kRpcService, "rpc.db.redis"},
      {opentelemetry::semconv::rpc::kRpcMethod, "rpc.db.hash_table.remove_all"},
      {opentelemetry::semconv::db::kDbSystemName, opentelemetry::semconv::db::DbSystemValues::kRedis}};
  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = __trace_attributes;

  rpc::telemetry::tracer __tracer = __child_ctx.make_tracer("rpc.db.hash_table.remove_all", std::move(__trace_option));

  if (ctx.get_task_context().task_id == 0) {
    FWLOGERROR("current not in a task");
    RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK, __trace_attributes}));
  }

  redis_args args(4);

  args.push("DEL");
  args.push(key.data(), key.size());

  FWLOGDEBUG("table [key={}] start to remove all data", key);

  uint64_t rpc_sequence = 0;
  result_type::value_type res = db_msg_dispatcher::me()->send_msg(
      static_cast<db_msg_dispatcher::channel_t::type>(channel), key.data(), key.size(), ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), nullptr, rpc_sequence, static_cast<int>(args.size()),
      args.get_args_values(), args.get_args_lengths());

  // args unavailable now

  if (res < 0) {
    RPC_DB_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  // 协程操作
  db_message_t db_message;
  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, db_message, await_options));
  FWLOGINFO("table [key={}] all data removed", key);

  RPC_DB_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, __trace_attributes}));
}

}  // namespace hash_table
}  // namespace db
}  // namespace rpc

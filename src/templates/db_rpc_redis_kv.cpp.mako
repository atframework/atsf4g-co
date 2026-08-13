## -*- coding: utf-8 -*-
<%page args="message_name,index,key_fields,all_fields,atomic_inc_fields,prefix_fmt_key,prefix_fmt_value_from_args,prefix_fmt_value_from_key,prefix_fmt_value_from_pb" />
<%def name="emit_mock_slot(slot, handler_type)">
namespace mock_detail {
using ${slot}_handler_t = ${handler_type};
static std::mutex g_${slot}_mutex;
static ${slot}_handler_t g_${slot}_handler;
static uint64_t g_${slot}_generation = 0;
static inline ${slot}_handler_t ${slot}_handler() {
  std::lock_guard<std::mutex> lock(g_${slot}_mutex);
  return g_${slot}_handler;
}
}  // namespace mock_detail
</%def>
<%def name="emit_mock_register(fn, slot, op_code, table_name)">
SERVER_FRAME_API rpc::unit_test::mock_rule_handle ${fn}(mock_detail::${slot}_handler_t __handler) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_register_typed_handler || !__handler) {
    return rpc::unit_test::mock_rule_handle{};
  }
  uint64_t __generation = 0;
  {
    std::lock_guard<std::mutex> __lock(mock_detail::g_${slot}_mutex);
    mock_detail::g_${slot}_handler = std::move(__handler);
    __generation = ++mock_detail::g_${slot}_generation;
  }
  auto __clear = [__generation]() {
    std::lock_guard<std::mutex> __lock(mock_detail::g_${slot}_mutex);
    if (__generation == mock_detail::g_${slot}_generation) {
      mock_detail::g_${slot}_handler = nullptr;
    }
  };
  __bridge.db_register_typed_handler("${table_name}", ${op_code}, __clear);
  auto __state = std::make_shared<char>(0);
  return rpc::unit_test::mock_rule_handle{std::shared_ptr<void>{__state.get(), [__state, __clear](void *) { __clear(); }}};
}
</%def>
<%def name="emit_mock_input_fill(key_fields)">
%   for key_field in key_fields:
    __input.set_${key_field["raw_name"]}(
%     for arg in key_field["set_args"]:
        ${arg}
%     endfor
    );
%   endfor
</%def>
<%def name="emit_mock_intercept_get(slot, with_version, key_fields, message_name)">
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::${slot}_handler()) {
    PROJECT_NAMESPACE_ID::${message_name} __input;
${emit_mock_input_fill(key_fields)}\
    rpc::unit_test::db_mock_meta __meta;
    int __res = RPC_AWAIT_CODE_RESULT(__handler(ctx, __input, rsp, __meta));
%   if with_version:
    version = __meta.version;
%   endif
    RPC_DB_RETURN_CODE(__res);
  }
#endif
</%def>
namespace detail {
static int32_t unpack_${message_name}(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FCTXLOGDEBUG(*ctx, "{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  shared_message<PROJECT_NAMESPACE_ID::${message_name}> table_pb{*ctx};
  uint64_t version = 0;
  bool record_existed = false;
  int32_t ret = rpc::db::unpack_message(*table_pb.get(), reply, version, record_existed);
  msg.head_message.set_response_int(version);
  if (record_existed) {
    msg.body_message =
        atfw::component::memory::stl::make_strong_rc<rpc::shared_abstract_message<google::protobuf::Message>>(std::move(table_pb));
  }
  return ret;
}
} // namespace detail

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
${emit_mock_slot("get_all", "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, PROJECT_NAMESPACE_ID::" + message_name + " &, rpc::unit_test::db_mock_meta &)>")}
${emit_mock_slot("replace", "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, rpc::unit_test::db_mock_meta &)>")}
% for inc_field in atomic_inc_fields:
${emit_mock_slot("inc_field_" + inc_field["raw_name"], "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, " + inc_field["cpp_type"] + " &, rpc::unit_test::db_mock_meta &)>")}
% endfor
% for partly_get in index.partly_get:
<%
    partly_slot_name = ""
    if partly_get.name != "":
        partly_slot_name += partly_get.name
    else:
        for field in partly_get.fields:
            partly_slot_name += field
%>
${emit_mock_slot("partly_get_" + partly_slot_name, "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, PROJECT_NAMESPACE_ID::" + message_name + " &, rpc::unit_test::db_mock_meta &)>")}
% endfor
#endif

SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , PROJECT_NAMESPACE_ID::${message_name} &rsp
% if index.enable_cas:
                                                             , uint64_t &version) {
% else:
                                                              ) {
% endif
${emit_mock_intercept_get("get_all", index.enable_cas, key_fields, message_name)}  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen},
                                                                output,
                                                                &detail::unpack_${message_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  rpc::shared_message<PROJECT_NAMESPACE_ID::${message_name}> tmp{*output->message};
  protobuf_move_message(rsp, std::move(*tmp));
% if index.enable_cas:
  version = output->version;
% endif
% for key_field in key_fields:
  rsp.set_${key_field["raw_name"]}(
  % for arg in key_field["set_args"]:
      ${arg}
  % endfor
  );
% endfor
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_type batch_get_all(rpc::context &ctx, gsl::span<table_key_t> keys,
                                           std::vector<batch_get_result_t> &rsp) {
  std::vector<std::string> db_keys;
  db_keys.reserve(keys.size());
  for (auto &key : keys) {
    char db_key[256];
    size_t keylen = sizeof(db_key);
    auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_key});
    if (result.size < static_cast<int64_t>(keylen)) {
      keylen = static_cast<size_t>(result.size);
    }
    db_keys.push_back(std::string{db_key, keylen});
  }
  rsp.reserve(keys.size());
  std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> outputs;
  outputs.resize(keys.size());
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::batch_get_all(
      ctx, db_msg_dispatcher::me()->get_db_channel_type(), gsl::span<std::string>{db_keys}, outputs,
      &detail::unpack_${message_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (size_t index = 0; index < static_cast<size_t>(outputs.size()); ++index) {
    auto &output = outputs[index];
    if (!output) {
      batch_get_result_t empty_result;
      empty_result.result = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
% if index.enable_cas:
      empty_result.version = 0;
% endif
      empty_result.message = nullptr;
      rsp.push_back(std::move(empty_result));
      continue;
    }
    batch_get_result_t result;
    result.result = output->result;
% if index.enable_cas:
    result.version = output->version;
% endif
    if (output->message) {
      result.message = atfw::component::memory::stl::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(*output->message);
% for key_field in key_fields:
      (*result.message)->set_${key_field["raw_name"]}(
      % for arg in key_field["set_args"]:
        keys[index].${arg}
      % endfor
      );
% endfor
    }
    rsp.push_back(std::move(result));
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_type replace(rpc::context &ctx,
                                                         shared_message<PROJECT_NAMESPACE_ID::${message_name}> &&store
% if index.enable_cas:
                                                         ,uint64_t &version) {
% else:
                                                          ) {
% endif
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::replace_handler()) {
    rpc::unit_test::db_mock_meta __meta;
% if index.enable_cas:
    __meta.version = version;
% endif
    int __res = RPC_AWAIT_CODE_RESULT(__handler(ctx, *store, __meta));
% if index.enable_cas:
    version = __meta.version;
% endif
    RPC_DB_RETURN_CODE(__res);
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_pb});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = size_t(result.size);
  }
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen},
                                                                shared_abstract_message<google::protobuf::Message>{std::move(store)},
% if index.enable_cas:
                                                                &version));
% else:
                                                                nullptr));
% endif
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

% if index.enable_cas:
SERVER_FRAME_API result_type insert(rpc::context &ctx,
                                 shared_message<PROJECT_NAMESPACE_ID::${message_name}> &&store,
                                 uint64_t &version) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::replace_handler()) {
    rpc::unit_test::db_mock_meta __meta;
    __meta.version = version;
    int __res = RPC_AWAIT_CODE_RESULT(__handler(ctx, *store, __meta));
    version = __meta.version;
    RPC_DB_RETURN_CODE(__res);
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_pb});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = size_t(result.size);
  }
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen},
                                                                shared_abstract_message<google::protobuf::Message>{std::move(store)},
                                                                version));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
% endif

% if len(atomic_inc_fields) > 0:
%     for inc_field in atomic_inc_fields:
namespace detail {
static int32_t unpack_${message_name}_inc_field_${inc_field["raw_name"]}(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FCTXLOGDEBUG(*ctx, "{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (reply->type != REDIS_REPLY_INTEGER) {
    FCTXLOGERROR(*ctx, "unpack failed, the redis reply type is not int (reply type={}).", reply->type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
  }

  shared_message<PROJECT_NAMESPACE_ID::${message_name}> table_pb{*ctx};
  table_pb->set_${inc_field["raw_name"]}(static_cast<uint64_t>(reply->integer));
  msg.body_message =
      atfw::component::memory::stl::make_strong_rc<rpc::shared_abstract_message<google::protobuf::Message>>(std::move(table_pb));
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}
}
SERVER_FRAME_API result_type inc_field_${inc_field["raw_name"]}(rpc::context &ctx
%         for key_field in key_fields:
                                                         , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%         endfor
                                                          , ${inc_field["cpp_type"]}& inc_value
) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::inc_field_${inc_field["raw_name"]}_handler()) {
    PROJECT_NAMESPACE_ID::${message_name} __input;
${emit_mock_input_fill(key_fields)}    ${inc_field["cpp_type"]} __inc_out = 0;
    rpc::unit_test::db_mock_meta __meta;
    int __res = RPC_AWAIT_CODE_RESULT(__handler(ctx, __input, __inc_out, __meta));
    inc_value = __inc_out;
    RPC_DB_RETURN_CODE(__res);
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  shared_message<PROJECT_NAMESPACE_ID::${message_name}> table_db{ctx};
  table_db->set_${inc_field["raw_name"]}(1);
  shared_abstract_message<google::protobuf::Message> message{table_db};
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::inc_field(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen},
                                                                gsl::string_view{"${inc_field["raw_name"]}"},
                                                                message,
                                                                &detail::unpack_${message_name}_inc_field_${inc_field["raw_name"]}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  inc_value = table_db->${inc_field["raw_name"]}();
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
%     endfor
% endif

% for partly_get in index.partly_get:
<%
    partly_field_name = ""
    if partly_get.name != "":
        partly_field_name += partly_get.name
    else:
        for field in partly_get.fields:
            _ = message.fields_by_name[field]
            partly_field_name += field
    partly_field_len = len(partly_get.fields)
    partly_field_len = partly_field_len + len(key_fields)
    if index.enable_cas:
        partly_field_len = partly_field_len + 1
%>
namespace detail {
static int32_t unpack_${message_name}_${partly_field_name}(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FCTXLOGDEBUG(*ctx, "{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  gsl::string_view partly_get_field[${partly_field_len}];
<%
  field_index = 0
%>
%     if index.enable_cas:
  partly_get_field[${field_index}] = gsl::string_view{RPC_DB_VERSION_NAME};
<%
  field_index = field_index + 1
%>
%     endif
%     for key_field in key_fields:
  partly_get_field[${field_index}] = gsl::string_view{"${key_field["raw_name"]}"};
<%
  field_index = field_index + 1
%>
%     endfor
%     for field in partly_get.fields:
%       if field not in all_fields:
        #error "partly get field ${field} must be in fields"
%       else:
  partly_get_field[${field_index}] = gsl::string_view{"${field}"};
%       endif
<%
  field_index = field_index + 1
%>
%     endfor

  shared_message<PROJECT_NAMESPACE_ID::${message_name}> table_pb{*ctx};
  uint64_t version = 0;
  bool record_existed = false;
  int32_t ret = rpc::db::unpack_message_with_field(*table_pb.get(), reply, partly_get_field, ${partly_field_len}, version, record_existed);
  msg.head_message.set_response_int(version);
  if (record_existed) {
    msg.body_message =
        atfw::component::memory::stl::make_strong_rc<rpc::shared_abstract_message<google::protobuf::Message>>(std::move(table_pb));
  }
  return ret;
}
} // namespace detail
SERVER_FRAME_API result_type partly_get_${partly_field_name}(rpc::context &ctx
%     for key_field in key_fields:
                                                         , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%     endfor
                                                         , PROJECT_NAMESPACE_ID::${message_name} &rsp
%     if index.enable_cas:
                                                         , uint64_t &version) {
%     else:
                                                          ) {
%     endif
${emit_mock_intercept_get("partly_get_" + partly_field_name, index.enable_cas, key_fields, message_name)}  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  gsl::string_view partly_get_field[${partly_field_len}];
<%
  field_index = 0
%>
%     if index.enable_cas:
  partly_get_field[${field_index}] = gsl::string_view{RPC_DB_VERSION_NAME};
<%
  field_index = field_index + 1
%>
%     endif
%     for key_field in key_fields:
  partly_get_field[${field_index}] = gsl::string_view{"${key_field["raw_name"]}"};
<%
  field_index = field_index + 1
%>
%     endfor
%     for field in partly_get.fields:
  partly_get_field[${field_index}] = gsl::string_view{"${field}"};
<%
  field_index = field_index + 1
%>
%     endfor
  auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::partly_get(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen},
                                                                partly_get_field,
                                                                ${partly_field_len},
                                                                output,
                                                                &detail::unpack_${message_name}_${partly_field_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  rpc::shared_message<PROJECT_NAMESPACE_ID::${message_name}> tmp{*output->message};
  protobuf_move_message(rsp, std::move(*tmp));
% if index.enable_cas:
  version = output->version;
% endif
% for key_field in key_fields:
  rsp.set_${key_field["raw_name"]}(
  % for arg in key_field["set_args"]:
      ${arg}
  % endfor
  );
% endfor
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_type batch_partly_get_${partly_field_name}(rpc::context &ctx, gsl::span<table_key_t> keys,
                                           std::vector<batch_get_result_t> &rsp) {
  std::vector<std::string> db_keys;
  db_keys.reserve(keys.size());
  for (auto &key : keys) {
    char db_key[256];
    size_t keylen = sizeof(db_key);
    auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_key});
    if (result.size < static_cast<int64_t>(keylen)) {
      keylen = static_cast<size_t>(result.size);
    }
    db_keys.push_back(std::string{db_key, keylen});
  }
  rsp.reserve(keys.size());
  std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> outputs;
  outputs.resize(keys.size());

  gsl::string_view partly_get_field[${partly_field_len}];
<%
  field_index = 0
%>
%     if index.enable_cas:
  partly_get_field[${field_index}] = gsl::string_view{RPC_DB_VERSION_NAME};
<%
  field_index = field_index + 1
%>
%     endif
%     for key_field in key_fields:
  partly_get_field[${field_index}] = gsl::string_view{"${key_field["raw_name"]}"};
<%
  field_index = field_index + 1
%>
%     endfor
%     for field in partly_get.fields:
  partly_get_field[${field_index}] = gsl::string_view{"${field}"};
<%
  field_index = field_index + 1
%>
%     endfor

  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::batch_partly_get(
      ctx, db_msg_dispatcher::me()->get_db_channel_type(), gsl::span<std::string>{db_keys},
      partly_get_field, ${partly_field_len}, outputs,
      &detail::unpack_${message_name}_${partly_field_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (size_t index = 0; index < outputs.size(); ++index) {
    auto &output = outputs[index];
    if (!output) {
      batch_get_result_t empty_result;
      empty_result.result = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
% if index.enable_cas:
      empty_result.version = 0;
% endif
      empty_result.message = nullptr;
      rsp.push_back(std::move(empty_result));
      continue;
    }
    batch_get_result_t result;
    result.result = output->result;
% if index.enable_cas:
    result.version = output->version;
% endif
    if (output->message) {
      result.message = atfw::component::memory::stl::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(*output->message);
% for key_field in key_fields:
      (*result.message)->set_${key_field["raw_name"]}(
      % for arg in key_field["set_args"]:
        keys[index].${arg}
      % endfor
      );
% endfor
    }
    rsp.push_back(std::move(result));
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
% endfor

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
namespace mock {
${emit_mock_register("get_all", "get_all", "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kv_get_all)", index.name)}

${emit_mock_register("replace", "replace", "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kv_set)", index.name)}
% for inc_field in atomic_inc_fields:

${emit_mock_register("inc_field_" + inc_field["raw_name"], "inc_field_" + inc_field["raw_name"], "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kv_inc_field)", index.name)}
% endfor
% for partly_get in index.partly_get:
<%
    partly_reg_name = ""
    if partly_get.name != "":
        partly_reg_name += partly_get.name
    else:
        for field in partly_get.fields:
            partly_reg_name += field
%>

${emit_mock_register("partly_get_" + partly_reg_name, "partly_get_" + partly_reg_name, "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kv_partly_get)", index.name)}
% endfor
}  // namespace mock
#endif

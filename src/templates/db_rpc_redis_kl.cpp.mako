## -*- coding: utf-8 -*-
<%page args="message_name,index,key_fields,all_fields,prefix_fmt_key,prefix_fmt_value_from_args,prefix_fmt_value_from_pb" />
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
<%def name="emit_mock_kl_output_convert(message_name)">
    if (0 == __res) {
      output.clear();
      output.reserve(__entries.size());
      for (auto &__entry : __entries) {
        ${message_name}_list_message __list_message;
        __list_message.list_index = __entry.first;
        shared_message<PROJECT_NAMESPACE_ID::${message_name}> __message{ctx};
        *__message = std::move(__entry.second);
        __list_message.message = atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(std::move(__message));
        output.push_back(std::move(__list_message));
      }
    }
</%def>
namespace detail {
static int32_t unpack_${message_name}(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FCTXLOGDEBUG(*ctx, "{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return rpc::db::unpack_list_message(ctx, reply, msg.body_message_list,
                                      [](rpc::context *sub_ctx) -> atfw::util::memory::strong_rc_ptr<shared_abstract_message<google::protobuf::Message>> {
                                        return atfw::util::memory::make_strong_rc<shared_abstract_message<google::protobuf::Message>>(
                                            shared_message<PROJECT_NAMESPACE_ID::${message_name}>{*sub_ctx});
                                      });
}
static int32_t unpack_${message_name}_indexs(rpc::context *ctx,
                                                   db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FCTXLOGDEBUG(*ctx, "{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return rpc::db::unpack_list_message_with_index(ctx, reply,
                                                 msg.body_message_list,
                                                 [](rpc::context *sub_ctx) -> atfw::util::memory::strong_rc_ptr<shared_abstract_message<google::protobuf::Message>> {
                                                   return atfw::util::memory::make_strong_rc<shared_abstract_message<google::protobuf::Message>>(
                                                       shared_message<PROJECT_NAMESPACE_ID::${message_name}>{*sub_ctx});
                                                 });
}
} // namespace detail

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
${emit_mock_slot("get_all", "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::" + message_name + ">> &, rpc::unit_test::db_mock_meta &)>")}
${emit_mock_slot("get_by_indexs", "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, gsl::span<const uint64_t>, std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::" + message_name + ">> &, rpc::unit_test::db_mock_meta &)>")}
${emit_mock_slot("add", "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, uint64_t &, rpc::unit_test::db_mock_meta &)>")}
${emit_mock_slot("update", "std::function<rpc::result_code_type(rpc::context &, uint64_t, const PROJECT_NAMESPACE_ID::" + message_name + " &, rpc::unit_test::db_mock_meta &)>")}
${emit_mock_slot("remove_by_index", "std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::" + message_name + " &, gsl::span<const uint64_t>, rpc::unit_test::db_mock_meta &)>")}
#endif

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                              , std::vector<${message_name}_list_message> &output
                                                              ) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::get_all_handler()) {
    PROJECT_NAMESPACE_ID::${message_name} __input;
${emit_mock_input_fill(key_fields)}    std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::${message_name}>> __entries;
    rpc::unit_test::db_mock_meta __meta;
    int __res = RPC_AWAIT_CODE_RESULT(__handler(ctx, __input, __entries, __meta));
${emit_mock_kl_output_convert(message_name)}    RPC_DB_RETURN_CODE(__res);
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto index = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (index.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(index.size);
  }
  std::vector<db_key_list_message_result_t> results;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen},
                                                                results,
                                                                &detail::unpack_${message_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (auto &result : results) {
    ${message_name}_list_message list_message;
    list_message.list_index = result.list_index;
    if (result.message) {
      list_message.message =
        atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(
            *result.message);
% for key_field in key_fields:
      (*list_message.message)->set_${key_field["raw_name"]}(
        % for arg in key_field["set_args"]:
          ${arg}
        % endfor
      );
% endfor
    }
    output.push_back(list_message);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_by_indexs(rpc::context &ctx
                                                             , gsl::span<uint64_t> list_index
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                              , std::vector<${message_name}_list_message> &output
                                                              ) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::get_by_indexs_handler()) {
    PROJECT_NAMESPACE_ID::${message_name} __input;
${emit_mock_input_fill(key_fields)}    std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::${message_name}>> __entries;
    rpc::unit_test::db_mock_meta __meta;
    int __res = RPC_AWAIT_CODE_RESULT(__handler(ctx, __input, gsl::span<const uint64_t>{list_index}, __entries, __meta));
${emit_mock_kl_output_convert(message_name)}    RPC_DB_RETURN_CODE(__res);
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto index = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (index.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(index.size);
  }
  std::vector<db_key_list_message_result_t> results;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen}, list_index,
                                                                results,
                                                                &detail::unpack_${message_name}_indexs));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (auto &result : results) {
    ${message_name}_list_message list_message;
    list_message.list_index = result.list_index;
    if (result.message) {
      list_message.message =
              atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(
                  *result.message);
% for key_field in key_fields:
      (*list_message.message)->set_${key_field["raw_name"]}(
        % for arg in key_field["set_args"]:
          ${arg}
        % endfor
      );
% endfor
    }
    output.push_back(list_message);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type add(rpc::context &ctx
                                                          , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &&store
                                                          ) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::add_handler()) {
    uint64_t __index_out = 0;
    rpc::unit_test::db_mock_meta __meta;
    RPC_DB_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__handler(ctx, *store, __index_out, __meta)));
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_pb});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen},
                                                                ${index.max_list_length},
                                                                std::move(store)));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type update(rpc::context &ctx
                                                          , uint64_t list_index
                                                          , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &&store
                                                          ) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::update_handler()) {
    rpc::unit_test::db_mock_meta __meta;
    RPC_DB_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__handler(ctx, list_index, *store, __meta)));
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_pb});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::update_by_index(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen}, list_index,
                                                                std::move(store)));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx
% for key_field in key_fields:
                                                                     , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                                      , gsl::span<uint64_t> list_index) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::remove_by_index_handler()) {
    PROJECT_NAMESPACE_ID::${message_name} __input;
${emit_mock_input_fill(key_fields)}    rpc::unit_test::db_mock_meta __meta;
    RPC_DB_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__handler(ctx, __input, gsl::span<const uint64_t>{list_index}, __meta)));
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::remove_by_index(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen}, list_index));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx
% for key_field in key_fields:
                                                                     , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                                      , gsl::span<const uint64_t> list_index) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (auto __handler = mock_detail::remove_by_index_handler()) {
    PROJECT_NAMESPACE_ID::${message_name} __input;
${emit_mock_input_fill(key_fields)}    rpc::unit_test::db_mock_meta __meta;
    RPC_DB_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__handler(ctx, __input, list_index, __meta)));
  }
#endif
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::remove_by_index(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen}, list_index));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
namespace mock {
${emit_mock_register("get_all", "get_all", "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kl_get_all)", index.name)}

${emit_mock_register("get_by_indexs", "get_by_indexs", "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kl_get_by_indexs)", index.name)}

${emit_mock_register("add", "add", "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kl_add_index)", index.name)}

${emit_mock_register("update", "update", "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kl_update_by_index)", index.name)}

${emit_mock_register("remove_by_index", "remove_by_index", "static_cast<int32_t>(rpc::db::hash_table::unit_test_request::op_type::kl_remove_by_index)", index.name)}
}  // namespace mock
#endif

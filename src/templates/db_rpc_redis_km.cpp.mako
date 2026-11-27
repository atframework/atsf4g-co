## -*- coding: utf-8 -*-
<%page args="message_name,index,index_km,key_fields,map_key_fields,prefix_fmt_key,prefix_fmt_value_from_args,prefix_fmt_value_from_pb" />
% if len(map_key_fields) == 0:
## The dispatcher already emitted #error for this index; emitting the helpers would need a map key array.
<% return %>
% endif
<%
    map_key_fmt = ":".join(["{}"] * len(map_key_fields))
    map_key_fmt_args = ", ".join(["map_key." + field["raw_name"] for field in map_key_fields])
%>
namespace detail {
static int32_t unpack_${message_name}_map(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FCTXLOGDEBUG(*ctx, "{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

% if index_km.enable_cas:
  uint64_t version = 0;
% endif
  int32_t ret = rpc::db::unpack_map_message(
      ctx, reply, msg.body_message_map,
      [](rpc::context *sub_ctx)
          -> atfw::util::memory::strong_rc_ptr<shared_abstract_message<google::protobuf::Message>> {
        shared_message<PROJECT_NAMESPACE_ID::${message_name}> message{*sub_ctx};
        return atfw::component::memory::stl::make_strong_rc<shared_abstract_message<google::protobuf::Message>>(
            message);
      },
% if index_km.enable_cas:
      &version);
  msg.head_message.set_response_int(version);
% else:
      nullptr);
% endif
  return ret;
}

static int32_t unpack_${message_name}_map_keys(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FCTXLOGDEBUG(*ctx, "{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

% if index_km.enable_cas:
  uint64_t version = 0;
% endif
  int32_t ret = rpc::db::unpack_map_message_with_keys(
      ctx, reply, msg.body_message_map,
      [](rpc::context *sub_ctx)
          -> atfw::util::memory::strong_rc_ptr<shared_abstract_message<google::protobuf::Message>> {
        shared_message<PROJECT_NAMESPACE_ID::${message_name}> message{*sub_ctx};
        return atfw::component::memory::stl::make_strong_rc<shared_abstract_message<google::protobuf::Message>>(
            message);
      },
% if index_km.enable_cas:
      &version);
  msg.head_message.set_response_int(version);
% else:
      nullptr);
% endif
  return ret;
}

// Text form of one map key: the declared map_keys values joined by ':'. Kept here so callers only ever deal
// with ${message_name}_map_key and never with the encoding.
static bool build_map_key_text(std::string &output, const ${message_name}_map_key &map_key) {
  char map_key_buffer[256];
  size_t keylen = sizeof(map_key_buffer);
  auto result = atfw::util::string::format_to_n(map_key_buffer, keylen, "${map_key_fmt}", ${map_key_fmt_args});
  if (result.size >= static_cast<int64_t>(keylen)) {
    FWLOGERROR("build map key of ${message_name} failed, buffer is too small");
    return false;
  }
  output.assign(map_key_buffer, static_cast<size_t>(result.size));
  return true;
}

}  // namespace detail

SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , std::vector<atfw::util::memory::strong_rc_ptr<
                                                                   shared_message<PROJECT_NAMESPACE_ID::${message_name}>>> &output
% if index_km.enable_cas:
                                                             , uint64_t &version) {
% else:
                                                              ) {
% endif
  rpc::internal::db_rpc_trace __trace(ctx, "rpc.db.${index.name}/get_all");
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }

  std::vector<db_key_map_message_result_t> results;
  output.clear();
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_map::get_all(
      __trace.child_context, db_msg_dispatcher::me()->get_db_channel_type(), gsl::string_view{db_key, keylen},
      results, &detail::unpack_${message_name}_map,
% if index_km.enable_cas:
      &version));
% else:
      nullptr));
% endif
  if (res < 0) {
    RPC_DB_RETURN_CODE(__trace.finish(res));
  }

  output.reserve(results.size());
  for (auto &result_item : results) {
    if (!result_item.message) {
      continue;
    }
    output.push_back(
        atfw::component::memory::stl::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(
            *result_item.message));
  }

  RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

SERVER_FRAME_API result_type get_by_map_key(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , gsl::span<const ${message_name}_map_key> map_keys
                                                             , std::vector<atfw::util::memory::strong_rc_ptr<
                                                                   shared_message<PROJECT_NAMESPACE_ID::${message_name}>>> &output
% if index_km.enable_cas:
                                                             , uint64_t &version) {
% else:
                                                             ) {
% endif
  rpc::internal::db_rpc_trace __trace(ctx, "rpc.db.${index.name}/get_by_map_key");
  if (map_keys.empty()) {
    FWLOGERROR("get_by_map_key of ${index.name} failed, map key list is empty");
    RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM));
  }

  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }

  std::vector<std::string> map_key_texts;
  map_key_texts.reserve(map_keys.size());
  for (const auto &map_key : map_keys) {
    std::string text;
    if (!detail::build_map_key_text(text, map_key)) {
      RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK));
    }
    map_key_texts.push_back(std::move(text));
  }

  std::vector<db_key_map_message_result_t> results;
  output.clear();
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_map::get_by_map_keys(
      __trace.child_context, db_msg_dispatcher::me()->get_db_channel_type(), gsl::string_view{db_key, keylen},
      gsl::span<const std::string>{map_key_texts.data(), map_key_texts.size()}, results,
      &detail::unpack_${message_name}_map_keys,
% if index_km.enable_cas:
      &version));
% else:
      nullptr));
% endif
  if (res < 0) {
    RPC_DB_RETURN_CODE(__trace.finish(res));
  }

  output.reserve(results.size());
  for (auto &result_item : results) {
    if (!result_item.message) {
      continue;
    }
    output.push_back(
        atfw::component::memory::stl::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(
            *result_item.message));
  }

  RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

SERVER_FRAME_API result_type replace_by_map_key(rpc::context &ctx,
                                                         gsl::span<const shared_message<PROJECT_NAMESPACE_ID::${message_name}>> stores
% if index_km.enable_cas:
                                                         , uint64_t &version) {
% else:
                                                          ) {
% endif
  rpc::internal::db_rpc_trace __trace(ctx, "rpc.db.${index.name}/replace_by_map_key");
  if (stores.empty()) {
    FWLOGERROR("replace_by_map_key of ${index.name} failed, store list is empty");
    RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM));
  }
  if (stores[0].is_moved()) {
    FWLOGERROR("replace_by_map_key of ${index.name} failed, store is moved");
    RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM));
  }

  // One redis key per call: every record must carry the same primary key.
  const auto *store = &*stores[0];
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto format_result =
      atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_pb});
  if (format_result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(format_result.size);
  }

  std::vector<rpc::db::hash_table::key_map::write_entry> entries;
  entries.reserve(stores.size());
  for (auto &store_item : stores) {
    if (store_item.is_moved()) {
      FWLOGERROR("replace_by_map_key of ${index.name} failed, store is moved");
      RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM));
    }
% for key_field in key_fields:
    if (store_item->${key_field["raw_name"]}() != store->${key_field["raw_name"]}()) {
      FWLOGERROR("replace_by_map_key of ${index.name} failed, records belong to different keys");
      RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM));
    }
% endfor

    ${message_name}_map_key map_key_value{*store_item};
    std::string map_key;
    if (!detail::build_map_key_text(map_key, map_key_value)) {
      RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK));
    }
    rpc::db::hash_table::key_map::write_entry entry;
    entry.map_key = std::move(map_key);
    entry.store = store_item.get();
    entries.push_back(std::move(entry));
  }

  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_map::replace(
      __trace.child_context, db_msg_dispatcher::me()->get_db_channel_type(), gsl::string_view{db_key, keylen}, entries,
% if index_km.enable_cas:
      &version));
% else:
      nullptr));
% endif
  if (res < 0) {
    RPC_DB_RETURN_CODE(__trace.finish(res));
  }

  RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

SERVER_FRAME_API result_type remove_by_map_key(rpc::context &ctx
% for key_field in key_fields:
                                                            , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                            , gsl::span<const ${message_name}_map_key> map_keys
                                                            ) {
  rpc::internal::db_rpc_trace __trace(ctx, "rpc.db.${index.name}/remove_by_map_key");
  if (map_keys.empty()) {
    RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
  }

  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }

  std::vector<std::string> map_key_texts;
  map_key_texts.reserve(map_keys.size());
  for (const auto &map_key : map_keys) {
    std::string text;
    if (!detail::build_map_key_text(text, map_key)) {
      RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK));
    }
    map_key_texts.push_back(std::move(text));
  }

  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_map::remove_by_map_keys(
      __trace.child_context, db_msg_dispatcher::me()->get_db_channel_type(), gsl::string_view{db_key, keylen},
      gsl::span<const std::string>{map_key_texts.data(), map_key_texts.size()}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(__trace.finish(res));
  }

  RPC_DB_RETURN_CODE(__trace.finish(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

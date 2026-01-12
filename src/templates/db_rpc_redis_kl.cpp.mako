## -*- coding: utf-8 -*-
<%page args="message_name,index,key_fields,prefix_fmt_key,prefix_fmt_value_from_args,prefix_fmt_value_from_pb" />
namespace detail {
static int32_t unpack_${message_name}(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FWLOGDEBUG("{}", "data not found.");
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
    FWLOGDEBUG("{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  return rpc::db::unpack_list_message_with_index(ctx, reply,
% if index.enable_cas:
                                                 true,
% else:
                                                 false,
% endif
                                                 msg.body_message_list,
                                                 [](rpc::context *sub_ctx) -> atfw::util::memory::strong_rc_ptr<shared_abstract_message<google::protobuf::Message>> {
                                                   return atfw::util::memory::make_strong_rc<shared_abstract_message<google::protobuf::Message>>(
                                                       shared_message<PROJECT_NAMESPACE_ID::${message_name}>{*sub_ctx});
                                                 });
}
} // namespace detail
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , std::vector<${message_name}_list_message> &output
                                                             ) {
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto index = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  db_key[index.size] = '\0';
  std::vector<db_key_list_message_result_t> results;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen},
                                                                results,
                                                                detail::unpack_${message_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (auto &result : results) {
    ${message_name}_list_message list_message;
    list_message.list_index = result.list_index;
    list_message.version = result.version;
    if (result.message) {
      list_message.message =
        atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(
            *result.message);
% for key_field in key_fields:
      (*list_message.message)->set_${key_field["raw_name"]}(${key_field["raw_name"]});
% endfor
    }
    output.push_back(list_message);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_by_indexs(rpc::context &ctx
                                                             , gsl::span<uint64_t> list_index
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , std::vector<${message_name}_list_message> &output
                                                             ) {
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto index = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  db_key[index.size] = '\0';
  std::vector<db_key_list_message_result_t> results;
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen}, list_index,
% if index.enable_cas:
                                                                true,
% else:
                                                                false,
% endif
                                                                results,
                                                                detail::unpack_${message_name}_indexs));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (auto &result : results) {
    ${message_name}_list_message list_message;
    list_message.list_index = result.list_index;
    list_message.version = result.version;
    if (result.message) {
      list_message.message =
              atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(
                  *result.message);
% for key_field in key_fields:
      (*list_message.message)->set_${key_field["raw_name"]}(${key_field["raw_name"]});
% endfor
    }
    output.push_back(list_message);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type replace(rpc::context &ctx
                                                         , uint64_t list_index
                                                         , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &&store
% if index.enable_cas:
                                                         ,uint64_t &version) {
% else:
                                                         ) {
% endif
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_pb});
  db_key[result.size] = '\0';
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::set_by_index(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen}, list_index,
                                                                std::move(store)
% if index.enable_cas:
                                                                , &version));
% else:
                                                                , nullptr));
% endif
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx
% for key_field in key_fields:
                                                                     , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                                     , gsl::span<uint64_t> list_index) {
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  db_key[result.size] = '\0';
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::remove_by_index(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen}, list_index,
% if index.enable_cas:
                                                                true));
% else:
                                                                false));
% endif
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx
% for key_field in key_fields:
                                                                     , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                                     , gsl::span<const uint64_t> list_index) {
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  db_key[result.size] = '\0';
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::remove_by_index(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen}, list_index,
% if index.enable_cas:
                                                                true));
% else:
                                                                false));
% endif
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
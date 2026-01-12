## -*- coding: utf-8 -*-
<%page args="message_name,index,key_fields,atomic_inc_fields,prefix_fmt_key,prefix_fmt_value_from_args,prefix_fmt_value_from_key,prefix_fmt_value_from_pb" />
namespace detail {
static int32_t unpack_${message_name}(rpc::context *ctx, db_message_t &msg, const redisReply *reply) {
  if (nullptr == reply) {
    FWLOGDEBUG("{}", "data not found.");
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  shared_message<PROJECT_NAMESPACE_ID::${message_name}> table_pb{*ctx};
  uint64_t version = 0;
  bool record_existed = false;
  int32_t ret = rpc::db::unpack_message(*table_pb.get(), reply, version, record_existed);
  msg.head_message.set_cas_version(version);
  if (record_existed) {
    msg.body_message =
        atfw::util::memory::make_strong_rc<rpc::shared_abstract_message<google::protobuf::Message>>(std::move(table_pb));
  }
  return ret;
}
} // namespace detail
SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &rsp
% if index.enable_cas:
                                                             , uint64_t &version) {
% else:
                                                             ) {
% endif
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  db_key[result.size] = '\0';
  auto output = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen},
                                                                output,
                                                                detail::unpack_${message_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  rsp = shared_message<PROJECT_NAMESPACE_ID::${message_name}>{*output->message};
% if index.enable_cas:
  version = output->version;
% endif
% for key_field in key_fields:
  rsp->set_${key_field["raw_name"]}(${key_field["raw_name"]});
% endfor
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_type batch_get_all(rpc::context &ctx, gsl::span<table_key_t> keys,
                                           std::vector<batch_get_result_t> &rsp) {
  std::vector<std::string> db_keys;
  db_keys.reserve(keys.size());
  for (auto &key : keys) {
    char db_key[256];
    size_t keylen = sizeof(db_key) - 1;
    auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_key});
    db_key[result.size] = '\0';
    db_keys.push_back(std::string{db_key, keylen});
  }
  rsp.reserve(keys.size());
  std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> outputs;
  outputs.resize(keys.size());
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::batch_get_all(
      ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, gsl::span<std::string>{db_keys}, outputs,
      detail::unpack_${message_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (int32_t index = 0; index < static_cast<int32_t>(outputs.size()); ++index) {
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
      result.message = atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(*output->message);
% for key_field in key_fields:
      (*result.message)->set_${key_field["raw_name"]}(keys[index].${key_field["raw_name"]});
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
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_pb});
  db_key[result.size] = '\0';
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
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

% if len(atomic_inc_fields) > 0:
%     for inc_field in atomic_inc_fields:
SERVER_FRAME_API result_type inc_field_${inc_field["raw_name"]}(rpc::context &ctx
%         for key_field in key_fields:
                                                         , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%         endfor
                                                         , ${inc_field["cpp_type"]}& inc_value
) {
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  db_key[result.size] = '\0';
  shared_message<PROJECT_NAMESPACE_ID::${message_name}> table_db{ctx};
  table_db->set_${inc_field["raw_name"]}(1);
  shared_abstract_message<google::protobuf::Message> message{table_db};
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::inc_field(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen},
                                                                gsl::string_view{"${inc_field["raw_name"]}"},
                                                                message,
                                                                detail::unpack_${message_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  inc_value = table_db->auto_inc_id();
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
    FWLOGDEBUG("{}", "data not found.");
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
  partly_get_field[${field_index}] = gsl::string_view{"${field}"};
<%
  field_index = field_index + 1
%>
%     endfor

  shared_message<PROJECT_NAMESPACE_ID::${message_name}> table_pb{*ctx};
  uint64_t version = 0;
  bool record_existed = false;
  int32_t ret = rpc::db::unpack_message_with_field(*table_pb.get(), reply, partly_get_field, ${partly_field_len}, version, record_existed);
  msg.head_message.set_cas_version(version);
  if (record_existed) {
    msg.body_message =
        atfw::util::memory::make_strong_rc<rpc::shared_abstract_message<google::protobuf::Message>>(std::move(table_pb));
  }
  return ret;
}
} // namespace detail
SERVER_FRAME_API result_type partly_get_${partly_field_name}(rpc::context &ctx
%     for key_field in key_fields:
                                                         , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%     endfor
                                                         , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &rsp
%     if index.enable_cas:
                                                         , uint64_t &version) {
%     else:
                                                         ) {
%     endif
  char db_key[256];
  size_t keylen = sizeof(db_key) - 1;
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  db_key[result.size] = '\0';
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
  auto output = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::partly_get(ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT,
                                                                gsl::string_view{db_key, keylen},
                                                                partly_get_field,
                                                                ${partly_field_len},
                                                                output,
                                                                detail::unpack_${message_name}_${partly_field_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  rsp = shared_message<PROJECT_NAMESPACE_ID::${message_name}>{*output->message};
% if index.enable_cas:
  version = output->version;
% endif
%     for key_field in key_fields:
  rsp->set_${key_field["raw_name"]}(${key_field["raw_name"]});
%     endfor
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_type batch_partly_get_${partly_field_name}(rpc::context &ctx, gsl::span<table_key_t> keys,
                                           std::vector<batch_get_result_t> &rsp) {
  std::vector<std::string> db_keys;
  db_keys.reserve(keys.size());
  for (auto &key : keys) {
    char db_key[256];
    size_t keylen = sizeof(db_key) - 1;
    auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_key});
    db_key[result.size] = '\0';
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
      ctx, db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, gsl::span<std::string>{db_keys},
      partly_get_field, ${partly_field_len}, outputs,
      detail::unpack_${message_name}_${partly_field_name}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  for (int32_t index = 0; index < static_cast<int32_t>(outputs.size()); ++index) {
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
      result.message = atfw::util::memory::make_strong_rc<shared_message<PROJECT_NAMESPACE_ID::${message_name}>>(*output->message);
% for key_field in key_fields:
      (*result.message)->set_${key_field["raw_name"]}(keys[index].${key_field["raw_name"]});
% endfor
    }
    rsp.push_back(std::move(result));
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
% endfor
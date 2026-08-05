## -*- coding: utf-8 -*-
<%!
import time
import sys
%><%page args="message_name,extension,message,index_type_enum,message_full_name" />
% for field in message.fields:
%     if not field.is_db_vaild_type():
// ${message_name} filed: {${field.get_name()}} not db vaild type
<% return %>
%     endif
% endfor
% for index in extension.index:
<%
    key_fields = []
    all_fields = []
    index_type_kv = True
    if index.type == index_type_enum.values_by_name["EN_ATFRAMEWORK_DB_INDEX_TYPE_KL"].descriptor.number:
        index_type_kv = False
    for key in index.key_fields:
        field = message.fields_by_name[key]
        if field.get_cpp_type() == "string_view":
            key_fields.append({
                "raw_name": key,
                "cpp_type": field.get_cpp_type(),
                "set_args": [key + ".data(),", key + ".size()"],
            })
        else:
            key_fields.append({
                "raw_name": key,
                "cpp_type": field.get_cpp_type(),
                "set_args": [key],
            })

    for field in message.fields:
        all_fields.append(field.get_name())

    prefix_fmt_key = "{}-"
    prefix_fmt_value_from_args = "db_msg_dispatcher::me()->get_record_prefix()"
    prefix_fmt_key += "{}"
    prefix_fmt_value_from_args += ", \""
    prefix_fmt_value_from_args += index.name
    prefix_fmt_value_from_args += "\""
    prefix_fmt_value_from_pb = prefix_fmt_value_from_args
    prefix_fmt_value_from_key = prefix_fmt_value_from_args
    for key_map in key_fields:
        prefix_fmt_key += ".{}"
        prefix_fmt_value_from_args += ", "
        prefix_fmt_value_from_args += key_map["raw_name"]
        prefix_fmt_value_from_pb += ", store->"
        prefix_fmt_value_from_pb += key_map["raw_name"]
        prefix_fmt_value_from_pb += "()"
        prefix_fmt_value_from_key += ", key."
        prefix_fmt_value_from_key += key_map["raw_name"]

    atomic_inc_fields = []
    for inc_field in index.atomic_inc_fields:
        if inc_field not in message.fields_by_name:
            continue
        field = message.fields_by_name[inc_field]
        cpp_type = field.get_cpp_type()
        if cpp_type not in ("int32_t", "int64_t", "uint32_t", "uint64_t"):
            continue
        atomic_inc_fields.append({
            "raw_name": inc_field,
            "cpp_type": cpp_type,
        })
%>
namespace ${index.name} {
%     if index_type_kv:
<%include file="db_rpc_redis_kv.cpp.mako" args="message_name=message_name,index=index,key_fields=key_fields,all_fields=all_fields,atomic_inc_fields=atomic_inc_fields,prefix_fmt_key=prefix_fmt_key,prefix_fmt_value_from_args=prefix_fmt_value_from_args,prefix_fmt_value_from_key=prefix_fmt_value_from_key,prefix_fmt_value_from_pb=prefix_fmt_value_from_pb" />
%     else:
<%include file="db_rpc_redis_kl.cpp.mako" args="message_name=message_name,index=index,key_fields=key_fields,all_fields=all_fields,prefix_fmt_key=prefix_fmt_key,prefix_fmt_value_from_args=prefix_fmt_value_from_args,prefix_fmt_value_from_key=prefix_fmt_value_from_key,prefix_fmt_value_from_pb=prefix_fmt_value_from_pb" />
%     endif

SERVER_FRAME_API result_type remove_all(rpc::context &ctx
%   for key_field in key_fields:
                                                             ,${key_field["cpp_type"]} ${key_field["raw_name"]}
%   endfor
) {
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::remove_all(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                gsl::string_view{db_key, keylen}));
  if (res < 0) {
    RPC_DB_RETURN_CODE(res);
  }
  RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_type set_ttl(rpc::context &ctx
%   for key_field in key_fields:
                                     , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%   endfor
                                     , uint64_t ttl_second
) {
    char db_key[256];
    size_t keylen = sizeof(db_key);
    auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
    if (result.size < static_cast<int64_t>(keylen)) {
        keylen = static_cast<size_t>(result.size);
    }
    auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set_ttl(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                  gsl::string_view{db_key, keylen}, ttl_second));
    if (res < 0) {
        RPC_DB_RETURN_CODE(res);
    }
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_type remove_ttl(rpc::context &ctx
%   for key_field in key_fields:
                                        , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%   endfor
) {
    char db_key[256];
    size_t keylen = sizeof(db_key);
    auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
    if (result.size < static_cast<int64_t>(keylen)) {
        keylen = static_cast<size_t>(result.size);
    }
    auto res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::remove_ttl(ctx, db_msg_dispatcher::me()->get_db_channel_type(),
                                                                     gsl::string_view{db_key, keylen}));
    if (res < 0) {
        RPC_DB_RETURN_CODE(res);
    }
    RPC_DB_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
namespace mock {
SERVER_FRAME_API std::string make_key(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%   endfor
) {
  char db_key[256];
  size_t keylen = sizeof(db_key);
  auto result = atfw::util::string::format_to_n(db_key, keylen, "${prefix_fmt_key}", ${prefix_fmt_value_from_args});
  if (result.size < static_cast<int64_t>(keylen)) {
    keylen = static_cast<size_t>(result.size);
  }
  return std::string{db_key, keylen};
}

SERVER_FRAME_API rpc::unit_test::mock_rule_handle set_error(
    rpc::db::hash_table::unit_test_request::op_type __op, int32_t __error_code) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_set_error_rule) {
    return rpc::unit_test::mock_rule_handle{};
  }
  return rpc::unit_test::mock_rule_handle{
      __bridge.db_set_error_rule("${index.name}", static_cast<int32_t>(__op), __error_code)};
}

SERVER_FRAME_API rpc::unit_test::mock_rule_handle force_not_found() {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_force_not_found_rule) {
    return rpc::unit_test::mock_rule_handle{};
  }
  return rpc::unit_test::mock_rule_handle{__bridge.db_force_not_found_rule("${index.name}")};
}

SERVER_FRAME_API void set_ttl(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%   endfor
    uint64_t __ttl_seconds) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_set_raw_ttl) {
    return;
  }
  __bridge.db_set_raw_ttl(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}), __ttl_seconds);
}
%   if index_type_kv:

SERVER_FRAME_API bool set_record(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%   endfor
    const PROJECT_NAMESPACE_ID::${message_name} &__record, uint64_t __version) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_set_raw_kv) {
    return false;
  }
  __bridge.db_set_raw_kv(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}),
                         "${message_full_name}", __record.SerializeAsString(), __version);
  return true;
}

SERVER_FRAME_API bool get(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%   endfor
    PROJECT_NAMESPACE_ID::${message_name} &__output, uint64_t *__version) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_get_raw_kv) {
    return false;
  }
  std::string __data;
  uint64_t __stored_version = 0;
  if (!__bridge.db_get_raw_kv(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}), nullptr,
                              &__data, &__stored_version)) {
    return false;
  }
  if (!__output.ParseFromString(__data)) {
    return false;
  }
  if (nullptr != __version) {
    *__version = __stored_version;
  }
  return true;
}

SERVER_FRAME_API bool has_record(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%   endfor
) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_get_raw_kv) {
    return false;
  }
  return __bridge.db_get_raw_kv(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}), nullptr,
                                nullptr, nullptr);
}

SERVER_FRAME_API bool version_equal(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%   endfor
    uint64_t __expected_version) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_get_raw_kv) {
    return false;
  }
  uint64_t __stored_version = 0;
  return __bridge.db_get_raw_kv(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}), nullptr,
                                nullptr, &__stored_version) &&
         __stored_version == __expected_version;
}
%   else:

SERVER_FRAME_API uint64_t append_entry(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%   endfor
    const PROJECT_NAMESPACE_ID::${message_name} &__entry) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_append_raw_kl) {
    return 0;
  }
  return __bridge.db_append_raw_kl(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}),
                                   "${message_full_name}", __entry.SerializeAsString());
}

SERVER_FRAME_API size_t count(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%   endfor
) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_get_raw_kl) {
    return 0;
  }
  std::vector<std::tuple<uint64_t, std::string, std::string>> __entries;
  if (!__bridge.db_get_raw_kl(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}), &__entries)) {
    return 0;
  }
  return __entries.size();
}

SERVER_FRAME_API std::vector<uint64_t> indexes(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%   endfor
) {
  std::vector<uint64_t> __ret;
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_get_raw_kl) {
    return __ret;
  }
  std::vector<std::tuple<uint64_t, std::string, std::string>> __entries;
  if (!__bridge.db_get_raw_kl(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}), &__entries)) {
    return __ret;
  }
  __ret.reserve(__entries.size());
  for (const auto &__entry : __entries) {
    __ret.push_back(std::get<0>(__entry));
  }
  return __ret;
}

SERVER_FRAME_API bool entry_at(
%   for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%   endfor
    uint64_t __index, PROJECT_NAMESPACE_ID::${message_name} &__output) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.db_get_raw_kl) {
    return false;
  }
  std::vector<std::tuple<uint64_t, std::string, std::string>> __entries;
  if (!__bridge.db_get_raw_kl(make_key(${', '.join(key_field["raw_name"] for key_field in key_fields)}), &__entries)) {
    return false;
  }
  for (const auto &__entry : __entries) {
    if (std::get<0>(__entry) == __index) {
      return __output.ParseFromString(std::get<2>(__entry));
    }
  }
  return false;
}
%   endif
}  // namespace mock
#endif

} // namespace ${index.name}
% endfor

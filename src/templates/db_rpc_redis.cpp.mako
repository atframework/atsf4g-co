## -*- coding: utf-8 -*-
<%!
import time
import sys
%><%page args="message_name,extension,message,index_type_enum" />
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

} // namespace ${index.name}
% endfor

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
    index_type_kv = True
    if index.type == index_type_enum.values_by_name["EN_ATFRAMEWORK_DB_INDEX_TYPE_KL"].descriptor.number:
        index_type_kv = False
    for key in index.key_fields:
        field = message.fields_by_name[key]
        key_fields.append({
            "raw_name": key,
            "cpp_type": field.get_cpp_type(),
        })

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
<%include file="db_rpc_redis_kv.h.mako" args="message_name=message_name,index=index,key_fields=key_fields,atomic_inc_fields=atomic_inc_fields" />
%     else:
<%include file="db_rpc_redis_kl.h.mako" args="message_name=message_name,index=index,key_fields=key_fields" />
%     endif
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_all(rpc::context &ctx
%     for key_field in key_fields:
                                                             ,${key_field["cpp_type"]} ${key_field["raw_name"]}
%     endfor
);
} // namespace ${index.name}
% endfor
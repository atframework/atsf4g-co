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
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_all(rpc::context &ctx
%     for key_field in key_fields:
                                                             ,${key_field["cpp_type"]} ${key_field["raw_name"]}
%     endfor
);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type set_ttl(rpc::context &ctx
%     for key_field in key_fields:
                                                             ,${key_field["cpp_type"]} ${key_field["raw_name"]}
%     endfor
                                                             ,uint64_t ttl_second
);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_ttl(rpc::context &ctx
%     for key_field in key_fields:
                                                              ,${key_field["cpp_type"]} ${key_field["raw_name"]}
%     endfor
);

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
// SS-style typed mock of ${message_full_name} (index "${index.name}"): one handler registration per
// generated table interface, mirroring <service>::mock. Registering installs the handler at the entry
// of the corresponding interface above; the handler receives a typed input message (key fields filled),
// fills the output record(s) and the extensible rpc::unit_test::db_mock_meta (CAS version etc.), and
// returns rpc::result_code_type (RPC_RETURN_CODE) so it may await nested RPC calls, bypassing the
// common mock layer entirely. Interfaces without a registered handler fall through to the in-memory
// mock backend. All functions are non-template exported functions implemented in the generated .cpp
// (see 3.6/8.5 in IMPLEMENTATION_PLAN.md), return an RAII rpc::unit_test::mock_rule_handle that
// deactivates the handler on destruction, and degrade to an empty handle when no mock engine is bound.
// Runtime teardown deactivates all registered handlers through the mock engine bridge.
namespace mock {
%     if index_type_kv:
/**
 * @brief Mock get_all: input carries the key fields; fill output and meta.version (CAS tables).
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle get_all(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      PROJECT_NAMESPACE_ID::${message_name} &, rpc::unit_test::db_mock_meta &)>
        __handler);

/**
 * @brief Mock replace: input is the record being stored; meta.version carries the expected CAS version
 *        in and the new version out (CAS tables).
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle replace(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      rpc::unit_test::db_mock_meta &)>
        __handler);
%         for inc_field in atomic_inc_fields:

/**
 * @brief Mock inc_field_${inc_field["raw_name"]}: input carries the key fields; set inc_value as the result.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle inc_field_${inc_field["raw_name"]}(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      ${inc_field["cpp_type"]} &, rpc::unit_test::db_mock_meta &)>
        __handler);
%         endfor
%         for partly_get in index.partly_get:
<%
    partly_mock_name = ""
    if partly_get.name != "":
        partly_mock_name += partly_get.name
    else:
        for field in partly_get.fields:
            partly_mock_name += field
%>

/**
 * @brief Mock partly_get_${partly_mock_name}: input carries the key fields; fill output and meta.version (CAS tables).
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle partly_get_${partly_mock_name}(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      PROJECT_NAMESPACE_ID::${message_name} &, rpc::unit_test::db_mock_meta &)>
        __handler);
%         endfor
%     else:
/**
 * @brief Mock get_all: input carries the key fields; fill (list_index, record) entries.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle get_all(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::${message_name}>> &,
                      rpc::unit_test::db_mock_meta &)>
        __handler);

/**
 * @brief Mock get_by_indexs: input carries the key fields; fill (list_index, record) entries.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle get_by_indexs(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      gsl::span<const uint64_t>,
                      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::${message_name}>> &,
                      rpc::unit_test::db_mock_meta &)>
        __handler);

/**
 * @brief Mock add: input is the entry being appended; set the allocated list index.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle add(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &, uint64_t &,
                      rpc::unit_test::db_mock_meta &)>
        __handler);

/**
 * @brief Mock update: input is the entry stored at list_index.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle update(
    std::function<rpc::result_code_type(rpc::context &, uint64_t, const PROJECT_NAMESPACE_ID::${message_name} &,
                      rpc::unit_test::db_mock_meta &)>
        __handler);

/**
 * @brief Mock remove_by_index: input carries the key fields.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle remove_by_index(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      gsl::span<const uint64_t>, rpc::unit_test::db_mock_meta &)>
        __handler);
%     endif

/**
 * @brief Mock remove_all: input carries the key fields.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle remove_all(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      rpc::unit_test::db_mock_meta &)>
        __handler);

/**
 * @brief Mock set_ttl: input carries the key fields.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle set_ttl(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &, uint64_t,
                      rpc::unit_test::db_mock_meta &)>
        __handler);

/**
 * @brief Mock remove_ttl: input carries the key fields.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle remove_ttl(
    std::function<rpc::result_code_type(rpc::context &, const PROJECT_NAMESPACE_ID::${message_name} &,
                      rpc::unit_test::db_mock_meta &)>
        __handler);
}  // namespace mock
#endif

} // namespace ${index.name}
% endfor

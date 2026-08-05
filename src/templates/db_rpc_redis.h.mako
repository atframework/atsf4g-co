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
// Per-table typed mock interface for ${message_full_name} (index "${index.name}"). All functions are
// non-template exported functions implemented in the generated .cpp (see 3.6 in
// IMPLEMENTATION_PLAN.md) and call through rpc::unit_test::get_mock_engine_bridge_for_unit_test();
// data access goes directly through the bound mock backend, without RPC or task context. When no
// mock engine is bound, rule helpers return empty handles and data helpers fail/no-op.
namespace mock {
/**
 * @brief Build the storage key of this table ("{record_prefix}-${index.name}.{key fields}").
 */
SERVER_FRAME_API std::string make_key(
%     for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%     endfor
);

/**
 * @brief Inject an error code for one op of this table until the returned handle is destroyed.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle set_error(
    rpc::db::hash_table::unit_test_request::op_type __op, int32_t __error_code);

/**
 * @brief Make all read ops of this table return EN_DB_RECORD_NOT_FOUND until the handle is destroyed.
 */
SERVER_FRAME_API rpc::unit_test::mock_rule_handle force_not_found();

/**
 * @brief Set a TTL on the mock backend entry (uses the mock engine clock).
 */
SERVER_FRAME_API void set_ttl(
%     for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%     endfor
    uint64_t __ttl_seconds);
%     if index_type_kv:

/**
 * @brief Write one typed record directly into the mock backend (presence of set fields survives).
 */
SERVER_FRAME_API bool set_record(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%         endfor
    const PROJECT_NAMESPACE_ID::${message_name} &__record, uint64_t __version = 0);

/**
 * @brief Read one typed record from the mock backend. Returns false when absent or undecodable.
 */
SERVER_FRAME_API bool get(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%         endfor
    PROJECT_NAMESPACE_ID::${message_name} &__output, uint64_t *__version = nullptr);

SERVER_FRAME_API bool has_record(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%         endfor
);

SERVER_FRAME_API bool version_equal(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%         endfor
    uint64_t __expected_version);
%     else:

/**
 * @brief Append one typed KL entry with the per-key monotonic index; returns the allocated index
 *        (0 when no mock engine is bound).
 */
SERVER_FRAME_API uint64_t append_entry(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%         endfor
    const PROJECT_NAMESPACE_ID::${message_name} &__entry);

SERVER_FRAME_API size_t count(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%         endfor
);

SERVER_FRAME_API std::vector<uint64_t> indexes(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]}${"," if not loop.last else ""}
%         endfor
);

SERVER_FRAME_API bool entry_at(
%         for key_field in key_fields:
    ${key_field["cpp_type"]} ${key_field["raw_name"]},
%         endfor
    uint64_t __index, PROJECT_NAMESPACE_ID::${message_name} &__output);
%     endif
}  // namespace mock
#endif

} // namespace ${index.name}
% endfor

## -*- coding: utf-8 -*-
<%page args="message_name,index,index_km,key_fields,map_key_fields" />
% if len(map_key_fields) == 0:
## The dispatcher already emitted #error for this index; emitting the declarations would need a map key array.
<% return %>
% endif
// One map key of this index, built from the declared `map_keys` fields. Callers pass these values; the
// text form used as the hash field name is an implementation detail of the generated code.
struct ATFW_UTIL_SYMBOL_VISIBLE ${message_name}_map_key {
% for map_key_field in map_key_fields:
  ${map_key_field["cpp_type"]} ${map_key_field["raw_name"]}{};
% endfor

  ${message_name}_map_key() = default;
<%
    map_key_init_list = ",\n        ".join(
        "{0}(store.{0}())".format(field["raw_name"]) for field in map_key_fields)
%>
  // Takes every map key field from the record itself, so callers do not repeat the field list.
  explicit ${message_name}_map_key(const PROJECT_NAMESPACE_ID::${message_name} &store)
      : ${map_key_init_list} {}
};

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , std::vector<atfw::util::memory::strong_rc_ptr<
                                                                   shared_message<PROJECT_NAMESPACE_ID::${message_name}>>> &output
% if index_km.enable_cas:
                                                             , uint64_t &version);
% else:
                                                             );
% endif

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_by_map_key(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , gsl::span<const ${message_name}_map_key> map_keys
                                                             , std::vector<atfw::util::memory::strong_rc_ptr<
                                                                   shared_message<PROJECT_NAMESPACE_ID::${message_name}>>> &output
% if index_km.enable_cas:
                                                             , uint64_t &version);
% else:
                                                             );
% endif

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type replace_by_map_key(rpc::context &ctx,
                                                         gsl::span<const shared_message<PROJECT_NAMESPACE_ID::${message_name}>> stores
% if index_km.enable_cas:
                                                         , uint64_t &version);
% else:
                                                         );
% endif

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_map_key(rpc::context &ctx
% for key_field in key_fields:
                                                            , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                            , gsl::span<const ${message_name}_map_key> map_keys
                                                            );

## -*- coding: utf-8 -*-
<%page args="message_name,index,key_fields" />
struct ${message_name}_list_message {
  uint64_t list_index;
% if index.enable_cas:
  uint64_t version;
% endif
  atfw::util::memory::strong_rc_ptr<shared_message<PROJECT_NAMESPACE_ID::${message_name}>> message;
};
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , std::vector<${message_name}_list_message> &output
                                                             );

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_by_indexs(rpc::context &ctx
                                                             , gsl::span<uint64_t> list_index
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , std::vector<${message_name}_list_message> &output
                                                             );

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type replace(rpc::context &ctx
                                                         , uint64_t list_index
                                                         , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &&store
% if index.enable_cas:
                                                         ,uint64_t &version);
% else:
                                                         );
% endif

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx
% for key_field in key_fields:
                                                                     , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                                     , gsl::span<uint64_t> list_index
                                                                     );

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx
% for key_field in key_fields:
                                                                     , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                                     , gsl::span<const uint64_t> list_index
                                                                     );
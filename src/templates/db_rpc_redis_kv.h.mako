## -*- coding: utf-8 -*-
<%page args="message_name,index,key_fields,atomic_inc_fields" />
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(rpc::context &ctx
% for key_field in key_fields:
                                                             , ${key_field["cpp_type"]} ${key_field["raw_name"]}
% endfor
                                                             , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &rsp
% if index.enable_cas:
                                                             , uint64_t &version);
% else:
                                                             );
% endif

struct table_key_t {
% for key_field in key_fields:
  ${key_field["cpp_type"]} ${key_field["raw_name"]};
% endfor
};

struct batch_get_result_t {
  int32_t result;
% if index.enable_cas:
  uint64_t version;
% endif
  atfw::util::memory::strong_rc_ptr<rpc::shared_message<PROJECT_NAMESPACE_ID::${message_name}>> message;
};

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type batch_get_all(rpc::context &ctx, gsl::span<table_key_t> keys,
                                           std::vector<batch_get_result_t> &rsp);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type replace(rpc::context &ctx,
                                                         shared_message<PROJECT_NAMESPACE_ID::${message_name}> &&store
% if index.enable_cas:
                                                         ,uint64_t &version);
% else:
                                                         );
% endif

% if len(atomic_inc_fields) > 0:
%     for inc_field in atomic_inc_fields:
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type inc_field_${inc_field["raw_name"]}(rpc::context &ctx
%         for key_field in key_fields:
                                                         , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%         endfor
                                                         , ${inc_field["cpp_type"]}& inc_value
);
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
%>
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type partly_get_${partly_field_name}(rpc::context &ctx
%     for key_field in key_fields:
                                                         , ${key_field["cpp_type"]} ${key_field["raw_name"]}
%     endfor
                                                         , shared_message<PROJECT_NAMESPACE_ID::${message_name}> &rsp
%     if index.enable_cas:
                                                         , uint64_t &version);
%     else:
                                                         );
%     endif

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type batch_partly_get_${partly_field_name}(rpc::context &ctx, gsl::span<table_key_t> keys,
                                           std::vector<batch_get_result_t> &rsp);
% endfor

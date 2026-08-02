// Copyright 2026 atframework
// Created by owent on 2016/08/02.
//

#include "rpc/internal/rpc_template_cs_message.h"

namespace rpc {
namespace internal {
SERVER_FRAME_API int setup_cs_rpc_request_header(atframework::CSMsgHead& head,
                                                 atfw::util::nostd::string_view version,
                                                 atfw::util::nostd::string_view caller,
                                                 atfw::util::nostd::string_view callee,
                                                 atfw::util::nostd::string_view rpc_full_name,
                                                 atfw::util::nostd::string_view type_full_name) {
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }
  request_meta->set_version(version.data(), version.size());
  request_meta->set_caller(caller.data(), caller.size());
  request_meta->set_callee(callee.data(), callee.size());
  request_meta->set_rpc_name(rpc_full_name.data(), rpc_full_name.size());
  request_meta->set_type_url(type_full_name.data(), type_full_name.size());

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

SERVER_FRAME_API int setup_cs_rpc_stream_header(atframework::CSMsgHead& head, atfw::util::nostd::string_view version,
                                                atfw::util::nostd::string_view caller,
                                                atfw::util::nostd::string_view callee,
                                                atfw::util::nostd::string_view rpc_full_name,
                                                atfw::util::nostd::string_view type_full_name) {
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(version.data(), version.size());
  stream_meta->set_caller(caller.data(), caller.size());
  stream_meta->set_callee(callee.data(), callee.size());
  stream_meta->set_rpc_name(rpc_full_name.data(), rpc_full_name.size());
  stream_meta->set_type_url(type_full_name.data(), type_full_name.size());
  stream_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  stream_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}
}  // namespace internal
}  // namespace rpc

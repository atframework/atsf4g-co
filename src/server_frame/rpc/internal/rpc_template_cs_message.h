// Copyright 2026 atframework
// Created by owent on 2016/08/02.
//

#pragma once

#include <nostd/string_view.h>

#include <time/time_utility.h>

#include <config/server_frame_build_feature.h>

#include <utility>

#include "rpc/internal/rpc_template.h"  // IWYU pragma: keep
#include "rpc/rpc_utils.h"              // IWYU pragma: keep

namespace rpc {
namespace internal {
SERVER_FRAME_API int setup_cs_rpc_request_header(atframework::CSMsgHead& head,
                                                 atfw::util::nostd::string_view version,
                                                 atfw::util::nostd::string_view caller,
                                                 atfw::util::nostd::string_view callee,
                                                 atfw::util::nostd::string_view rpc_full_name,
                                                 atfw::util::nostd::string_view type_full_name);

SERVER_FRAME_API int setup_cs_rpc_stream_header(atframework::CSMsgHead& head, atfw::util::nostd::string_view version,
                                                atfw::util::nostd::string_view caller,
                                                atfw::util::nostd::string_view callee,
                                                atfw::util::nostd::string_view rpc_full_name,
                                                atfw::util::nostd::string_view type_full_name);

template <class TBodyType>
ATFW_UTIL_SYMBOL_VISIBLE int pack_cs_stream_message(atframework::CSMsg& msg,
                                                    TBodyType&& body,  // NOLINT(cppcoreguidelines-missing-std-forward)
                                                    atfw::util::nostd::string_view service_full_name,
                                                    atfw::util::nostd::string_view rpc_full_name,
                                                    atfw::util::nostd::string_view type_full_name) {
  int res = rpc::setup_rpc_stream_header(*msg.mutable_head()->mutable_rpc_stream(), service_full_name, rpc_full_name,
                                         type_full_name);
  if (res < 0) {
    return res;
  }

  res = pack_rpc_body(std::forward<TBodyType>(body), msg.mutable_body_bin(), rpc_full_name, type_full_name);
  if (res < 0) {
    return res;
  }

  if (!msg.has_head() || msg.head().timestamp() == 0) {
    msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}
}  // namespace internal
}  // namespace rpc

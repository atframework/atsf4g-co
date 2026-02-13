// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.AuthsvrClientService, please don't edit it

#include "authsvrclientservice.h"

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <data/session.h>
#include <dispatcher/cs_msg_dispatcher.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
namespace {
template <class StringViewLikeT>
inline static atfw::util::nostd::string_view __to_string_view(const StringViewLikeT &input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}
template <class TBodyType>
inline static int __pack_body(TBodyType &body, std::string *output, atfw::util::nostd::string_view rpc_full_name,
                              atfw::util::nostd::string_view type_full_name) {
  if (false == body.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               body.InitializationErrorString());
    return hello::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
               protobuf_mini_dumper_get_readable(body));
    return hello::err::EN_SUCCESS;
  }
}

}  // namespace

namespace authsvrclientservice {}  // namespace authsvrclientservice
}  // namespace rpc

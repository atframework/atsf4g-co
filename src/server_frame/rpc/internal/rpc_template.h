// Copyright 2026 atframework
// Created by owent on 2016/08/02.
//

#pragma once

#include <config/compile_optimize.h>

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <log/log_wrapper.h>

#include <config/server_frame_build_feature.h>

#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/extension/atframework.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <string>

namespace rpc {
namespace internal {
template <class StringViewLikeT>
ATFW_UTIL_FORCEINLINE atfw::util::nostd::string_view to_string_view(const StringViewLikeT &input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}

template <class TBodyType>
ATFW_UTIL_FORCEINLINE int pack_rpc_body(TBodyType &&input,  // NOLINT(cppcoreguidelines-missing-std-forward)
                                        std::string *output, atfw::util::nostd::string_view rpc_full_name,
                                        atfw::util::nostd::string_view type_full_name) {
  if (false == input.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               input.InitializationErrorString());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
             protobuf_mini_dumper_get_readable(input));
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

template <class TBodyType>
ATFW_UTIL_FORCEINLINE int unpack_rpc_body(TBodyType &&output,        // NOLINT(cppcoreguidelines-missing-std-forward)
                                          const std::string &input,  // NOLINT(cppcoreguidelines-missing-std-forward)
                                          atfw::util::nostd::string_view rpc_full_name,
                                          atfw::util::nostd::string_view type_full_name) {
  if (false == output.ParseFromString(input)) {
    FWLOGERROR("rpc {} parse message {} failed, msg: {}", rpc_full_name, type_full_name,
               output.InitializationErrorString());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name,
             protobuf_mini_dumper_get_readable(output));
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

template <class TCode, class TConvertList>
ATFW_UTIL_SYMBOL_VISIBLE bool redirect_rpc_result_to_info_log(
    TCode &origin_result, TConvertList &&convert_list,  // NOLINT(cppcoreguidelines-missing-std-forward)
    atfw::util::nostd::string_view rpc_full_name, atfw::util::nostd::string_view type_full_name) {
  for (const auto &check : convert_list) {
    if (origin_result == check) {
      FWLOGINFO("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name, origin_result,
                protobuf_mini_dumper_get_error_msg(origin_result));

      return true;
    }
  }

  return false;
}

template <class TCode, class TConvertList>
ATFW_UTIL_SYMBOL_VISIBLE bool redirect_rpc_result_to_warning_log(
    TCode &origin_result, TConvertList &&convert_list,  // NOLINT(cppcoreguidelines-missing-std-forward)
    atfw::util::nostd::string_view rpc_full_name, atfw::util::nostd::string_view type_full_name) {
  for (const auto &check : convert_list) {
    if (origin_result == check) {
      FWLOGWARNING("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name, origin_result,
                   protobuf_mini_dumper_get_error_msg(origin_result));

      return true;
    }
  }

  return false;
}

}  // namespace internal
}  // namespace rpc

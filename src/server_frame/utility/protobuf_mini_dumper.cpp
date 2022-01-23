// Copyright 2021 atframework
// Created by owent on 2017/2/6.
//

#include "utility/protobuf_mini_dumper.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <common/string_oprs.h>

#include <config/server_frame_build_feature.h>

#include <sstream>

#include "utility/environment_helper.h"

#define MSG_DISPATCHER_DEBUG_PRINT_BOUND 4096

const char *protobuf_mini_dumper_get_readable(const ::google::protobuf::Message &msg, uint8_t idx) {
  //    static char msg_buffer[MSG_DISPATCHER_DEBUG_PRINT_BOUND] = {0};
  static std::string debug_string[256];

  ::google::protobuf::TextFormat::Printer printer;
  printer.SetUseUtf8StringEscaping(true);
  // printer.SetExpandAny(true);
  printer.SetUseShortRepeatedPrimitives(true);
  printer.SetSingleLineMode(false);
  printer.SetTruncateStringFieldLongerThan(MSG_DISPATCHER_DEBUG_PRINT_BOUND);
  printer.SetPrintMessageFieldsInIndexOrder(false);

  debug_string[idx].clear();
  printer.PrintToString(msg, &debug_string[idx]);

  //    msg_buffer[0] = 0;
  //    size_t sz = protobuf_mini_dumper_dump_readable(msg, msg_buffer, MSG_DISPATCHER_DEBUG_PRINT_BOUND - 1, 0);
  //
  //    if (sz > MSG_DISPATCHER_DEBUG_PRINT_BOUND - 5) {
  //        msg_buffer[MSG_DISPATCHER_DEBUG_PRINT_BOUND - 5] = '.';
  //        msg_buffer[MSG_DISPATCHER_DEBUG_PRINT_BOUND - 4] = '.';
  //        msg_buffer[MSG_DISPATCHER_DEBUG_PRINT_BOUND - 3] = '.';
  //        msg_buffer[MSG_DISPATCHER_DEBUG_PRINT_BOUND - 2] = '}';
  //        msg_buffer[MSG_DISPATCHER_DEBUG_PRINT_BOUND - 1] = 0;
  //    }
  return debug_string[idx].c_str();
}

static std::string build_error_code_msg(const ::google::protobuf::EnumValueDescriptor &desc) {
  bool has_descritpion = false;
  std::stringstream ss;

  if (desc.options().HasExtension(PROJECT_NAMESPACE_ID::error_code::description)) {
    auto description = desc.options().GetExtension(PROJECT_NAMESPACE_ID::error_code::description);
    if (!description.empty()) {
      ss << description << "[";
      has_descritpion = true;
    }
  }

  ss << desc.name();

  if (has_descritpion) {
    ss << "]";
  }
  ss << "(" << desc.number() << ")";

  return ss.str();
}

const char *protobuf_mini_dumper_get_error_msg(int error_code) {
  const char *ret = "Unknown Error Code";

  using error_code_desc_map_t = UTIL_ENV_AUTO_MAP(int, std::string);
  static error_code_desc_map_t cs_error_desc;
  static error_code_desc_map_t ss_error_desc;

  if (0 == error_code) {
    ret = "Success";
  }

  error_code_desc_map_t::const_iterator iter = cs_error_desc.find(error_code);
  if (iter != cs_error_desc.end()) {
    return iter->second.c_str();
  }

  iter = ss_error_desc.find(error_code);
  if (iter != ss_error_desc.end()) {
    return iter->second.c_str();
  }

  const ::google::protobuf::EnumValueDescriptor *desc =
      PROJECT_NAMESPACE_ID::EnErrorCode_descriptor()->FindValueByNumber(error_code);
  if (nullptr != desc) {
    cs_error_desc[error_code] = build_error_code_msg(*desc);
    ret = cs_error_desc[error_code].c_str();
    return ret;
  }

  desc = PROJECT_NAMESPACE_ID::err::EnSysErrorType_descriptor()->FindValueByNumber(error_code);
  if (nullptr != desc) {
    ss_error_desc[error_code] = build_error_code_msg(*desc);
    ret = ss_error_desc[error_code].c_str();
    return ret;
  }

  return ret;
}

std::string protobuf_mini_dumper_get_error_msg(int error_code, const ::google::protobuf::EnumDescriptor *enum_desc,
                                               bool fallback_common_errmsg) {
  const ::google::protobuf::EnumValueDescriptor *desc = nullptr;
  if (nullptr != enum_desc) {
    desc = enum_desc->FindValueByNumber(error_code);
  }
  if (nullptr != desc) {
    return build_error_code_msg(*desc);
  }

  if (fallback_common_errmsg) {
    return protobuf_mini_dumper_get_error_msg(error_code);
  }

  std::stringstream ss;
  ss << "UNKNOWN(" << error_code << ")";
  return ss.str();
}

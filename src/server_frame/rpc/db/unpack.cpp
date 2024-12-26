// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>

#include <hiredis_happ.h>

#include "unpack.h"

namespace rpc {
namespace db {
namespace detail {
int32_t do_nothing(PROJECT_NAMESPACE_ID::table_all_message &, const redisReply *) {
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t unpack_integer(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *data) {
  if (nullptr == data) {
    WLOGDEBUG("data mot found.");
    // 数据找不到，直接成功结束，外层会判为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (REDIS_REPLY_STRING == data->type) {
    // 坑爹的redis的数据库回包可能回字符串类型
    int64_t d = 0;
    atfw::util::string::str2int(d, data->str);
    msg.mutable_simple()->set_msg_i64(d);
  } else if (REDIS_REPLY_INTEGER == data->type) {
    msg.mutable_simple()->set_msg_i64(data->integer);
  } else {
    WLOGERROR("data type error, type=%d", data->type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t unpack_str(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *data) {
  if (nullptr == data) {
    WLOGDEBUG("data mot found.");
    // 数据找不到，直接成功结束，外层会判为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (REDIS_REPLY_STRING != data->type && REDIS_REPLY_STATUS != data->type && REDIS_REPLY_ERROR != data->type) {
    WLOGERROR("data type error, type=%d", data->type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  msg.mutable_simple()->set_msg_str(data->str, data->len);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t unpack_arr_str(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *data) {
  if (nullptr == data) {
    WLOGDEBUG("data mot found.");
    // 数据找不到，直接成功结束，外层会判为无数据
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (REDIS_REPLY_ARRAY != data->type) {
    WLOGERROR("data type error, type=%d", data->type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  PROJECT_NAMESPACE_ID::table_simple_info *simple_info = msg.mutable_simple();
  for (size_t i = 0; i < data->elements; ++i) {
    const redisReply *subr = data->element[i];
    if (REDIS_REPLY_STRING != subr->type && REDIS_REPLY_STATUS != subr->type && REDIS_REPLY_ERROR != subr->type) {
      continue;
    }

    simple_info->add_arr_str(subr->str, subr->len);
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}
}  // namespace detail
}  // namespace db
}  // namespace rpc
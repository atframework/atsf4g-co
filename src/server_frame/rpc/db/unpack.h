// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#ifndef RPC_DB_UNPACK_H
#define RPC_DB_UNPACK_H

#pragma once

#include <config/server_frame_build_feature.h>

#include <vector>

PROJECT_NAMESPACE_BEGIN
class table_all_message;
PROJECT_NAMESPACE_END

extern "C" struct redisReply;

namespace rpc {
namespace db {
namespace detail {

int32_t do_nothing(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *data);

int32_t unpack_integer(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *data);

int32_t unpack_str(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *data);

int32_t unpack_arr_str(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *data);
}  // namespace detail
}  // namespace db
}  // namespace rpc

#endif  //_RPC_DB_UNPACK_H

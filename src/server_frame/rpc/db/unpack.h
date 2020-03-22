//
// Created by owt50 on 2016/9/28.
//

#ifndef RPC_DB_UNPACK_H
#define RPC_DB_UNPACK_H

#pragma once

#include <vector>

namespace hello {
    class table_all_message;
}

extern "C" struct redisReply;

namespace rpc {
    namespace db {
        namespace detail {

            int32_t do_nothing(hello::table_all_message &msg, const redisReply *data);

            int32_t unpack_integer(hello::table_all_message &msg, const redisReply *data);

            int32_t unpack_str(hello::table_all_message &msg, const redisReply *data);

            int32_t unpack_arr_str(hello::table_all_message &msg, const redisReply *data);
        } // namespace detail
    }     // namespace db
} // namespace rpc

#endif //_RPC_DB_UNPACK_H

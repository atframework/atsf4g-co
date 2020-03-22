//
// Created by owt50 on 2016/9/28.
//

#ifndef RPC_DB_DB_MACROS_H
#define RPC_DB_DB_MACROS_H

#pragma once

#include <cstddef>
#include <inttypes.h>
#include <stdint.h>

#include <common/string_oprs.h>

namespace rpc {
    namespace db {
        typedef char user_table_key_t[256];
        inline size_t format_user_key(user_table_key_t &key, const char *table, const char *open_id, uint32_t zone_id) {
            size_t keylen = sizeof(key) - 1;
            int __snprintf_writen_length = UTIL_STRFUNC_SNPRINTF(key, keylen, "%s:%u:%s", table, zone_id, open_id);
            if (__snprintf_writen_length < 0) {
                key[sizeof(key) - 1] = '\0';
                keylen = 0;
            } else {
                keylen = static_cast<size_t>(__snprintf_writen_length);
                key[__snprintf_writen_length] = '\0';
            }

            return keylen;
        }

        inline size_t format_user_key(user_table_key_t &key, const char *table, uint64_t user_id, uint32_t zone_id) {
            size_t keylen = sizeof(key) - 1;
            int __snprintf_writen_length = UTIL_STRFUNC_SNPRINTF(key, keylen, "%s:%u:%llu", table, zone_id, static_cast<unsigned long long>(user_id));
            if (__snprintf_writen_length < 0) {
                key[sizeof(key) - 1] = '\0';
                keylen = 0;
            } else {
                keylen = static_cast<size_t>(__snprintf_writen_length);
                key[__snprintf_writen_length] = '\0';
            }

            return keylen;
        }

    } // namespace db
} // namespace rpc

#endif //_RPC_DB_DB_MACROS_H

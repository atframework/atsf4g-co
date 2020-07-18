//
// Created by owt50 on 2016/9/28.
//

#ifndef RPC_DB_PLAYER_H
#define RPC_DB_PLAYER_H

#pragma once

#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>


#include <protocol/pbdesc/svr.table.pb.h>

namespace rpc {
    namespace db {
        namespace player {
            /**
             * @brief 获取用户表所有数据的rpc操作
             * @param openid 登入用户的openid
             * @param rsp 返回的登入信息
             * @return 0或错误码
             */
            int get_all(uint64_t user_id, uint32_t zone_id, hello::table_user &rsp, std::string &version);

            int get_basic(uint64_t user_id, uint32_t zone_id, hello::table_user &rsp);

            /**
             * @brief 设置用户表的rpc操作
             * @param openid 登入用户的openid
             * @param store 要保持的数据
             * @warning 默认值会被忽略，比如空message或者空字符串，或者0不会更新
             * @return 0或错误码
             */
            int set(uint64_t user_id, uint32_t zone_id, hello::table_user &store, std::string &version);

        } // namespace player
    }     // namespace db
} // namespace rpc

#endif //_RPC_DB_PLAYER_H

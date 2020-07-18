//
// Created by owt50 on 2016/9/28.
//

#ifndef RPC_DB_LOGIN_H
#define RPC_DB_LOGIN_H

#pragma once

#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>

#include <protocol/pbdesc/svr.table.pb.h>

namespace rpc {
    namespace db {
        namespace login {

            /**
             * @brief 获取登入表的rpc操作
             * @param openid 登入用户的openid
             * @param rsp 返回的登入信息
             * @return 0或错误码
             */
            int get(const char *openid, uint32_t zone_id, hello::table_login &rsp, std::string &version);

            /**
             * @brief 设置登入表的rpc操作
             * @param openid 登入用户的openid
             * @param store 要保持的数据
             * @note 未设置的值会采用默认值，部分字段更新请使用update接口
             * @return 0或错误码
             */
            int set(const char *openid, uint32_t zone_id, hello::table_login &store, std::string &version);
        } // namespace login
    }     // namespace db
} // namespace rpc

#endif // ATF4G_CO_LOGIN_H

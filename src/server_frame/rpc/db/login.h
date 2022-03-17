// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#ifndef RPC_DB_LOGIN_H
#define RPC_DB_LOGIN_H

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <rpc/db/db_utils.h>

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

namespace rpc {
class context;

namespace db {
namespace login {

/**
 * @brief 获取登入表的rpc操作
 * @param openid 登入用户的openid
 * @param rsp 返回的登入信息
 * @return 0或错误码
 */
result_type get(rpc::context &ctx, const char *openid, uint32_t zone_id, PROJECT_NAMESPACE_ID::table_login &rsp,
                std::string &version);

/**
 * @brief 设置登入表的rpc操作
 * @param openid 登入用户的openid
 * @param store 要保持的数据
 * @note 未设置的值会采用默认值，部分字段更新请使用update接口
 * @return 0或错误码
 */
result_type set(rpc::context &ctx, const char *openid, uint32_t zone_id, const PROJECT_NAMESPACE_ID::table_login &store,
                std::string &version);
}  // namespace login
}  // namespace db
}  // namespace rpc

#endif  // ATF4G_CO_LOGIN_H

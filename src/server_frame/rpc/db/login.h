// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#include "rpc/db/db_utils.h"
#include "rpc/rpc_shared_message.h"

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
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get(rpc::context &ctx, const char *openid, uint32_t zone_id,
                                                         shared_message<PROJECT_NAMESPACE_ID::table_login> &rsp,
                                                         uint64_t &version);

/**
 * @brief 设置登入表的rpc操作
 * @param openid 登入用户的openid
 * @param store 要保持的数据
 * @note 未设置的值会采用默认值，部分字段更新请使用update接口
 * @return 0或错误码
 */
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type set(rpc::context &ctx, const char *openid, uint32_t zone_id,
                                                         shared_message<PROJECT_NAMESPACE_ID::table_login> &&store,
                                                         uint64_t &version);
}  // namespace login
}  // namespace db
}  // namespace rpc

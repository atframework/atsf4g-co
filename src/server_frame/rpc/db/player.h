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
namespace player {
/**
 * @brief 获取用户表所有数据的rpc操作
 * @param openid 登入用户的openid
 * @param rsp 返回的登入信息
 * @return 0或错误码
 */
EXPLICIT_NODISCARD_ATTR result_type get_all(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                            shared_message<PROJECT_NAMESPACE_ID::table_user> &rsp,
                                            std::string &version);

EXPLICIT_NODISCARD_ATTR result_type get_basic(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                              shared_message<PROJECT_NAMESPACE_ID::table_user> &rsp,
                                              std::string *version = nullptr);

/**
 * @brief 设置用户表的rpc操作
 * @param openid 登入用户的openid
 * @param store 要保持的数据
 * @warning 默认值会被忽略，比如空message或者空字符串，或者0不会更新
 * @return 0或错误码
 */
EXPLICIT_NODISCARD_ATTR result_type set(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                        const shared_message<PROJECT_NAMESPACE_ID::table_user> &store,
                                        std::string &version);

}  // namespace player
}  // namespace db
}  // namespace rpc

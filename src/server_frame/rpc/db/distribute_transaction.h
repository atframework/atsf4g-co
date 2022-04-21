// Copyright 2021 atframework
// Created by owent on 2022-03-29.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <gsl/select-gsl.h>

#include <config/server_frame_build_feature.h>

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#include "rpc/db/db_utils.h"

namespace rpc {
class context;

namespace db {
namespace distribute_transaction {

/**
 * @brief 分布式事务表的rpc操作 - 获取
 * @param zone_id zone id 对于非Read-Your-Writes模型，总是为0
 * @param transaction_uuid 事务唯一ID
 * @param output 返回的事务数据
 * @param version 版本号
 * @return 0或错误码
 */
EXPLICIT_NODISCARD_ATTR result_type get(rpc::context &ctx, uint32_t zone_id, gsl::string_view transaction_uuid,
                                        PROJECT_NAMESPACE_ID::table_distribute_transaction &output,
                                        std::string &version);

/**
 * @brief 分布式事务表的rpc操作 - 保存
 * @param zone_id zone id 对于非Read-Your-Writes模型，总是为0
 * @param transaction_uuid 事务唯一ID
 * @param store 要保持的数据
 * @param version 版本号
 *
 * @return 0或错误码
 */
EXPLICIT_NODISCARD_ATTR result_type set(rpc::context &ctx, uint32_t zone_id, gsl::string_view transaction_uuid,
                                        const PROJECT_NAMESPACE_ID::table_distribute_transaction &store,
                                        std::string &version);

/**
 * @brief 分布式事务表的rpc操作 - 删除
 * @param zone_id zone id 对于非Read-Your-Writes模型，总是为0
 * @param transaction_uuid 事务唯一ID
 * @param store 要保持的数据
 * @param version 版本号
 *
 * @return 0或错误码
 */
EXPLICIT_NODISCARD_ATTR result_type remove(rpc::context &ctx, uint32_t zone_id, gsl::string_view transaction_uuid);
}  // namespace distribute_transaction
}  // namespace db
}  // namespace rpc

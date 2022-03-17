// Copyright 2021 atframework
// Created by owent on 2022/03/17.
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

#include "rpc/db/db_macros.h"
#include "rpc/db/db_utils.h"

namespace rpc {
class context;

namespace db {
namespace hash_table {
/**
 * @brief Get all data of hash table
 * @param channel DB channel
 * @param key key
 * @param output output message from DB
 * @param unpack_fn unpack callback
 * @return future of 0 or error code
 */
result_type get_all(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                    PROJECT_NAMESPACE_ID::table_all_message &output,
                    int32_t (*unpack_fn)(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *reply));

/**
 * @brief Set data of hash table
 * @param channel DB channel
 * @param key key
 * @param store data to store
 * @param version 期望的版本号，留空或0表示忽略版本号检查
 * @param output output message from DB
 * @param unpack_fn unpack callback
 * @warning 默认值会被忽略，比如空message或者空字符串，或者0不会更新
 * @return future of 0 or error code
 */
result_type set(rpc::context &ctx, uint32_t channel, gsl::string_view key, const google::protobuf::Message &store,
                std::string &version, PROJECT_NAMESPACE_ID::table_all_message &output,
                int32_t (*unpack_fn)(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *reply));

}  // namespace hash_table
}  // namespace db
}  // namespace rpc

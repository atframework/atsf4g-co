// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#pragma once

#include <cstdint>

#include "config/server_frame_build_feature.h"
#include "rpc/rpc_common_types.h"

PROJECT_NAMESPACE_BEGIN
class DLoginBasicDataCache;
class DUserBasicData;
class DUserCacheMetaBasicProfile;
class table_user;
class table_login_lock;
PROJECT_NAMESPACE_END

namespace rpc {
class context;

namespace user {
/**
 * @brief 分配User ID
 * @param out player's user id(最后3个bits是校验位)
 * @note 我们取2^5作为一个池的分配数量，这样当QPS为1K时能承载30/s的分配量。
 *       即便是大批玩家涌入比较极端的情况下，数据库访问30k/s时，能提供百万级的分配QPS。
 * @return allocated user id or error code(< 0)
 */
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::rpc_result<int64_t> allocate_user_id(::rpc::context& ctx);

/**
 * @brief 检测User ID是否合法
 * @param in player's account id
 * @return true or false
 */
SERVER_FRAME_API bool is_valid_user_id(int64_t in) noexcept;

SERVER_FRAME_API void merge_basic_profile(PROJECT_NAMESPACE_ID::DUserBasicData& output,
                                          const PROJECT_NAMESPACE_ID::DUserCacheMetaBasicProfile& input) noexcept;

SERVER_FRAME_API void convert_to_client_data(PROJECT_NAMESPACE_ID::DLoginBasicDataCache& output,
                                             const PROJECT_NAMESPACE_ID::table_user& input_user) noexcept;

SERVER_FRAME_API void convert_to_client_data(PROJECT_NAMESPACE_ID::DUserBasicData& output,
                                             const PROJECT_NAMESPACE_ID::table_user& input) noexcept;

}  // namespace user
}  // namespace rpc

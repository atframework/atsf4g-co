// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#ifndef RPC_GAME_PLAYER_H
#define RPC_GAME_PLAYER_H

#pragma once

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#include "rpc/rpc_common_types.h"

namespace rpc {
class context;

namespace game {
namespace player {
/**
 * @brief 分配User ID
 * @param out player's user id(最后3个bits是校验位)
 * @note 我们取2^5作为一个池的分配数量，这样当QPS为1K时能承载30/s的分配量。
 *       即便是大批玩家涌入比较极端的情况下，数据库访问30k/s时，能提供百万级的分配QPS。
 * @return allocated user id or error code(< 0)
 */
EXPLICIT_NODISCARD_ATTR rpc::rpc_result<int64_t> alloc_user_id(::rpc::context &ctx);

/**
 * @brief 检测User ID是否合法
 * @param in player's account id
 * @return true or false
 */
bool is_valid_user_id(int64_t in);
}  // namespace player
}  // namespace game
}  // namespace rpc

#endif  //_RPC_GAME_PLAYER_H

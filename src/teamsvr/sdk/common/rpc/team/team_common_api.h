// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

#include <cstdint>

namespace rpc {
namespace team {
namespace team_api {
// 按 zone_id + team_id 一致性哈希选择 ready 的 teamsvr-room 节点，无可用节点时返回 0
TEAM_SDK_COMMON_API uint64_t get_teamsvr_room_server_id_of_zone(uint32_t zone_id, int64_t team_id);
// 是否存在 ready 的 teamsvr-room 节点
TEAM_SDK_COMMON_API bool has_teamsvr_room();
}  // namespace team_api
}  // namespace team
}  // namespace rpc

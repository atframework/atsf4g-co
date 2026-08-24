// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

#include <cstdint>

namespace atframework {
namespace dtmq {
class DChannelIdKey;
}  // namespace dtmq
namespace team {
class DTeamKey;
}  // namespace team
}  // namespace atframework

namespace rpc {
namespace team {
namespace team_api {

// 按 team_key 的 (zone_id, team_id) 选择 ready 的 teamsvr-room 节点: zone_id 决定节点发现集(zone_id 为 0
// 表示不分区的全局队伍,使用跨区全局发现集)， team_id 在发现集内做一致性哈希；无可用节点时返回 0
TEAM_SDK_COMMON_API uint64_t get_teamsvr_room_server_id_of_zone(const atfw::team::DTeamKey& team_key);

// 由 DTeamKey 生成队伍房间 DTMQ 频道的标准 channel key(zone_id 为 0 表示不分区队伍)
TEAM_SDK_COMMON_API atfw::dtmq::DChannelIdKey make_team_room_channel_key(const atfw::team::DTeamKey& team_key);

// 是否存在 ready 的 teamsvr-room 节点
TEAM_SDK_COMMON_API bool has_teamsvr_room();

}  // namespace team_api
}  // namespace team
}  // namespace rpc

// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/pbdesc/com.struct.match.pb.h>
#include <protocol/pbdesc/match_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <cstdint>
#include <vector>

class matching_room;

// 每次调用都从 server_frame 配置管理器读取当前配置组。
class matching_logic {
 public:
  using unit_view = std::vector<const PROJECT_NAMESPACE_ID::DMatchingUnit*>;

  // 校验 unit 自身是否合法以及是否超过匹配池的组队上限。
  static int32_t validate_unit(int32_t matching_pool_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 新房只有一个 Unit，直接用模板容量索引选择合法 faction，不执行多 Unit 完整求解。
  static PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation check_unit_can_create_room(
      const PROJECT_NAMESPACE_ID::DMatchingScope& scope, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
      int32_t global_matching_users);
  // 只检查 unit 能否作为一个整体加入指定房间，不判断房间是否成局。
  static PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation check_unit_can_join(
      const matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
      int32_t global_matching_users);
  // 把一个已经满员的 faction 作为整体加入目标房间，保留其容量和 Unit 成员关系。
  static PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation check_faction_can_join(const matching_room& room,
                                                                                  const unit_view& faction_units,
                                                                                  uint32_t faction_capacity,
                                                                                  int64_t now,
                                                                                  int32_t global_matching_users);
  // 只检查房间当前已有的 unit 是否满足成局条件，不尝试加入新 unit。
  static PROJECT_NAMESPACE_ID::DMatchingRoomReadyEvaluation check_room_ready(const matching_room& room, int64_t now,
                                                                             int32_t global_matching_users);
};

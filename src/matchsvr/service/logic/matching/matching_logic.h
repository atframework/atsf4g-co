// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/pbdesc/com.struct.match.pb.h>
#include <protocol/pbdesc/match_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <cstdint>

class matching_room;

// 每次调用都从 server_frame 配置管理器读取当前配置组。
class matching_logic {
 public:
  struct unit_join_result {
    bool can_join = false;
    int32_t result_template_id = 0;
    int32_t result = PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND;
  };

  struct room_ready_result {
    bool ready = false;
    int32_t result_template_id = 0;
    int32_t result = PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND;
  };

  // 校验 unit 自身是否合法以及是否超过匹配池的组队上限。
  static int32_t validate_unit(int32_t matching_pool_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 只检查 unit 能否作为一个整体加入指定房间，不判断房间是否成局。
  static unit_join_result check_unit_can_join(const matching_room& room,
                                              const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
                                              int32_t global_matching_players);
  // 只检查房间当前已有的 unit 是否满足成局条件，不尝试加入新 unit。
  static room_ready_result check_room_ready(const matching_room& room, int64_t now, int32_t global_matching_players);
};

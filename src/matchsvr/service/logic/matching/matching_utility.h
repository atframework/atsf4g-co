// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.match.pb.h>
#include <protocol/pbdesc/com.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <cstdint>
#include <vector>

namespace matching_utility {

namespace detail {

// 判断关卡 ID 是否不能作为匹配候选使用。
bool is_invalid_level_id(int32_t level_id);

// 复制并规范化 Unit 的候选关卡，输入无效时返回空集合。
std::vector<int32_t> copy_acceptable_level_ids(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);

}  // namespace detail

// 比较完整的玩家路由键，避免不同区服的相同 user_id 被视为同一玩家。
bool same_user(const PROJECT_NAMESPACE_ID::DUserIDKey& left, const PROJECT_NAMESPACE_ID::DUserIDKey& right) noexcept;

// 判断不可拆分的匹配 Unit 是否包含指定玩家。
bool unit_has_user(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                   const PROJECT_NAMESPACE_ID::DUserIDKey& expected) noexcept;

// 排序、去重并校验 Unit 的可选关卡；候选集合必须显式提供且非空。
bool normalize_acceptable_level_ids(PROJECT_NAMESPACE_ID::DMatchingUnit& unit);

// 计算全部 Unit 可选关卡的有序交集；任一 Unit 没有合法候选时返回空集合。
std::vector<int32_t> get_compatible_level_ids(const std::vector<const PROJECT_NAMESPACE_ID::DMatchingUnit*>& units);

}  // namespace matching_utility

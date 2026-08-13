// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.match.pb.h>
#include <protocol/pbdesc/com.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

namespace matching_utility {

// 比较完整的玩家路由键，避免不同区服的相同 user_id 被视为同一玩家。
bool same_user(const PROJECT_NAMESPACE_ID::DUserIDKey& left, const PROJECT_NAMESPACE_ID::DUserIDKey& right) noexcept;

// 判断不可拆分的匹配 Unit 是否包含指定玩家。
bool unit_has_user(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                   const PROJECT_NAMESPACE_ID::DUserIDKey& expected) noexcept;

// 判断用户键列表是否包含指定玩家。
bool contains_user(const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DUserIDKey>& users,
                   const PROJECT_NAMESPACE_ID::DUserIDKey& expected) noexcept;

}  // namespace matching_utility

// Copyright 2026 atframework

#include "logic/matching/matching_utility.h"

#include <algorithm>

namespace matching_utility {

bool same_user(const PROJECT_NAMESPACE_ID::DUserIDKey& left, const PROJECT_NAMESPACE_ID::DUserIDKey& right) noexcept {
  return left.user_id() == right.user_id() && left.zone_id() == right.zone_id();
}

bool unit_has_user(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                   const PROJECT_NAMESPACE_ID::DUserIDKey& expected) noexcept {
  return std::any_of(unit.users().begin(), unit.users().end(),
                     [&expected](const auto& user_inst) { return same_user(user_inst.user_key(), expected); });
}

bool contains_user(const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DUserIDKey>& users,
                   const PROJECT_NAMESPACE_ID::DUserIDKey& expected) noexcept {
  return std::any_of(users.begin(), users.end(), [&expected](const auto& value) { return same_user(value, expected); });
}

}  // namespace matching_utility

// Copyright 2026 atframework

#include "logic/matching/matching_utility.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace matching_utility {
namespace detail {
bool is_invalid_level_id(int32_t level_id) { return level_id <= 0; }

std::vector<int32_t> copy_acceptable_level_ids(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  std::vector<int32_t> result(unit.acceptable_level_ids().begin(), unit.acceptable_level_ids().end());
  if (std::any_of(result.begin(), result.end(), is_invalid_level_id)) {
    return {};
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}
}  // namespace detail

bool same_user(const PROJECT_NAMESPACE_ID::DUserIDKey& left, const PROJECT_NAMESPACE_ID::DUserIDKey& right) noexcept {
  return left.user_id() == right.user_id() && left.zone_id() == right.zone_id();
}

bool unit_has_user(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                   const PROJECT_NAMESPACE_ID::DUserIDKey& expected) noexcept {
  return std::any_of(unit.users().begin(), unit.users().end(),
                     [&expected](const auto& user_inst) { return same_user(user_inst.user_key(), expected); });
}

bool normalize_acceptable_level_ids(PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  auto normalized = detail::copy_acceptable_level_ids(unit);
  if (normalized.empty()) {
    return false;
  }
  unit.clear_acceptable_level_ids();
  for (int32_t level_id : normalized) {
    unit.add_acceptable_level_ids(level_id);
  }
  return true;
}

std::vector<int32_t> get_compatible_level_ids(const std::vector<const PROJECT_NAMESPACE_ID::DMatchingUnit*>& units) {
  if (units.empty() || units.front() == nullptr) {
    return {};
  }
  auto result = detail::copy_acceptable_level_ids(*units.front());
  for (size_t index = 1; !result.empty() && index < units.size(); ++index) {
    if (units[index] == nullptr) {
      return {};
    }
    const auto candidates = detail::copy_acceptable_level_ids(*units[index]);
    std::vector<int32_t> intersection;
    std::set_intersection(result.begin(), result.end(), candidates.begin(), candidates.end(),
                          std::back_inserter(intersection));
    result = std::move(intersection);
  }
  return result;
}

}  // namespace matching_utility

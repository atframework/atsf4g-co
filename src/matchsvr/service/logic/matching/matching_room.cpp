// Copyright 2026 atframework

#include "logic/matching/matching_room.h"

#include "logic/matching/matching_logic.h"
#include "logic/matching/matching_unit.h"
#include "logic/matching/matching_utility.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <rpc/rpc_context.h>
#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

static void log_room_status_change(const matching_room& room, PROJECT_NAMESPACE_ID::EnMatchingRoomStatus from,
                                   const char* reason, int64_t previous_battle_expire_time = 0) {
  if (from == room.get_status()) {
    return;
  }
  FWLOGINFO(
      "matching Room state changed, matching_id={}, pool_id={}, state_from={}, state_to={}, reason={}, "
      "level_id={}, units={}, users={}, factions={}, result={}, room_internal_event_id={}, "
      "expire_time={}, confirm_expire_time={}, battle_expire_time={}, previous_battle_expire_time={}, "
      "waiting_end_timepoint={}, successful_users={}, terminal_time={}",
      room.get_matching_id(), room.get_scope().matching_pool_id(), static_cast<int>(from),
      static_cast<int>(room.get_status()), reason, room.get_selected_level_id(), room.get_units().size(),
      room.get_user_count(), room.get_faction_assignments().size(), room.get_result(), room.get_last_event_id(),
      room.get_expire_time(), room.get_confirm_expire_time(), room.get_battle_create_expire_time(),
      previous_battle_expire_time, room.get_waiting_end_timepoint(), room.get_successful_user_count(),
      room.get_terminal_time());
}
}  // namespace

bool matching_room::calculate_faction_statistics(
    const unit_map_t& units,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>& assignments,
    faction_statistics& result) {
  result = faction_statistics{};
  if (assignments.empty()) {
    return true;
  }

  std::unordered_set<uint64_t> assigned_unit_ids;
  for (const auto& assignment : assignments) {
    if (assignment.user_capacity() == 0 || assignment.unit_ids_size() <= 0) {
      return false;
    }
    size_t faction_users = 0;
    bool fill_enabled = false;
    for (uint64_t unit_id : assignment.unit_ids()) {
      auto unit_iter = units.find(unit_id);
      if (unit_iter == units.end() || !assigned_unit_ids.emplace(unit_id).second) {
        return false;
      }
      if (!unit_iter->second) {
        return false;
      }
      const auto& unit = unit_iter->second->data_;
      const size_t unit_size = static_cast<size_t>(unit.users_size());
      if (unit.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE) {
        if (assignment.unit_ids_size() != 1 || unit_size != assignment.user_capacity()) {
          return false;
        }
      } else if (unit.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE) {
        fill_enabled = true;
      } else {
        return false;
      }
      faction_users += unit_size;
    }
    if (faction_users > assignment.user_capacity()) {
      return false;
    }
    result.assigned_user_counts.emplace_back(static_cast<uint32_t>(faction_users));
    ++result.faction_count_by_capacity[assignment.user_capacity()];
    if (faction_users == assignment.user_capacity()) {
      ++result.completed_faction_count;
    }
    if (fill_enabled) {
      result.fill_enabled_faction_capacities.emplace(assignment.user_capacity());
    }
    result.pending_user_count += static_cast<size_t>(assignment.user_capacity()) - faction_users;
  }
  if (assigned_unit_ids.size() != units.size()) {
    return false;
  }
  return true;
}

void matching_room::commit_faction_assignments(
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>&& assignments,
    faction_statistics&& statistics) {
  assert(static_cast<size_t>(assignments.size()) == statistics.assigned_user_counts.size());
  for (int position = 0; position < assignments.size(); ++position) {
    assignments.Mutable(position)->set_assigned_user_count(
        statistics.assigned_user_counts[static_cast<size_t>(position)]);
  }
  faction_assignments_.Swap(&assignments);
  faction_count_by_capacity_ = std::move(statistics.faction_count_by_capacity);
  fill_enabled_faction_capacities_ = std::move(statistics.fill_enabled_faction_capacities);
  completed_faction_count_ = statistics.completed_faction_count;
  pending_faction_user_count_ = statistics.pending_user_count;
}

matching_room::matching_room(std::string matching_id, const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                             int32_t selected_level_id, int64_t now, int64_t expire_time)
    : matching_id_(std::move(matching_id)),
      scope_(),
      selected_level_id_(selected_level_id),
      status_(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING),
      user_count_(0),
      completed_faction_count_(0),
      pending_faction_user_count_(0),
      faction_ids_finalized_(false),
      created_time_(now),
      expire_time_(expire_time),
      terminal_time_(0),
      confirm_expire_time_(0),
      last_event_id_(0),
      result_(0),
      orbit_ready_processing_(false),
      orbit_server_id_(0) {
  scope_.set_level_type(scope.level_type());
  scope_.set_region(scope.region());
  scope_.set_battle_version(scope.battle_version());
  scope_.set_matching_pool_id(scope.matching_pool_id());
  orbit_room_key_.set_client_id("room_key_" + matching_id_);
}

bool matching_room::has_unit(uint64_t unit_id) const noexcept { return units_.find(unit_id) != units_.end(); }

matching_room::unit_ptr_t matching_room::find_unit(uint64_t unit_id) const noexcept {
  auto unit_iter = units_.find(unit_id);
  return unit_iter == units_.end() ? nullptr : unit_iter->second;
}

bool matching_room::has_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const noexcept {
  for (const auto& unit : units_) {
    if (!unit.second) {
      continue;
    }
    for (const auto& user : unit.second->data_.users()) {
      if (matching_utility::same_user(user.user_key(), user_key)) {
        return true;
      }
    }
  }

  for (const auto& unit : admitted_units_) {
    for (const auto& user : unit.second.users()) {
      if (matching_utility::same_user(user.user_key(), user_key)) {
        return true;
      }
    }
  }

  return false;
}

const PROJECT_NAMESPACE_ID::DMatchingOrbitInitData& matching_room::get_match_orbit_user_init_detail(
    const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const {
  static const PROJECT_NAMESPACE_ID::DMatchingOrbitInitData empty_detail;
  auto iter = orbit_users_init_detail_.find(user_key);
  return iter == orbit_users_init_detail_.end() ? empty_detail : iter->second;
}

void matching_room::add_orbit_user_init_detail(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                               const PROJECT_NAMESPACE_ID::DOrbitUserInitDataDetail& detail,
                                               const std::string& user_open_id) {
  auto& data = orbit_users_init_detail_[user_key];
  protobuf_copy_message(*data.mutable_orbit_init_data(), detail);
  data.set_user_open_id(user_open_id);
}

bool matching_room::add_unit(const unit_ptr_t& unit) {
  if (!unit || status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING || unit->get_unit_id() == 0 ||
      units_.find(unit->get_unit_id()) != units_.end()) {
    return false;
  }
  if (!matching_utility::normalize_acceptable_level_ids(unit->data_)) {
    return false;
  }
  std::vector<int32_t> compatible_level_ids;
  if (units_.empty()) {
    compatible_level_ids.assign(unit->data_.acceptable_level_ids().begin(), unit->data_.acceptable_level_ids().end());
  } else {
    std::set_intersection(compatible_level_ids_.begin(), compatible_level_ids_.end(),
                          unit->data_.acceptable_level_ids().begin(), unit->data_.acceptable_level_ids().end(),
                          std::back_inserter(compatible_level_ids));
  }
  if (compatible_level_ids.empty()) {
    return false;
  }
  for (int left = 0; left < unit->data_.users_size(); ++left) {
    const auto& user = unit->data_.users(left);
    if (has_user(user.user_key())) {
      return false;
    }
    for (int right = left + 1; right < unit->data_.users_size(); ++right) {
      if (matching_utility::same_user(user.user_key(), unit->data_.users(right).user_key())) {
        return false;
      }
    }
  }
  const size_t unit_size = static_cast<size_t>(unit->data_.users_size());
  unit->data_.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_SEARCHING);
  units_.emplace(unit->get_unit_id(), unit);
  compatible_level_ids_ = std::move(compatible_level_ids);
  if (std::find(compatible_level_ids_.begin(), compatible_level_ids_.end(), selected_level_id_) ==
      compatible_level_ids_.end()) {
    selected_level_id_ = compatible_level_ids_.front();
  }
  user_count_ += unit_size;
  if (unit_size_counts_.size() <= unit_size) {
    unit_size_counts_.resize(unit_size + 1, 0);
  }
  ++unit_size_counts_[unit_size];
  finalized_unit_faction_ids_.clear();
  faction_ids_finalized_ = false;
  return true;
}

bool matching_room::remove_unit(uint64_t unit_id) {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING &&
      status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return false;
  }
  auto unit_iter = units_.find(unit_id);
  if (unit_iter == units_.end()) {
    return false;
  }
  if (!unit_iter->second) {
    return false;
  }
  const size_t unit_size = static_cast<size_t>(unit_iter->second->data_.users_size());
  user_count_ -= unit_size;
  if (unit_size < unit_size_counts_.size() && unit_size_counts_[unit_size] > 0) {
    --unit_size_counts_[unit_size];
  }
  units_.erase(unit_iter);
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> remaining_assignments;
  remaining_assignments.Reserve(faction_assignments_.size());
  for (const auto& assignment : faction_assignments_) {
    auto* remaining_assignment = remaining_assignments.Add();
    protobuf_copy_message(*remaining_assignment, assignment);
    remaining_assignment->clear_unit_ids();
    for (uint64_t assigned_unit_id : assignment.unit_ids()) {
      if (assigned_unit_id != unit_id) {
        remaining_assignment->add_unit_ids(assigned_unit_id);
      }
    }
    if (remaining_assignment->unit_ids_size() == 0) {
      remaining_assignments.RemoveLast();
    }
  }
  faction_statistics statistics;
  const bool statistics_valid = calculate_faction_statistics(units_, remaining_assignments, statistics);
  assert(statistics_valid);
  if (statistics_valid) {
    commit_faction_assignments(std::move(remaining_assignments), std::move(statistics));
  }
  finalized_unit_faction_ids_.clear();
  faction_ids_finalized_ = false;
  std::vector<const PROJECT_NAMESPACE_ID::DMatchingUnit*> remaining_units;
  remaining_units.reserve(units_.size());
  for (const auto& stored : units_) {
    if (stored.second) {
      remaining_units.emplace_back(&stored.second->data_);
    }
  }
  compatible_level_ids_ = matching_utility::get_compatible_level_ids(remaining_units);
  if (!compatible_level_ids_.empty() && std::find(compatible_level_ids_.begin(), compatible_level_ids_.end(),
                                                  selected_level_id_) == compatible_level_ids_.end()) {
    selected_level_id_ = compatible_level_ids_.front();
  }
  return true;
}

void matching_room::begin_confirmation(int64_t expire_time) noexcept {
  const auto previous_status = status_;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING;
  confirm_expire_time_ = expire_time;
  for (auto& unit : units_) {
    if (!unit.second) {
      continue;
    }
    unit.second->begin_confirmation(expire_time);
  }
  log_room_status_change(*this, previous_status, "begin_confirmation");
}

bool matching_room::confirm_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool accepted) noexcept {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return false;
  }
  for (auto& unit : units_) {
    if (!unit.second) {
      continue;
    }
    if (unit.second->confirm_user(user_key, accepted)) {
      return true;
    }
  }
  return false;
}

bool matching_room::are_all_users_confirmed() const noexcept {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING || units_.empty()) {
    return false;
  }
  for (const auto& unit : units_) {
    if (!unit.second) {
      return false;
    }
    for (const auto& user : unit.second->data_.users()) {
      if (user.confirm_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED) {
        return false;
      }
    }
  }
  return true;
}

void matching_room::resume_matching(int64_t expire_time) noexcept {
  const auto previous_status = status_;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING;
  confirm_expire_time_ = 0;
  expire_time_ = expire_time;
  finalized_unit_faction_ids_.clear();
  faction_ids_finalized_ = false;
  for (auto& unit : units_) {
    if (!unit.second) {
      continue;
    }
    unit.second->resume_searching(expire_time);
  }
  log_room_status_change(*this, previous_status, "resume_matching");
}

bool matching_room::set_faction_assignments(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>& value) {
  faction_statistics statistics;
  if (!calculate_faction_statistics(units_, value, statistics)) {
    return false;
  }
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> normalized_assignments;
  normalized_assignments.Reserve(value.size());
  for (const auto& assignment : value) {
    protobuf_copy_message(*normalized_assignments.Add(), assignment);
  }
  commit_faction_assignments(std::move(normalized_assignments), std::move(statistics));
  finalized_unit_faction_ids_.clear();
  faction_ids_finalized_ = false;
  return true;
}

void matching_room::clear_faction_assignments() noexcept {
  faction_assignments_.Clear();
  faction_count_by_capacity_.clear();
  fill_enabled_faction_capacities_.clear();
  completed_faction_count_ = 0;
  pending_faction_user_count_ = 0;
  finalized_unit_faction_ids_.clear();
  faction_ids_finalized_ = false;
}

bool matching_room::faction_assignment_precedes(
    const PROJECT_NAMESPACE_ID::DMatchingFactionAssignment* left,
    const PROJECT_NAMESPACE_ID::DMatchingFactionAssignment* right) noexcept {
  return *std::min_element(left->unit_ids().begin(), left->unit_ids().end()) <
         *std::min_element(right->unit_ids().begin(), right->unit_ids().end());
}

bool matching_room::finalize_faction_ids() {
  if (faction_ids_finalized_) {
    return true;
  }
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return false;
  }
  std::vector<const PROJECT_NAMESPACE_ID::DMatchingFactionAssignment*> ordered_factions;
  ordered_factions.reserve(static_cast<size_t>(faction_assignments_.size()));
  for (const auto& assignment : faction_assignments_) {
    if (assignment.unit_ids().empty()) {
      return false;
    }
    ordered_factions.emplace_back(&assignment);
  }
  std::sort(ordered_factions.begin(), ordered_factions.end(), faction_assignment_precedes);

  std::unordered_map<uint64_t, int32_t> finalized_ids;
  finalized_ids.reserve(units_.size());
  int32_t faction_id = matching_logic::get_start_faction_id();
  for (const auto* assignment : ordered_factions) {
    for (uint64_t unit_id : assignment->unit_ids()) {
      if (!finalized_ids.emplace(unit_id, faction_id).second) {
        return false;
      }
    }
    ++faction_id;
  }
  if (!faction_assignments_.empty() && finalized_ids.size() != units_.size()) {
    return false;
  }
  finalized_unit_faction_ids_ = std::move(finalized_ids);
  faction_ids_finalized_ = true;
  return true;
}

int32_t matching_room::get_unit_faction_id(uint64_t unit_id) const noexcept {
  auto faction_iter = finalized_unit_faction_ids_.find(unit_id);
  return faction_iter == finalized_unit_faction_ids_.end() ? 0 : faction_iter->second;
}

bool matching_room::open_waiting_room(int64_t now, int64_t end_timepoint) {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE || !faction_ids_finalized_) {
    return false;
  }
  const auto previous_status = status_;
  const int64_t previous_battle_expire_time = battle_create_expire_time_;
  waiting_faction_map_t factions;
  for (const auto& assignment : faction_assignments_) {
    if (assignment.unit_ids().empty()) {
      return false;
    }
    const int32_t faction_id = get_unit_faction_id(assignment.unit_ids(0));
    if (faction_id <= 0 || faction_id == std::numeric_limits<int32_t>::max()) {
      return false;
    }
    auto& faction = factions[faction_id];
    faction.user_capacity = assignment.user_capacity();
    faction.successful_user_count = assignment.assigned_user_count();
    faction.unit_ids.assign(assignment.unit_ids().begin(), assignment.unit_ids().end());
    for (uint64_t unit_id : faction.unit_ids) {
      auto unit = find_unit(unit_id);
      if (!unit || get_unit_faction_id(unit_id) != faction_id) {
        return false;
      }
      faction.fill_enabled = faction.fill_enabled || unit->get_data().faction_fill_policy() ==
                                                         PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE;
    }
  }
  for (const auto& value : units_) {
    value.second->refresh_view_from_room(*this);
    value.second->mark_terminal(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED, 0, now);
    protobuf_copy_message(admitted_units_[value.first], value.second->get_data());
  }
  units_.clear();
  orbit_users_init_detail_.clear();
  waiting_factions_.swap(factions);
  if (!waiting_factions_.empty()) {
    next_waiting_faction_id_ = waiting_factions_.rbegin()->first + 1;
  }
  successful_user_count_ = user_count_;
  waiting_end_timepoint_ = end_timepoint;
  expire_time_ = end_timepoint;
  confirm_expire_time_ = 0;
  battle_create_expire_time_ = 0;
  terminal_time_ = 0;
  result_ = 0;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_WAITING;
  log_room_status_change(*this, previous_status, "open_waiting_room", previous_battle_expire_time);
  return true;
}

bool matching_room::is_waiting_open(int64_t now) const noexcept {
  return status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_WAITING && now < waiting_end_timepoint_;
}

bool matching_room::add_waiting_units(const std::vector<unit_ptr_t>& units, int32_t faction_id,
                                      uint32_t faction_capacity, bool creates_faction, int64_t now,
                                      int64_t confirm_expire_time) {
  if (!is_waiting_open(now) || units.empty() || faction_capacity == 0 || confirm_expire_time <= now) {
    return false;
  }
  waiting_faction_map_t::iterator faction_iter = waiting_factions_.end();
  if (creates_faction) {
    if (faction_id != 0 || next_waiting_faction_id_ == std::numeric_limits<int32_t>::max()) {
      return false;
    }
  } else {
    faction_iter = waiting_factions_.find(faction_id);
    if (faction_iter == waiting_factions_.end() || faction_iter->second.confirming_creation ||
        faction_iter->second.user_capacity != faction_capacity || !faction_iter->second.fill_enabled) {
      return false;
    }
  }
  size_t incoming_users = 0;
  std::unordered_set<uint64_t> incoming_unit_ids;
  std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> incoming_user_keys;
  bool fill_enabled = false;
  for (const auto& unit : units) {
    if (!unit || unit->is_terminal() || has_unit(unit->get_unit_id()) ||
        admitted_units_.find(unit->get_unit_id()) != admitted_units_.end() ||
        !incoming_unit_ids.emplace(unit->get_unit_id()).second || unit->get_data().users().empty()) {
      return false;
    }
    const auto& data = unit->get_data();
    if (std::find(data.acceptable_level_ids().begin(), data.acceptable_level_ids().end(), selected_level_id_) ==
        data.acceptable_level_ids().end()) {
      return false;
    }
    const bool unit_fill_enabled =
        data.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE;
    if (!unit_fill_enabled &&
        (data.faction_fill_policy() != PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE ||
         !creates_faction || units.size() != 1 || static_cast<uint32_t>(data.users_size()) != faction_capacity)) {
      return false;
    }
    fill_enabled = fill_enabled || unit_fill_enabled;
    incoming_users += static_cast<size_t>(data.users_size());
    for (const auto& user : data.users()) {
      if (has_user(user.user_key()) || !incoming_user_keys.emplace(user.user_key()).second) {
        return false;
      }
    }
  }
  const size_t occupied_users =
      creates_faction ? 0 : faction_iter->second.successful_user_count + faction_iter->second.reserved_user_count;
  if (occupied_users + incoming_users > faction_capacity ||
      (creates_faction && units.size() > 1 && incoming_users != faction_capacity)) {
    return false;
  }
  if (creates_faction) {
    faction_id = next_waiting_faction_id_++;
    auto& faction = waiting_factions_[faction_id];
    faction.user_capacity = faction_capacity;
    faction.fill_enabled = fill_enabled;
    faction.confirming_creation = true;
    faction_iter = waiting_factions_.find(faction_id);
  }
  for (const auto& unit : units) {
    units_.emplace(unit->get_unit_id(), unit);
    finalized_unit_faction_ids_[unit->get_unit_id()] = faction_id;
    faction_iter->second.unit_ids.emplace_back(unit->get_unit_id());
    const size_t unit_size = static_cast<size_t>(unit->get_data().users_size());
    if (unit_size_counts_.size() <= unit_size) {
      unit_size_counts_.resize(unit_size + 1, 0);
    }
    ++unit_size_counts_[unit_size];
    unit->begin_confirmation(std::min(confirm_expire_time, waiting_end_timepoint_));
    unit->refresh_view_from_room(*this);
  }
  faction_iter->second.reserved_user_count += incoming_users;
  user_count_ += incoming_users;
  refresh_waiting_faction_assignments();
  for (const auto& unit : units) {
    FWLOGINFO(
        "waiting Room Unit admitted, matching_id={}, unit_id={}, faction_id={}, creates_faction={}, "
        "faction_capacity={}, unit_users={}, users={}, successful_users={}, reserved_users={}, "
        "confirm_expire_time={}, waiting_end_timepoint={}, unit_wal_event_id={}",
        matching_id_, unit->get_unit_id(), faction_id, creates_faction, faction_capacity, unit->get_data().users_size(),
        user_count_, successful_user_count_, get_reserved_user_count(), unit->get_view().confirm_expire_time(),
        waiting_end_timepoint_, unit->get_last_event_id());
  }
  return true;
}

std::vector<uint64_t> matching_room::get_waiting_confirmation_cohort(uint64_t unit_id) const {
  if (!has_unit(unit_id)) {
    return {};
  }
  const auto faction_iter = waiting_factions_.find(get_unit_faction_id(unit_id));
  if (faction_iter == waiting_factions_.end()) {
    return {};
  }
  if (!faction_iter->second.confirming_creation) {
    return {unit_id};
  }
  auto result = faction_iter->second.unit_ids;
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<uint64_t> matching_room::release_waiting_confirmation_cohort(uint64_t unit_id) {
  auto cohort = get_waiting_confirmation_cohort(unit_id);
  if (cohort.empty()) {
    return {};
  }
  for (uint64_t member_id : cohort) {
    const auto member = find_unit(member_id);
    if (!member || !member->are_all_users_confirmed()) {
      return {};
    }
  }
  waiting_factions_.find(get_unit_faction_id(unit_id))->second.confirming_creation = false;
  return cohort;
}

void matching_room::erase_orbit_user_init_details(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  for (const auto& user : unit.users()) {
    orbit_users_init_detail_.erase(user.user_key());
  }
}

bool matching_room::remove_waiting_unit(uint64_t unit_id) {
  auto unit_iter = units_.find(unit_id);
  auto faction_iter = waiting_factions_.find(get_unit_faction_id(unit_id));
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_WAITING || unit_iter == units_.end() ||
      !unit_iter->second || faction_iter == waiting_factions_.end()) {
    return false;
  }
  const size_t unit_size = static_cast<size_t>(unit_iter->second->get_data().users_size());
  const int32_t removed_faction_id = faction_iter->first;
  erase_orbit_user_init_details(unit_iter->second->get_data());
  faction_iter->second.reserved_user_count -= unit_size;
  auto& membership = faction_iter->second.unit_ids;
  membership.erase(std::remove(membership.begin(), membership.end(), unit_id), membership.end());
  if (membership.empty()) {
    waiting_factions_.erase(faction_iter);
  }
  units_.erase(unit_iter);
  finalized_unit_faction_ids_.erase(unit_id);
  user_count_ -= unit_size;
  if (unit_size < unit_size_counts_.size() && unit_size_counts_[unit_size] > 0) {
    --unit_size_counts_[unit_size];
  }
  refresh_waiting_faction_assignments();
  FWLOGINFO(
      "waiting Room Unit removed, matching_id={}, unit_id={}, faction_id={}, unit_users={}, users={}, "
      "successful_users={}, reserved_users={}, waiting_end_timepoint={}",
      matching_id_, unit_id, removed_faction_id, unit_size, user_count_, successful_user_count_,
      get_reserved_user_count(), waiting_end_timepoint_);
  return true;
}

bool matching_room::complete_waiting_unit(uint64_t unit_id, int64_t now) {
  auto unit_iter = units_.find(unit_id);
  auto faction_iter = waiting_factions_.find(get_unit_faction_id(unit_id));
  if (!is_waiting_open(now) || unit_iter == units_.end() || !unit_iter->second ||
      unit_iter->second->get_view().status() !=
          PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE ||
      faction_iter == waiting_factions_.end()) {
    return false;
  }
  const size_t unit_size = static_cast<size_t>(unit_iter->second->get_data().users_size());
  unit_iter->second->refresh_view_from_room(*this);
  unit_iter->second->mark_terminal(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED, 0, now);
  protobuf_copy_message(admitted_units_[unit_id], unit_iter->second->get_data());
  erase_orbit_user_init_details(unit_iter->second->get_data());
  faction_iter->second.reserved_user_count -= unit_size;
  faction_iter->second.successful_user_count += unit_size;
  successful_user_count_ += unit_size;
  units_.erase(unit_iter);
  FWLOGINFO(
      "waiting Room Unit completed, matching_id={}, unit_id={}, faction_id={}, unit_users={}, users={}, "
      "successful_users={}, reserved_users={}, waiting_end_timepoint={}, now={}",
      matching_id_, unit_id, faction_iter->first, unit_size, user_count_, successful_user_count_,
      get_reserved_user_count(), waiting_end_timepoint_, now);
  return true;
}

void matching_room::mark_waiting_closed(int64_t now) {
  if (status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_WAITING && units_.empty()) {
    mark_finished(now);
  }
}

void matching_room::refresh_waiting_faction_assignments() {
  faction_assignments_.Clear();
  faction_count_by_capacity_.clear();
  fill_enabled_faction_capacities_.clear();
  completed_faction_count_ = 0;
  pending_faction_user_count_ = 0;
  for (const auto& value : waiting_factions_) {
    const auto& faction = value.second;
    const size_t users = faction.successful_user_count + faction.reserved_user_count;
    auto* assignment = faction_assignments_.Add();
    assignment->set_user_capacity(faction.user_capacity);
    assignment->set_assigned_user_count(static_cast<uint32_t>(users));
    for (uint64_t unit_id : faction.unit_ids) {
      assignment->add_unit_ids(unit_id);
    }
    ++faction_count_by_capacity_[faction.user_capacity];
    if (faction.fill_enabled) {
      fill_enabled_faction_capacities_.emplace(faction.user_capacity);
    }
    if (users == faction.user_capacity) {
      ++completed_faction_count_;
    }
    pending_faction_user_count_ += faction.user_capacity - users;
  }
}

void matching_room::extend_expire_time(int64_t value) noexcept { expire_time_ = std::max(expire_time_, value); }

void matching_room::mark_creating_battle(uint64_t orbit_server_id, int64_t expire_time) noexcept {
  assert(faction_assignments_.empty() || faction_ids_finalized_);
  const auto previous_status = status_;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE;
  orbit_server_id_ = orbit_server_id;
  confirm_expire_time_ = 0;
  battle_create_expire_time_ = expire_time;
  for (auto& unit : units_) {
    if (unit.second) {
      unit.second->data_.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_MATCHED);
    }
  }
  log_room_status_change(*this, previous_status, "begin_battle_creation");
}

uint64_t matching_room::get_orbit_server_id() const noexcept { return orbit_server_id_; }

bool matching_room::begin_orbit_ready(uint64_t source_server_id) noexcept {
  if (source_server_id == 0 || source_server_id != orbit_server_id_ ||
      status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE || orbit_ready_processing_) {
    return false;
  }
  orbit_ready_processing_ = true;
  return true;
}

void matching_room::mark_finished(int64_t now) {
  const auto previous_status = status_;
  const int64_t previous_battle_expire_time = battle_create_expire_time_;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED;
  result_ = 0;
  terminal_time_ = now;
  battle_create_expire_time_ = 0;
  log_room_status_change(*this, previous_status, "mark_finished", previous_battle_expire_time);
}

void matching_room::mark_failed(int32_t result, int64_t now) noexcept {
  const auto previous_status = status_;
  const int64_t previous_battle_expire_time = battle_create_expire_time_;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED;
  result_ = result;
  terminal_time_ = now;
  battle_create_expire_time_ = 0;
  for (auto& unit : units_) {
    if (unit.second) {
      unit.second->data_.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED);
    }
  }
  log_room_status_change(*this, previous_status, "mark_failed", previous_battle_expire_time);
}

void matching_room::mark_timeout(int64_t now) noexcept {
  const auto previous_status = status_;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT;
  terminal_time_ = now;
  for (auto& unit : units_) {
    if (unit.second) {
      unit.second->data_.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED);
    }
  }
  log_room_status_change(*this, previous_status, "mark_timeout");
}

void matching_room::mark_cancelled(int64_t now) noexcept {
  const auto previous_status = status_;
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED;
  terminal_time_ = now;
  for (auto& unit : units_) {
    if (unit.second) {
      unit.second->data_.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED);
    }
  }
  log_room_status_change(*this, previous_status, "mark_cancelled");
}

void matching_room::dump(PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& output) const {
  output.Clear();
  output.set_matching_id(matching_id_);
  protobuf_copy_message(*output.mutable_scope(), scope_);
  output.set_selected_level_id(selected_level_id_);
  output.set_status(status_);
  output.set_created_time(created_time_);
  output.set_expire_time(expire_time_);
  output.set_result(result_);
  output.set_last_event_id(last_event_id_);
  output.set_confirm_expire_time(confirm_expire_time_);
  for (const auto& unit : units_) {
    if (unit.second) {
      protobuf_copy_message(*output.add_units(), unit.second->data_);
    }
  }
  // 搜索阶段或搜索超时的 faction 仍可能局部调整，不作为对外承诺；进入确认后才同步最终战斗阵营。
  if (status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_WAITING ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED) {
    for (const auto& assignment : faction_assignments_) {
      protobuf_copy_message(*output.add_faction_assignments(), assignment);
    }
  }
  protobuf_copy_message(*output.mutable_orbit_room_key(), orbit_room_key_);
}

void matching_room::publish(rpc::context& ctx, PROJECT_NAMESPACE_ID::DMatchingEventLog&& event_log) {
  event_log.set_room_status(status_);
  event_log.set_event_id(++last_event_id_);
  FCTXLOGDEBUG(ctx, "record internal room event, matching_id={}, room_internal_event_id={}, event_case={}, status={}",
               matching_id_, last_event_id_, static_cast<int>(event_log.event_case()), static_cast<int>(status_));
}

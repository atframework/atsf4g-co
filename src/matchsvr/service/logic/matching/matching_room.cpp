// Copyright 2026 atframework

#include "logic/matching/matching_room.h"

#include "logic/matching/matching_utility.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <rpc/rpc_context.h>
#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <cassert>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
constexpr int32_t kFirstBattleFactionId = 1001;

struct faction_statistics {
  std::unordered_map<size_t, size_t> faction_count_by_capacity;
  std::set<size_t> fill_enabled_faction_capacities;
  size_t completed_faction_count = 0;
  size_t pending_user_count = 0;
};

bool calculate_faction_statistics(
    const std::unordered_map<uint64_t, PROJECT_NAMESPACE_ID::DMatchingUnit>& units,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>& assignments,
    faction_statistics& output) {
  faction_statistics result;
  if (assignments.empty()) {
    output = std::move(result);
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
      const auto& unit = unit_iter->second;
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
  output = std::move(result);
  return true;
}
}  // namespace

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
      result_template_id_(0),
      result_(0),
      orbit_ready_processing_(false),
      orbit_server_id_(0),
      wal_publisher_(create_matching_wal_publisher(*this)) {
  scope_.set_level_type(scope.level_type());
  scope_.set_region(scope.region());
  scope_.set_battle_version(scope.battle_version());
  scope_.set_matching_pool_id(scope.matching_pool_id());
  orbit_room_key_.set_client_id("room_key_" + matching_id_);
}

bool matching_room::has_unit(uint64_t unit_id) const noexcept { return units_.find(unit_id) != units_.end(); }

bool matching_room::has_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const noexcept {
  for (const auto& unit : units_) {
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
  auto iter = orbit_users_init_detail_.find(user_key.SerializeAsString());
  return iter == orbit_users_init_detail_.end() ? empty_detail : iter->second;
}

void matching_room::add_orbit_user_init_detail(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                               const PROJECT_NAMESPACE_ID::DOrbitUserInitDataDetail& detail,
                                               const std::string& user_open_id) {
  auto& data = orbit_users_init_detail_[user_key.SerializeAsString()];
  protobuf_copy_message(*data.mutable_orbit_init_data(), detail);
  data.set_user_open_id(user_open_id);
}

bool matching_room::add_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING || unit.unit_id() == 0 ||
      units_.find(unit.unit_id()) != units_.end()) {
    return false;
  }
  PROJECT_NAMESPACE_ID::DMatchingUnit normalized_unit;
  protobuf_copy_message(normalized_unit, unit);
  if (!matching_utility::normalize_acceptable_level_ids(normalized_unit)) {
    return false;
  }
  std::vector<const PROJECT_NAMESPACE_ID::DMatchingUnit*> prospective_units;
  prospective_units.reserve(units_.size() + 1);
  for (const auto& stored : units_) {
    prospective_units.emplace_back(&stored.second);
  }
  prospective_units.emplace_back(&normalized_unit);
  auto compatible_level_ids = matching_utility::get_compatible_level_ids(prospective_units);
  if (compatible_level_ids.empty()) {
    return false;
  }
  for (int left = 0; left < normalized_unit.users_size(); ++left) {
    const auto& user = normalized_unit.users(left);
    if (has_user(user.user_key())) {
      return false;
    }
    for (int right = left + 1; right < normalized_unit.users_size(); ++right) {
      if (matching_utility::same_user(user.user_key(), normalized_unit.users(right).user_key())) {
        return false;
      }
    }
  }
  const size_t unit_size = static_cast<size_t>(normalized_unit.users_size());
  auto& stored = units_[normalized_unit.unit_id()];
  protobuf_move_message(stored, std::move(normalized_unit));
  stored.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_SEARCHING);
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
  result_template_id_ = 0;
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
  const size_t unit_size = static_cast<size_t>(unit_iter->second.users_size());
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
  faction_assignments_.Swap(&remaining_assignments);
  faction_statistics statistics;
  const bool statistics_valid = calculate_faction_statistics(units_, faction_assignments_, statistics);
  assert(statistics_valid);
  if (statistics_valid) {
    faction_count_by_capacity_ = std::move(statistics.faction_count_by_capacity);
    fill_enabled_faction_capacities_ = std::move(statistics.fill_enabled_faction_capacities);
    completed_faction_count_ = statistics.completed_faction_count;
    pending_faction_user_count_ = statistics.pending_user_count;
  }
  result_template_id_ = 0;
  finalized_unit_faction_ids_.clear();
  faction_ids_finalized_ = false;
  std::vector<const PROJECT_NAMESPACE_ID::DMatchingUnit*> remaining_units;
  remaining_units.reserve(units_.size());
  for (const auto& stored : units_) {
    remaining_units.emplace_back(&stored.second);
  }
  compatible_level_ids_ = matching_utility::get_compatible_level_ids(remaining_units);
  if (!compatible_level_ids_.empty() && std::find(compatible_level_ids_.begin(), compatible_level_ids_.end(),
                                                  selected_level_id_) == compatible_level_ids_.end()) {
    selected_level_id_ = compatible_level_ids_.front();
  }
  return true;
}

void matching_room::begin_confirmation(int64_t expire_time) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING;
  confirm_expire_time_ = expire_time;
  for (auto& unit : units_) {
    for (auto& user : *unit.second.mutable_users()) {
      user.set_confirm_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING);
    }
  }
}

bool matching_room::confirm_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool accepted) noexcept {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return false;
  }
  for (auto& unit : units_) {
    for (auto& user : *unit.second.mutable_users()) {
      if (matching_utility::same_user(user.user_key(), user_key)) {
        user.set_confirm_status(accepted ? PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED
                                         : PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_REFUSED);
        return true;
      }
    }
  }
  return false;
}

bool matching_room::are_all_users_confirmed() const noexcept {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING || units_.empty()) {
    return false;
  }
  for (const auto& unit : units_) {
    for (const auto& user : unit.second.users()) {
      if (user.confirm_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED) {
        return false;
      }
    }
  }
  return true;
}

void matching_room::resume_matching(int64_t expire_time) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING;
  result_template_id_ = 0;
  confirm_expire_time_ = 0;
  expire_time_ = expire_time;
  finalized_unit_faction_ids_.clear();
  faction_ids_finalized_ = false;
  for (auto& unit : units_) {
    unit.second.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_SEARCHING);
    for (auto& user : *unit.second.mutable_users()) {
      user.set_confirm_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING);
    }
  }
}

void matching_room::set_result_template_id(int32_t value) noexcept { result_template_id_ = value; }

bool matching_room::set_faction_assignments(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>& value) {
  faction_statistics statistics;
  if (!calculate_faction_statistics(units_, value, statistics)) {
    return false;
  }
  faction_assignments_.Clear();
  faction_assignments_.Reserve(value.size());
  for (const auto& assignment : value) {
    protobuf_copy_message(*faction_assignments_.Add(), assignment);
  }
  faction_count_by_capacity_ = std::move(statistics.faction_count_by_capacity);
  fill_enabled_faction_capacities_ = std::move(statistics.fill_enabled_faction_capacities);
  completed_faction_count_ = statistics.completed_faction_count;
  pending_faction_user_count_ = statistics.pending_user_count;
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
  std::sort(ordered_factions.begin(), ordered_factions.end(), [](const auto* left, const auto* right) {
    return *std::min_element(left->unit_ids().begin(), left->unit_ids().end()) <
           *std::min_element(right->unit_ids().begin(), right->unit_ids().end());
  });

  std::unordered_map<uint64_t, int32_t> finalized_ids;
  finalized_ids.reserve(units_.size());
  int32_t faction_id = kFirstBattleFactionId;
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

void matching_room::extend_expire_time(int64_t value) noexcept { expire_time_ = std::max(expire_time_, value); }

void matching_room::mark_creating_battle(uint64_t orbit_server_id, int64_t expire_time) noexcept {
  assert(faction_assignments_.empty() || faction_ids_finalized_);
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE;
  orbit_server_id_ = orbit_server_id;
  confirm_expire_time_ = 0;
  battle_create_expire_time_ = expire_time;
  for (auto& unit : units_) {
    unit.second.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_MATCHED);
  }
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
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED;
  result_ = 0;
  terminal_time_ = now;
  battle_create_expire_time_ = 0;
}

void matching_room::mark_failed(int32_t result, int64_t now) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED;
  result_ = result;
  terminal_time_ = now;
  battle_create_expire_time_ = 0;
  for (auto& unit : units_) {
    unit.second.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED);
  }
}

void matching_room::mark_timeout(int64_t now) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT;
  terminal_time_ = now;
  for (auto& unit : units_) {
    unit.second.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED);
  }
}

void matching_room::mark_cancelled(int64_t now) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED;
  terminal_time_ = now;
  for (auto& unit : units_) {
    unit.second.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED);
  }
}

void matching_room::dump(PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& output) const {
  output.Clear();
  output.set_matching_id(matching_id_);
  protobuf_copy_message(*output.mutable_scope(), scope_);
  output.set_selected_level_id(selected_level_id_);
  output.set_status(status_);
  output.set_result_template_id(result_template_id_);
  output.set_created_time(created_time_);
  output.set_expire_time(expire_time_);
  output.set_result(result_);
  output.set_last_event_id(last_event_id_);
  output.set_confirm_expire_time(confirm_expire_time_);
  for (const auto& unit : units_) {
    protobuf_copy_message(*output.add_units(), unit.second);
  }
  // 搜索阶段或搜索超时的 faction 仍可能局部调整，不作为对外承诺；进入确认后才同步最终战斗阵营。
  if (status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED) {
    for (const auto& assignment : faction_assignments_) {
      protobuf_copy_message(*output.add_faction_assignments(), assignment);
    }
  }
  protobuf_copy_message(*output.mutable_orbit_room_key(), orbit_room_key_);
}

bool matching_room::dump_player_view(uint64_t unit_id, PROJECT_NAMESPACE_ID::DMatchingPlayerView& output) const {
  auto unit_iter = units_.find(unit_id);
  if (unit_iter == units_.end()) {
    return false;
  }

  dump_player_view(unit_iter->second, output);
  return true;
}

void matching_room::dump_player_view(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                                     PROJECT_NAMESPACE_ID::DMatchingPlayerView& output) const {
  output.Clear();
  output.set_matching_id(matching_id_);
  output.set_status(status_);
  protobuf_copy_message(*output.mutable_unit(), unit);
  output.set_result(result_);
  output.set_expire_time(expire_time_);
  output.set_last_event_id(last_event_id_);
  output.set_confirm_expire_time(confirm_expire_time_);
  protobuf_copy_message(*output.mutable_orbit_room_key(), orbit_room_key_);
  output.set_orbit_expired_timepoint(orbit_expired_timepoint_);
  output.set_selected_level_id(selected_level_id_);
  if (status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED ||
      status_ == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED) {
    const int32_t faction_id = get_unit_faction_id(unit.unit_id());
    if (faction_id != 0) {
      output.set_faction_id(faction_id);
    }
  }
}

bool matching_room::subscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, uint64_t server_id,
                              int64_t acknowledge_event_id) {
  if (!wal_publisher_ || server_id == 0) {
    FCTXLOGERROR(ctx, "subscribe matching WAL rejected, matching_id={}, user={}:{}, server_id={:#x}, wal_ready={}",
                 matching_id_, user_key.user_id(), user_key.zone_id(), server_id, static_cast<bool>(wal_publisher_));
    return false;
  }
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  auto subscriber = wal_publisher_->find_subscriber(user_key, wal_ctx);
  if (subscriber && subscriber->get_private_data()) {
    subscriber->get_private_data()->set_server_id(server_id);
    for (const auto& unit : units_) {
      if (matching_utility::unit_has_user(unit.second, user_key)) {
        subscriber->get_private_data()->set_unit_id(unit.first);
        break;
      }
    }
    wal_publisher_->receive_subscribe_request(user_key, acknowledge_event_id, atfw::util::time::time_utility::now(),
                                              wal_ctx);
    FCTXLOGDEBUG(ctx,
                 "refresh matching WAL subscriber, matching_id={}, user={}:{}, server_id={:#x}, "
                 "acknowledge_event_id={}, result={}",
                 matching_id_, user_key.user_id(), user_key.zone_id(), server_id, acknowledge_event_id, result);
    return result >= 0;
  }

  auto private_data = atfw::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::DMatchingSubscriberData>();
  if (!private_data) {
    FCTXLOGERROR(ctx, "create matching WAL subscriber data failed, matching_id={}, user={}:{}", matching_id_,
                 user_key.user_id(), user_key.zone_id());
    return false;
  }
  private_data->set_server_id(server_id);
  for (const auto& unit : units_) {
    if (matching_utility::unit_has_user(unit.second, user_key)) {
      private_data->set_unit_id(unit.first);
      break;
    }
  }
  private_data->set_last_send_event_id(acknowledge_event_id);
  private_data->set_valid_event_id_bound(last_event_id_ + 1);
  const bool created = static_cast<bool>(wal_publisher_->create_subscriber(
      user_key, atfw::util::time::time_utility::now(), acknowledge_event_id, wal_ctx, std::move(private_data)));
  if (created) {
    FCTXLOGDEBUG(ctx,
                 "create matching WAL subscriber, matching_id={}, user={}:{}, server_id={:#x}, "
                 "acknowledge_event_id={}",
                 matching_id_, user_key.user_id(), user_key.zone_id(), server_id, acknowledge_event_id);
  } else {
    FCTXLOGERROR(ctx, "create matching WAL subscriber failed, matching_id={}, user={}:{}, server_id={:#x}",
                 matching_id_, user_key.user_id(), user_key.zone_id(), server_id);
  }
  return created;
}

void matching_room::unsubscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  if (!wal_publisher_) {
    return;
  }
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  wal_publisher_->remove_subscriber(user_key, atfw::util::distributed_system::wal_unsubscribe_reason::kClientRequest,
                                    wal_ctx);
}

bool matching_room::get_subscriber_route(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, uint64_t& server_id,
                                         int64_t& acknowledge_event_id) {
  if (!wal_publisher_) {
    return false;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  auto subscriber = wal_publisher_->find_subscriber(user_key, wal_ctx);
  if (!subscriber || !subscriber->get_private_data()) {
    return false;
  }
  server_id = subscriber->get_private_data()->server_id();
  acknowledge_event_id = subscriber->get_private_data()->last_send_event_id();
  return server_id != 0;
}

void matching_room::publish(rpc::context& ctx, PROJECT_NAMESPACE_ID::DMatchingEventLog&& event_log) {
  if (!wal_publisher_) {
    FCTXLOGERROR(ctx, "publish matching event failed without WAL publisher, matching_id={}, event_case={}",
                 matching_id_, static_cast<int>(event_log.event_case()));
    return;
  }
  event_log.set_room_status(status_);
  event_log.set_event_id(++last_event_id_);
  const auto event_case = event_log.event_case();
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  auto log = wal_publisher_->allocate_log(atfw::util::time::time_utility::now(), event_log.event_case(), wal_ctx,
                                          std::move(event_log));
  if (wal_publisher_->emplace_back_log(std::move(log), wal_ctx) <
      atfw::util::distributed_system::wal_result_code::kOk) {
    FCTXLOGERROR(ctx, "publish matching event failed to append WAL, matching_id={}, event_id={}, status={}",
                 matching_id_, last_event_id_, static_cast<int>(status_));
    return;
  }
  FCTXLOGDEBUG(ctx, "publish matching event, matching_id={}, event_id={}, event_case={}, status={}", matching_id_,
               last_event_id_, static_cast<int>(event_case), static_cast<int>(status_));
  wal_publisher_->broadcast(wal_ctx);
  wal_publisher_->tick(atfw::util::time::time_utility::now(), wal_ctx);
}

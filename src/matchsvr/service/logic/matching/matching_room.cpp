// Copyright 2026 atframework

#include "logic/matching/matching_room.h"

#include "logic/matching/matching_utility.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <rpc/rpc_context.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

matching_room::matching_room(std::string matching_id, const PROJECT_NAMESPACE_ID::DMatchingScope& scope, int64_t now,
                             int64_t expire_time)
    : matching_id_(std::move(matching_id)),
      scope_(scope),
      status_(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING),
      created_time_(now),
      expire_time_(expire_time),
      terminal_time_(0),
      confirm_expire_time_(0),
      last_event_id_(0),
      result_template_id_(0),
      result_(0),
      wal_publisher_(create_matching_wal_publisher(*this)) {}

const std::string& matching_room::get_matching_id() const noexcept { return matching_id_; }
const PROJECT_NAMESPACE_ID::DMatchingScope& matching_room::get_scope() const noexcept { return scope_; }
PROJECT_NAMESPACE_ID::EnMatchingRoomStatus matching_room::get_status() const noexcept { return status_; }
int64_t matching_room::get_created_time() const noexcept { return created_time_; }
int64_t matching_room::get_expire_time() const noexcept { return expire_time_; }
int64_t matching_room::get_terminal_time() const noexcept { return terminal_time_; }
int64_t matching_room::get_confirm_expire_time() const noexcept { return confirm_expire_time_; }
int64_t matching_room::get_last_event_id() const noexcept { return last_event_id_; }
int32_t matching_room::get_result_template_id() const noexcept { return result_template_id_; }
const std::unordered_map<uint64_t, PROJECT_NAMESPACE_ID::DMatchingUnit>& matching_room::get_units() const noexcept {
  return units_;
}

size_t matching_room::get_player_count() const noexcept {
  size_t result = 0;
  for (const auto& unit : units_) {
    result += static_cast<size_t>(unit.second.players_size());
  }
  return result;
}

bool matching_room::has_unit(uint64_t unit_id) const noexcept { return units_.find(unit_id) != units_.end(); }

bool matching_room::has_player(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const noexcept {
  for (const auto& unit : units_) {
    for (const auto& player : unit.second.players()) {
      if (matching_utility::same_user(player.user_key(), user_key)) {
        return true;
      }
    }
  }
  return false;
}

bool matching_room::add_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING || unit.unit_id() == 0 ||
      units_.find(unit.unit_id()) != units_.end()) {
    return false;
  }
  for (int left = 0; left < unit.players_size(); ++left) {
    const auto& player = unit.players(left);
    if (has_player(player.user_key())) {
      return false;
    }
    for (int right = left + 1; right < unit.players_size(); ++right) {
      if (matching_utility::same_user(player.user_key(), unit.players(right).user_key())) {
        return false;
      }
    }
  }
  auto& stored = units_[unit.unit_id()];
  stored.CopyFrom(unit);
  stored.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_SEARCHING);
  return true;
}

bool matching_room::remove_unit(uint64_t unit_id) {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING &&
      status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return false;
  }
  return units_.erase(unit_id) > 0;
}

void matching_room::begin_confirmation(int64_t expire_time) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING;
  confirm_expire_time_ = expire_time;
  for (auto& unit : units_) {
    for (auto& player : *unit.second.mutable_players()) {
      player.set_confirm_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING);
    }
  }
}

bool matching_room::confirm_player(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool accepted) noexcept {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return false;
  }
  for (auto& unit : units_) {
    for (auto& player : *unit.second.mutable_players()) {
      if (matching_utility::same_user(player.user_key(), user_key)) {
        player.set_confirm_status(accepted ? PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED
                                           : PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_REFUSED);
        return true;
      }
    }
  }
  return false;
}

bool matching_room::are_all_players_confirmed() const noexcept {
  if (status_ != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING || units_.empty()) {
    return false;
  }
  for (const auto& unit : units_) {
    for (const auto& player : unit.second.players()) {
      if (player.confirm_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED) {
        return false;
      }
    }
  }
  return true;
}

void matching_room::resume_matching(int64_t expire_time) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING;
  confirm_expire_time_ = 0;
  expire_time_ = expire_time;
  result_template_id_ = 0;
  for (auto& unit : units_) {
    unit.second.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_SEARCHING);
    for (auto& player : *unit.second.mutable_players()) {
      player.set_confirm_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING);
    }
  }
}

void matching_room::set_result_template_id(int32_t value) noexcept { result_template_id_ = value; }

void matching_room::extend_expire_time(int64_t value) noexcept { expire_time_ = std::max(expire_time_, value); }

void matching_room::mark_creating_battle() noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE;
  confirm_expire_time_ = 0;
  for (auto& unit : units_) {
    unit.second.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_MATCHED);
  }
}

void matching_room::mark_finished(std::string battle_room_id, int64_t now) {
  battle_room_id_ = std::move(battle_room_id);
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED;
  result_ = 0;
  terminal_time_ = now;
}

void matching_room::mark_failed(int32_t result, int64_t now) noexcept {
  status_ = PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED;
  result_ = result;
  terminal_time_ = now;
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
  output.mutable_scope()->CopyFrom(scope_);
  output.set_status(status_);
  output.set_result_template_id(result_template_id_);
  output.set_created_time(created_time_);
  output.set_expire_time(expire_time_);
  output.set_battle_room_id(battle_room_id_);
  output.set_result(result_);
  output.set_last_event_id(last_event_id_);
  output.set_confirm_expire_time(confirm_expire_time_);
  for (const auto& unit : units_) {
    output.add_units()->CopyFrom(unit.second);
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

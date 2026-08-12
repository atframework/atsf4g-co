// Copyright 2026 atframework

#include "logic/matching/matching_manager.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel/config_easy_api.h>

#include <rpc/db/uuid.h>
#include <rpc/rpc_context.h>

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

#include "logic/matching/matching_logic.h"
#include "logic/matching/matching_utility.h"

namespace {
constexpr int64_t kDefaultSearchTimeout = 120;
constexpr int64_t kDefaultConfirmTimeout = 15;
constexpr int64_t kTerminalRetention = 60;

int64_t get_search_timeout_seconds(int32_t matching_pool_id) {
  auto pool = excel::get_ExcelMatchingPool_by_id(matching_pool_id);
  return pool && pool->search_timeout_seconds() > 0 ? pool->search_timeout_seconds() : kDefaultSearchTimeout;
}

int64_t get_confirm_timeout_seconds(int32_t matching_pool_id) {
  auto pool = excel::get_ExcelMatchingPool_by_id(matching_pool_id);
  return pool && pool->confirm_timeout_seconds() > 0 ? pool->confirm_timeout_seconds() : kDefaultConfirmTimeout;
}

PROJECT_NAMESPACE_ID::DMatchingEventLog make_add_unit_event(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  PROJECT_NAMESPACE_ID::DMatchingEventLog result;
  protobuf_copy_message(*result.mutable_add_unit(), unit);
  return result;
}

PROJECT_NAMESPACE_ID::DMatchingEventLog make_remove_unit_event(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                                                               const std::string& target_matching_id = {}) {
  PROJECT_NAMESPACE_ID::DMatchingEventLog result;
  protobuf_copy_message(*result.mutable_remove_unit()->mutable_unit(), unit);

  result.mutable_remove_unit()->set_switch_to_matching_id(target_matching_id);
  return result;
}
}  // namespace

bool matching_manager::bucket_key::operator<(const bucket_key& other) const noexcept {
  return std::tie(level_type, region, battle_version, matching_pool_id) <
         std::tie(other.level_type, other.region, other.battle_version, other.matching_pool_id);
}

bool matching_manager::queue_entry::operator<(const queue_entry& other) const noexcept {
  return std::tie(created_time, matching_id) < std::tie(other.created_time, other.matching_id);
}

bool matching_manager::user_key::operator==(const user_key& other) const noexcept {
  return user_id == other.user_id && zone_id == other.zone_id;
}

size_t matching_manager::user_key_hash::operator()(const user_key& value) const noexcept {
  const size_t first = std::hash<uint64_t>{}(value.user_id);
  const size_t second = std::hash<uint32_t>{}(value.zone_id);
  return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
}

matching_manager::matching_manager() : battle_start_handler_(stub_start_battle) {}
matching_manager::~matching_manager() = default;

int32_t matching_manager::init() {
  if (!battle_start_handler_) {
    battle_start_handler_ = stub_start_battle;
  }
  return 0;
}

int32_t matching_manager::tick() {
  const int64_t now = atfw::util::time::time_utility::get_now();
  rpc::context ctx{rpc::context::create_without_task()};
  std::vector<std::string> recycle_rooms;
  for (const auto& value : rooms_) {
    auto& room = value.second;
    if (!room) {
      recycle_rooms.emplace_back(value.first);
      continue;
    }
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      // 时间扩散可能在没有新请求时改变成局条件，因此每个 tick 都重新计算一次。
      evaluate_room(ctx, room, now);
      if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING &&
          now >= room->get_expire_time()) {
        unindex_room(*room);
        unindex_all_units(*room);
        room->mark_timeout(now);
        PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
        event_log.set_timeout(now);
        room->publish(ctx, std::move(event_log));
      }
    } else if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING &&
               now >= room->get_confirm_expire_time()) {
      handle_confirm_timeout(ctx, room, now);
    }
    if (room->get_terminal_time() > 0 && now - room->get_terminal_time() >= kTerminalRetention) {
      recycle_rooms.emplace_back(value.first);
    }
  }
  for (const auto& matching_id : recycle_rooms) {
    auto room_iter = rooms_.find(matching_id);
    if (room_iter != rooms_.end() && room_iter->second) {
      unindex_all_units(*room_iter->second);
    }
    rooms_.erase(matching_id);
  }
  return static_cast<int32_t>(recycle_rooms.size());
}

void matching_manager::clear() {
  searching_rooms_by_bucket_.clear();
  user_to_unit_.clear();
  unit_to_room_.clear();
  rooms_.clear();
}

int32_t matching_manager::create_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCreateReq& request,
                                          PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) {
  response.Clear();
  const auto& scope = request.scope();
  const auto& unit = request.unit();
  FCTXLOGDEBUG(ctx,
               "create matching, unit_id={}, user={}:{}, user_count={}, level_type={}, region={}, "
               "battle_version={}, matching_pool_id={}, subscriber_server_id={:#x}, acknowledge_event_id={}",
               unit.unit_id(), request.operator_user().user_id(), request.operator_user().zone_id(),
               unit.users_size(), scope.level_type(), scope.region(), scope.battle_version(),
               scope.matching_pool_id(), request.subscriber_server_id(), request.acknowledge_event_id());
  if (scope.level_type() <= 0 || scope.region().empty() || scope.battle_version().empty() ||
      scope.matching_pool_id() <= 0) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "create matching rejected by invalid scope, unit_id={}, result={}", unit.unit_id(),
                 response.result());
    return response.result();
  }
  if (request.operator_user().user_id() == 0 ||
      !matching_utility::unit_has_user(unit, request.operator_user()) ||
      !matching_utility::same_user(request.operator_user(), unit.captain_user_key())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "create matching rejected by invalid operator, unit_id={}, user={}:{}, result={}", unit.unit_id(),
                 request.operator_user().user_id(), request.operator_user().zone_id(), response.result());
    return response.result();
  }
  int32_t result = matching_logic::validate_unit(scope.matching_pool_id(), unit);
  if (result < 0) {
    response.set_result(result);
    FCTXLOGERROR(ctx, "create matching rejected by unit validation, unit_id={}, result={}", unit.unit_id(), result);
    return result;
  }
  if (unit_to_room_.find(unit.unit_id()) != unit_to_room_.end()) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "create matching rejected by duplicated unit, unit_id={}, result={}", unit.unit_id(),
                 response.result());
    return response.result();
  }
  for (const auto& user : unit.users()) {
    if (user_to_unit_.find(user_key{user.user_key().user_id(), user.user_key().zone_id()}) !=
        user_to_unit_.end()) {
      response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
      FCTXLOGERROR(ctx, "create matching rejected by duplicated user, unit_id={}, user={}:{}, result={}",
                   unit.unit_id(), user.user_key().user_id(), user.user_key().zone_id(), response.result());
      return response.result();
    }
  }

  const int64_t now = atfw::util::time::time_utility::get_now();
  matching_room::ptr_t selected_room;
  int32_t selected_result_template_id = 0;
  bool created_room = false;
  selected_room = find_joinable_room(scope, unit, now, nullptr, selected_result_template_id);

  if (!selected_room) {
    auto pool = excel::get_ExcelMatchingPool_by_id(scope.matching_pool_id());
    if (!pool) {
      response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
      FCTXLOGERROR(ctx, "create matching failed to find pool, unit_id={}, matching_pool_id={}, result={}",
                   unit.unit_id(), scope.matching_pool_id(), response.result());
      return response.result();
    }
    const int64_t timeout = get_search_timeout_seconds(scope.matching_pool_id());
    selected_room = std::make_shared<matching_room>(rpc::db::uuid::generate_short_uuid(), scope, now, now + timeout);
    created_room = true;
    FCTXLOGDEBUG(ctx, "create matching allocated room, matching_id={}, unit_id={}, expire_time={}",
                 selected_room->get_matching_id(), unit.unit_id(), now + timeout);
  } else {
    FCTXLOGDEBUG(ctx, "create matching selected existing room, matching_id={}, unit_id={}, current_user_count={}",
                 selected_room->get_matching_id(), unit.unit_id(), selected_room->get_user_count());
  }

  if (!selected_room->add_unit(unit)) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "create matching failed to add unit, matching_id={}, unit_id={}, status={}, result={}",
                 selected_room->get_matching_id(), unit.unit_id(), static_cast<int>(selected_room->get_status()),
                 response.result());
    return response.result();
  }
  selected_room->extend_expire_time(now + get_search_timeout_seconds(scope.matching_pool_id()));
  if (created_room) {
    rooms_.emplace(selected_room->get_matching_id(), selected_room);
    index_room(selected_room);
  }
  index_unit(selected_room->get_matching_id(), unit);
  selected_room->subscribe(ctx, request.operator_user(), request.subscriber_server_id(),
                           request.acknowledge_event_id());
  selected_room->publish(ctx, make_add_unit_event(unit));

  if (selected_result_template_id != 0) {
    selected_room->set_result_template_id(selected_result_template_id);
  }
  evaluate_room(ctx, selected_room, now);

  response.set_result(selected_room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED
                          ? PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED
                          : 0);
  selected_room->dump(*response.mutable_snapshot());
  FCTXLOGDEBUG(ctx,
               "create matching finish, matching_id={}, unit_id={}, created_room={}, user_count={}, status={}, "
               "result={}, last_event_id={}",
               selected_room->get_matching_id(), unit.unit_id(), created_room, selected_room->get_user_count(),
               static_cast<int>(selected_room->get_status()), response.result(), selected_room->get_last_event_id());
  return response.result();
}

int32_t matching_manager::cancel_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCancelReq& request,
                                          PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) {
  response.Clear();
  FCTXLOGDEBUG(ctx, "cancel matching, matching_id={}, unit_id={}, user={}:{}", request.matching_id(), request.unit_id(),
               request.operator_user().user_id(), request.operator_user().zone_id());
  auto room = find_room(request.matching_id(), request.unit_id());
  if (!room || request.unit_id() == 0 || !room->has_unit(request.unit_id()) ||
      !room->has_user(request.operator_user())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
    FCTXLOGERROR(ctx, "cancel matching failed to find room or user, matching_id={}, unit_id={}, result={}",
                 request.matching_id(), request.unit_id(), response.result());
    return response.result();
  }
  auto unit_iter = room->get_units().find(request.unit_id());
  if (unit_iter == room->get_units().end() ||
      !matching_utility::unit_has_user(unit_iter->second, request.operator_user())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
    FCTXLOGERROR(ctx, "cancel matching failed to find unit member, matching_id={}, unit_id={}, result={}",
                 request.matching_id(), request.unit_id(), response.result());
    return response.result();
  }
  if (room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "cancel matching rejected by room status, matching_id={}, unit_id={}, status={}, result={}",
                 room->get_matching_id(), request.unit_id(), static_cast<int>(room->get_status()), response.result());
    room->dump(*response.mutable_snapshot());
    return response.result();
  }

  PROJECT_NAMESPACE_ID::DMatchingUnit removed_unit;
  protobuf_copy_message(removed_unit, unit_iter->second);

  if (!room->remove_unit(request.unit_id())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "cancel matching failed to remove unit, matching_id={}, unit_id={}, status={}, result={}",
                 room->get_matching_id(), request.unit_id(), static_cast<int>(room->get_status()), response.result());
    return response.result();
  }
  unindex_unit(removed_unit);
  room->publish(ctx, make_remove_unit_event(removed_unit));
  if (room->get_units().empty()) {
    unindex_room(*room);
    room->mark_cancelled(atfw::util::time::time_utility::get_now());
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_cancel(atfw::util::time::time_utility::get_now());
    room->publish(ctx, std::move(event_log));
  } else {
    evaluate_room(ctx, room, atfw::util::time::time_utility::get_now());
  }
  response.set_result(0);
  room->dump(*response.mutable_snapshot());
  FCTXLOGDEBUG(ctx,
               "cancel matching finish, matching_id={}, unit_id={}, remaining_units={}, status={}, last_event_id={}",
               room->get_matching_id(), request.unit_id(), room->get_units().size(),
               static_cast<int>(room->get_status()), room->get_last_event_id());
  return response.result();
}

int32_t matching_manager::check_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
                                         PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) {
  response.Clear();
  FCTXLOGDEBUG(ctx,
               "check matching, matching_id={}, unit_id={}, user={}:{}, subscriber_server_id={:#x}, "
               "acknowledge_event_id={}",
               request.matching_id(), request.unit_id(), request.operator_user().user_id(),
               request.operator_user().zone_id(), request.subscriber_server_id(), request.acknowledge_event_id());
  auto room = find_room(request.matching_id(), request.unit_id());
  if (!room) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
    FCTXLOGERROR(ctx, "check matching failed to find room, matching_id={}, unit_id={}, result={}",
                 request.matching_id(), request.unit_id(), response.result());
    return response.result();
  }
  auto unit_iter = room->get_units().find(request.unit_id());
  if (unit_iter == room->get_units().end() ||
      !matching_utility::unit_has_user(unit_iter->second, request.operator_user())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
    FCTXLOGERROR(ctx, "check matching failed to find unit member, matching_id={}, unit_id={}, result={}",
                 request.matching_id(), request.unit_id(), response.result());
    return response.result();
  }
  room->subscribe(ctx, request.operator_user(), request.subscriber_server_id(), request.acknowledge_event_id());

  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    const int64_t now = atfw::util::time::time_utility::get_now();
    PROJECT_NAMESPACE_ID::DMatchingUnit moving_unit;
    protobuf_copy_message(moving_unit, unit_iter->second);

    int32_t result_template_id = 0;
    auto target_room = find_joinable_room(room->get_scope(), moving_unit, now, room.get(), result_template_id);
    if (target_room && move_unit(ctx, room, target_room, moving_unit, now)) {
      FCTXLOGDEBUG(ctx, "check matching moved unit, source_matching_id={}, target_matching_id={}, unit_id={}",
                   room->get_matching_id(), target_room->get_matching_id(), moving_unit.unit_id());
      if (result_template_id != 0) {
        target_room->set_result_template_id(result_template_id);
      }
      if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
        evaluate_room(ctx, room, now);
      }
      room = std::move(target_room);
    }
    evaluate_room(ctx, room, now);
  }
  response.set_result(0);
  room->dump(*response.mutable_snapshot());
  FCTXLOGDEBUG(ctx, "check matching finish, matching_id={}, unit_id={}, status={}, last_event_id={}",
               room->get_matching_id(), request.unit_id(), static_cast<int>(room->get_status()),
               room->get_last_event_id());
  return response.result();
}

int32_t matching_manager::confirm_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingConfirmReq& request,
                                           PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) {
  response.Clear();
  FCTXLOGDEBUG(ctx, "confirm matching, matching_id={}, unit_id={}, user={}:{}, confirmed={}", request.matching_id(),
               request.unit_id(), request.operator_user().user_id(), request.operator_user().zone_id(),
               request.confirmed());
  auto room = find_room(request.matching_id(), request.unit_id());
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
    FCTXLOGERROR(ctx, "confirm matching failed to find confirming room, matching_id={}, unit_id={}, result={}",
                 request.matching_id(), request.unit_id(), response.result());
    return response.result();
  }
  auto unit_iter = room->get_units().find(request.unit_id());
  if (unit_iter == room->get_units().end() ||
      !matching_utility::unit_has_user(unit_iter->second, request.operator_user()) ||
      !room->confirm_user(request.operator_user(), request.confirmed())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
    FCTXLOGERROR(ctx, "confirm matching failed to update user, matching_id={}, unit_id={}, user={}:{}, result={}",
                 request.matching_id(), request.unit_id(), request.operator_user().user_id(),
                 request.operator_user().zone_id(), response.result());
    return response.result();
  }
  room->subscribe(ctx, request.operator_user(), request.subscriber_server_id(), request.acknowledge_event_id());

  PROJECT_NAMESPACE_ID::DMatchingEventLog confirm_event;
  auto* confirm_data =
      request.confirmed() ? confirm_event.mutable_confirm_user() : confirm_event.mutable_refuse_confirm();
  protobuf_copy_message(*confirm_data->mutable_user_key(), request.operator_user());

  confirm_data->set_status(request.confirmed() ? PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED
                                               : PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_REFUSED);
  room->publish(ctx, std::move(confirm_event));

  const int64_t now = atfw::util::time::time_utility::get_now();
  if (!request.confirmed()) {
    PROJECT_NAMESPACE_ID::DMatchingUnit removed_unit;
    protobuf_copy_message(removed_unit, unit_iter->second);

    room->remove_unit(request.unit_id());
    unindex_unit(removed_unit);
    room->publish(ctx, make_remove_unit_event(removed_unit));
    if (room->get_units().empty()) {
      room->mark_cancelled(now);
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_cancel(now);
      room->publish(ctx, std::move(event_log));
    } else {
      room->resume_matching(now + get_search_timeout_seconds(room->get_scope().matching_pool_id()));
      index_room(room);
    }
  } else if (room->are_all_users_confirmed()) {
    start_battle(ctx, room, now);
  }

  response.set_result(0);
  room->dump(*response.mutable_snapshot());
  FCTXLOGDEBUG(ctx,
               "confirm matching finish, matching_id={}, unit_id={}, user={}:{}, confirmed={}, status={}, "
               "remaining_units={}, last_event_id={}",
               room->get_matching_id(), request.unit_id(), request.operator_user().user_id(),
               request.operator_user().zone_id(), request.confirmed(), static_cast<int>(room->get_status()),
               room->get_units().size(), room->get_last_event_id());
  return response.result();
}

void matching_manager::set_battle_start_handler(battle_start_handler_t handler) {
  battle_start_handler_ = handler ? std::move(handler) : stub_start_battle;
}

int32_t matching_manager::get_total_matching_user_count() const noexcept {
  int32_t result = 0;
  for (const auto& bucket : searching_rooms_by_bucket_) {
    for (const auto& entry : bucket.second) {
      auto room_iter = rooms_.find(entry.matching_id);
      if (room_iter != rooms_.end() && room_iter->second &&
          room_iter->second->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
        result += static_cast<int32_t>(room_iter->second->get_user_count());
      }
    }
  }
  return result;
}

size_t matching_manager::get_room_count() const noexcept { return rooms_.size(); }

matching_manager::bucket_key matching_manager::make_bucket_key(const PROJECT_NAMESPACE_ID::DMatchingScope& scope) {
  return bucket_key{scope.level_type(), scope.region(), scope.battle_version(), scope.matching_pool_id()};
}

matching_room::ptr_t matching_manager::find_room(const std::string& matching_id, uint64_t unit_id) const {
  if (!matching_id.empty()) {
    auto room_iter = rooms_.find(matching_id);
    if (room_iter != rooms_.end() && room_iter->second && (unit_id == 0 || room_iter->second->has_unit(unit_id))) {
      return room_iter->second;
    }
  }
  auto unit_iter = unit_to_room_.find(unit_id);
  if (unit_iter == unit_to_room_.end()) {
    return nullptr;
  }
  auto room_iter = rooms_.find(unit_iter->second);
  return room_iter == rooms_.end() ? nullptr : room_iter->second;
}

matching_room::ptr_t matching_manager::find_joinable_room(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                                          const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
                                                          const matching_room* source_room,
                                                          int32_t& result_template_id) const {
  result_template_id = 0;
  auto bucket_iter = searching_rooms_by_bucket_.find(make_bucket_key(scope));
  if (bucket_iter == searching_rooms_by_bucket_.end()) {
    return nullptr;
  }

  const int32_t global_matching_users = get_total_matching_user_count();
  for (const auto& entry : bucket_iter->second) {
    auto room_iter = rooms_.find(entry.matching_id);
    if (room_iter == rooms_.end() || !room_iter->second || room_iter->second.get() == source_room ||
        room_iter->second->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      continue;
    }
    if (source_room != nullptr && room_iter->second->get_user_count() + static_cast<size_t>(unit.users_size()) <=
                                      source_room->get_user_count()) {
      continue;
    }
    auto check = matching_logic::check_unit_can_join(*room_iter->second, unit, now, global_matching_users);
    if (!check.can_join) {
      continue;
    }
    result_template_id = check.result_template_id;
    return room_iter->second;
  }
  return nullptr;
}

bool matching_manager::move_unit(rpc::context& ctx, const matching_room::ptr_t& source_room,
                                 const matching_room::ptr_t& target_room,
                                 const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now) {
  if (!source_room || !target_room || source_room == target_room || !source_room->has_unit(unit.unit_id()) ||
      !target_room->add_unit(unit)) {
    FCTXLOGERROR(ctx, "move matching unit rejected, source_matching_id={}, target_matching_id={}, unit_id={}",
                 source_room ? source_room->get_matching_id() : std::string{},
                 target_room ? target_room->get_matching_id() : std::string{}, unit.unit_id());
    return false;
  }
  if (!source_room->remove_unit(unit.unit_id())) {
    target_room->remove_unit(unit.unit_id());
    FCTXLOGERROR(ctx,
                 "move matching unit failed to remove source, source_matching_id={}, target_matching_id={}, "
                 "unit_id={}",
                 source_room->get_matching_id(), target_room->get_matching_id(), unit.unit_id());
    return false;
  }

  target_room->extend_expire_time(now + get_search_timeout_seconds(target_room->get_scope().matching_pool_id()));
  index_unit(target_room->get_matching_id(), unit);
  for (const auto& user : unit.users()) {
    uint64_t server_id = 0;
    int64_t acknowledge_event_id = 0;
    if (source_room->get_subscriber_route(user.user_key(), server_id, acknowledge_event_id)) {
      target_room->subscribe(ctx, user.user_key(), server_id, 0);
      source_room->unsubscribe(ctx, user.user_key());
    }
  }
  source_room->publish(ctx, make_remove_unit_event(unit, target_room->get_matching_id()));
  target_room->publish(ctx, make_add_unit_event(unit));
  if (source_room->get_units().empty()) {
    unindex_room(*source_room);
    source_room->mark_cancelled(now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_cancel(now);
    source_room->publish(ctx, std::move(event_log));
  }
  FCTXLOGDEBUG(ctx,
               "move matching unit finish, source_matching_id={}, target_matching_id={}, unit_id={}, "
               "source_units={}, target_units={}",
               source_room->get_matching_id(), target_room->get_matching_id(), unit.unit_id(),
               source_room->get_units().size(), target_room->get_units().size());
  return true;
}

void matching_manager::index_unit(const std::string& matching_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  unit_to_room_[unit.unit_id()] = matching_id;
  for (const auto& user : unit.users()) {
    user_to_unit_[user_key{user.user_key().user_id(), user.user_key().zone_id()}] = unit.unit_id();
  }
}

void matching_manager::unindex_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  unit_to_room_.erase(unit.unit_id());
  for (const auto& user : unit.users()) {
    user_to_unit_.erase(user_key{user.user_key().user_id(), user.user_key().zone_id()});
  }
}

void matching_manager::index_room(const matching_room::ptr_t& room) {
  searching_rooms_by_bucket_[make_bucket_key(room->get_scope())].insert(
      queue_entry{room->get_created_time(), room->get_matching_id()});
}

void matching_manager::unindex_room(const matching_room& room) {
  auto bucket_iter = searching_rooms_by_bucket_.find(make_bucket_key(room.get_scope()));
  if (bucket_iter == searching_rooms_by_bucket_.end()) {
    return;
  }
  bucket_iter->second.erase(queue_entry{room.get_created_time(), room.get_matching_id()});
  if (bucket_iter->second.empty()) {
    searching_rooms_by_bucket_.erase(bucket_iter);
  }
}

void matching_manager::unindex_all_units(const matching_room& room) {
  for (const auto& unit : room.get_units()) {
    unindex_unit(unit.second);
  }
}

void matching_manager::start_battle(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now) {
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return;
  }
  unindex_room(*room);
  room->mark_creating_battle();
  FCTXLOGDEBUG(ctx, "start battle for matching, matching_id={}, user_count={}, result_template_id={}",
               room->get_matching_id(), room->get_user_count(), room->get_result_template_id());

  PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
  room->dump(snapshot);
  std::string battle_room_id;
  const int32_t result = battle_start_handler_ ? battle_start_handler_(snapshot, battle_room_id)
                                               : PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED;
  if (result < 0) {
    // 战斗创建失败后立即释放活动索引，允许 unit 重新发起匹配。
    unindex_all_units(*room);
    room->mark_failed(result, now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(result);
    room->publish(ctx, std::move(event_log));
    FCTXLOGERROR(ctx, "start battle for matching failed, matching_id={}, result={}", room->get_matching_id(), result);
  } else {
    // 创建成功后保留 unit 到终态房间的查询索引，房间回收时再释放。
    const std::string created_battle_room_id = battle_room_id;
    room->mark_finished(std::move(battle_room_id), now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    room->dump(*event_log.mutable_matched());
    room->publish(ctx, std::move(event_log));
    FCTXLOGDEBUG(ctx, "start battle for matching finish, matching_id={}, battle_room_id={}, status={}",
                 room->get_matching_id(), created_battle_room_id, static_cast<int>(room->get_status()));
  }
}

void matching_manager::evaluate_room(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now) {
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    return;
  }
  auto result = matching_logic::check_room_ready(*room, now, get_total_matching_user_count());
  if (result.result == 0 && result.result_template_id != 0) {
    room->set_result_template_id(result.result_template_id);
  }
  if (result.ready) {
    unindex_room(*room);
    room->begin_confirmation(now + get_confirm_timeout_seconds(room->get_scope().matching_pool_id()));
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_notify_confirm(room->get_confirm_expire_time());
    room->publish(ctx, std::move(event_log));
    FCTXLOGDEBUG(ctx,
                 "matching room ready for confirmation, matching_id={}, user_count={}, result_template_id={}, "
                 "confirm_expire_time={}",
                 room->get_matching_id(), room->get_user_count(), room->get_result_template_id(),
                 room->get_confirm_expire_time());
  }
}

void matching_manager::handle_confirm_timeout(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now) {
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return;
  }
  FCTXLOGDEBUG(ctx, "handle matching confirmation timeout, matching_id={}, units={}, user_count={}",
               room->get_matching_id(), room->get_units().size(), room->get_user_count());
  std::vector<PROJECT_NAMESPACE_ID::DMatchingUnit> removed_units;
  for (const auto& unit : room->get_units()) {
    const bool has_unconfirmed =
        std::any_of(unit.second.users().begin(), unit.second.users().end(), [](const auto& user) {
          return user.confirm_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED;
        });
    if (has_unconfirmed) {
      removed_units.emplace_back(unit.second);
    }
  }
  for (const auto& unit : removed_units) {
    for (const auto& user : unit.users()) {
      if (user.confirm_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED) {
        continue;
      }
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      protobuf_copy_message(*event_log.mutable_refuse_confirm()->mutable_user_key(), user.user_key());

      event_log.mutable_refuse_confirm()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_TIMEOUT);
      room->publish(ctx, std::move(event_log));
    }
    room->remove_unit(unit.unit_id());
    unindex_unit(unit);
    room->publish(ctx, make_remove_unit_event(unit));
  }
  if (room->get_units().empty()) {
    room->mark_timeout(now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_timeout(now);
    room->publish(ctx, std::move(event_log));
    FCTXLOGDEBUG(ctx, "matching confirmation timeout finished with empty room, matching_id={}, status={}",
                 room->get_matching_id(), static_cast<int>(room->get_status()));
  } else {
    room->resume_matching(now + get_search_timeout_seconds(room->get_scope().matching_pool_id()));
    index_room(room);
    FCTXLOGDEBUG(ctx,
                 "matching confirmation timeout resumed search, matching_id={}, remaining_units={}, expire_time={}",
                 room->get_matching_id(), room->get_units().size(), room->get_expire_time());
  }
}

int32_t matching_manager::stub_start_battle(const PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& snapshot,
                                            std::string& battle_room_id) {
  battle_room_id = "battle-stub-" + snapshot.matching_id();
  FWLOGINFO("battlesvr stub accepted matching {}, users will be forwarded after battlesvr is implemented",
            snapshot.matching_id());
  return 0;
}

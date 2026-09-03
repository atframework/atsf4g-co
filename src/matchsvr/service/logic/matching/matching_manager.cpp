// Copyright 2026 atframework

#include "logic/matching/matching_manager.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.level.config.pb.h>
#include <protocol/config/com.struct.matching.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel/config_easy_api.h>

#include <logic/logic_server_setup.h>
#include <rpc/db/uuid.h>
#include <rpc/matching/matchsvrnotifyservice.atfw.gen.h>
#include <rpc/orbit/orbitsvrservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "logic/matching/matching_logic.h"
#include "logic/matching/matching_utility.h"

#ifdef min
#  undef min
#endif

bool matching_manager::bucket_key::operator<(const bucket_key& other) const noexcept {
  return std::tie(level_type, region, battle_version, matching_pool_id) <
         std::tie(other.level_type, other.region, other.battle_version, other.matching_pool_id);
}

bool matching_manager::migration_candidate::operator<(const migration_candidate& other) const noexcept {
  if (complete_faction != other.complete_faction) {
    return complete_faction;
  }
  return unit_ids < other.unit_ids;
}

bool matching_manager::queue_entry::operator<(const queue_entry& other) const noexcept {
  return std::tie(created_time, matching_id) < std::tie(other.created_time, other.matching_id);
}

bool matching_manager::queue_entry::room_precedes(const matching_room::ptr_t& left,
                                                  const matching_room::ptr_t& right) noexcept {
  if (left->get_created_time() != right->get_created_time()) {
    return left->get_created_time() < right->get_created_time();
  }
  return left->get_matching_id() < right->get_matching_id();
}

bool matching_manager::user_key::operator==(const user_key& other) const noexcept {
  return user_id == other.user_id && zone_id == other.zone_id;
}

size_t matching_manager::user_key_hash::operator()(const user_key& value) const noexcept {
  const size_t first = std::hash<uint64_t>{}(value.user_id);
  const size_t second = std::hash<uint32_t>{}(value.zone_id);
  return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
}

matching_manager::matching_manager() = default;
matching_manager::~matching_manager() = default;

int32_t matching_manager::init() { return 0; }

int32_t matching_manager::tick() {
  const int64_t now = atfw::util::time::time_utility::get_now();
  rpc::context ctx = logic_server_get_current_tick_context();
  if (now != last_heartbeat_check_time_) {
    last_heartbeat_check_time_ = now;
    std::vector<uint64_t> expired_unit_ids;
    for (const auto& value : units_) {
      if (!value.second) {
        continue;
      }
      auto room = value.second->get_room();
      if (room && room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING &&
          value.second->is_heartbeat_expired(now - matching_logic::get_unit_heartbeat_timeout_seconds())) {
        expired_unit_ids.emplace_back(value.first);
      }
    }
    std::sort(expired_unit_ids.begin(), expired_unit_ids.end());
    for (uint64_t unit_id : expired_unit_ids) {
      auto unit = find_unit(unit_id);
      if (unit) {
        handle_unit_heartbeat_timeout(ctx, unit, now);
      }
    }
  }

  std::vector<std::string> recycle_rooms;
  std::vector<matching_room::ptr_t> rebalance_targets;
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
        FCTXLOGDEBUG(ctx,
                     "matching room search timeout, matching_id={}, units={}, users={}, created_time={}, "
                     "expire_time={}, now={}",
                     room->get_matching_id(), room->get_units().size(), room->get_user_count(),
                     room->get_created_time(), room->get_expire_time(), now);
        unindex_room(*room);
        unindex_all_units(*room);
        room->mark_timeout(now);
        PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
        event_log.set_timeout(now);
        publish_room_event(ctx, room, std::move(event_log));
      }
      if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
        rebalance_targets.emplace_back(room);
      }
    } else if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING &&
               now >= room->get_confirm_expire_time()) {
      handle_confirm_timeout(ctx, room, now);
    } else if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE &&
               now >= room->get_battle_create_expire_time()) {
      handle_battle_create_timeout(ctx, room, now);
    }
    if (room->get_terminal_time() > 0 &&
        now - room->get_terminal_time() >= matching_logic::get_terminal_retention_seconds()) {
      recycle_rooms.emplace_back(value.first);
    }
  }
  std::sort(rebalance_targets.begin(), rebalance_targets.end(), queue_entry::room_precedes);
  size_t remaining_migration_count = matching_logic::get_max_rebalance_migrations_per_tick();
  for (const auto& target_room : rebalance_targets) {
    if (remaining_migration_count == 0) {
      break;
    }
    if (target_room && target_room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      const size_t target_migration_count = rebalance_room(
          ctx, target_room, now,
          (std::min)(matching_logic::get_max_rebalance_migrations_per_target(), remaining_migration_count));
      remaining_migration_count -= (std::min)(target_migration_count, remaining_migration_count);
    }
  }
  for (const auto& matching_id : recycle_rooms) {
    auto room_iter = rooms_.find(matching_id);
    if (room_iter != rooms_.end() && room_iter->second) {
      FCTXLOGDEBUG(
          ctx, "recycle matching room, matching_id={}, status={}, units={}, users={}, terminal_time={}, now={}",
          matching_id, static_cast<int>(room_iter->second->get_status()), room_iter->second->get_units().size(),
          room_iter->second->get_user_count(), room_iter->second->get_terminal_time(), now);
      unindex_all_units(*room_iter->second);
    }
    rooms_.erase(matching_id);
  }
  std::vector<uint64_t> recycle_units;
  for (const auto& value : units_) {
    if (!value.second) {
      recycle_units.emplace_back(value.first);
      continue;
    }
    if (value.second->get_terminal_time() > 0 &&
        now - value.second->get_terminal_time() >= matching_logic::get_terminal_retention_seconds()) {
      recycle_units.emplace_back(value.first);
    }
  }
  for (uint64_t unit_id : recycle_units) {
    units_.erase(unit_id);
  }
  return static_cast<int32_t>(recycle_rooms.size());
}

void matching_manager::clear() {
  searching_rooms_by_bucket_.clear();
  user_to_unit_.clear();
  unit_to_room_.clear();
  rooms_.clear();
  units_.clear();
  last_heartbeat_check_time_ = 0;
}

int32_t matching_manager::create_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCreateReq& request,
                                          PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) {
  response.Clear();
  const auto& scope = request.scope();
  PROJECT_NAMESPACE_ID::DMatchingUnit unit;
  protobuf_copy_message(unit, request.unit());
  if (!matching_utility::normalize_acceptable_level_ids(unit)) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "create matching rejected by invalid acceptable levels, unit_id={}, result={}", unit.unit_id(),
                 response.result());
    return response.result();
  }
  const int32_t initial_level_id = unit.acceptable_level_ids(0);
  FCTXLOGDEBUG(ctx,
               "create matching, unit_id={}, user={}:{}, user_count={}, level_type={}, region={}, "
               "battle_version={}, matching_pool_id={}, acceptable_level_count={}, subscriber_route_count={}",
               unit.unit_id(), request.operator_user().user_id(), request.operator_user().zone_id(), unit.users_size(),
               scope.level_type(), scope.region(), scope.battle_version(), scope.matching_pool_id(),
               unit.acceptable_level_ids_size(), request.subscriber_routes_size());
  if (scope.level_type() <= 0 || scope.region().empty() || scope.battle_version().empty() ||
      scope.matching_pool_id() <= 0) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "create matching rejected by invalid scope, unit_id={}, result={}", unit.unit_id(),
                 response.result());
    return response.result();
  }
  for (int32_t level_id : unit.acceptable_level_ids()) {
    const auto level_cfg = excel::get_ExcelLevel_by_level_id(level_id);
    if (!level_cfg || level_cfg->level_type() != scope.level_type() ||
        level_cfg->matching_pool_id() != scope.matching_pool_id()) {
      response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
      FCTXLOGERROR(ctx,
                   "create matching rejected by level candidate configuration, unit_id={}, level_id={}, "
                   "level_type={}, matching_pool_id={}, result={}",
                   unit.unit_id(), level_id, scope.level_type(), scope.matching_pool_id(), response.result());
      return response.result();
    }
  }
  // 操作人合法检查
  if (request.operator_user().user_id() == 0 ||
      !matching_utility::same_user(request.operator_user(), unit.captain_user_key())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "create matching rejected by invalid operator, unit_id={}, user={}:{}, result={}", unit.unit_id(),
                 request.operator_user().user_id(), request.operator_user().zone_id(), response.result());
    return response.result();
  }
  // 队伍合法检查
  int32_t result = matching_logic::validate_unit(scope.matching_pool_id(), unit);
  if (result < 0) {
    response.set_result(result);
    FCTXLOGERROR(ctx, "create matching rejected by unit validation, unit_id={}, result={}", unit.unit_id(), result);
    return result;
  }

  auto runtime_unit = std::make_shared<matching_unit>(unit);
  if (!runtime_unit || !runtime_unit->validate_subscriber_routes(request.subscriber_routes())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "create matching rejected by invalid subscriber routes, unit_id={}, route_count={}, result={}",
                 unit.unit_id(), request.subscriber_routes_size(), response.result());
    return response.result();
  }

  // unit和玩家是否在匹配
  if (unit_to_room_.find(unit.unit_id()) != unit_to_room_.end()) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_USER_ALREADY_IN_MATCHING);
    FCTXLOGERROR(ctx, "create matching rejected by duplicated unit, unit_id={}, result={}", unit.unit_id(),
                 response.result());
    return response.result();
  }
  for (const auto& user : unit.users()) {
    if (user_to_unit_.find(user_key{user.user_key().user_id(), user.user_key().zone_id()}) != user_to_unit_.end()) {
      response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_USER_ALREADY_IN_MATCHING);
      FCTXLOGERROR(ctx, "create matching rejected by duplicated user, unit_id={}, user={}:{}, result={}",
                   unit.unit_id(), user.user_key().user_id(), user.user_key().zone_id(), response.result());
      return response.result();
    }
  }

  const int64_t now = atfw::util::time::time_utility::get_now();
  auto room_selection = find_joinable_room(ctx, scope, unit, now);
  auto selected_room = room_selection.room;
  bool created_room = false;

  if (!selected_room) {
    auto initial_evaluation = matching_logic::check_unit_can_create_room(
        scope, unit, now, get_total_matching_user_count() + unit.users_size());
    if (!initial_evaluation.evaluation.can_join()) {
      response.set_result(initial_evaluation.evaluation.result() != 0
                              ? initial_evaluation.evaluation.result()
                              : PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
      FCTXLOGERROR(ctx,
                   "create matching rejected because unit cannot fit any result template, unit_id={}, user_count={}, "
                   "matching_pool_id={}, result={}",
                   unit.unit_id(), unit.users_size(), scope.matching_pool_id(), response.result());
      return response.result();
    }
    const int64_t timeout = matching_logic::get_search_timeout_seconds(scope.matching_pool_id());
    selected_room = std::make_shared<matching_room>(rpc::db::uuid::generate_standard_uuid(), scope, initial_level_id,
                                                    now, now + timeout);
    protobuf_move_message(room_selection.evaluation, std::move(initial_evaluation.evaluation));
    created_room = true;
    FCTXLOGDEBUG(ctx, "create matching allocated room, matching_id={}, unit_id={}, expire_time={}",
                 selected_room->get_matching_id(), unit.unit_id(), now + timeout);
  } else {
    FCTXLOGDEBUG(ctx, "create matching selected existing room, matching_id={}, unit_id={}, current_user_count={}",
                 selected_room->get_matching_id(), unit.unit_id(), selected_room->get_user_count());
  }

  if (!selected_room->add_unit(runtime_unit)) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "create matching failed to add unit, matching_id={}, unit_id={}, status={}, result={}",
                 selected_room->get_matching_id(), unit.unit_id(), static_cast<int>(selected_room->get_status()),
                 response.result());
    return response.result();
  }
  selected_room->extend_expire_time(now + matching_logic::get_search_timeout_seconds(scope.matching_pool_id()));
  const bool assignments_committed =
      selected_room->set_faction_assignments(room_selection.evaluation.faction_assignments());
  if (!assignments_committed) {
    selected_room->remove_unit(unit.unit_id());
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
    FCTXLOGERROR(ctx,
                 "create matching failed to commit faction assignments, matching_id={}, unit_id={}, "
                 "created_room={}, result={}",
                 selected_room->get_matching_id(), unit.unit_id(), created_room, response.result());
    return response.result();
  }
  if (created_room) {
    rooms_.emplace(selected_room->get_matching_id(), selected_room);
    index_room(selected_room);
  }
  runtime_unit->bind_room(selected_room);
  runtime_unit->refresh_view_from_room(*selected_room);
  if (!runtime_unit->initialize_subscribers(ctx, request.subscriber_routes())) {
    selected_room->remove_unit(unit.unit_id());
    if (created_room) {
      unindex_room(*selected_room);
      rooms_.erase(selected_room->get_matching_id());
    }
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED);
    FCTXLOGERROR(ctx, "create matching failed to initialize Unit subscribers, matching_id={}, unit_id={}, result={}",
                 selected_room->get_matching_id(), unit.unit_id(), response.result());
    return response.result();
  }
  units_.emplace(unit.unit_id(), runtime_unit);
  index_unit(selected_room->get_matching_id(), unit);
  publish_room_event(ctx, selected_room, matching_logic::make_add_unit_event(unit));

  evaluate_room(ctx, selected_room, now);

  response.set_result(selected_room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED
                          ? PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED
                          : 0);
  response.set_matching_id(selected_room->get_matching_id());
  protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
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
  FCTXLOGDEBUG(ctx, "cancel matching, unit_id={}, user={}:{}", request.unit_id(), request.operator_user().user_id(),
               request.operator_user().zone_id());
  auto runtime_unit = find_unit(request.unit_id());
  auto room = runtime_unit ? runtime_unit->get_room() : nullptr;
  if (!room || request.unit_id() == 0 || !room->has_unit(request.unit_id()) ||
      !room->has_user(request.operator_user())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND);
    FCTXLOGERROR(ctx, "cancel matching failed to find room or user, unit_id={}, result={}", request.unit_id(),
                 response.result());
    return response.result();
  }
  response.set_matching_id(room->get_matching_id());
  auto unit_iter = room->get_units().find(request.unit_id());
  if (unit_iter == room->get_units().end() || !unit_iter->second ||
      !matching_utility::unit_has_user(unit_iter->second->get_data(), request.operator_user())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND);
    FCTXLOGERROR(ctx, "cancel matching failed to find unit member, matching_id={}, unit_id={}, result={}",
                 room->get_matching_id(), request.unit_id(), response.result());
    return response.result();
  }
  if (room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    // 取消匹配只能在匹配中状态下进行
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CANNOT_CANCEL);
    FCTXLOGERROR(ctx, "cancel matching rejected by room status, matching_id={}, unit_id={}, status={}, result={}",
                 room->get_matching_id(), request.unit_id(), static_cast<int>(room->get_status()), response.result());
    runtime_unit->refresh_view_from_room(*room);
    protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
    return response.result();
  }

  auto remove_event = matching_logic::make_remove_unit_event(unit_iter->second->get_data());
  protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());

  if (!room->remove_unit(request.unit_id())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "cancel matching failed to remove unit, matching_id={}, unit_id={}, status={}, result={}",
                 room->get_matching_id(), request.unit_id(), static_cast<int>(room->get_status()), response.result());
    return response.result();
  }
  unindex_unit(remove_event.remove_unit().unit());
  if (room->get_units().empty()) {
    unindex_room(*room);
    room->mark_cancelled(atfw::util::time::time_utility::get_now());
    publish_room_event(ctx, room, std::move(remove_event));
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_cancel(atfw::util::time::time_utility::get_now());
    publish_room_event(ctx, room, std::move(event_log));
  } else {
    publish_room_event(ctx, room, std::move(remove_event));
    evaluate_room(ctx, room, atfw::util::time::time_utility::get_now());
  }
  response.set_result(0);
  protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
  FCTXLOGDEBUG(ctx,
               "cancel matching finish, matching_id={}, unit_id={}, remaining_units={}, status={}, last_event_id={}",
               room->get_matching_id(), request.unit_id(), room->get_units().size(),
               static_cast<int>(room->get_status()), room->get_last_event_id());
  return response.result();
}

int32_t matching_manager::check_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
                                         PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response,
                                         uint64_t source_server_id) {
  response.Clear();
  FCTXLOGDEBUG(ctx, "matching heartbeat, unit_id={}, subscriber_server_id={:#x}, user={}:{}", request.unit_id(),
               request.subscriber_server_id(), request.heartbeat_data().user_key().user_id(),
               request.heartbeat_data().user_key().zone_id());
  if (source_server_id != 0 && request.subscriber_server_id() != source_server_id) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx,
                 "matching heartbeat rejected mismatched source, unit_id={}, subscriber_server_id={:#x}, "
                 "source_server_id={:#x}",
                 request.unit_id(), request.subscriber_server_id(), source_server_id);
    return response.result();
  }
  auto runtime_unit = find_unit(request.unit_id());
  if (!runtime_unit) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND);
    FCTXLOGERROR(ctx, "matching heartbeat failed to find Unit, unit_id={}, result={}", request.unit_id(),
                 response.result());
    return response.result();
  }
  if (!request.has_heartbeat_data() ||
      !runtime_unit->heartbeat(ctx, request.subscriber_server_id(), request.heartbeat_data())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "matching heartbeat rejected, unit_id={}, subscriber_server_id={:#x}, user={}:{}",
                 request.unit_id(), request.subscriber_server_id(), request.heartbeat_data().user_key().user_id(),
                 request.heartbeat_data().user_key().zone_id());
    return response.result();
  }

  auto room = runtime_unit->get_room();
  if (room) {
    response.set_matching_id(room->get_matching_id());
  }
  if (runtime_unit->is_terminal()) {
    response.set_result(0);
    protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
    return response.result();
  }
  if (!room || !room->has_unit(request.unit_id())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND);
    FCTXLOGERROR(ctx, "matching heartbeat failed to find active Unit membership, unit_id={}, result={}",
                 request.unit_id(), response.result());
    return response.result();
  }
  protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());

  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    const int64_t now = atfw::util::time::time_utility::get_now();
    // 规则时间窗可能让当前房间直接满足动态模板；先成局，避免 ready 房间被 rebalance 拆走。
    evaluate_room(ctx, room, now);
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING &&
        rebalance_room(ctx, room, now, matching_logic::get_max_rebalance_migrations_per_target()) > 0) {
      room = runtime_unit->get_room();
      if (!room) {
        response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_NOT_FOUND);
        FCTXLOGERROR(ctx, "check matching lost unit after rebalance, unit_id={}, result={}", request.unit_id(),
                     response.result());
        return response.result();
      }
    }
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      evaluate_room(ctx, room, now);
    }
  }
  response.set_result(0);
  if (room->has_unit(request.unit_id())) {
    runtime_unit->refresh_view_from_room(*room);
    protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
  }
  FCTXLOGDEBUG(ctx, "matching heartbeat finish, matching_id={}, unit_id={}, status={}, last_event_id={}",
               room->get_matching_id(), request.unit_id(), static_cast<int>(room->get_status()),
               room->get_last_event_id());
  return response.result();
}

int32_t matching_manager::confirm_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingConfirmReq& request,
                                           PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) {
  response.Clear();
  FCTXLOGDEBUG(ctx, "confirm matching, unit_id={}, user={}:{}, confirmed={}", request.unit_id(),
               request.operator_user().user_id(), request.operator_user().zone_id(), request.confirmed());
  auto runtime_unit = find_unit(request.unit_id());
  auto room = runtime_unit ? runtime_unit->get_room() : nullptr;
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_STATUS_NOT_MATCHING);
    FCTXLOGERROR(ctx, "confirm matching failed to find confirming Unit, unit_id={}, result={}", request.unit_id(),
                 response.result());
    return response.result();
  }
  response.set_matching_id(room->get_matching_id());
  auto unit_iter = room->get_units().find(request.unit_id());
  if (unit_iter == room->get_units().end() || !unit_iter->second ||
      !matching_utility::unit_has_user(unit_iter->second->get_data(), request.operator_user()) ||
      !room->confirm_user(request.operator_user(), request.confirmed())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND);
    FCTXLOGERROR(ctx, "confirm matching failed to update user, matching_id={}, unit_id={}, user={}:{}, result={}",
                 room->get_matching_id(), request.unit_id(), request.operator_user().user_id(),
                 request.operator_user().zone_id(), response.result());
    return response.result();
  }
  // 战斗准备数据只保存在 matchsvr，待房间就绪后定向发送给 orbitsvr；不写入房间事件日志。
  if (request.confirmed()) {
    room->add_orbit_user_init_detail(request.operator_user(), request.orbit_init_data(), request.user_open_id());
  }

  if (request.subscriber_server_id() != 0 &&
      !runtime_unit->subscribe(ctx, request.operator_user(), request.subscriber_server_id(),
                               request.acknowledge_event_id())) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    return response.result();
  }

  const int64_t now = atfw::util::time::time_utility::get_now();
  runtime_unit->refresh_view_from_room(*room);
  protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
  if (!request.confirmed()) {
    auto remove_event = matching_logic::make_remove_unit_event(unit_iter->second->get_data());

    room->remove_unit(request.unit_id());
    unindex_unit(remove_event.remove_unit().unit());
    if (room->get_units().empty()) {
      room->mark_cancelled(now);
    } else {
      room->resume_matching(now + matching_logic::get_search_timeout_seconds(room->get_scope().matching_pool_id()));
      index_room(room);
    }
    // 拒绝结果由本次 RPC 回包告知操作玩家；房间广播只表达 Unit 被移除后的最终状态。
    publish_room_event(ctx, room, std::move(remove_event));
    if (room->get_units().empty()) {
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_cancel(now);
      publish_room_event(ctx, room, std::move(event_log));
    }
  } else if (room->are_all_users_confirmed()) {
    start_battle(ctx, room, now);
  }

  response.set_result(0);
  const auto confirmed_unit = room->find_unit(request.unit_id());
  if (confirmed_unit) {
    runtime_unit->refresh_view_from_room(*room);
    protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
  } else {
    protobuf_copy_message(*response.mutable_snapshot(), runtime_unit->get_view());
  }
  FCTXLOGDEBUG(ctx,
               "confirm matching finish, matching_id={}, unit_id={}, user={}:{}, confirmed={}, status={}, "
               "remaining_units={}, last_event_id={}",
               room->get_matching_id(), request.unit_id(), request.operator_user().user_id(),
               request.operator_user().zone_id(), request.confirmed(), static_cast<int>(room->get_status()),
               room->get_units().size(), room->get_last_event_id());
  return response.result();
}

rpc::result_code_type matching_manager::orbit_room_ready(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq& request,
    PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp& response, uint64_t source_server_id) {
  FCTXLOGDEBUG(ctx, "orbit room ready, matching_id={}, source={:#x}, start_success={}, expired_timepoint={}",
               request.matching_id(), source_server_id, request.start_success(), request.expired_timepoint());
  response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  auto room_iter = rooms_.find(request.matching_id());
  if (room_iter == rooms_.end() || !room_iter->second) {
    FCTXLOGERROR(ctx, "orbit room ready rejected, matching_id={}, source={:#x}, result={}", request.matching_id(),
                 source_server_id, response.result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  auto room = room_iter->second;
  if (source_server_id == 0 || source_server_id != room->get_orbit_server_id()) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_SOURCE_SERVER_ID_NOT_FOUND);
    FCTXLOGERROR(ctx, "orbit room ready rejected by source, matching_id={}, source={:#x}, expected_source={:#x}",
                 request.matching_id(), source_server_id, room->get_orbit_server_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (!request.start_success()) {
    const auto previous_status = room->get_status();
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE &&
        !room->is_orbit_ready_processing()) {
      const int32_t result = PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_START_FAILED;
      unindex_all_units(*room);
      room->mark_failed(result, atfw::util::time::time_utility::get_now());
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_failed(result);
      publish_room_event(ctx, room, std::move(event_log));
      response.set_result(result);
    } else if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED ||
               room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED) {
      // 同一 Orbit 的重复或延迟回调只返回既有终态，不允许覆盖已经提交的结果。
      response.set_result(room->get_result());
    } else {
      response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    }
    FCTXLOGERROR(ctx, "orbit reported room start failure, matching_id={}, source={:#x}, previous_status={}, result={}",
                 request.matching_id(), source_server_id, static_cast<int>(previous_status), response.result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED) {
    response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    FCTXLOGDEBUG(ctx, "ignore duplicated orbit room ready after finished, matching_id={}, source={:#x}",
                 request.matching_id(), source_server_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED) {
    response.set_result(room->get_result());
    FCTXLOGDEBUG(ctx, "ignore delayed orbit room ready after failure, matching_id={}, source={:#x}, result={}",
                 request.matching_id(), source_server_id, response.result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (!room->begin_orbit_ready(source_server_id)) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(
        ctx, "orbit room ready rejected by state, matching_id={}, source={:#x}, expected_source={:#x}, status={}",
        request.matching_id(), source_server_id, room->get_orbit_server_id(), static_cast<int>(room->get_status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 向 orbit 服务器发送初始化请求，确保 orbit 服务器知道所有玩家的加入信息
  auto init_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitUserInitReq>(ctx);
  auto init_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitUserInitRsp>(ctx);
  protobuf_copy_message(*init_request->mutable_room_key(), room->get_orbit_room_key());
  init_request->set_is_last_one(true);

  std::set<uint64_t> notified_lobbysvrs;
  size_t skipped_notify_user_count = 0;
  for (const auto& unit_value : room->get_units()) {
    auto runtime_unit = unit_value.second;
    if (!runtime_unit) {
      continue;
    }
    const int32_t faction_id = room->get_unit_faction_id(unit_value.first);
    for (const auto& matching_user : runtime_unit->get_data().users()) {
      auto* orbit_user = init_request->add_user_list();
      protobuf_copy_message(*orbit_user->mutable_user_key()->mutable_user_key(), matching_user.user_key());
      auto match_orbit_user_data = room->get_match_orbit_user_init_detail(matching_user.user_key());
      orbit_user->mutable_user_key()->set_orbit_user_key(match_orbit_user_data.user_open_id());  // openid_id
      protobuf_copy_message(*orbit_user->mutable_data(), match_orbit_user_data.orbit_init_data());
      orbit_user->set_faction_id(faction_id);
      const auto route = runtime_unit ? runtime_unit->get_subscriber_route(matching_user.user_key()) : std::nullopt;
      if (!route.has_value()) {
        ++skipped_notify_user_count;
        FCTXLOGERROR(ctx,
                     "orbit room ready skipped user without subscriber route, matching_id={}, room_id={}, "
                     "user={}:{}",
                     request.matching_id(), room->get_orbit_room_key().client_id(), matching_user.user_key().user_id(),
                     matching_user.user_key().zone_id());
        continue;
      }
      notified_lobbysvrs.emplace(route->server_id);
    }
  }

  constexpr int32_t retry_time = 3;
  int32_t attempt_count = 0;
  int result = 0;
  for (int i = 0; i < retry_time; ++i) {
    attempt_count = i + 1;
    result =
        RPC_AWAIT_CODE_RESULT(rpc::orbit::init_user(ctx, room->get_orbit_server_id(), *init_request, *init_response));
    if (result == 0) {
      result = init_response->result_code();
      FCTXLOGDEBUG(ctx,
                   "initialize orbit room users finished, matching_id={}, orbitsvr_id={:#x}, attempt={}, "
                   "users={}, result={}",
                   request.matching_id(), room->get_orbit_server_id(), i + 1, init_request->user_list_size(), result);
      break;
    }
    FCTXLOGERROR(ctx,
                 "initialize orbit room users RPC failed, matching_id={}, orbitsvr_id={:#x}, attempt={}, result={}",
                 request.matching_id(), room->get_orbit_server_id(), i + 1, result);
  }

  if (result != 0) {
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE) {
      unindex_all_units(*room);
      room->mark_failed(result, atfw::util::time::time_utility::get_now());
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_failed(result);
      publish_room_event(ctx, room, std::move(event_log));
    }
    response.set_result(room->get_result());
    FCTXLOGERROR(ctx,
                 "initialize orbit room users failed, matching_id={}, orbitsvr_id={:#x}, attempts={}, status={}, "
                 "result={}",
                 request.matching_id(), room->get_orbit_server_id(), attempt_count,
                 static_cast<int>(room->get_status()), response.result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE) {
    response.set_result(room->get_result());
    FCTXLOGERROR(ctx, "orbit room state changed while initializing users, matching_id={}, status={}, result={}",
                 request.matching_id(), static_cast<int>(room->get_status()), response.result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  room->mark_finished(atfw::util::time::time_utility::get_now());
  room->set_orbit_expired_timepoint(request.expired_timepoint());
  PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
  auto matched_event = event_log.mutable_matched();
  room->dump(*matched_event);
  matched_event->set_orbit_expired_timepoint(request.expired_timepoint());
  publish_room_event(ctx, room, std::move(event_log));
  response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  FCTXLOGDEBUG(ctx,
               "orbit room ready handled, matching_id={}, room_id={}, users={}, lobbysvrs={}, "
               "skipped_notify_users={}",
               request.matching_id(), room->get_orbit_room_key().client_id(), init_request->user_list_size(),
               notified_lobbysvrs.size(), skipped_notify_user_count);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
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

size_t matching_manager::get_room_unit_count(const std::string& matching_id) const noexcept {
  auto room_iter = rooms_.find(matching_id);
  return room_iter == rooms_.end() || !room_iter->second ? 0 : room_iter->second->get_units().size();
}

size_t matching_manager::get_room_faction_count(const std::string& matching_id) const noexcept {
  auto room_iter = rooms_.find(matching_id);
  return room_iter == rooms_.end() || !room_iter->second
             ? 0
             : static_cast<size_t>(room_iter->second->get_faction_assignments().size());
}

size_t matching_manager::get_room_count() const noexcept { return rooms_.size(); }

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
bool matching_manager::prepare_battle_creation_for_test(const std::string& matching_id, uint64_t orbit_server_id,
                                                        int64_t expire_time) {
  auto room_iter = rooms_.find(matching_id);
  if (room_iter == rooms_.end() || !room_iter->second || orbit_server_id == 0 ||
      room_iter->second->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return false;
  }
  if (!room_iter->second->finalize_faction_ids()) {
    return false;
  }
  unindex_room(*room_iter->second);
  room_iter->second->mark_creating_battle(orbit_server_id, expire_time);
  return true;
}
#endif

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

matching_unit::ptr_t matching_manager::find_unit(uint64_t unit_id) const {
  auto iter = units_.find(unit_id);
  return iter == units_.end() ? nullptr : iter->second;
}

void matching_manager::publish_room_event(rpc::context& ctx, const matching_room::ptr_t& room,
                                          PROJECT_NAMESPACE_ID::DMatchingEventLog&& event_log,
                                          PROJECT_NAMESPACE_ID::EnMatchingUnitLifecycleStatus removed_unit_status) {
  if (!room) {
    return;
  }
  const auto event_case = event_log.event_case();
  uint64_t removed_unit_id = 0;
  const bool is_migration = event_case == PROJECT_NAMESPACE_ID::DMatchingEventLog::kRemoveUnit &&
                            !event_log.remove_unit().switch_to_matching_id().empty();
  if (event_case == PROJECT_NAMESPACE_ID::DMatchingEventLog::kRemoveUnit) {
    removed_unit_id = event_log.remove_unit().unit().unit_id();
  }
  room->publish(ctx, std::move(event_log));

  std::vector<matching_unit::ptr_t> affected_units;
  affected_units.reserve(room->get_units().size() + (removed_unit_id == 0 ? 0 : 1));
  for (const auto& value : room->get_units()) {
    auto unit = find_unit(value.first);
    if (unit) {
      affected_units.emplace_back(std::move(unit));
    }
  }
  if (removed_unit_id != 0) {
    auto removed_unit = find_unit(removed_unit_id);
    if (removed_unit) {
      affected_units.emplace_back(std::move(removed_unit));
    }
  }

  const int64_t now = atfw::util::time::time_utility::get_now();
  std::unordered_set<uint64_t> published;
  for (const auto& unit : affected_units) {
    if (!unit || !published.emplace(unit->get_unit_id()).second) {
      continue;
    }
    if (unit->get_unit_id() == removed_unit_id && !is_migration) {
      unit->mark_terminal(removed_unit_status, room->get_result(), now);
      unit->unbind_room();
      unit->publish(ctx, PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_TERMINAL);
      continue;
    }

    auto current_room = unit->get_room();
    if (!current_room) {
      continue;
    }
    const bool changed = unit->refresh_view_from_room(*current_room);
    PROJECT_NAMESPACE_ID::EnMatchingUnitEventType output_type =
        PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_INVALID;
    if (unit->get_last_event_id() == 0) {
      output_type = PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_CREATED;
    } else if (current_room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING &&
               (event_case == PROJECT_NAMESPACE_ID::DMatchingEventLog::kNotifyConfirm ||
                event_case == PROJECT_NAMESPACE_ID::DMatchingEventLog::kMatched)) {
      output_type = PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_CONFIRM_REQUIRED;
    } else if (current_room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED) {
      output_type = PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_BATTLE_READY;
    } else if (unit->is_terminal()) {
      output_type = PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_TERMINAL;
    } else if (changed) {
      output_type = PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_VIEW_CHANGED;
    }
    if (output_type != PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_INVALID) {
      if (unit->is_terminal()) {
        unit->set_terminal_time(now);
      }
      unit->publish(ctx, output_type);
    }
  }
}

matching_manager::joinable_room_result matching_manager::find_joinable_room(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
    const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now) const {
  joinable_room_result result;
  auto bucket_iter = searching_rooms_by_bucket_.find(make_bucket_key(scope));
  if (bucket_iter == searching_rooms_by_bucket_.end()) {
    FCTXLOGDEBUG(ctx,
                 "find joinable matching room without bucket, unit_id={}, level_type={}, region={}, "
                 "battle_version={}, matching_pool_id={}",
                 unit.unit_id(), scope.level_type(), scope.region(), scope.battle_version(), scope.matching_pool_id());
    return result;
  }

  // 规则组选择要把本次待加入 Unit 也计入全服匹配人数，确保加入已有房间和新建房间使用同一规则边界。
  const int32_t global_matching_users = get_total_matching_user_count() + unit.users_size();
  std::tuple<int32_t, int64_t> selected_score{-1, 0};
  size_t evaluated_room_count = 0;
  size_t joinable_room_count = 0;
  for (const auto& entry : bucket_iter->second) {
    auto room_iter = rooms_.find(entry.matching_id);
    if (room_iter == rooms_.end() || !room_iter->second ||
        room_iter->second->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      continue;
    }
    ++evaluated_room_count;
    auto evaluation = matching_logic::check_unit_can_join(*room_iter->second, unit, now, global_matching_users);
    if (!evaluation.evaluation.can_join()) {
      continue;
    }
    ++joinable_room_count;
    const int32_t placement_priority = evaluation.progress.joins_existing && evaluation.progress.completes_faction
                                           ? 2
                                           : (evaluation.progress.joins_existing ? 1 : 0);
    const auto score =
        std::make_tuple(placement_priority, -static_cast<int64_t>(evaluation.progress.remaining_user_count));
    if (!result.room || score > selected_score) {
      result.room = room_iter->second;
      selected_score = score;
      protobuf_move_message(result.evaluation, std::move(evaluation.evaluation));
    }
  }
  FCTXLOGDEBUG(ctx,
               "find joinable matching room finished, unit_id={}, bucket_rooms={}, evaluated_rooms={}, "
               "joinable_rooms={}, selected_matching_id={}, placement_priority={}, remaining_faction_users={}",
               unit.unit_id(), bucket_iter->second.size(), evaluated_room_count, joinable_room_count,
               result.room ? result.room->get_matching_id() : std::string{}, std::get<0>(selected_score),
               -std::get<1>(selected_score));
  return result;
}

size_t matching_manager::rebalance_room(rpc::context& ctx, const matching_room::ptr_t& target_room, int64_t now,
                                        size_t max_migration_count) {
  if (!target_room || target_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING ||
      target_room->get_units().empty() || max_migration_count == 0) {
    return 0;
  }

  auto bucket_iter = searching_rooms_by_bucket_.find(make_bucket_key(target_room->get_scope()));
  if (bucket_iter == searching_rooms_by_bucket_.end()) {
    return 0;
  }

  int32_t global_matching_users = get_total_matching_user_count();
  const auto target_ready = matching_logic::check_room_ready(*target_room, now, global_matching_users);
  if (target_ready.ready()) {
    evaluate_room(ctx, target_room, now);
    return 0;
  }
  if (target_ready.result() != 0) {
    FCTXLOGERROR(ctx, "rebalance room rejected by target ready check, matching_id={}, result={}",
                 target_room->get_matching_id(), target_ready.result());
    return 0;
  }

  const queue_entry target_entry{target_room->get_created_time(), target_room->get_matching_id()};
  std::vector<queue_entry> source_entries;
  for (const auto& entry : bucket_iter->second) {
    if (target_entry < entry) {
      source_entries.emplace_back(entry);
    }
  }

  size_t source_room_count = 0;
  size_t migration_candidate_count = 0;
  size_t migration_count = 0;

  // move 可能从桶索引移除 source 或 target，因此遍历预先复制的稳定房间键。
  for (const auto& entry : source_entries) {
    if (migration_count >= max_migration_count ||
        target_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      break;
    }
    auto source_iter = rooms_.find(entry.matching_id);
    if (source_iter == rooms_.end() || !source_iter->second || source_iter->second == target_room ||
        source_iter->second->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      continue;
    }
    auto source_room = source_iter->second;
    ++source_room_count;
    const auto source_ready = matching_logic::check_room_ready(*source_room, now, global_matching_users);
    if (source_ready.ready()) {
      evaluate_room(ctx, source_room, now);
      global_matching_users = get_total_matching_user_count();
      continue;
    }
    if (source_ready.result() != 0) {
      FCTXLOGERROR(ctx,
                   "rebalance skipped invalid source room, source_matching_id={}, target_matching_id={}, result={}",
                   source_room->get_matching_id(), target_room->get_matching_id(), source_ready.result());
      continue;
    }
    auto migration_candidates = collect_migration_candidates(ctx, *source_room);
    migration_candidate_count += migration_candidates.size();
    for (const auto& candidate : migration_candidates) {
      if (migration_count >= max_migration_count ||
          target_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING ||
          source_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
        break;
      }
      auto evaluation = evaluate_candidate(target_room, source_room, candidate, global_matching_users);
      if (!evaluation.accepted) {
        continue;
      }
      FCTXLOGDEBUG(ctx,
                   "rebalance matching room migrating, source_matching_id={}, target_matching_id={}, unit_count={}, "
                   "first_unit_id={}, complete_faction={}, faction_capacity={}, source_users={}, target_users={}",
                   source_room->get_matching_id(), target_room->get_matching_id(), candidate.unit_ids.size(),
                   candidate.unit_ids.front(), candidate.complete_faction, candidate.faction_capacity,
                   source_room->get_user_count(), target_room->get_user_count());
      if (!move_units(ctx, source_room, target_room, candidate.unit_ids, now)) {
        FCTXLOGERROR(ctx,
                     "rebalance matching room failed to move units, source_matching_id={}, target_matching_id={}, "
                     "unit_count={}, first_unit_id={}",
                     source_room->get_matching_id(), target_room->get_matching_id(), candidate.unit_ids.size(),
                     candidate.unit_ids.front());
        continue;
      }
      const bool target_assignments_committed =
          target_room->set_faction_assignments(evaluation.target_result.faction_assignments());
      if (!target_assignments_committed) {
        FCTXLOGERROR(ctx,
                     "rebalance generated invalid target assignments, source_matching_id={}, target_matching_id={}",
                     source_room->get_matching_id(), target_room->get_matching_id());
        return migration_count;
      }
      ++migration_count;
      if (!source_room->get_units().empty()) {
        evaluate_room(ctx, source_room, now);
      }
      evaluate_room(ctx, target_room, now);
      global_matching_users = get_total_matching_user_count();
    }
  }
  if (migration_count > 0) {
    FCTXLOGDEBUG(ctx,
                 "rebalance matching room finished, target_matching_id={}, source_rooms={}, migration_candidates={}, "
                 "migrations={}, target_units={}, target_users={}, target_status={}",
                 target_room->get_matching_id(), source_room_count, migration_candidate_count, migration_count,
                 target_room->get_units().size(), target_room->get_user_count(),
                 static_cast<int>(target_room->get_status()));
  }
  return migration_count;
}

bool matching_manager::move_units(rpc::context& ctx, const matching_room::ptr_t& source_room,
                                  const matching_room::ptr_t& target_room, const std::vector<uint64_t>& unit_ids,
                                  int64_t now) {
  if (!source_room || !target_room) {
    FCTXLOGERROR(ctx, "move matching units rejected by missing room, source_ready={}, target_ready={}, unit_count={}",
                 static_cast<bool>(source_room), static_cast<bool>(target_room), unit_ids.size());
    return false;
  }
  if (source_room == target_room || unit_ids.empty() ||
      source_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING ||
      target_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    FCTXLOGERROR(ctx,
                 "move matching units rejected by precondition, source_matching_id={}, target_matching_id={}, "
                 "source_status={}, target_status={}, unit_count={}",
                 source_room->get_matching_id(), target_room->get_matching_id(),
                 static_cast<int>(source_room->get_status()), static_cast<int>(target_room->get_status()),
                 unit_ids.size());
    return false;
  }

  FCTXLOGDEBUG(ctx,
               "move matching units, source_matching_id={}, target_matching_id={}, unit_count={}, first_unit_id={}",
               source_room->get_matching_id(), target_room->get_matching_id(), unit_ids.size(), unit_ids.front());

  std::vector<matching_unit::ptr_t> moved_units;
  moved_units.reserve(unit_ids.size());
  for (uint64_t unit_id : unit_ids) {
    auto source_unit_iter = source_room->get_units().find(unit_id);
    if (unit_id == 0 || source_unit_iter == source_room->get_units().end()) {
      FCTXLOGERROR(ctx,
                   "move matching units failed to find source unit, source_matching_id={}, target_matching_id={}, "
                   "unit_id={}",
                   source_room->get_matching_id(), target_room->get_matching_id(), unit_id);
      return false;
    }
    if (!source_unit_iter->second) {
      FCTXLOGERROR(ctx, "move matching units found null runtime Unit, source_matching_id={}, unit_id={}",
                   source_room->get_matching_id(), unit_id);
      return false;
    }
    moved_units.emplace_back(source_unit_iter->second);
  }

  const int32_t old_target_template_id = target_room->get_result_template_id();
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> old_target_assignments;
  for (const auto& assignment : target_room->get_faction_assignments()) {
    protobuf_copy_message(*old_target_assignments.Add(), assignment);
  }
  std::vector<uint64_t> added_unit_ids;
  for (const auto& unit : moved_units) {
    if (!target_room->add_unit(unit)) {
      target_room->clear_faction_assignments();
      for (auto added_iter = added_unit_ids.rbegin(); added_iter != added_unit_ids.rend(); ++added_iter) {
        const uint64_t added_unit_id = *added_iter;
        target_room->remove_unit(added_unit_id);
      }
      const bool target_assignments_restored = target_room->set_faction_assignments(old_target_assignments);
      if (!target_assignments_restored) {
        FCTXLOGERROR(ctx, "move matching units failed to restore target assignments, target_matching_id={}",
                     target_room->get_matching_id());
      }
      target_room->set_result_template_id(old_target_template_id);
      FCTXLOGERROR(ctx,
                   "move matching units failed to add target unit, source_matching_id={}, target_matching_id={}, "
                   "unit_id={}, restored_units={}",
                   source_room->get_matching_id(), target_room->get_matching_id(), unit->get_unit_id(),
                   added_unit_ids.size());
      return false;
    }
    added_unit_ids.emplace_back(unit->get_unit_id());
  }

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> old_source_assignments;
  for (const auto& assignment : source_room->get_faction_assignments()) {
    protobuf_copy_message(*old_source_assignments.Add(), assignment);
  }
  const int32_t old_source_template_id = source_room->get_result_template_id();
  std::unordered_set<uint64_t> removed_unit_ids;
  for (const auto& unit : moved_units) {
    if (source_room->remove_unit(unit->get_unit_id())) {
      removed_unit_ids.emplace(unit->get_unit_id());
      continue;
    }
    for (const auto& rollback_unit : moved_units) {
      if (removed_unit_ids.find(rollback_unit->get_unit_id()) != removed_unit_ids.end()) {
        source_room->add_unit(rollback_unit);
      }
    }
    const bool source_assignments_restored = source_room->set_faction_assignments(old_source_assignments);
    if (!source_assignments_restored) {
      FCTXLOGERROR(ctx, "move matching units failed to restore source assignments, source_matching_id={}",
                   source_room->get_matching_id());
    }
    source_room->set_result_template_id(old_source_template_id);
    target_room->clear_faction_assignments();
    for (auto added_iter = added_unit_ids.rbegin(); added_iter != added_unit_ids.rend(); ++added_iter) {
      target_room->remove_unit(*added_iter);
    }
    const bool target_assignments_restored = target_room->set_faction_assignments(old_target_assignments);
    if (!target_assignments_restored) {
      FCTXLOGERROR(ctx, "move matching units failed to restore target assignments, target_matching_id={}",
                   target_room->get_matching_id());
    }
    target_room->set_result_template_id(old_target_template_id);
    FCTXLOGERROR(ctx, "move matching units lost source precondition, source_matching_id={}, unit_count={}",
                 source_room->get_matching_id(), unit_ids.size());
    return false;
  }

  target_room->extend_expire_time(
      now + matching_logic::get_search_timeout_seconds(target_room->get_scope().matching_pool_id()));
  for (const auto& moved_unit : moved_units) {
    index_unit(target_room->get_matching_id(), moved_unit->get_data());
    moved_unit->bind_room(target_room);
    publish_room_event(ctx, source_room,
                       matching_logic::make_remove_unit_event(moved_unit->get_data(), target_room->get_matching_id()));
    publish_room_event(ctx, target_room, matching_logic::make_add_unit_event(moved_unit->get_data()));
  }
  if (source_room->get_units().empty()) {
    unindex_room(*source_room);
    source_room->mark_cancelled(now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_cancel(now);
    publish_room_event(ctx, source_room, std::move(event_log));
  }
  FCTXLOGDEBUG(ctx,
               "move matching units finish, source_matching_id={}, target_matching_id={}, moved_unit_count={}, "
               "source_units={}, target_units={}",
               source_room->get_matching_id(), target_room->get_matching_id(), unit_ids.size(),
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
    if (unit.second) {
      unindex_unit(unit.second->get_data());
    }
  }
}

void matching_manager::start_battle(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now) {
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return;
  }
  unindex_room(*room);
  const uint64_t orbit_server_id = matching_logic::get_orbitsvr_server_id(room->get_matching_id());
  if (orbit_server_id == 0) {
    unindex_all_units(*room);
    room->mark_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED);
    publish_room_event(ctx, room, std::move(event_log));
    FCTXLOGERROR(ctx, "start orbit room failed, no ready orbitsvr, matching_id={}", room->get_matching_id());
    return;
  }

  if (!room->finalize_faction_ids()) {
    unindex_all_units(*room);
    room->mark_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED);
    publish_room_event(ctx, room, std::move(event_log));
    FCTXLOGERROR(ctx, "start orbit room failed to finalize faction ids, matching_id={}", room->get_matching_id());
    return;
  }

  room->mark_creating_battle(orbit_server_id,
                             now + matching_logic::get_search_timeout_seconds(room->get_scope().matching_pool_id()));
  FCTXLOGDEBUG(ctx,
               "start battle for matching, matching_id={}, orbitsvr_id={:#x}, level_id={}, user_count={}, "
               "result_template_id={}, battle_create_expire_time={}",
               room->get_matching_id(), orbit_server_id, room->get_selected_level_id(), room->get_user_count(),
               room->get_result_template_id(), room->get_battle_create_expire_time());

  auto invoke_result = rpc::async_invoke(
      ctx, "matching_manager.create_orbit_room",
      [this, room, orbit_server_id](rpc::context& child_ctx) -> rpc::result_code_type {
        auto request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq>(child_ctx);
        auto response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitCreateRoomRsp>(child_ctx);
        protobuf_copy_message(*request->mutable_room_key(), room->get_orbit_room_key());

        auto level_cfg = excel::get_ExcelLevel_by_level_id(room->get_selected_level_id());
        if (!level_cfg) {
          FCTXLOGERROR(child_ctx,
                       "create orbit room failed, level_cfg not found, matching_id={}, level_id={}, level_type={}",
                       room->get_matching_id(), room->get_selected_level_id(), room->get_scope().level_type());
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED);
        }

        const int32_t client_template_id = level_cfg->client_template_id();

        request->mutable_room_data()->set_client_template_id(client_template_id);
        request->mutable_room_data()->set_region(room->get_scope().region());
        request->mutable_room_data()->set_match_id(room->get_matching_id());
        const int32_t result =
            RPC_AWAIT_CODE_RESULT(rpc::orbit::create_room(child_ctx, orbit_server_id, *request, *response));
        const int32_t business_result = result == 0 ? response->result_code() : result;
        if (business_result != 0 &&
            room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE) {
          // 失败直接解散房间
          unindex_all_units(*room);
          room->mark_failed(business_result, atfw::util::time::time_utility::get_now());
          PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
          event_log.set_failed(business_result);
          publish_room_event(child_ctx, room, std::move(event_log));
          FCTXLOGERROR(child_ctx, "create orbit room failed, matching_id={}, orbitsvr_id={:#x}, result={}",
                       room->get_matching_id(), orbit_server_id, business_result);
        } else if (business_result == 0) {
          FCTXLOGDEBUG(child_ctx, "create orbit room finished, matching_id={}, orbitsvr_id={:#x}, room_id={}",
                       room->get_matching_id(), orbit_server_id, room->get_orbit_room_key().client_id());
        }
        RPC_RETURN_CODE(business_result);
      });

  if (invoke_result.is_error()) {
    unindex_all_units(*room);
    room->mark_failed(*invoke_result.get_error(), now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(*invoke_result.get_error());
    publish_room_event(ctx, room, std::move(event_log));
    FCTXLOGERROR(ctx, "dispatch create orbit room task failed, matching_id={}, orbitsvr_id={:#x}, result={}",
                 room->get_matching_id(), orbit_server_id, *invoke_result.get_error());
  }
}

void matching_manager::evaluate_room(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now) {
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    return;
  }
  auto result = matching_logic::check_room_ready(*room, now, get_total_matching_user_count());
  if (result.result() == 0) {
    room->set_result_template_id(result.result_template_id());
  }
  if (result.ready()) {
    unindex_room(*room);
    room->begin_confirmation(now + matching_logic::get_confirm_timeout_seconds(room->get_scope().matching_pool_id()));
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_notify_confirm(room->get_confirm_expire_time());
    publish_room_event(ctx, room, std::move(event_log));
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
  std::vector<uint64_t> removed_unit_ids;
  for (const auto& unit : room->get_units()) {
    if (!unit.second) {
      continue;
    }
    const auto& users = unit.second->get_data().users();
    const bool has_unconfirmed = std::any_of(users.begin(), users.end(), [](const auto& user) {
      return user.confirm_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_ACCEPTED;
    });
    if (has_unconfirmed) {
      removed_unit_ids.emplace_back(unit.first);
    }
  }
  std::vector<PROJECT_NAMESPACE_ID::DMatchingEventLog> remove_events;
  remove_events.reserve(removed_unit_ids.size());
  for (uint64_t unit_id : removed_unit_ids) {
    auto unit_iter = room->get_units().find(unit_id);
    if (unit_iter == room->get_units().end()) {
      continue;
    }
    if (!unit_iter->second) {
      continue;
    }
    remove_events.emplace_back(matching_logic::make_remove_unit_event(unit_iter->second->get_data()));
    room->remove_unit(unit_id);
    unindex_unit(remove_events.back().remove_unit().unit());
  }
  if (room->get_units().empty()) {
    room->mark_timeout(now);
  } else {
    room->resume_matching(now + matching_logic::get_search_timeout_seconds(room->get_scope().matching_pool_id()));
    index_room(room);
  }
  // 确认超时不广播单个玩家的私有确认状态，只同步 Unit 移除及房间最终状态。
  for (auto& event_log : remove_events) {
    publish_room_event(ctx, room, std::move(event_log));
  }
  if (room->get_units().empty()) {
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_timeout(now);
    publish_room_event(ctx, room, std::move(event_log));
    FCTXLOGDEBUG(ctx, "matching confirmation timeout finished with empty room, matching_id={}, status={}",
                 room->get_matching_id(), static_cast<int>(room->get_status()));
  } else {
    FCTXLOGDEBUG(ctx,
                 "matching confirmation timeout resumed search, matching_id={}, remaining_units={}, expire_time={}",
                 room->get_matching_id(), room->get_units().size(), room->get_expire_time());
  }
}

void matching_manager::handle_unit_heartbeat_timeout(rpc::context& ctx, const matching_unit::ptr_t& unit, int64_t now) {
  auto room = unit ? unit->get_room() : nullptr;
  if (!unit || !room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING ||
      !room->has_unit(unit->get_unit_id())) {
    return;
  }

  auto remove_event = matching_logic::make_remove_unit_event(unit->get_data());
  if (!room->remove_unit(unit->get_unit_id())) {
    return;
  }
  unindex_unit(remove_event.remove_unit().unit());
  const bool room_empty = room->get_units().empty();
  if (room_empty) {
    unindex_room(*room);
    room->mark_timeout(now);
  }
  publish_room_event(ctx, room, std::move(remove_event),
                     PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT);
  if (room_empty) {
    PROJECT_NAMESPACE_ID::DMatchingEventLog timeout_event;
    timeout_event.set_timeout(now);
    publish_room_event(ctx, room, std::move(timeout_event));
  } else {
    evaluate_room(ctx, room, now);
  }
  FCTXLOGINFO(ctx, "matching Unit heartbeat expired, matching_id={}, unit_id={}, room_empty={}, now={}",
              room->get_matching_id(), unit->get_unit_id(), room_empty, now);
}

void matching_manager::handle_battle_create_timeout(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now) {
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE) {
    return;
  }
  unindex_room(*room);
  unindex_all_units(*room);
  room->mark_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, now);
  PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
  event_log.set_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED);
  publish_room_event(ctx, room, std::move(event_log));
  FCTXLOGERROR(ctx, "create battle timeout, matching_id={}, orbit_server_id={:#x}", room->get_matching_id(),
               room->get_orbit_server_id());
}

std::vector<matching_manager::migration_candidate> matching_manager::collect_migration_candidates(
    rpc::context& ctx, const matching_room& source_room) {
  std::vector<migration_candidate> result;
  for (const auto& assignment : source_room.get_faction_assignments()) {
    const size_t faction_users = static_cast<size_t>(assignment.assigned_user_count());
    if (assignment.user_capacity() == 0 || faction_users == 0 || faction_users > assignment.user_capacity()) {
      FCTXLOGERROR(ctx,
                   "rebalance source assignment has invalid capacity, source_matching_id={}, capacity={}, users={}",
                   source_room.get_matching_id(), assignment.user_capacity(), faction_users);
      result.clear();
      return result;
    }
    if (faction_users == assignment.user_capacity()) {
      migration_candidate candidate;
      candidate.faction_capacity = assignment.user_capacity();
      candidate.complete_faction = true;
      candidate.unit_ids.assign(assignment.unit_ids().begin(), assignment.unit_ids().end());
      std::sort(candidate.unit_ids.begin(), candidate.unit_ids.end());
      result.emplace_back(std::move(candidate));
      continue;
    }
    for (uint64_t unit_id : assignment.unit_ids()) {
      migration_candidate candidate;
      candidate.faction_capacity = assignment.user_capacity();
      candidate.unit_ids.emplace_back(unit_id);
      result.emplace_back(std::move(candidate));
    }
  }
  // 不使用 faction 模板的规则只按总人数成局，因此房间没有 faction membership。
  if (source_room.get_faction_assignments().empty()) {
    for (const auto& unit : source_room.get_units()) {
      migration_candidate candidate;
      candidate.unit_ids.emplace_back(unit.first);
      result.emplace_back(std::move(candidate));
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

matching_manager::candidate_evaluation matching_manager::evaluate_candidate(
    const matching_room::ptr_t& target_room, const matching_room::ptr_t& source_room,
    const matching_manager::migration_candidate& candidate, int32_t current_global_matching_users) {
  candidate_evaluation result;
  matching_logic::unit_view moved_units;
  moved_units.reserve(candidate.unit_ids.size());
  size_t moved_user_count = 0;
  for (uint64_t unit_id : candidate.unit_ids) {
    auto unit_iter = source_room->get_units().find(unit_id);
    if (unit_iter == source_room->get_units().end()) {
      return result;
    }
    if (!unit_iter->second) {
      return result;
    }
    moved_units.emplace_back(&unit_iter->second->get_data());
    moved_user_count += static_cast<size_t>(unit_iter->second->get_data().users_size());
  }
  if (moved_units.empty() || (!candidate.complete_faction && moved_units.size() != 1)) {
    return result;
  }

  const int64_t now = atfw::util::time::time_utility::get_now();

  auto target_evaluation =
      candidate.complete_faction
          ? matching_logic::check_faction_can_join(*target_room, moved_units, candidate.faction_capacity, now,
                                                   current_global_matching_users)
          : matching_logic::check_unit_can_join_for_rebalance(*target_room, *moved_units.front(), now,
                                                              current_global_matching_users);
  protobuf_move_message(result.target_result, std::move(target_evaluation.evaluation));
  if (!result.target_result.can_join()) {
    return result;
  }
  if (target_evaluation.progress.has_faction) {
    if (target_evaluation.progress.joins_existing) {
      if (target_room->get_pending_faction_user_count() < moved_user_count) {
        return result;
      }
    } else if (!target_evaluation.progress.completes_faction) {
      // target 补全不创建新的未满 faction；这种迁移只是在房间间搬运缺口。
      return result;
    }
  }

  result.accepted = true;
  return result;
}

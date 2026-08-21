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
#include <cassert>
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

namespace {
constexpr int64_t kDefaultSearchTimeout = 120;
constexpr int64_t kDefaultConfirmTimeout = 15;
constexpr int64_t kTerminalRetention = 60;
constexpr size_t kMaxRebalanceMigrationsPerTarget = 8;
constexpr size_t kMaxRebalanceMigrationsPerTick = 64;

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

struct faction_join_progress {
  bool has_faction = false;
  bool joins_existing = false;
  bool completes_faction = false;
  size_t remaining_user_count = 0;
};

faction_join_progress get_faction_join_progress(const matching_room& room,
                                                const PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation& evaluation,
                                                const matching_logic::unit_view& joined_units) {
  faction_join_progress result;
  if (joined_units.empty()) {
    return result;
  }
  std::unordered_map<uint64_t, const PROJECT_NAMESPACE_ID::DMatchingUnit*> joined_units_by_id;
  for (const auto* unit : joined_units) {
    if (unit == nullptr || !joined_units_by_id.emplace(unit->unit_id(), unit).second) {
      return result;
    }
  }
  const PROJECT_NAMESPACE_ID::DMatchingFactionAssignment* joined_faction = nullptr;
  for (const auto& assignment : evaluation.faction_assignments()) {
    if (std::find(assignment.unit_ids().begin(), assignment.unit_ids().end(), joined_units.front()->unit_id()) !=
        assignment.unit_ids().end()) {
      joined_faction = &assignment;
      break;
    }
  }
  if (joined_faction == nullptr) {
    return result;
  }
  for (const auto& assignment : room.get_faction_assignments()) {
    if (!std::all_of(assignment.unit_ids().begin(), assignment.unit_ids().end(), [&](uint64_t existing_unit_id) {
          return std::find(joined_faction->unit_ids().begin(), joined_faction->unit_ids().end(), existing_unit_id) !=
                 joined_faction->unit_ids().end();
        })) {
      continue;
    }
    result.joins_existing = true;
    break;
  }
  size_t user_count = 0;
  for (uint64_t unit_id : joined_faction->unit_ids()) {
    auto unit_iter = room.get_units().find(unit_id);
    if (unit_iter != room.get_units().end()) {
      user_count += static_cast<size_t>(unit_iter->second.users_size());
      continue;
    }
    auto joined_iter = joined_units_by_id.find(unit_id);
    if (joined_iter == joined_units_by_id.end()) {
      return {};
    }
    user_count += static_cast<size_t>(joined_iter->second->users_size());
  }
  if (user_count > joined_faction->user_capacity()) {
    return {};
  }
  result.has_faction = true;
  result.completes_faction = user_count == joined_faction->user_capacity();
  result.remaining_user_count = static_cast<size_t>(joined_faction->user_capacity()) - user_count;
  return result;
}

uint64_t get_orbitsvr_server_id() {
  auto* module = logic_server_last_common_module();
  if (module == nullptr) {
    return 0;
  }
  auto discovery =
      module->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kOrbitSvr));
  if (!discovery) {
    return 0;
  }
  const auto& nodes = discovery->get_sorted_nodes();
  return nodes.empty() || !nodes.front() ? 0 : nodes.front()->get_discovery_info().id();
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

matching_manager::matching_manager() = default;
matching_manager::~matching_manager() = default;

int32_t matching_manager::init() { return 0; }

int32_t matching_manager::tick() {
  const int64_t now = atfw::util::time::time_utility::get_now();
  rpc::context ctx{rpc::context::create_without_task()};
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
        unindex_room(*room);
        unindex_all_units(*room);
        room->mark_timeout(now);
        PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
        event_log.set_timeout(now);
        room->publish(ctx, std::move(event_log));
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
    if (room->get_terminal_time() > 0 && now - room->get_terminal_time() >= kTerminalRetention) {
      recycle_rooms.emplace_back(value.first);
    }
  }
  std::sort(rebalance_targets.begin(), rebalance_targets.end(), [](const auto& left, const auto& right) {
    return queue_entry{left->get_created_time(), left->get_matching_id()} <
           queue_entry{right->get_created_time(), right->get_matching_id()};
  });
  size_t remaining_migration_count = kMaxRebalanceMigrationsPerTick;
  for (const auto& target_room : rebalance_targets) {
    if (remaining_migration_count == 0) {
      break;
    }
    if (target_room && target_room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      const size_t target_migration_count = rebalance_room(
          ctx, target_room, now, (std::min)(kMaxRebalanceMigrationsPerTarget, remaining_migration_count));
      assert(target_migration_count <= remaining_migration_count);
      remaining_migration_count -= (std::min)(target_migration_count, remaining_migration_count);
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
  PROJECT_NAMESPACE_ID::DMatchingUnit unit;
  protobuf_copy_message(unit, request.unit());
  int32_t preferred_level_id = request.preferred_level_id();
  if (!matching_utility::normalize_acceptable_level_ids(unit)) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx,
                 "create matching rejected by invalid acceptable levels, unit_id={}, preferred_level_id={}, result={}",
                 unit.unit_id(), preferred_level_id, response.result());
    return response.result();
  }
  if (preferred_level_id <= 0) {
    preferred_level_id = unit.acceptable_level_ids(0);
  }
  if (std::find(unit.acceptable_level_ids().begin(), unit.acceptable_level_ids().end(), preferred_level_id) ==
      unit.acceptable_level_ids().end()) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx,
                 "create matching rejected because preferred level is not acceptable, unit_id={}, "
                 "preferred_level_id={}, result={}",
                 unit.unit_id(), preferred_level_id, response.result());
    return response.result();
  }
  FCTXLOGDEBUG(ctx,
               "create matching, unit_id={}, user={}:{}, user_count={}, level_type={}, region={}, "
               "battle_version={}, matching_pool_id={}, preferred_level_id={}, acceptable_level_count={}, "
               "subscriber_server_id={:#x}, acknowledge_event_id={}",
               unit.unit_id(), request.operator_user().user_id(), request.operator_user().zone_id(), unit.users_size(),
               scope.level_type(), scope.region(), scope.battle_version(), scope.matching_pool_id(), preferred_level_id,
               unit.acceptable_level_ids_size(), request.subscriber_server_id(), request.acknowledge_event_id());
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
  if (request.operator_user().user_id() == 0 || !matching_utility::unit_has_user(unit, request.operator_user()) ||
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

  // unit和玩家是否在匹配
  if (unit_to_room_.find(unit.unit_id()) != unit_to_room_.end()) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "create matching rejected by duplicated unit, unit_id={}, result={}", unit.unit_id(),
                 response.result());
    return response.result();
  }
  for (const auto& user : unit.users()) {
    if (user_to_unit_.find(user_key{user.user_key().user_id(), user.user_key().zone_id()}) != user_to_unit_.end()) {
      response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
      FCTXLOGERROR(ctx, "create matching rejected by duplicated user, unit_id={}, user={}:{}, result={}",
                   unit.unit_id(), user.user_key().user_id(), user.user_key().zone_id(), response.result());
      return response.result();
    }
  }

  const int64_t now = atfw::util::time::time_utility::get_now();
  matching_room::ptr_t selected_room;
  PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation join_evaluation;
  PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation initial_evaluation;
  bool created_room = false;
  selected_room = find_joinable_room(scope, unit, now, join_evaluation);

  if (!selected_room) {
    initial_evaluation = matching_logic::check_unit_can_create_room(
        scope, unit, now, get_total_matching_user_count() + unit.users_size());
    if (!initial_evaluation.can_join()) {
      response.set_result(initial_evaluation.result() != 0 ? initial_evaluation.result()
                                                           : PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
      FCTXLOGERROR(ctx,
                   "create matching rejected because unit cannot fit any result template, unit_id={}, user_count={}, "
                   "matching_pool_id={}, result={}",
                   unit.unit_id(), unit.users_size(), scope.matching_pool_id(), response.result());
      return response.result();
    }
    const int64_t timeout = get_search_timeout_seconds(scope.matching_pool_id());
    selected_room = std::make_shared<matching_room>(rpc::db::uuid::generate_standard_uuid(), scope, preferred_level_id,
                                                    now, now + timeout);
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
  bool assignments_committed = false;
  if (created_room) {
    assignments_committed = selected_room->set_faction_assignments(initial_evaluation.faction_assignments());
  } else {
    assignments_committed = selected_room->set_faction_assignments(join_evaluation.faction_assignments());
  }
  assert(assignments_committed);
  if (!assignments_committed) {
    selected_room->remove_unit(unit.unit_id());
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
    return response.result();
  }
  if (created_room) {
    rooms_.emplace(selected_room->get_matching_id(), selected_room);
    index_room(selected_room);
  }
  index_unit(selected_room->get_matching_id(), unit);
  // Unit 由当前 lobbysvr 提交，先把所有成员都绑定到该通知入口。这样 notify_confirm 会覆盖全员；
  // 成员后续通过 check/confirm 可刷新成自己当前所在的 lobbysvr 路由。
  for (const auto& user : unit.users()) {
    selected_room->subscribe(
        ctx, user.user_key(), request.subscriber_server_id(),
        matching_utility::same_user(user.user_key(), request.operator_user()) ? request.acknowledge_event_id() : 0);
  }
  selected_room->publish(ctx, make_add_unit_event(unit));

  evaluate_room(ctx, selected_room, now);

  response.set_result(selected_room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED
                          ? PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED
                          : 0);
  selected_room->dump_player_view(unit.unit_id(), *response.mutable_snapshot());
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
    room->dump_player_view(request.unit_id(), *response.mutable_snapshot());
    return response.result();
  }

  auto remove_event = make_remove_unit_event(unit_iter->second);
  room->dump_player_view(request.unit_id(), *response.mutable_snapshot());

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
    room->publish(ctx, std::move(remove_event));
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_cancel(atfw::util::time::time_utility::get_now());
    room->publish(ctx, std::move(event_log));
  } else {
    room->publish(ctx, std::move(remove_event));
    evaluate_room(ctx, room, atfw::util::time::time_utility::get_now());
  }
  response.set_result(0);
  response.mutable_snapshot()->set_status(room->get_status());
  response.mutable_snapshot()->set_last_event_id(room->get_last_event_id());
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
  room->dump_player_view(request.unit_id(), *response.mutable_snapshot());

  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    const int64_t now = atfw::util::time::time_utility::get_now();
    // 规则时间窗可能让当前房间直接满足动态模板；先成局，避免 ready 房间被 rebalance 拆走。
    evaluate_room(ctx, room, now);
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING &&
        rebalance_room(ctx, room, now, kMaxRebalanceMigrationsPerTarget) > 0) {
      room = find_room(std::string{}, request.unit_id());
      if (!room) {
        response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
        return response.result();
      }
    }
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      evaluate_room(ctx, room, now);
    }
  }
  response.set_result(0);
  room->dump_player_view(request.unit_id(), *response.mutable_snapshot());
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
  // 战斗准备数据只保存在 matchsvr，待房间就绪后定向发送给 orbitsvr；不写入房间事件日志。
  if (request.confirmed()) {
    room->add_orbit_user_init_detail(request.operator_user(), request.orbit_init_data());
  }

  room->subscribe(ctx, request.operator_user(), request.subscriber_server_id(), request.acknowledge_event_id());

  const int64_t now = atfw::util::time::time_utility::get_now();
  room->dump_player_view(request.unit_id(), *response.mutable_snapshot());
  if (!request.confirmed()) {
    auto remove_event = make_remove_unit_event(unit_iter->second);

    room->remove_unit(request.unit_id());
    unindex_unit(remove_event.remove_unit().unit());
    if (room->get_units().empty()) {
      room->mark_cancelled(now);
    } else {
      room->resume_matching(now + get_search_timeout_seconds(room->get_scope().matching_pool_id()));
      index_room(room);
    }
    // 拒绝结果由本次 RPC 回包告知操作玩家；房间广播只表达 Unit 被移除后的最终状态。
    room->publish(ctx, std::move(remove_event));
    if (room->get_units().empty()) {
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_cancel(now);
      room->publish(ctx, std::move(event_log));
    }
  } else if (room->are_all_users_confirmed()) {
    start_battle(ctx, room, now);
  }

  response.set_result(0);
  if (!room->dump_player_view(request.unit_id(), *response.mutable_snapshot())) {
    response.mutable_snapshot()->set_matching_id(room->get_matching_id());
    response.mutable_snapshot()->set_status(room->get_status());
    response.mutable_snapshot()->set_result(0);
    response.mutable_snapshot()->set_last_event_id(room->get_last_event_id());
    response.mutable_snapshot()->set_selected_level_id(room->get_selected_level_id());
    response.mutable_snapshot()->clear_faction_id();
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
  response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  auto room_iter = rooms_.find(request.matching_id());
  if (room_iter == rooms_.end() || !room_iter->second) {
    FCTXLOGERROR(ctx, "orbit room ready rejected, matching_id={}, source={:#x}, result={}", request.matching_id(),
                 source_server_id, response.result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  auto room = room_iter->second;
  if (source_server_id == 0 || source_server_id != room->get_orbit_server_id()) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    FCTXLOGERROR(ctx, "orbit room ready rejected by source, matching_id={}, source={:#x}, expected_source={:#x}",
                 request.matching_id(), source_server_id, room->get_orbit_server_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (!request.start_success()) {
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE &&
        !room->is_orbit_ready_processing()) {
      const int32_t result = PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_START_FAILED;
      unindex_all_units(*room);
      room->mark_failed(result, atfw::util::time::time_utility::get_now());
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_failed(result);
      room->publish(ctx, std::move(event_log));
      response.set_result(result);
    } else if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED ||
               room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED) {
      // 同一 Orbit 的重复或延迟回调只返回既有终态，不允许覆盖已经提交的结果。
      response.set_result(room->get_result());
    } else {
      response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED) {
    response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED) {
    response.set_result(room->get_result());
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
    const int32_t faction_id = room->get_unit_faction_id(unit_value.first);
    for (const auto& matching_user : unit_value.second.users()) {
      auto* orbit_user = init_request->add_user_list();
      protobuf_copy_message(*orbit_user->mutable_user_key()->mutable_user_key(), matching_user.user_key());
      protobuf_copy_message(*orbit_user->mutable_data(), room->get_orbit_user_init_detail(matching_user.user_key()));
      orbit_user->set_faction_id(faction_id);
      uint64_t lobbysvr_id = 0;
      int64_t acknowledge_event_id = 0;
      if (!room->get_subscriber_route(matching_user.user_key(), lobbysvr_id, acknowledge_event_id) ||
          lobbysvr_id == 0) {
        ++skipped_notify_user_count;
        FCTXLOGERROR(ctx,
                     "orbit room ready skipped user without subscriber route, matching_id={}, room_id={}, "
                     "user={}:{}",
                     request.matching_id(), room->get_orbit_room_key().client_id(), matching_user.user_key().user_id(),
                     matching_user.user_key().zone_id());
        continue;
      }
      notified_lobbysvrs.emplace(lobbysvr_id);
    }
  }

  int32_t retry_time = 3;
  int result = 0;
  for (int i = 0; i < retry_time; ++i) {
    result =
        RPC_AWAIT_CODE_RESULT(rpc::orbit::init_user(ctx, room->get_orbit_server_id(), *init_request, *init_response));
    if (result == 0) {
      result = init_response->result_code();
      break;
    }
  }

  if (result != 0) {
    if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE) {
      unindex_all_units(*room);
      room->mark_failed(result, atfw::util::time::time_utility::get_now());
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_failed(result);
      room->publish(ctx, std::move(event_log));
    }
    response.set_result(room->get_result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE) {
    response.set_result(room->get_result());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  room->mark_finished(atfw::util::time::time_utility::get_now());
  room->set_orbit_expired_timepoint(request.expired_timepoint());
  PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
  auto matched_event = event_log.mutable_matched();
  room->dump(*matched_event);
  matched_event->set_orbit_expired_timepoint(request.expired_timepoint());
  room->publish(ctx, std::move(event_log));
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
  return room_iter == rooms_.end() || !room_iter->second ? 0 : room_iter->second->get_faction_assignments().size();
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

matching_room::ptr_t matching_manager::find_joinable_room(
    const PROJECT_NAMESPACE_ID::DMatchingScope& scope, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
    PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation& evaluation) const {
  evaluation.Clear();
  auto bucket_iter = searching_rooms_by_bucket_.find(make_bucket_key(scope));
  if (bucket_iter == searching_rooms_by_bucket_.end()) {
    return nullptr;
  }

  // 规则组选择要把本次待加入 Unit 也计入全服匹配人数，确保加入已有房间和新建房间使用同一规则边界。
  const int32_t global_matching_users = get_total_matching_user_count() + unit.users_size();
  matching_room::ptr_t selected_room;
  PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation selected_evaluation;
  std::tuple<int32_t, int64_t> selected_score{-1, 0};
  for (const auto& entry : bucket_iter->second) {
    auto room_iter = rooms_.find(entry.matching_id);
    if (room_iter == rooms_.end() || !room_iter->second ||
        room_iter->second->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      continue;
    }
    evaluation = matching_logic::check_unit_can_join(*room_iter->second, unit, now, global_matching_users);
    if (!evaluation.can_join()) {
      continue;
    }
    const matching_logic::unit_view joined_units{&unit};
    const auto progress = get_faction_join_progress(*room_iter->second, evaluation, joined_units);
    const int32_t placement_priority =
        progress.joins_existing && progress.completes_faction ? 2 : (progress.joins_existing ? 1 : 0);
    const auto score = std::make_tuple(placement_priority, -static_cast<int64_t>(progress.remaining_user_count));
    if (!selected_room || score > selected_score) {
      selected_room = room_iter->second;
      selected_score = score;
      protobuf_move_message(selected_evaluation, std::move(evaluation));
    }
  }
  if (selected_room) {
    protobuf_move_message(evaluation, std::move(selected_evaluation));
  }
  return selected_room;
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

  struct migration_candidate {
    std::vector<uint64_t> unit_ids;
    uint32_t faction_capacity = 0;
    bool complete_faction = false;
    size_t source_completed_faction_count_after = 0;
    size_t source_pending_user_count_after = 0;
  };

  const auto collect_migration_candidates = [](const matching_room& source_room) {
    std::vector<migration_candidate> result;
    const size_t source_completed_faction_count = source_room.get_completed_faction_count();
    const size_t source_pending_user_count = source_room.get_pending_faction_user_count();
    for (const auto& assignment : source_room.get_faction_assignments()) {
      size_t faction_users = 0;
      for (uint64_t unit_id : assignment.unit_ids()) {
        auto unit_iter = source_room.get_units().find(unit_id);
        assert(unit_iter != source_room.get_units().end());
        if (unit_iter == source_room.get_units().end()) {
          result.clear();
          return result;
        }
        faction_users += static_cast<size_t>(unit_iter->second.users_size());
      }
      assert(assignment.user_capacity() > 0 && faction_users <= assignment.user_capacity());
      if (assignment.user_capacity() == 0 || faction_users > assignment.user_capacity()) {
        result.clear();
        return result;
      }
      if (faction_users == assignment.user_capacity()) {
        migration_candidate candidate;
        candidate.faction_capacity = assignment.user_capacity();
        candidate.complete_faction = true;
        candidate.source_completed_faction_count_after = source_completed_faction_count - 1;
        candidate.source_pending_user_count_after = source_pending_user_count;
        candidate.unit_ids.assign(assignment.unit_ids().begin(), assignment.unit_ids().end());
        std::sort(candidate.unit_ids.begin(), candidate.unit_ids.end());
        result.emplace_back(std::move(candidate));
        continue;
      }
      for (uint64_t unit_id : assignment.unit_ids()) {
        migration_candidate candidate;
        candidate.faction_capacity = assignment.user_capacity();
        candidate.unit_ids.emplace_back(unit_id);
        candidate.source_completed_faction_count_after = source_completed_faction_count;
        const size_t unit_size = static_cast<size_t>(source_room.get_units().at(unit_id).users_size());
        if (assignment.unit_ids_size() == 1) {
          candidate.source_pending_user_count_after =
              source_pending_user_count - (static_cast<size_t>(assignment.user_capacity()) - faction_users);
        } else {
          candidate.source_pending_user_count_after = source_pending_user_count + unit_size;
        }
        result.emplace_back(std::move(candidate));
      }
    }
    // 不使用 faction 模板的规则只按总人数成局，因此房间没有 faction membership。
    if (source_room.get_faction_assignments().empty()) {
      for (const auto& unit : source_room.get_units()) {
        migration_candidate candidate;
        candidate.unit_ids.emplace_back(unit.first);
        candidate.source_completed_faction_count_after = source_completed_faction_count;
        candidate.source_pending_user_count_after = source_pending_user_count;
        result.emplace_back(std::move(candidate));
      }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
      if (left.complete_faction != right.complete_faction) {
        return left.complete_faction;
      }
      return left.unit_ids < right.unit_ids;
    });
    return result;
  };

  using rebalance_score_t = std::tuple<int32_t, int32_t, int64_t, int32_t, size_t, int64_t, int64_t>;
  struct candidate_evaluation {
    bool accepted = false;
    PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation target_result;
    rebalance_score_t score;
  };

  const auto evaluate_candidate = [&](const matching_room::ptr_t& source_room, const migration_candidate& candidate,
                                      int32_t current_global_matching_users) -> candidate_evaluation {
    candidate_evaluation result;
    if (!source_room || source_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      return result;
    }
    matching_logic::unit_view moved_units;
    moved_units.reserve(candidate.unit_ids.size());
    size_t moved_user_count = 0;
    for (uint64_t unit_id : candidate.unit_ids) {
      auto unit_iter = source_room->get_units().find(unit_id);
      if (unit_iter == source_room->get_units().end()) {
        return result;
      }
      moved_units.emplace_back(&unit_iter->second);
      moved_user_count += static_cast<size_t>(unit_iter->second.users_size());
    }

    result.target_result =
        candidate.complete_faction
            ? matching_logic::check_faction_can_join(*target_room, moved_units, candidate.faction_capacity, now,
                                                     current_global_matching_users)
            : matching_logic::check_unit_can_join(*target_room, *moved_units.front(), now,
                                                  current_global_matching_users);
    if (!result.target_result.can_join()) {
      return result;
    }
    const auto target_progress = get_faction_join_progress(*target_room, result.target_result, moved_units);
    if (!target_progress.has_faction && !result.target_result.faction_assignments().empty()) {
      return result;
    }
    int64_t target_pending_reduction = 0;
    if (target_progress.has_faction) {
      if (target_progress.joins_existing) {
        assert(target_room->get_pending_faction_user_count() >= moved_user_count);
        if (target_room->get_pending_faction_user_count() < moved_user_count) {
          return result;
        }
        target_pending_reduction = static_cast<int64_t>(moved_user_count);
      } else if (!target_progress.completes_faction) {
        // target 补全不创建新的未满 faction；这种迁移只是在房间间搬运缺口。
        return result;
      }
    } else {
      // 无 faction 模板只按总人数补全，加入任意合法 Unit 都会让固定 target 严格接近成局。
      target_pending_reduction = static_cast<int64_t>(moved_user_count);
    }

    const bool source_empty = moved_units.size() == source_room->get_units().size();
    result.score = std::make_tuple(
        target_progress.completes_faction ? 1 : 0, target_progress.joins_existing ? 1 : 0, target_pending_reduction,
        source_empty ? 1 : 0, candidate.source_completed_faction_count_after,
        -static_cast<int64_t>(candidate.source_pending_user_count_after), source_room->get_created_time());
    result.accepted = true;
    return result;
  };

  struct rebalance_plan {
    matching_room::ptr_t source_room;
    migration_candidate candidate;
    rebalance_score_t score;
  };
  std::vector<rebalance_plan> candidate_plans;
  const queue_entry target_entry{target_room->get_created_time(), target_room->get_matching_id()};

  // target 只会增加成员和占用模板容量，候选可行性只会收紧。初筛失败的候选无需在本次调用内重新尝试。
  for (auto entry_iter = bucket_iter->second.begin(); entry_iter != bucket_iter->second.end();) {
    const auto entry = *entry_iter;
    ++entry_iter;
    if (!(target_entry < entry)) {
      continue;
    }
    auto source_iter = rooms_.find(entry.matching_id);
    if (source_iter == rooms_.end() || !source_iter->second || source_iter->second == target_room ||
        source_iter->second->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      continue;
    }
    auto source_room = source_iter->second;
    const auto source_ready = matching_logic::check_room_ready(*source_room, now, global_matching_users);
    if (source_ready.ready()) {
      evaluate_room(ctx, source_room, now);
      global_matching_users = get_total_matching_user_count();
      continue;
    }
    if (source_ready.result() != 0) {
      continue;
    }
    for (auto& candidate : collect_migration_candidates(*source_room)) {
      auto evaluation = evaluate_candidate(source_room, candidate, global_matching_users);
      if (evaluation.accepted) {
        candidate_plans.emplace_back(rebalance_plan{source_room, std::move(candidate), std::move(evaluation.score)});
      }
    }
  }

  std::sort(candidate_plans.begin(), candidate_plans.end(), [](const auto& left, const auto& right) {
    if (left.score != right.score) {
      return left.score > right.score;
    }
    if (left.candidate.unit_ids != right.candidate.unit_ids) {
      return left.candidate.unit_ids < right.candidate.unit_ids;
    }
    return left.source_room->get_matching_id() < right.source_room->get_matching_id();
  });

  size_t migration_count = 0;
  for (const auto& plan : candidate_plans) {
    if (migration_count >= max_migration_count ||
        target_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      break;
    }
    if (!plan.source_room || plan.source_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
      continue;
    }
    const auto source_ready = matching_logic::check_room_ready(*plan.source_room, now, global_matching_users);
    if (source_ready.ready()) {
      evaluate_room(ctx, plan.source_room, now);
      global_matching_users = get_total_matching_user_count();
      continue;
    }
    if (source_ready.result() != 0) {
      continue;
    }

    auto evaluation = evaluate_candidate(plan.source_room, plan.candidate, global_matching_users);
    if (!evaluation.accepted || !move_units(ctx, plan.source_room, target_room, plan.candidate.unit_ids, now)) {
      continue;
    }
    const bool target_assignments_committed =
        target_room->set_faction_assignments(evaluation.target_result.faction_assignments());
    assert(target_assignments_committed);
    if (!target_assignments_committed) {
      FCTXLOGERROR(ctx, "rebalance generated invalid target assignments, source_matching_id={}, target_matching_id={}",
                   plan.source_room->get_matching_id(), target_room->get_matching_id());
      break;
    }
    ++migration_count;
    if (!plan.source_room->get_units().empty()) {
      evaluate_room(ctx, plan.source_room, now);
    }
    evaluate_room(ctx, target_room, now);
    global_matching_users = get_total_matching_user_count();
  }
  return migration_count;
}

bool matching_manager::move_units(rpc::context& ctx, const matching_room::ptr_t& source_room,
                                  const matching_room::ptr_t& target_room, const std::vector<uint64_t>& unit_ids,
                                  int64_t now) {
  if (!source_room || !target_room || source_room == target_room || unit_ids.empty() ||
      source_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING ||
      target_room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING) {
    return false;
  }

  std::vector<PROJECT_NAMESPACE_ID::DMatchingUnit> moved_units;
  moved_units.reserve(unit_ids.size());
  for (uint64_t unit_id : unit_ids) {
    auto source_unit_iter = source_room->get_units().find(unit_id);
    if (unit_id == 0 || source_unit_iter == source_room->get_units().end()) {
      return false;
    }
    moved_units.emplace_back();
    protobuf_copy_message(moved_units.back(), source_unit_iter->second);
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
      assert(target_assignments_restored);
      target_room->set_result_template_id(old_target_template_id);
      return false;
    }
    added_unit_ids.emplace_back(unit.unit_id());
  }

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> old_source_assignments;
  for (const auto& assignment : source_room->get_faction_assignments()) {
    protobuf_copy_message(*old_source_assignments.Add(), assignment);
  }
  const int32_t old_source_template_id = source_room->get_result_template_id();
  std::unordered_set<uint64_t> removed_unit_ids;
  for (const auto& unit : moved_units) {
    if (source_room->remove_unit(unit.unit_id())) {
      removed_unit_ids.emplace(unit.unit_id());
      continue;
    }
    for (const auto& rollback_unit : moved_units) {
      if (removed_unit_ids.find(rollback_unit.unit_id()) != removed_unit_ids.end()) {
        source_room->add_unit(rollback_unit);
      }
    }
    const bool source_assignments_restored = source_room->set_faction_assignments(old_source_assignments);
    assert(source_assignments_restored);
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
    assert(target_assignments_restored);
    if (!target_assignments_restored) {
      FCTXLOGERROR(ctx, "move matching units failed to restore target assignments, target_matching_id={}",
                   target_room->get_matching_id());
    }
    target_room->set_result_template_id(old_target_template_id);
    FCTXLOGERROR(ctx, "move matching units lost source precondition, source_matching_id={}, unit_count={}",
                 source_room->get_matching_id(), unit_ids.size());
    return false;
  }

  target_room->extend_expire_time(now + get_search_timeout_seconds(target_room->get_scope().matching_pool_id()));
  for (const auto& moved_unit : moved_units) {
    index_unit(target_room->get_matching_id(), moved_unit);
    std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> transferred_subscribers;
    for (const auto& user : moved_unit.users()) {
      uint64_t server_id = 0;
      int64_t acknowledge_event_id = 0;
      if (source_room->get_subscriber_route(user.user_key(), server_id, acknowledge_event_id)) {
        target_room->subscribe(ctx, user.user_key(), server_id, 0);
        transferred_subscribers.emplace_back();
        protobuf_copy_message(transferred_subscribers.back(), user.user_key());
      }
    }
    source_room->publish(ctx, make_remove_unit_event(moved_unit, target_room->get_matching_id()));
    for (const auto& user_key : transferred_subscribers) {
      source_room->unsubscribe(ctx, user_key);
    }
    target_room->publish(ctx, make_add_unit_event(moved_unit));
  }
  if (source_room->get_units().empty()) {
    unindex_room(*source_room);
    source_room->mark_cancelled(now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_cancel(now);
    source_room->publish(ctx, std::move(event_log));
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
    unindex_unit(unit.second);
  }
}

void matching_manager::start_battle(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now) {
  if (!room || room->get_status() != PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING) {
    return;
  }
  unindex_room(*room);
  const uint64_t orbit_server_id = get_orbitsvr_server_id();
  if (orbit_server_id == 0) {
    unindex_all_units(*room);
    room->mark_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED);
    room->publish(ctx, std::move(event_log));
    FCTXLOGERROR(ctx, "start orbit room failed, no ready orbitsvr, matching_id={}", room->get_matching_id());
    return;
  }

  if (!room->finalize_faction_ids()) {
    unindex_all_units(*room);
    room->mark_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED);
    room->publish(ctx, std::move(event_log));
    FCTXLOGERROR(ctx, "start orbit room failed to finalize faction ids, matching_id={}", room->get_matching_id());
    return;
  }

  room->mark_creating_battle(orbit_server_id, now + get_search_timeout_seconds(room->get_scope().matching_pool_id()));
  FCTXLOGDEBUG(ctx, "start battle for matching, matching_id={}, user_count={}, result_template_id={}",
               room->get_matching_id(), room->get_user_count(), room->get_result_template_id());

  auto invoke_result = rpc::async_invoke(
      ctx, "matching_manager.create_orbit_room",
      [this, room, orbit_server_id](rpc::context& child_ctx) -> rpc::result_code_type {
        auto request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq>(child_ctx);
        auto response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitCreateRoomRsp>(child_ctx);
        protobuf_copy_message(*request->mutable_room_key(), room->get_orbit_room_key());

        auto level_cfg = excel::get_ExcelLevel_by_level_id(room->get_selected_level_id());
        if (!level_cfg) {
          FCTXLOGERROR(child_ctx, "create orbit room failed, level_cfg not found, matching_id={}, level_type={}",
                       room->get_matching_id(), room->get_scope().level_type());
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
          room->publish(child_ctx, std::move(event_log));
          FCTXLOGERROR(child_ctx, "create orbit room failed, matching_id={}, orbitsvr_id={:#x}, result={}",
                       room->get_matching_id(), orbit_server_id, business_result);
        }
        RPC_RETURN_CODE(business_result);
      });

  if (invoke_result.is_error()) {
    unindex_all_units(*room);
    room->mark_failed(*invoke_result.get_error(), now);
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(*invoke_result.get_error());
    room->publish(ctx, std::move(event_log));
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
  std::vector<uint64_t> removed_unit_ids;
  for (const auto& unit : room->get_units()) {
    const bool has_unconfirmed =
        std::any_of(unit.second.users().begin(), unit.second.users().end(), [](const auto& user) {
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
    remove_events.emplace_back(make_remove_unit_event(unit_iter->second));
    room->remove_unit(unit_id);
    unindex_unit(remove_events.back().remove_unit().unit());
  }
  if (room->get_units().empty()) {
    room->mark_timeout(now);
  } else {
    room->resume_matching(now + get_search_timeout_seconds(room->get_scope().matching_pool_id()));
    index_room(room);
  }
  // 确认超时不广播单个玩家的私有确认状态，只同步 Unit 移除及房间最终状态。
  for (auto& event_log : remove_events) {
    room->publish(ctx, std::move(event_log));
  }
  if (room->get_units().empty()) {
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_timeout(now);
    room->publish(ctx, std::move(event_log));
    FCTXLOGDEBUG(ctx, "matching confirmation timeout finished with empty room, matching_id={}, status={}",
                 room->get_matching_id(), static_cast<int>(room->get_status()));
  } else {
    FCTXLOGDEBUG(ctx,
                 "matching confirmation timeout resumed search, matching_id={}, remaining_units={}, expire_time={}",
                 room->get_matching_id(), room->get_units().size(), room->get_expire_time());
  }
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
  room->publish(ctx, std::move(event_log));
  FCTXLOGERROR(ctx, "create battle timeout, matching_id={}, orbit_server_id={:#x}", room->get_matching_id(),
               room->get_orbit_server_id());
}

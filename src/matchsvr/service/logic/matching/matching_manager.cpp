// Copyright 2026 atframework

#include "logic/matching/matching_manager.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/config/com.struct.level.config.pb.h>

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
               unit.unit_id(), request.operator_user().user_id(), request.operator_user().zone_id(), unit.users_size(),
               scope.level_type(), scope.region(), scope.battle_version(), scope.matching_pool_id(),
               request.subscriber_server_id(), request.acknowledge_event_id());
  if (scope.level_type() <= 0 || scope.region().empty() || scope.battle_version().empty() ||
      scope.matching_pool_id() <= 0) {
    response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    FCTXLOGERROR(ctx, "create matching rejected by invalid scope, unit_id={}, result={}", unit.unit_id(),
                 response.result());
    return response.result();
  }
  if (request.operator_user().user_id() == 0 || !matching_utility::unit_has_user(unit, request.operator_user()) ||
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
    if (user_to_unit_.find(user_key{user.user_key().user_id(), user.user_key().zone_id()}) != user_to_unit_.end()) {
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
    selected_room = std::make_shared<matching_room>(rpc::db::uuid::generate_standard_uuid(), scope, now, now + timeout);
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
  // Unit 由当前 lobbysvr 提交，先把所有成员都绑定到该通知入口。这样 notify_confirm 会覆盖全员；
  // 成员后续通过 check/confirm 可刷新成自己当前所在的 lobbysvr 路由。
  for (const auto& user : unit.users()) {
    selected_room->subscribe(
        ctx, user.user_key(), request.subscriber_server_id(),
        matching_utility::same_user(user.user_key(), request.operator_user()) ? request.acknowledge_event_id() : 0);
  }
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
  // 战斗准备数据只保存在 matchsvr，待房间就绪后定向发送给 orbitsvr；不写入房间事件日志。
  if (request.confirmed()) {
    room->add_orbit_user_init_detail(request.operator_user(), request.orbit_init_data());
  }

  room->subscribe(ctx, request.operator_user(), request.subscriber_server_id(), request.acknowledge_event_id());

  const int64_t now = atfw::util::time::time_utility::get_now();
  if (!request.confirmed()) {
    PROJECT_NAMESPACE_ID::DMatchingUnit removed_unit;
    protobuf_copy_message(removed_unit, unit_iter->second);

    room->remove_unit(request.unit_id());
    unindex_unit(removed_unit);
    if (room->get_units().empty()) {
      room->mark_cancelled(now);
    } else {
      room->resume_matching(now + get_search_timeout_seconds(room->get_scope().matching_pool_id()));
      index_room(room);
    }
    // 拒绝结果由本次 RPC 回包告知操作玩家；房间广播只表达 Unit 被移除后的最终状态。
    room->publish(ctx, make_remove_unit_event(removed_unit));
    if (room->get_units().empty()) {
      PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
      event_log.set_cancel(now);
      room->publish(ctx, std::move(event_log));
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
  if (room->get_status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED) {
    response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (source_server_id == 0 || source_server_id != room->get_orbit_server_id() || !room->begin_orbit_ready()) {
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

  std::map<uint64_t, std::vector<PROJECT_NAMESPACE_ID::DUserIDKey>> users_by_lobbysvr;
  // faction_id 从 1001 开始，方便日志查询，后面放到配置中
  int32_t faction_id = 1001;
  size_t skipped_notify_user_count = 0;
  for (const auto& unit_value : room->get_units()) {
    for (const auto& matching_user : unit_value.second.users()) {
      auto* orbit_user = init_request->add_user_list();
      protobuf_copy_message(*orbit_user->mutable_user_key()->mutable_user_key(), matching_user.user_key());
      protobuf_copy_message(*orbit_user->mutable_data(), room->get_orbit_user_init_detail(matching_user.user_key()));
      orbit_user->set_faction_id(faction_id);
      faction_id++;
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
      users_by_lobbysvr[lobbysvr_id].emplace_back(matching_user.user_key());
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
    unindex_all_units(*room);
    room->mark_failed(result, atfw::util::time::time_utility::get_now());
    PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
    event_log.set_failed(result);
    room->publish(ctx, std::move(event_log));
    response.set_result(result);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  room->mark_finished(atfw::util::time::time_utility::get_now());
  PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
  room->dump(*event_log.mutable_matched());
  room->publish(ctx, std::move(event_log));
  response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  FCTXLOGDEBUG(ctx,
               "orbit room ready handled, matching_id={}, room_id={}, users={}, lobbysvrs={}, "
               "skipped_notify_users={}",
               request.matching_id(), room->get_orbit_room_key().client_id(), init_request->user_list_size(),
               users_by_lobbysvr.size(), skipped_notify_user_count);
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
    if (source_room != nullptr &&
        room_iter->second->get_user_count() + static_cast<size_t>(unit.users_size()) <= source_room->get_user_count()) {
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

  room->mark_creating_battle(orbit_server_id);
  FCTXLOGDEBUG(ctx, "start battle for matching, matching_id={}, user_count={}, result_template_id={}",
               room->get_matching_id(), room->get_user_count(), room->get_result_template_id());

  auto invoke_result = rpc::async_invoke(
      ctx, "matching_manager.create_orbit_room",
      [this, room, orbit_server_id](rpc::context& child_ctx) -> rpc::result_code_type {
        auto request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq>(child_ctx);
        auto response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSOrbitCreateRoomRsp>(child_ctx);
        protobuf_copy_message(*request->mutable_room_key(), room->get_orbit_room_key());

        auto level_cfg = excel::get_ExcelLevel_by_level_id(room->get_scope().level_id());
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
    room->remove_unit(unit.unit_id());
    unindex_unit(unit);
  }
  if (room->get_units().empty()) {
    room->mark_timeout(now);
  } else {
    room->resume_matching(now + get_search_timeout_seconds(room->get_scope().matching_pool_id()));
    index_room(room);
  }
  // 确认超时不广播单个玩家的私有确认状态，只同步 Unit 移除及房间最终状态。
  for (const auto& unit : removed_units) {
    room->publish(ctx, make_remove_unit_event(unit));
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

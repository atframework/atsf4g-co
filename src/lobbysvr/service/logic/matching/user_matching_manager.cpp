// Copyright 2026 atframework

#include "logic/matching/user_matching_manager.h"

#include <algorithm>
#include <string>
#include <vector>

#include <atframe/atapp.h>
#include <config/excel/config_easy_api.h>
#include <config/server_frame_build_feature.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/lobbysvr_config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <logic/orbit/user_orbit_manager.h>
#include <logic/user/task_action_user_gm_cmd_nomsg.h>

#include <config/logic_config.h>
#include <data/user.h>
#include <rpc/db/uuid.h>
#include <rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h>
#include <rpc/matching/matching_api.h>
#include <rpc/matching/matchsvrservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_shared_message.h>

namespace {
static bool init_user_matching_manager_gm_handle() {
  task_action_user_gm_cmd_nomsg::init_gm_cmd("matching_start", user_matching_manager::on_gm_cmd_start_matching,
                                             "matching_start <level_id> <region>");
  return true;
}

bool is_matching_not_found(int32_t result) {
  return result == PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND ||
         result == PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND ||
         result == PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_NOT_FOUND;
}

}  // namespace

user_matching_manager::user_matching_manager(user& owner) : owner_(&owner), dirty_(false) {
  ATFW_EXPLICIT_UNUSED_ATTR static bool init_gm_handle = init_user_matching_manager_gm_handle();
}

user_matching_manager::~user_matching_manager() = default;

void user_matching_manager::create_init(rpc::context&) {
  data_.Clear();
  processing_event_id_ = 0;
  last_reported_acknowledge_event_id_ = 0;
  last_heartbeat_time_ = 0;
  periodic_heartbeat_inflight_ = false;
  dirty_ = false;
}

rpc::result_code_type user_matching_manager::login_init(rpc::context& ctx) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(recover_matching(ctx)));
}

rpc::result_code_type user_matching_manager::recover_matching(rpc::context& ctx) {
  if (!is_in_matching()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  int32_t result = RPC_AWAIT_CODE_RESULT(query_matchsvr_snapshot(ctx, *response));
  if (result != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
    if (is_matching_not_found(result)) {
      // 玩家离线期间 Unit 可能已经完成并被 Matchsvr 回收。本地 CREATING_BATTLE 视图中的
      // Orbit key 是此时唯一可继续追踪对局结果的数据，必须先交给 Orbit 再清理匹配状态。
      if (data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE ||
          data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING ||
          data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED) {
        const int32_t join_result = owner_->get_user_orbit_manager().join_orbit_room(
            ctx, data_.view().orbit_room_key(), atfw::util::time::time_utility::get_now());
        if (join_result != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
          FWLOGERROR("{} recover missing matching Unit through Orbit failed, unit_id={}, result={}({})", *owner_,
                     get_current_unit_id(), join_result, protobuf_mini_dumper_get_error_msg(join_result));
        }
      }
      clear_matching_state();
      dirty_ = true;
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    // 路由、传输或 Matchsvr 不可用只能说明本次查询失败，不能证明 Unit 已被回收。
    // 保留本地匹配状态，等待后续登录恢复或定时心跳再次查询。
    RPC_RETURN_CODE(result);
  }

  if (response->matching_id().empty()) {
    if (data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE ||
        data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING ||
        data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED) {
      const int32_t join_result = owner_->get_user_orbit_manager().join_orbit_room(
          ctx, data_.view().orbit_room_key(), atfw::util::time::time_utility::get_now());
      if (join_result != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
        FWLOGERROR("{} recover missing matching Unit through Orbit failed, unit_id={}, result={}({})", *owner_,
                   get_current_unit_id(), join_result, protobuf_mini_dumper_get_error_msg(join_result));
      }
    }
    clear_matching_state();
    dirty_ = true;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  update_view(response->snapshot());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

void user_matching_manager::init_from_table_data(rpc::context&, const PROJECT_NAMESPACE_ID::table_user& user_table) {
  data_.Clear();
  if (user_table.has_matching_data()) {
    protobuf_copy_message(data_, user_table.matching_data());
  }
  processing_event_id_ = 0;
  last_reported_acknowledge_event_id_ = 0;
  last_heartbeat_time_ = 0;
  periodic_heartbeat_inflight_ = false;
  dirty_ = false;
}

int user_matching_manager::dump(rpc::context&, PROJECT_NAMESPACE_ID::table_user& user_table) const {
  protobuf_copy_message(*user_table.mutable_matching_data(), data_);
  return 0;
}

bool user_matching_manager::is_dirty() const { return dirty_; }

void user_matching_manager::clear_dirty() { dirty_ = false; }

void user_matching_manager::refresh_feature_limit_second(rpc::context& ctx) {
  if (!data_.has_view() || get_current_unit_id() == 0 || periodic_heartbeat_inflight_ ||
      (!is_in_matching() && last_reported_acknowledge_event_id_ >= get_acknowledge_event_id())) {
    return;
  }

  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  time_t heartbeat_interval = static_cast<time_t>(server_cfg.matching().heartbeat_interval().seconds());
  if (heartbeat_interval <= 0) {
    heartbeat_interval = 2;
  }
  const time_t now = atfw::util::time::time_utility::get_now();
  if (last_heartbeat_time_ > 0 && now < last_heartbeat_time_ + heartbeat_interval) {
    return;
  }

  last_heartbeat_time_ = now;
  const uint64_t unit_id = get_current_unit_id();
  const std::string previous_view = data_.view().SerializeAsString();
  periodic_heartbeat_inflight_ = true;
  auto owner = owner_->shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "user_matching_manager.heartbeat",
      [owner, unit_id, previous_view](rpc::context& child_ctx) -> rpc::result_code_type {
        auto& manager = owner->get_user_matching_manager();
        if (unit_id != manager.get_current_unit_id()) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        }
        auto response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(child_ctx);
        const int32_t result = RPC_AWAIT_CODE_RESULT(manager.query_matchsvr_snapshot(child_ctx, *response));
        if (unit_id != manager.get_current_unit_id()) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        }
        manager.periodic_heartbeat_inflight_ = false;
        if (result == PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
          manager.update_view(response->snapshot());
          if (previous_view != manager.data_.view().SerializeAsString()) {
            manager.on_client_view_changed(child_ctx);
          }
        }
        RPC_RETURN_CODE(result);
      });
  if (invoke_result.is_error()) {
    if (unit_id == get_current_unit_id()) {
      periodic_heartbeat_inflight_ = false;
    }
    FWLOGERROR("{} dispatch matching heartbeat failed, unit_id={}, result={}({})", *owner_, get_current_unit_id(),
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

bool user_matching_manager::is_in_matching() const {
  if (!data_.has_view() || data_.view().unit().unit_id() == 0) {
    return false;
  }

  switch (data_.view().status()) {
    case PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING:
    case PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING:
    case PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE:
      return true;
    default:
      return false;
  }
}

bool user_matching_manager::is_in_orbit_or_matching() const {
  return is_in_matching() || owner_->get_user_orbit_manager().is_orbit_room_exist();
}

rpc::result_code_type user_matching_manager::start_matching(rpc::context& ctx,
                                                            const PROJECT_NAMESPACE_ID::CSMatchingStartReq& request,
                                                            PROJECT_NAMESPACE_ID::SCMatchingStartRsp& response) {
  if (is_in_matching()) {
    FWLOGERROR("{} start matching rejected by active matching, unit_id={}, status={}", *owner_, get_current_unit_id(),
               static_cast<int>(data_.view().status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_USER_ALREADY_IN_MATCHING);
  }

  if (is_in_orbit_or_matching()) {
    FWLOGERROR("{} start matching rejected by orbit, unit_id={}, status={}", *owner_, get_current_unit_id(),
               static_cast<int>(data_.view().status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_USER_IN_BATTLE);
  }

  FWLOGDEBUG("{} start matching, level_select={}, level_count={}, request={}", *owner_,
             request.level_select().DebugString(), request.battle_version(), request.DebugString());

  // TODO 通知battle锁背包
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCreateReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  // TODO 接入battle_versnion
  std::vector<int32_t> acceptable_level_ids;
  int32_t ret = fill_matching_scope(request.level_select(), request.battle_version(), *rpc_request->mutable_scope(),
                                    acceptable_level_ids);
  if (ret != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
    FWLOGERROR("{} fill_matching_scope failed, ret={}, level_select={},battle_version={}", *owner_, ret,
               request.level_select().DebugString(), request.battle_version());
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(fill_matching_unit(ctx, *rpc_request->mutable_unit()));
  if (ret != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
    FWLOGERROR("{} fill_matching_unit failed, ret={}, level_count={}, battle_version={}", *owner_, ret,
               request.level_select().DebugString(), request.battle_version());
    RPC_RETURN_CODE(ret);
  }
  for (int32_t level_id : acceptable_level_ids) {
    rpc_request->mutable_unit()->add_acceptable_level_ids(level_id);
  }
  rpc_request->mutable_unit()->set_faction_fill_policy(
      request.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE
          ? PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE
          : PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE);

  fill_operator_user(*rpc_request->mutable_operator_user());

  // 后续接组队

  const auto& operator_user = rpc_request->operator_user();
  bool operator_is_member = false;
  for (auto& matching_user : *rpc_request->mutable_unit()->mutable_users()) {
    matching_user.set_confirm_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING);
    operator_is_member = operator_is_member || (matching_user.user_key().user_id() == operator_user.user_id() &&
                                                matching_user.user_key().zone_id() == operator_user.zone_id());
  }

  auto* subscriber_route = rpc_request->add_subscriber_routes();
  protobuf_copy_message(*subscriber_route->mutable_user_key(), rpc_request->operator_user());
  subscriber_route->set_server_id(logic_config::me()->get_local_server_id());
  const uint64_t matchsvr_id = rpc::matching_api::get_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWLOGERROR("{} start matching failed, no ready matchsvr, unit_id={}", *owner_, rpc_request->unit().unit_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }

  int32_t result = RPC_AWAIT_CODE_RESULT(rpc::matching::create_matching(ctx, matchsvr_id, *rpc_request, *rpc_response));
  if (result < 0) {
    FWLOGERROR("{} start matching RPC failed, matchsvr_id={:#x}, unit_id={}, result={}({})", *owner_, matchsvr_id,
               rpc_request->unit().unit_id(), result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (rpc_response->result() != 0) {
    FWLOGERROR("{} start matching rejected by matchsvr, matchsvr_id={:#x}, unit_id={}, result={}({})", *owner_,
               matchsvr_id, rpc_request->unit().unit_id(), rpc_response->result(),
               protobuf_mini_dumper_get_error_msg(rpc_response->result()));
    RPC_RETURN_CODE(rpc_response->result());
  }
  update_view(rpc_response->snapshot());
  data_.set_matchsvr_server_id(matchsvr_id);
  dirty_ = true;

  dump_client_view(*response.mutable_view());
  auto* level_parameter = response.mutable_level_parameter();
  level_parameter->set_region(request.level_select().region());
  for (int32_t level_id : acceptable_level_ids) {
    level_parameter->add_level_ids(level_id);
  }

  FWLOGDEBUG("{} start matching finish, level_select={}, battle_version={}, unit_id={}", *owner_,
             request.level_select().DebugString(), request.battle_version(), get_current_unit_id());

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type user_matching_manager::check_matching(rpc::context& ctx,
                                                            PROJECT_NAMESPACE_ID::SCMatchingCheckRsp& response) {
  if (!is_in_matching()) {
    dump_client_view(*response.mutable_view());
    RPC_RETURN_CODE(data_.has_view() ? static_cast<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SUCCESS)
                                     : static_cast<int32_t>(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND));
  }

  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  int32_t result = RPC_AWAIT_CODE_RESULT(query_matchsvr_snapshot(ctx, *rpc_response));
  if (result != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
    RPC_RETURN_CODE(result);
  }
  update_view(rpc_response->snapshot());
  dump_client_view(*response.mutable_view());
  FWLOGDEBUG("{} check matching finish, unit_id={}, status={}, last_event_id={}", *owner_, get_current_unit_id(),
             static_cast<int>(rpc_response->snapshot().status()), rpc_response->snapshot().last_event_id());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type user_matching_manager::query_matchsvr_snapshot(
    rpc::context& ctx, PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) {
  const uint64_t unit_id = get_current_unit_id();
  if (unit_id == 0) {
    FWLOGERROR("{} check matching failed to find current Unit", *owner_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }

  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCheckReq>(ctx);
  rpc_request->set_unit_id(unit_id);
  rpc_request->set_subscriber_server_id(logic_config::me()->get_local_server_id());
  auto* heartbeat_data = rpc_request->mutable_heartbeat_data();
  fill_operator_user(*heartbeat_data->mutable_user_key());
  const int64_t acknowledge_event_id = get_acknowledge_event_id();
  heartbeat_data->set_acknowledge_event_id(acknowledge_event_id);

  const uint64_t matchsvr_id = get_current_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWLOGERROR("{} check matching failed, Unit has no owning matchsvr, unit_id={}", *owner_, unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_SERVER_NOT_FOUND);
  }
  int32_t result = RPC_AWAIT_CODE_RESULT(rpc::matching::matching_heart_bear(ctx, matchsvr_id, *rpc_request, response));
  if (result < 0) {
    FWLOGERROR("{} check matching RPC failed, matchsvr_id={:#x}, unit_id={}, result={}({})", *owner_, matchsvr_id,
               unit_id, result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (response.result() != 0) {
    FWLOGERROR("{} check matching rejected by matchsvr, unit_id={}, result={}({})", *owner_, unit_id, response.result(),
               protobuf_mini_dumper_get_error_msg(response.result()));
    RPC_RETURN_CODE(response.result());
  }
  if (unit_id == get_current_unit_id()) {
    last_reported_acknowledge_event_id_ = std::max(last_reported_acknowledge_event_id_, acknowledge_event_id);
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type user_matching_manager::cancel_matching(rpc::context& ctx,
                                                             const PROJECT_NAMESPACE_ID::CSMatchingCancelReq& request,
                                                             PROJECT_NAMESPACE_ID::SCMatchingCancelRsp& response) {
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCancelReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  const uint64_t unit_id = get_current_unit_id();
  FWLOGDEBUG("{} cancel matching, request_unit_id={}, current_unit_id={}", *owner_, request.unit_id(), unit_id);
  if (request.unit_id() == 0 || request.unit_id() != unit_id) {
    dump_client_view(*response.mutable_view());
    FWLOGERROR("{} cancel matching rejected by stale Unit, request_unit_id={}, current_unit_id={}", *owner_,
               request.unit_id(), unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_MATCHING);
  }
  rpc_request->set_unit_id(unit_id);
  fill_operator_user(*rpc_request->mutable_operator_user());
  const uint64_t matchsvr_id = get_current_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWLOGERROR("{} cancel matching failed, Unit has no owning matchsvr, unit_id={}", *owner_, unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_SERVER_NOT_FOUND);
  }
  int32_t result = RPC_AWAIT_CODE_RESULT(rpc::matching::cancel_matching(ctx, matchsvr_id, *rpc_request, *rpc_response));
  if (result < 0) {
    FWLOGERROR("{} cancel matching RPC failed, matchsvr_id={:#x}, unit_id={}, result={}({})", *owner_, matchsvr_id,
               unit_id, result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (rpc_response->has_snapshot()) {
    update_view(rpc_response->snapshot());
  }
  dump_client_view(*response.mutable_view());
  FWLOGDEBUG("{} cancel matching finish, unit_id={}, result={}({}), status={}", *owner_, unit_id,
             rpc_response->result(), protobuf_mini_dumper_get_error_msg(rpc_response->result()),
             rpc_response->has_snapshot()
                 ? static_cast<int>(rpc_response->snapshot().status())
                 : static_cast<int>(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_INVALID));
  RPC_RETURN_CODE(rpc_response->result());
}

// 玩家的静默确认
rpc::result_code_type user_matching_manager::confirm_matching(rpc::context& ctx,
                                                              const PROJECT_NAMESPACE_ID::CSMatchingConfirmReq& request,
                                                              PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp& response) {
  // TODO 填充玩家的战斗数据
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingConfirmReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  const uint64_t unit_id = get_current_unit_id();
  FWLOGDEBUG("{} confirm matching, request_unit_id={}, current_unit_id={}, confirmed={}", *owner_, request.unit_id(),
             unit_id, request.confirmed());
  if (request.unit_id() == 0 || request.unit_id() != unit_id) {
    dump_client_view(*response.mutable_view());
    FWLOGERROR("{} confirm matching rejected by stale Unit, request_unit_id={}, current_unit_id={}", *owner_,
               request.unit_id(), unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_unit_id(unit_id);
  rpc_request->set_confirmed(request.confirmed());
  fill_operator_user(*rpc_request->mutable_operator_user());
  rpc_request->set_subscriber_server_id(logic_config::me()->get_local_server_id());
  rpc_request->set_acknowledge_event_id(get_acknowledge_event_id());
  rpc_request->set_user_open_id(owner_->get_open_id());
  const uint64_t matchsvr_id = get_current_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWLOGERROR("{} confirm matching failed, Unit has no owning matchsvr, unit_id={}", *owner_, unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_SERVER_NOT_FOUND);
  }
  int32_t result =
      RPC_AWAIT_CODE_RESULT(rpc::matching::confirm_matching(ctx, matchsvr_id, *rpc_request, *rpc_response));
  if (result < 0) {
    FWLOGERROR("{} confirm matching RPC failed, matchsvr_id={:#x}, unit_id={}, result={}({})", *owner_, matchsvr_id,
               unit_id, result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (rpc_response->has_snapshot()) {
    update_view(rpc_response->snapshot());
  }
  dump_client_view(*response.mutable_view());
  FWLOGDEBUG("{} confirm matching finish, unit_id={}, confirmed={}, result={}({}), status={}", *owner_, unit_id,
             request.confirmed(), rpc_response->result(), protobuf_mini_dumper_get_error_msg(rpc_response->result()),
             rpc_response->has_snapshot()
                 ? static_cast<int>(rpc_response->snapshot().status())
                 : static_cast<int>(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_INVALID));
  RPC_RETURN_CODE(rpc_response->result());
}

user_matching_manager::matching_sync_result user_matching_manager::acknowledge_matching_sync(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingEventSync& sync, uint64_t source_matchsvr_id) {
  matching_sync_result result;
  const uint64_t local_unit_id = get_current_unit_id();
  if (sync.unit_id() == 0 || !sync.has_unit_view() || sync.unit_view().unit().unit_id() != sync.unit_id()) {
    FWLOGERROR("{} ignore invalid matching Unit sync, sync_unit_id={}, has_view={}", *owner_, sync.unit_id(),
               sync.has_unit_view());
    return result;
  }
  if (local_unit_id != 0 && local_unit_id != sync.unit_id() && is_in_matching()) {
    FWLOGERROR("{} ignore matching sync from unexpected Unit, local_unit_id={}, sync_unit_id={}, local_status={}",
               *owner_, local_unit_id, sync.unit_id(), static_cast<int>(data_.view().status()));
    result.accepted = true;
    result.acknowledge_event_id = sync.unit_view().last_event_id();
    return result;
  }
  if (source_matchsvr_id != 0 && data_.matchsvr_server_id() != 0 && data_.matchsvr_server_id() != source_matchsvr_id &&
      local_unit_id == sync.unit_id()) {
    FWLOGERROR(
        "{} ignore matching sync from unexpected Matchsvr, unit_id={}, expected_matchsvr_id={:#x}, "
        "source_matchsvr_id={:#x}",
        *owner_, sync.unit_id(), data_.matchsvr_server_id(), source_matchsvr_id);
    return result;
  }
  if (source_matchsvr_id != 0) {
    data_.set_matchsvr_server_id(source_matchsvr_id);
    dirty_ = true;
  }

  FWLOGDEBUG(
      "{} acknowledge matching Unit sync, unit_id={}, event_count={}, local_view_event_id={}, acknowledge_event_id={}",
      *owner_, sync.unit_id(), sync.event_logs_size(), get_last_event_id(), data_.acknowledge_event_id());
  update_view(sync.unit_view());
  bool has_pending_event = processing_event_id_ != 0;
  // WAL 增量已压缩或重新订阅时只会收到当前完整视图。此时不重放 Lobby 缓存，而是从 Matchsvr
  // 当前状态恢复尚未完成的副作用，并在成功后一次推进到快照游标。
  if (sync.event_logs().empty() && data_.acknowledge_event_id() < get_last_event_id() && processing_event_id_ == 0) {
    if (data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING) {
      processing_event_id_ = get_last_event_id();
      result.confirm_event_id = processing_event_id_;
      has_pending_event = true;
    } else {
      bool snapshot_effect_succeeded = true;
      if (data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED &&
          !owner_->get_user_orbit_manager().is_orbit_room_exist()) {
        snapshot_effect_succeeded = owner_->get_user_orbit_manager().join_orbit_room(
                                        ctx, data_.view().orbit_room_key(), data_.view().orbit_expired_timepoint()) ==
                                    PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
      }
      if (snapshot_effect_succeeded) {
        data_.set_acknowledge_event_id(get_last_event_id());
        dirty_ = true;
      } else {
        has_pending_event = true;
      }
    }
  }
  for (const auto& event_log : sync.event_logs()) {
    if (event_log.event_id() <= data_.acknowledge_event_id()) {
      continue;
    }
    if (event_log.event_id() != data_.acknowledge_event_id() + 1 ||
        event_log.event_id() > sync.unit_view().last_event_id()) {
      FWLOGERROR(
          "{} stop matching Unit sync at non-contiguous event, unit_id={}, event_id={}, acknowledge_event_id={}, "
          "view_event_id={}",
          *owner_, sync.unit_id(), event_log.event_id(), data_.acknowledge_event_id(),
          sync.unit_view().last_event_id());
      has_pending_event = true;
      break;
    }
    if (processing_event_id_ != 0) {
      has_pending_event = true;
      break;
    }

    if (event_log.event_type() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_CONFIRM_REQUIRED &&
        data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING) {
      processing_event_id_ = event_log.event_id();
      result.confirm_event_id = event_log.event_id();
      has_pending_event = true;
      break;
    }

    if (event_log.event_type() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_BATTLE_READY &&
        !owner_->get_user_orbit_manager().is_orbit_room_exist()) {
      const int32_t join_result = owner_->get_user_orbit_manager().join_orbit_room(
          ctx, data_.view().orbit_room_key(), data_.view().orbit_expired_timepoint());
      if (join_result != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
        FWLOGERROR("{} handle matching battle-ready event failed, unit_id={}, event_id={}, result={}", *owner_,
                   sync.unit_id(), event_log.event_id(), join_result);
        has_pending_event = true;
        break;
      }
    }

    data_.set_acknowledge_event_id(event_log.event_id());
    dirty_ = true;
  }
  result.accepted = true;
  result.has_pending_event = has_pending_event;
  result.acknowledge_event_id = data_.acknowledge_event_id();
  FWLOGDEBUG(
      "{} acknowledge matching Unit sync finish, unit_id={}, status={}, view_event_id={}, acknowledge_event_id={}, "
      "confirm_event_id={}",
      *owner_, get_current_unit_id(), static_cast<int>(data_.view().status()), get_last_event_id(),
      result.acknowledge_event_id, result.confirm_event_id);
  on_client_view_changed(ctx);
  return result;
}

bool user_matching_manager::finish_matching_event(rpc::context& ctx, uint64_t unit_id, int64_t event_id, bool success) {
  if (unit_id != get_current_unit_id() || event_id <= 0 || processing_event_id_ != event_id) {
    FWLOGERROR(
        "{} ignore mismatched matching event completion, unit_id={}, current_unit_id={}, event_id={}, "
        "processing_event_id={}, success={}",
        *owner_, unit_id, get_current_unit_id(), event_id, processing_event_id_, success);
    return false;
  }
  processing_event_id_ = 0;
  if (!success) {
    return false;
  }
  if (event_id <= data_.acknowledge_event_id() || event_id > get_last_event_id()) {
    FWLOGERROR(
        "{} reject invalid matching event completion, unit_id={}, event_id={}, acknowledge_event_id={}, "
        "view_event_id={}",
        *owner_, unit_id, event_id, data_.acknowledge_event_id(), get_last_event_id());
    return false;
  }
  data_.set_acknowledge_event_id(event_id);
  on_client_view_changed(ctx);
  return true;
}

const PROJECT_NAMESPACE_ID::DMatchingUnitView& user_matching_manager::get_view() const { return data_.view(); }

PROJECT_NAMESPACE_ID::DMatchingClientView user_matching_manager::get_client_view() const {
  PROJECT_NAMESPACE_ID::DMatchingClientView result;
  dump_client_view(result);
  return result;
}

int64_t user_matching_manager::get_last_event_id() const { return data_.view().last_event_id(); }

uint64_t user_matching_manager::get_current_matchsvr_server_id() const { return data_.matchsvr_server_id(); }

void user_matching_manager::update_view(const PROJECT_NAMESPACE_ID::DMatchingUnitView& view) {
  const bool unit_changed = get_current_unit_id() != view.unit().unit_id();
  if (!unit_changed && view.last_event_id() < get_last_event_id()) {
    return;
  }
  if (atfw::atapp::protobuf_equal(data_.view(), view)) {
    return;
  }

  protobuf_copy_message(*data_.mutable_view(), view);
  if (unit_changed) {
    data_.set_acknowledge_event_id(0);
    processing_event_id_ = 0;
    last_reported_acknowledge_event_id_ = 0;
    last_heartbeat_time_ = 0;
    periodic_heartbeat_inflight_ = false;
  }
  dirty_ = true;
}

void user_matching_manager::clear_matching_state() {
  data_.Clear();
  processing_event_id_ = 0;
  last_reported_acknowledge_event_id_ = 0;
  last_heartbeat_time_ = 0;
  periodic_heartbeat_inflight_ = false;
}

void user_matching_manager::dump_dirty_data(PROJECT_NAMESPACE_ID::DMatchingClientViewDirtyChg& output) const {
  output.Clear();
  dump_client_view(*output.mutable_client_view());
}

void user_matching_manager::dump_client_view(PROJECT_NAMESPACE_ID::DMatchingClientView& output) const {
  if (!data_.has_view()) {
    return;
  }
  output.set_unit_id(data_.view().unit().unit_id());
  output.set_status(data_.view().status());
  output.set_result(data_.view().result());
  output.set_expire_time(data_.view().expire_time());
  output.set_selected_level_id(data_.view().selected_level_id());
  output.set_faction_id(data_.view().faction_id());
}

int32_t user_matching_manager::fill_matching_scope(const PROJECT_NAMESPACE_ID::DLevelSelect& level_select,
                                                   const std::string& battle_version,
                                                   PROJECT_NAMESPACE_ID::DMatchingScope& output,
                                                   std::vector<int32_t>& acceptable_level_ids) const {
  output.Clear();
  acceptable_level_ids.clear();
  if (level_select.level_ids_size() <= 0) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
  }

  acceptable_level_ids.assign(level_select.level_ids().begin(), level_select.level_ids().end());
  if (acceptable_level_ids.empty() || std::any_of(acceptable_level_ids.begin(), acceptable_level_ids.end(),
                                                  [](int32_t level_id) { return level_id <= 0; })) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
  }
  std::sort(acceptable_level_ids.begin(), acceptable_level_ids.end());
  acceptable_level_ids.erase(std::unique(acceptable_level_ids.begin(), acceptable_level_ids.end()),
                             acceptable_level_ids.end());
  output.set_region(level_select.region());
  output.set_battle_version(battle_version);

  int32_t level_type = 0;
  int32_t matching_pool_id = 0;
  for (int32_t level_id : acceptable_level_ids) {
    const auto level_cfg = excel::get_ExcelLevel_by_level_id(level_id);
    if (!level_cfg) {
      return PROJECT_NAMESPACE_ID::EN_LEVEL_CFG_NOT_FOUND;
    }
    FWLOGDEBUG("{} fill_matching_scope, level_type={}, level_id={}, battle_version={}, level_cfg={}", *owner_,
               level_cfg->level_type(), level_id, battle_version, level_cfg ? level_cfg->DebugString() : "null");
    if ((level_type > 0 && level_type != level_cfg->level_type()) ||
        (matching_pool_id > 0 && matching_pool_id != level_cfg->matching_pool_id())) {
      return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
    }
    level_type = level_cfg->level_type();
    matching_pool_id = level_cfg->matching_pool_id();
  }
  if (matching_pool_id <= 0 || !excel::get_ExcelMatchingPool_by_id(matching_pool_id)) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_POOL_ID_CFG_NOT_FOUND;
  }

  output.set_level_type(level_type);
  output.set_matching_pool_id(matching_pool_id);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

rpc::result_code_type user_matching_manager::fill_matching_unit(rpc::context& ctx,
                                                                PROJECT_NAMESPACE_ID::DMatchingUnit& output) const {
  output.Clear();
  const int64_t unit_id = RPC_AWAIT_TYPE_RESULT(
      rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_MATCHING_UNIT));
  if (unit_id <= 0) {
    RPC_RETURN_CODE(static_cast<int32_t>(unit_id));
  }
  output.set_unit_id(static_cast<uint64_t>(unit_id));
  output.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_SEARCHING);
  output.mutable_parameter()->set_role_level(owner_->get_user_data().user_level());
  output.mutable_parameter()->set_search_start_time(atfw::util::time::time_utility::get_now());
  auto* matching_user = output.add_users();
  fill_operator_user(*matching_user->mutable_user_key());
  matching_user->set_confirm_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING);
  output.set_client_version(owner_->get_client_info().client_version());
  // 组队未接入前，队长固定为当前玩家，unit 只包含当前玩家。
  fill_operator_user(*output.mutable_captain_user_key());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

void user_matching_manager::fill_operator_user(PROJECT_NAMESPACE_ID::DUserIDKey& output) const {
  output.set_user_id(owner_->get_user_id());
  output.set_zone_id(owner_->get_zone_id());
}

uint64_t user_matching_manager::get_current_unit_id() const { return data_.view().unit().unit_id(); }

int64_t user_matching_manager::get_acknowledge_event_id() const { return data_.acknowledge_event_id(); }

void user_matching_manager::on_client_view_changed(rpc::context& ctx) {
  dirty_ = true;
  owner_->insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(this), "user.user_matching_manager.dirty", [](gsl::string_view, user&) {
        user::dirty_sync_handle_t handle;
        handle.build_fn = [](rpc::context&, user& user_inst, user::dirty_message_container& output) {
          auto& mgr = user_inst.get_user_matching_manager();
          if (!mgr.dirty_) {
            return;
          }
          if (!output.user_dirty) {
            output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
          }
          if (!output.user_dirty) {
            FWLOGERROR("malloc dirty msg body failed");
            return;
          }
          mgr.dump_dirty_data(*output.user_dirty->mutable_dirty_matching_chg());
        };
        handle.clear_fn = [](rpc::context&, user& user_inst) { user_inst.get_user_matching_manager().clear_dirty(); };
        return handle;
      });
  owner_->send_all_syn_msg(ctx);
}

void user_matching_manager::on_gm_cmd_start_matching(std::shared_ptr<rpc::context> ctx, user_ptr_t user_inst,
                                                     std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp,
                                                     ::util::cli::cmd_option_list& params) {
  if (!user_inst) {
    rsp->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    return;
  }

  if (!task_action_user_gm_cmd_nomsg::check_params_number(params, 2)) {
    return;
  }

  int32_t level_id = 0;
  level_id = params[0]->to_int32();
  if (level_id <= 0) {
    rsp->set_result_code(PROJECT_NAMESPACE_ID::EN_LEVEL_CFG_NOT_FOUND);
    return;
  }
  std::string region = params[1]->to_string();
  auto user_ptr = user_inst->shared_from_this();

  auto invoke_result = rpc::async_invoke(
      *ctx, "user_matching_manager.gm_start_matching",
      [user_ptr, rsp, level_id, region](rpc::context& child_ctx) -> rpc::result_code_type {
        auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSMatchingStartReq>(child_ctx);
        auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCMatchingStartRsp>(child_ctx);
        rpc_request->set_battle_version("gm_test");
        auto* level_select = rpc_request->mutable_level_select();
        auto cfg = excel::get_ExcelLevel_by_level_id(level_id);
        if (!cfg) {
          FWLOGERROR("{} gm_start_matching failed to find level cfg, level_id={}", *user_ptr, level_id);
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_LEVEL_CFG_NOT_FOUND);
        }
        level_select->add_level_ids(level_id);
        level_select->set_region(region);
        const int32_t ret = RPC_AWAIT_CODE_RESULT(
            user_ptr->get_user_matching_manager().start_matching(child_ctx, *rpc_request, *rpc_response));
        if (ret != 0) {
          rsp->set_result_code(ret);
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    rsp->set_result_code(*invoke_result.get_error());
    return;
  }
  return;
}

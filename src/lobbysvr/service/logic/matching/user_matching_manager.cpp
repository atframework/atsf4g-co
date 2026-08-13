// Copyright 2026 atframework

#include "logic/matching/user_matching_manager.h"

#include <algorithm>

#include <atframe/atapp.h>
#include <config/excel/config_easy_api.h>
#include <config/server_frame_build_feature.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <data/user.h>
#include <rpc/db/uuid.h>
#include <rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h>
#include <rpc/matching/matching_api.h>
#include <rpc/matching/matchsvrservice.atfw.gen.h>
#include <rpc/rpc_shared_message.h>

namespace {
bool same_user(const PROJECT_NAMESPACE_ID::DUserIDKey& left, const PROJECT_NAMESPACE_ID::DUserIDKey& right) {
  return left.user_id() == right.user_id() && left.zone_id() == right.zone_id();
}

void erase_unit(PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& snapshot, uint64_t unit_id) {
  auto* units = snapshot.mutable_units();
  for (int index = 0; index < units->size(); ++index) {
    if (units->Get(index).unit_id() == unit_id) {
      units->DeleteSubrange(index, 1);
      return;
    }
  }
}

bool is_terminal_status(PROJECT_NAMESPACE_ID::EnMatchingRoomStatus status) {
  return status == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED ||
         status == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED ||
         status == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT ||
         status == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED;
}
}  // namespace

user_matching_manager::user_matching_manager(user& owner) : owner_(&owner), dirty_(false) {}

user_matching_manager::~user_matching_manager() = default;

void user_matching_manager::create_init(rpc::context&) {
  data_.Clear();
  pending_switch_matching_id_.clear();
  dirty_ = false;
}

rpc::result_code_type user_matching_manager::login_init(rpc::context& ctx) {
  if (data_.snapshot().matching_id().empty() || is_terminal_status(data_.snapshot().status())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSMatchingCheckReq>(ctx);
  auto response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCMatchingCheckRsp>(ctx);
  request->set_matching_id(data_.snapshot().matching_id());
  for (const auto& unit : data_.snapshot().units()) {
    for (const auto& matching_user : unit.users()) {
      if (matching_user.user_key().user_id() == owner_->get_user_id() &&
          matching_user.user_key().zone_id() == owner_->get_zone_id()) {
        request->set_unit_id(unit.unit_id());
        break;
      }
    }
    if (request->unit_id() != 0) {
      break;
    }
  }
  request->set_acknowledge_event_id(data_.client_acknowledge_event_id());
  if (request->unit_id() == 0) {
    data_.Clear();
    dirty_ = true;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int32_t result = RPC_AWAIT_CODE_RESULT(check_matching(ctx, *request, *response));
  if (result == PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND) {
    data_.Clear();
    dirty_ = true;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  RPC_RETURN_CODE(result);
}

void user_matching_manager::init_from_table_data(rpc::context&, const PROJECT_NAMESPACE_ID::table_user& user) {
  data_.Clear();
  if (user.has_matching_data()) {
    protobuf_copy_message(data_, user.matching_data());
  }
  pending_switch_matching_id_.clear();
  dirty_ = false;
}

int user_matching_manager::dump(rpc::context&, PROJECT_NAMESPACE_ID::table_user& user) const {
  protobuf_copy_message(*user.mutable_matching_data(), data_);
  return 0;
}

bool user_matching_manager::is_dirty() const { return dirty_; }

void user_matching_manager::clear_dirty() { dirty_ = false; }

rpc::result_code_type user_matching_manager::start_matching(rpc::context& ctx,
                                                            const PROJECT_NAMESPACE_ID::CSMatchingStartReq& request,
                                                            PROJECT_NAMESPACE_ID::SCMatchingStartRsp& response) {
  if (!data_.snapshot().matching_id().empty() && !is_terminal_status(data_.snapshot().status())) {
    FWPLOGERROR(*owner_, "start matching rejected by active matching, matching_id={}, status={}",
                data_.snapshot().matching_id(), static_cast<int>(data_.snapshot().status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
  }

  FWPLOGDEBUG(*owner_, "start matching, level_type={}, level_id={}, battle_version={}, request={}",
              request.level_select().level_type(), request.level_select().level_id(), request.battle_version(),
              request.DebugString());

  // TODO 通知battle锁背包
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCreateReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  // TODO 接入battle_versnion
  int32_t ret = fill_matching_scope(request.level_select(), request.battle_version(), *rpc_request->mutable_scope());
  if (ret != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
    FWPLOGERROR(*owner_, "fill_matching_scope failed, ret={}, level_type={}, level_id={}, battle_version={}", ret,
                request.level_select().level_type(), request.level_select().level_id(), request.battle_version());
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(fill_matching_unit(ctx, *rpc_request->mutable_unit()));
  if (ret != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
    FWPLOGERROR(*owner_, "fill_matching_unit failed, ret={}, level_type={}, level_id={}, battle_version={}", ret,
                request.level_select().level_type(), request.level_select().level_id(), request.battle_version());
    RPC_RETURN_CODE(ret);
  }

  fill_operator_user(*rpc_request->mutable_operator_user());

  // 后续接组队

  const auto& operator_user = rpc_request->operator_user();
  bool operator_is_member = false;
  for (auto& matching_user : *rpc_request->mutable_unit()->mutable_users()) {
    matching_user.set_confirm_status(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING);
    operator_is_member = operator_is_member || same_user(matching_user.user_key(), operator_user);
  }

  rpc_request->set_subscriber_server_id(logic_config::me()->get_local_server_id());
  // 新 matching_id 的 WAL 从头计数，不能沿用上一场客户端游标。
  rpc_request->set_acknowledge_event_id(0);
  const uint64_t matchsvr_id = rpc::matching_api::get_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWPLOGERROR(*owner_, "start matching failed, no ready matchsvr, unit_id={}", rpc_request->unit().unit_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }

  int32_t result = RPC_AWAIT_CODE_RESULT(rpc::matching::create_matching(ctx, matchsvr_id, *rpc_request, *rpc_response));
  if (result < 0) {
    FWPLOGERROR(*owner_, "start matching RPC failed, matchsvr_id={:#x}, unit_id={}, result={}({})", matchsvr_id,
                rpc_request->unit().unit_id(), result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (rpc_response->result() != 0) {
    FWPLOGERROR(*owner_, "start matching rejected by matchsvr, matchsvr_id={:#x}, unit_id={}, result={}({})",
                matchsvr_id, rpc_request->unit().unit_id(), rpc_response->result(),
                protobuf_mini_dumper_get_error_msg(rpc_response->result()));
    RPC_RETURN_CODE(rpc_response->result());
  }
  update_snapshot(rpc_response->snapshot());

  response.set_matching_id(rpc_response->snapshot().matching_id());
  protobuf_copy_message(*response.mutable_level_parameter(), request.level_select());

  FWPLOGDEBUG(*owner_, "start matching finish, level_type={}, level_id={}, battle_version={}, matching_id={}",
              request.level_select().level_type(), request.level_select().level_id(), request.battle_version(),
              response.matching_id());

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type user_matching_manager::check_matching(rpc::context& ctx,
                                                            const PROJECT_NAMESPACE_ID::CSMatchingCheckReq& request,
                                                            PROJECT_NAMESPACE_ID::SCMatchingCheckRsp& response) {
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCheckReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);

  FWPLOGDEBUG(*owner_, "check matching, request={}", request.DebugString());

  if (request.matching_id() != data_.snapshot().matching_id()) {
    FWPLOGERROR(*owner_, "check matching rejected by local snapshot, request_matching_id={}, local_matching_id={}",
                request.matching_id(), data_.snapshot().matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_matching_id(data_.snapshot().matching_id());
  const uint64_t unit_id = get_current_unit_id();
  if (unit_id == 0) {
    FWPLOGERROR(*owner_, "check matching failed to find current unit, matching_id={}", request.matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_unit_id(unit_id);
  fill_operator_user(*rpc_request->mutable_operator_user());
  rpc_request->set_subscriber_server_id(logic_config::me()->get_local_server_id());
  if (data_.snapshot().matching_id().empty() || data_.snapshot().matching_id() == request.matching_id()) {
    const int64_t acknowledged_event_id =
        std::min(std::max<int64_t>(0, request.acknowledge_event_id()), data_.last_event_id());
    data_.set_client_acknowledge_event_id(std::max(data_.client_acknowledge_event_id(), acknowledged_event_id));
    dirty_ = true;
  }
  rpc_request->set_acknowledge_event_id(get_acknowledge_event_id());

  const uint64_t matchsvr_id = rpc::matching_api::get_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWPLOGERROR(*owner_, "check matching failed, no ready matchsvr, matching_id={}, unit_id={}", request.matching_id(),
                unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  int32_t result = RPC_AWAIT_CODE_RESULT(rpc::matching::check_matching(ctx, matchsvr_id, *rpc_request, *rpc_response));
  if (result < 0) {
    FWPLOGERROR(*owner_, "check matching RPC failed, matchsvr_id={:#x}, matching_id={}, unit_id={}, result={}({})",
                matchsvr_id, request.matching_id(), unit_id, result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (rpc_response->result() != 0) {
    FWPLOGERROR(*owner_, "check matching rejected by matchsvr, matching_id={}, unit_id={}, result={}({})",
                request.matching_id(), unit_id, rpc_response->result(),
                protobuf_mini_dumper_get_error_msg(rpc_response->result()));
    RPC_RETURN_CODE(rpc_response->result());
  }
  update_snapshot(rpc_response->snapshot());
  protobuf_copy_message(*response.mutable_snapshot(), rpc_response->snapshot());
  FWPLOGDEBUG(*owner_, "check matching finish, matching_id={}, unit_id={}, status={}, last_event_id={}",
              rpc_response->snapshot().matching_id(), unit_id, static_cast<int>(rpc_response->snapshot().status()),
              rpc_response->snapshot().last_event_id());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type user_matching_manager::cancel_matching(rpc::context& ctx,
                                                             const PROJECT_NAMESPACE_ID::CSMatchingCancelReq& request,
                                                             PROJECT_NAMESPACE_ID::SCMatchingCancelRsp& response) {
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCancelReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  FWPLOGDEBUG(*owner_, "cancel matching, matching_id={}", request.matching_id());
  if (request.matching_id() != data_.snapshot().matching_id()) {
    FWPLOGERROR(*owner_, "cancel matching rejected by local snapshot, request_matching_id={}, local_matching_id={}",
                request.matching_id(), data_.snapshot().matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_matching_id(data_.snapshot().matching_id());
  const uint64_t unit_id = get_current_unit_id();
  if (unit_id == 0) {
    FWPLOGERROR(*owner_, "cancel matching failed to find current unit, matching_id={}", request.matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_unit_id(unit_id);
  fill_operator_user(*rpc_request->mutable_operator_user());
  const uint64_t matchsvr_id = rpc::matching_api::get_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWPLOGERROR(*owner_, "cancel matching failed, no ready matchsvr, matching_id={}, unit_id={}", request.matching_id(),
                unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  int32_t result = RPC_AWAIT_CODE_RESULT(rpc::matching::cancel_matching(ctx, matchsvr_id, *rpc_request, *rpc_response));
  if (result < 0) {
    FWPLOGERROR(*owner_, "cancel matching RPC failed, matchsvr_id={:#x}, matching_id={}, unit_id={}, result={}({})",
                matchsvr_id, request.matching_id(), unit_id, result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (rpc_response->has_snapshot()) {
    update_snapshot(rpc_response->snapshot());
    protobuf_copy_message(*response.mutable_snapshot(), rpc_response->snapshot());
  }
  FWPLOGDEBUG(*owner_, "cancel matching finish, matching_id={}, unit_id={}, result={}({}), status={}",
              request.matching_id(), unit_id, rpc_response->result(),
              protobuf_mini_dumper_get_error_msg(rpc_response->result()),
              rpc_response->has_snapshot() ? static_cast<int>(rpc_response->snapshot().status())
                                           : static_cast<int>(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_INVALID));
  RPC_RETURN_CODE(rpc_response->result());
}

// 玩家的静默确认
rpc::result_code_type user_matching_manager::confirm_matching(rpc::context& ctx,
                                                              const PROJECT_NAMESPACE_ID::CSMatchingConfirmReq& request,
                                                              PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp& response) {
  // TODO 填充玩家的战斗数据
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingConfirmReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  FWPLOGDEBUG(*owner_, "confirm matching, matching_id={}, confirmed={}", request.matching_id(), request.confirmed());
  if (request.matching_id() != data_.snapshot().matching_id()) {
    FWPLOGERROR(*owner_, "confirm matching rejected by local snapshot, request_matching_id={}, local_matching_id={}",
                request.matching_id(), data_.snapshot().matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_matching_id(data_.snapshot().matching_id());
  const uint64_t unit_id = get_current_unit_id();
  if (unit_id == 0) {
    FWPLOGERROR(*owner_, "confirm matching failed to find current unit, matching_id={}", request.matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_unit_id(unit_id);
  rpc_request->set_confirmed(request.confirmed());
  fill_operator_user(*rpc_request->mutable_operator_user());
  rpc_request->set_subscriber_server_id(logic_config::me()->get_local_server_id());
  rpc_request->set_acknowledge_event_id(get_acknowledge_event_id());
  const uint64_t matchsvr_id = rpc::matching_api::get_matchsvr_server_id();
  if (matchsvr_id == 0) {
    FWPLOGERROR(*owner_, "confirm matching failed, no ready matchsvr, matching_id={}, unit_id={}",
                request.matching_id(), unit_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  int32_t result =
      RPC_AWAIT_CODE_RESULT(rpc::matching::confirm_matching(ctx, matchsvr_id, *rpc_request, *rpc_response));
  if (result < 0) {
    FWPLOGERROR(*owner_, "confirm matching RPC failed, matchsvr_id={:#x}, matching_id={}, unit_id={}, result={}({})",
                matchsvr_id, request.matching_id(), unit_id, result, protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (rpc_response->has_snapshot()) {
    update_snapshot(rpc_response->snapshot());
    protobuf_copy_message(*response.mutable_snapshot(), rpc_response->snapshot());
  }
  FWPLOGDEBUG(*owner_, "confirm matching finish, matching_id={}, unit_id={}, confirmed={}, result={}({}), status={}",
              request.matching_id(), unit_id, request.confirmed(), rpc_response->result(),
              protobuf_mini_dumper_get_error_msg(rpc_response->result()),
              rpc_response->has_snapshot() ? static_cast<int>(rpc_response->snapshot().status())
                                           : static_cast<int>(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_INVALID));
  RPC_RETURN_CODE(rpc_response->result());
}

void user_matching_manager::acknowledge_matching_sync(rpc::context& ctx,
                                                      const PROJECT_NAMESPACE_ID::SSMatchingEventSync& sync) {
  const std::string previous_matching_id = data_.snapshot().matching_id();
  const bool is_switch =
      previous_matching_id != sync.matching_id() && pending_switch_matching_id_ == sync.matching_id();
  if (!previous_matching_id.empty() && previous_matching_id != sync.matching_id() && !is_switch &&
      !is_terminal_status(data_.snapshot().status())) {
    FWPLOGERROR(*owner_,
                "ignore matching event sync from unexpected room, local_matching_id={}, sync_matching_id={}, "
                "pending_switch_matching_id={}, local_status={}",
                previous_matching_id, sync.matching_id(), pending_switch_matching_id_,
                static_cast<int>(data_.snapshot().status()));
    return;
  }

  FWPLOGDEBUG(*owner_,
              "acknowledge matching event sync, matching_id={}, previous_matching_id={}, is_switch={}, snapshot={}, "
              "event_count={}, local_last_event_id={}",
              sync.matching_id(), previous_matching_id, is_switch, sync.has_room_snapshot(), sync.event_logs_size(),
              data_.last_event_id());

  if (sync.has_room_snapshot()) {
    update_snapshot(sync.room_snapshot());
  } else {
    if (previous_matching_id != sync.matching_id()) {
      data_.mutable_snapshot()->Clear();
      data_.mutable_snapshot()->set_matching_id(sync.matching_id());
      data_.set_last_event_id(0);
      data_.set_client_acknowledge_event_id(0);
    }
    for (const auto& event_log : sync.event_logs()) {
      if (event_log.event_id() <= data_.last_event_id()) {
        FWPLOGDEBUG(*owner_, "skip duplicated matching event, matching_id={}, event_id={}, local_last_event_id={}",
                    sync.matching_id(), event_log.event_id(), data_.last_event_id());
        continue;
      }
      apply_event(event_log);
      data_.set_last_event_id(event_log.event_id());
      data_.mutable_snapshot()->set_last_event_id(event_log.event_id());
    }
    dirty_ = true;
  }
  if (is_switch) {
    pending_switch_matching_id_.clear();
  }
  FWPLOGDEBUG(*owner_, "acknowledge matching event sync finish, matching_id={}, status={}, last_event_id={}",
              data_.snapshot().matching_id(), static_cast<int>(data_.snapshot().status()), data_.last_event_id());
  send_log_sync(ctx, sync, is_switch);
}

const PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& user_matching_manager::get_snapshot() const {
  return data_.snapshot();
}

int64_t user_matching_manager::get_last_event_id() const { return data_.last_event_id(); }

void user_matching_manager::update_snapshot(const PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& snapshot) {
  if (data_.snapshot().matching_id() != snapshot.matching_id() || snapshot.last_event_id() >= data_.last_event_id()) {
    if (data_.snapshot().matching_id() != snapshot.matching_id()) {
      data_.set_client_acknowledge_event_id(0);
    }
    protobuf_copy_message(*data_.mutable_snapshot(), snapshot);
    data_.set_last_event_id(std::max(data_.last_event_id(), snapshot.last_event_id()));
    dirty_ = true;
  }
}

void user_matching_manager::apply_event(const PROJECT_NAMESPACE_ID::DMatchingEventLog& event_log) {
  auto& snapshot = *data_.mutable_snapshot();
  snapshot.set_status(event_log.room_status());
  switch (event_log.event_case()) {
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::kAddUnit:
      erase_unit(snapshot, event_log.add_unit().unit_id());
      protobuf_copy_message(*snapshot.add_units(), event_log.add_unit());
      break;
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::kRemoveUnit:
      for (const auto& matching_user : event_log.remove_unit().unit().users()) {
        if (matching_user.user_key().user_id() == owner_->get_user_id() &&
            matching_user.user_key().zone_id() == owner_->get_zone_id()) {
          pending_switch_matching_id_ = event_log.remove_unit().switch_to_matching_id();
          break;
        }
      }
      erase_unit(snapshot, event_log.remove_unit().unit().unit_id());
      break;
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::kMatched:
      // The matched event carries the final room snapshot (including the battle room ID).
      // Replace the incremental view before forwarding it to the client.
      protobuf_copy_message(snapshot, event_log.matched());
      break;
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::kTimeout:
      snapshot.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT);
      break;
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::kCancel:
      snapshot.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED);
      break;
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::kFailed:
      snapshot.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED);
      snapshot.set_result(event_log.failed());
      break;
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::kNotifyConfirm:
      snapshot.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING);
      snapshot.set_confirm_expire_time(event_log.notify_confirm());
      break;
    case PROJECT_NAMESPACE_ID::DMatchingEventLog::EVENT_NOT_SET:
      break;
  }
}

int32_t user_matching_manager::fill_matching_scope(const PROJECT_NAMESPACE_ID::DLevelSelect& level_select,
                                                   const std::string& battle_version,
                                                   PROJECT_NAMESPACE_ID::DMatchingScope& output) const {
  output.Clear();
  if (level_select.level_type() <= 0) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
  }

  output.set_level_type(level_select.level_type());
  output.set_region(level_select.region());
  output.set_battle_version(battle_version);
  output.set_level_id(level_select.level_id());

  // TODO: 接入 level_type -> matching_pool_id 的配置映射（旧项目为 ExcelLevelType.match_making_pool_id）
  auto level_cfg = excel::get_ExcelLevel_by_level_id(level_select.level_id());
  FWPLOGDEBUG(*owner_, "fill_matching_scope, level_type={}, level_id={}, battle_version={}, level_cfg={}",
              level_select.level_type(), level_select.level_id(), battle_version,
              level_cfg ? level_cfg->DebugString() : "null");
  if (!level_cfg) {
    return PROJECT_NAMESPACE_ID::EN_LEVEL_CFG_NOT_FOUND;
  }
  const int32_t matching_pool_id = level_cfg->matching_pool_id();
  if (matching_pool_id <= 0 || !excel::get_ExcelMatchingPool_by_id(matching_pool_id)) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_POOL_ID_CFG_NOT_FOUND;
  }

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

uint64_t user_matching_manager::get_current_unit_id() const {
  for (const auto& unit : data_.snapshot().units()) {
    for (const auto& matching_user : unit.users()) {
      if (matching_user.user_key().user_id() == owner_->get_user_id() &&
          matching_user.user_key().zone_id() == owner_->get_zone_id()) {
        return unit.unit_id();
      }
    }
  }
  return 0;
}

int64_t user_matching_manager::get_acknowledge_event_id() const { return data_.client_acknowledge_event_id(); }

void user_matching_manager::send_log_sync(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingEventSync& sync,
                                          bool is_switch) {
  auto session = owner_->get_session();
  if (!session) {
    FWPLOGDEBUG(*owner_, "skip matching log sync to offline client, matching_id={}, event_count={}", sync.matching_id(),
                sync.event_logs_size());
    return;
  }
  auto output = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCMatchingLogSync>(ctx);
  output->set_matching_id(sync.matching_id());
  output->set_is_switch(is_switch);
  protobuf_copy_message(*output->mutable_event_logs(), sync.event_logs());
  protobuf_copy_message(*output->mutable_snapshot(), sync.room_snapshot());
  FWPLOGDEBUG(*owner_, "send matching log sync to client, matching_id={}, is_switch={}, event_count={}, snapshot={}",
              sync.matching_id(), is_switch, sync.event_logs_size(), sync.has_room_snapshot());
  rpc::lobbysvrclientservice::send_matching_log_sync(ctx, *output, *session);
}

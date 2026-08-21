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
bool same_user(const PROJECT_NAMESPACE_ID::DUserIDKey& left, const PROJECT_NAMESPACE_ID::DUserIDKey& right) {
  return left.user_id() == right.user_id() && left.zone_id() == right.zone_id();
}

static bool init_user_matching_manager_gm_handle() {
  task_action_user_gm_cmd_nomsg::init_gm_cmd("matching_start", user_matching_manager::on_gm_cmd_start_matching,
                                             "matching_start <level_id> ");
  return true;
}

}  // namespace

user_matching_manager::user_matching_manager(user& owner) : owner_(&owner), dirty_(false) {
  static bool init_gm_handle = init_user_matching_manager_gm_handle();
}

user_matching_manager::~user_matching_manager() = default;

void user_matching_manager::create_init(rpc::context&) {
  data_.Clear();
  pending_switch_matching_id_.clear();
  dirty_ = false;
}

rpc::result_code_type user_matching_manager::login_init(rpc::context& ctx) {
  if (!is_in_matching()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSMatchingCheckReq>(ctx);
  auto response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCMatchingCheckRsp>(ctx);
  request->set_matching_id(data_.view().matching_id());
  request->set_unit_id(data_.view().unit().unit_id());
  request->set_acknowledge_event_id(data_.client_acknowledge_event_id());
  if (request->unit_id() == 0) {
    data_.Clear();
    dirty_ = true;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int32_t result = RPC_AWAIT_CODE_RESULT(check_matching(ctx, *request, *response));
  if (result == PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND) {
    // 如何玩家离线前处于匹配，并且已经confirm了匹配，重登后不知道是否已经进入了对局，直接转交给orbit确认
    if (data_.view().status() == PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE) {
      FWPLOGINFO(*owner_, "login_init matching not found, but status is creating battle, matching_id={}",
                 data_.view().matching_id());
      time_t now = atfw::util::time::time_utility::get_now();
      owner_->get_user_orbit_manager().join_orbit_room(ctx, data_.view().orbit_room_key(), now);
    }
    data_.Clear();
    dirty_ = true;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  RPC_RETURN_CODE(result);
}

void user_matching_manager::init_from_table_data(rpc::context&, const PROJECT_NAMESPACE_ID::table_user& user_table) {
  data_.Clear();
  bool migrated_legacy_snapshot = false;
  if (user_table.has_matching_data()) {
    protobuf_copy_message(data_, user_table.matching_data());
  }
  if (!data_.has_view() && data_.has_legacy_snapshot()) {
    const auto& legacy = data_.legacy_snapshot();
    auto* view = data_.mutable_view();
    view->set_matching_id(legacy.matching_id());
    view->set_status(legacy.status());
    view->set_result(legacy.result());
    view->set_expire_time(legacy.expire_time());
    view->set_last_event_id(legacy.last_event_id());
    view->set_confirm_expire_time(legacy.confirm_expire_time());
    protobuf_copy_message(*view->mutable_orbit_room_key(), legacy.orbit_room_key());
    view->set_orbit_expired_timepoint(legacy.orbit_expired_timepoint());
    view->set_selected_level_id(legacy.selected_level_id());
    for (const auto& unit : legacy.units()) {
      bool is_current_unit = false;
      for (const auto& matching_user : unit.users()) {
        is_current_unit = matching_user.user_key().user_id() == owner_->get_user_id() &&
                          matching_user.user_key().zone_id() == owner_->get_zone_id();
        if (is_current_unit) {
          break;
        }
      }
      if (is_current_unit) {
        protobuf_copy_message(*view->mutable_unit(), unit);
        break;
      }
    }
    // 旧快照中的匹配期 faction 序号不是最终战斗 ID，迁移时不再推导 faction_id。
    data_.clear_legacy_snapshot();
    migrated_legacy_snapshot = true;
  }
  pending_switch_matching_id_.clear();
  dirty_ = migrated_legacy_snapshot;
}

int user_matching_manager::dump(rpc::context&, PROJECT_NAMESPACE_ID::table_user& user_table) const {
  protobuf_copy_message(*user_table.mutable_matching_data(), data_);
  return 0;
}

bool user_matching_manager::is_dirty() const { return dirty_; }

void user_matching_manager::clear_dirty() { dirty_ = false; }

bool user_matching_manager::is_in_matching() const {
  if (data_.view().matching_id().empty()) {
    return false;
  }

  switch (data_.view().status()) {
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING:
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING:
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE:
      return true;
    default:
      return false;
  }
}

rpc::result_code_type user_matching_manager::start_matching(rpc::context& ctx,
                                                            const PROJECT_NAMESPACE_ID::CSMatchingStartReq& request,
                                                            PROJECT_NAMESPACE_ID::SCMatchingStartRsp& response) {
  if (is_in_matching()) {
    FWPLOGERROR(*owner_, "start matching rejected by active matching, matching_id={}, status={}",
                data_.view().matching_id(), static_cast<int>(data_.view().status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_USER_ALREADY_IN_MATCHING);
  }

  if (owner_->is_in_orbit()) {
    FWPLOGERROR(*owner_, "start matching rejected by orbit, matching_id={}, status={}", data_.view().matching_id(),
                static_cast<int>(data_.view().status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT);
  }

  FWPLOGDEBUG(*owner_,
              "start matching, level_type={}, preferred_level_id={}, level_count={}, battle_version={}, request={}",
              request.level_select().level_type(), request.level_select().level_id(),
              request.level_select().level_ids_size(), request.battle_version(), request.DebugString());

  // TODO 通知battle锁背包
  auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCreateReq>(ctx);
  auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(ctx);
  // TODO 接入battle_versnion
  std::vector<int32_t> acceptable_level_ids;
  int32_t preferred_level_id = 0;
  int32_t ret = fill_matching_scope(request.level_select(), request.battle_version(), *rpc_request->mutable_scope(),
                                    acceptable_level_ids, preferred_level_id);
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
  rpc_request->set_preferred_level_id(preferred_level_id);
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
  update_view(rpc_response->snapshot());

  response.set_matching_id(rpc_response->snapshot().matching_id());
  auto* level_parameter = response.mutable_level_parameter();
  level_parameter->set_level_type(request.level_select().level_type());
  level_parameter->set_level_id(preferred_level_id);
  level_parameter->set_region(request.level_select().region());
  for (int32_t level_id : acceptable_level_ids) {
    level_parameter->add_level_ids(level_id);
  }

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

  if (request.matching_id() != data_.view().matching_id()) {
    FWPLOGERROR(*owner_, "check matching rejected by local snapshot, request_matching_id={}, local_matching_id={}",
                request.matching_id(), data_.view().matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_matching_id(data_.view().matching_id());
  const uint64_t unit_id = get_current_unit_id();
  if (unit_id == 0) {
    FWPLOGERROR(*owner_, "check matching failed to find current unit, matching_id={}", request.matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_unit_id(unit_id);
  fill_operator_user(*rpc_request->mutable_operator_user());
  rpc_request->set_subscriber_server_id(logic_config::me()->get_local_server_id());
  if (data_.view().matching_id().empty() || data_.view().matching_id() == request.matching_id()) {
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
  update_view(rpc_response->snapshot());
  protobuf_copy_message(*response.mutable_view(), rpc_response->snapshot());
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
  if (request.matching_id() != data_.view().matching_id()) {
    FWPLOGERROR(*owner_, "cancel matching rejected by local snapshot, request_matching_id={}, local_matching_id={}",
                request.matching_id(), data_.view().matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_matching_id(data_.view().matching_id());
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
    update_view(rpc_response->snapshot());
    protobuf_copy_message(*response.mutable_view(), rpc_response->snapshot());
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
  if (request.matching_id() != data_.view().matching_id()) {
    FWPLOGERROR(*owner_, "confirm matching rejected by local snapshot, request_matching_id={}, local_matching_id={}",
                request.matching_id(), data_.view().matching_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  }
  rpc_request->set_matching_id(data_.view().matching_id());
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
    update_view(rpc_response->snapshot());
    protobuf_copy_message(*response.mutable_view(), rpc_response->snapshot());
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
  const std::string previous_matching_id = data_.view().matching_id();
  for (const auto& event_log : sync.event_logs()) {
    if (event_log.event_case() == PROJECT_NAMESPACE_ID::DMatchingEventLog::kRemoveUnit &&
        event_log.remove_unit().unit().unit_id() == data_.view().unit().unit_id()) {
      pending_switch_matching_id_ = event_log.remove_unit().switch_to_matching_id();
    }
  }
  const bool is_switch =
      previous_matching_id != sync.matching_id() && pending_switch_matching_id_ == sync.matching_id();
  if (!previous_matching_id.empty() && previous_matching_id != sync.matching_id() && !is_switch && is_in_matching()) {
    FWPLOGERROR(*owner_,
                "ignore matching event sync from unexpected room, local_matching_id={}, sync_matching_id={}, "
                "pending_switch_matching_id={}, local_status={}",
                previous_matching_id, sync.matching_id(), pending_switch_matching_id_,
                static_cast<int>(data_.view().status()));
    return;
  }

  FWPLOGDEBUG(*owner_,
              "acknowledge matching event sync, matching_id={}, previous_matching_id={}, is_switch={}, view={}, "
              "event_count={}, local_last_event_id={}",
              sync.matching_id(), previous_matching_id, is_switch, sync.has_player_view(), sync.event_logs_size(),
              data_.last_event_id());

  if (!sync.has_player_view()) {
    FWPLOGERROR(*owner_, "ignore matching event sync without player view, matching_id={}", sync.matching_id());
    return;
  }
  update_view(sync.player_view());
  for (const auto& event_log : sync.event_logs()) {
    if (event_log.event_case() == PROJECT_NAMESPACE_ID::DMatchingEventLog::kMatched) {
      owner_->get_user_orbit_manager().join_orbit_room(ctx, data_.view().orbit_room_key(),
                                                       data_.view().orbit_expired_timepoint());
      break;
    }
  }
  if (is_switch) {
    pending_switch_matching_id_.clear();
  }
  FWPLOGDEBUG(*owner_, "acknowledge matching event sync finish, matching_id={}, status={}, last_event_id={}",
              data_.view().matching_id(), static_cast<int>(data_.view().status()), data_.last_event_id());
  send_log_sync(ctx, sync, is_switch);
}

const PROJECT_NAMESPACE_ID::DMatchingPlayerView& user_matching_manager::get_view() const { return data_.view(); }

int64_t user_matching_manager::get_last_event_id() const { return data_.last_event_id(); }

void user_matching_manager::update_view(const PROJECT_NAMESPACE_ID::DMatchingPlayerView& view) {
  const bool matching_id_changed = data_.view().matching_id() != view.matching_id();
  if (matching_id_changed || view.last_event_id() >= data_.last_event_id()) {
    if (matching_id_changed) {
      data_.set_client_acknowledge_event_id(0);
    }
    protobuf_copy_message(*data_.mutable_view(), view);
    data_.clear_legacy_snapshot();
    data_.set_last_event_id(matching_id_changed ? view.last_event_id()
                                                : std::max(data_.last_event_id(), view.last_event_id()));
    dirty_ = true;
  }
}

int32_t user_matching_manager::fill_matching_scope(const PROJECT_NAMESPACE_ID::DLevelSelect& level_select,
                                                   const std::string& battle_version,
                                                   PROJECT_NAMESPACE_ID::DMatchingScope& output,
                                                   std::vector<int32_t>& acceptable_level_ids,
                                                   int32_t& preferred_level_id) const {
  output.Clear();
  acceptable_level_ids.clear();
  preferred_level_id = 0;
  if (level_select.level_type() <= 0) {
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
  preferred_level_id = level_select.level_id() > 0 ? level_select.level_id() : acceptable_level_ids.front();
  if (!std::binary_search(acceptable_level_ids.begin(), acceptable_level_ids.end(), preferred_level_id)) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
  }

  output.set_level_type(level_select.level_type());
  output.set_region(level_select.region());
  output.set_battle_version(battle_version);

  int32_t matching_pool_id = 0;
  for (int32_t level_id : acceptable_level_ids) {
    const auto level_cfg = excel::get_ExcelLevel_by_level_id(level_id);
    FWPLOGDEBUG(*owner_, "fill_matching_scope, level_type={}, level_id={}, battle_version={}, level_cfg={}",
                level_select.level_type(), level_id, battle_version, level_cfg ? level_cfg->DebugString() : "null");
    if (!level_cfg) {
      return PROJECT_NAMESPACE_ID::EN_LEVEL_CFG_NOT_FOUND;
    }
    if (level_cfg->level_type() != level_select.level_type() ||
        (matching_pool_id > 0 && matching_pool_id != level_cfg->matching_pool_id())) {
      return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
    }
    matching_pool_id = level_cfg->matching_pool_id();
  }
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

uint64_t user_matching_manager::get_current_unit_id() const { return data_.view().unit().unit_id(); }

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
  if (sync.has_player_view()) {
    protobuf_copy_message(*output->mutable_view(), sync.player_view());
  }
  FWPLOGDEBUG(*owner_, "send matching log sync to client, matching_id={}, is_switch={}, event_count={}, view={}",
              sync.matching_id(), is_switch, sync.event_logs_size(), sync.has_player_view());
  rpc::lobbysvrclientservice::send_matching_log_sync(ctx, *output, *session);
}

void user_matching_manager::on_gm_cmd_start_matching(std::shared_ptr<rpc::context> ctx, user_ptr_t user_inst,
                                                     std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp,
                                                     ::util::cli::cmd_option_list& params) {
  if (!user_inst) {
    rsp->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    return;
  }

  if (!task_action_user_gm_cmd_nomsg::check_params_number(params, 1)) {
    return;
  }

  int32_t level_id = 0;
  level_id = params[0]->to_int32();
  if (level_id <= 0) {
    rsp->set_result_code(PROJECT_NAMESPACE_ID::EN_LEVEL_CFG_NOT_FOUND);
    return;
  }
  auto user_ptr = user_inst->shared_from_this();

  auto invoke_result = rpc::async_invoke(
      *ctx, "user_matching_manager.gm_start_matching",
      [user_ptr, rsp, level_id](rpc::context& child_ctx) -> rpc::result_code_type {
        auto rpc_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSMatchingStartReq>(child_ctx);
        auto rpc_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCMatchingStartRsp>(child_ctx);
        rpc_request->set_battle_version("gm_test");
        auto* level_select = rpc_request->mutable_level_select();
        level_select->set_level_id(level_id);
        auto cfg = excel::get_ExcelLevel_by_level_id(level_id);
        if (!cfg) {
          FWPLOGERROR(*user_ptr, "gm_start_matching failed to find level cfg, level_id={}", level_id);
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_LEVEL_CFG_NOT_FOUND);
        }
        level_select->set_level_type(cfg->level_type());
        level_select->add_level_ids(level_id);
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
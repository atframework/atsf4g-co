// Copyright 2026 atframework
// @brief Created by jijunliang with mako-generator.py at 2026-08-04 17:03:47

#include "logic/action/task_action_matching_event_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <rpc/rpc_shared_message.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <rpc/rpc_context.h>

#include <utility>

#include <data/user.h>
#include <logic/matching/user_matching_manager.h>
#include <logic/user_manager.h>
#include <rpc/matching/matchsvrservice.atfw.gen.h>

namespace {
// 产品流程由 lobbysvr 统一确认，客户端只订阅匹配状态。
constexpr bool kAutoConfirmMatching = true;
}  // namespace

task_action_matching_event_sync::task_action_matching_event_sync(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_matching_event_sync::~task_action_matching_event_sync() {}

const char* task_action_matching_event_sync::name() const { return "task_action_matching_event_sync"; }

task_action_matching_event_sync::result_type task_action_matching_event_sync::operator()() {
  const rpc_request_type& req_body = get_request_body();
  const uint64_t source_matchsvr_id = get_request_node_id();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  FCTXLOGDEBUG(get_shared_context(), "receive matching Unit sync, unit_id={}, users={}, view={}, event_count={}",
               req_body.unit_id(), req_body.user_keys_size(), req_body.has_unit_view(), req_body.event_logs_size());

  for (const auto& user_key : req_body.user_keys()) {
    auto user_inst = user_manager::me()->find_as<user>(user_key.user_id(), user_key.zone_id());
    if (!user_inst) {
      // 玩家离线时由持久化游标在下次登录执行 check_matching 恢复。
      FCTXLOGDEBUG(get_shared_context(), "skip matching Unit sync for offline user, unit_id={}, user={}:{}",
                   req_body.unit_id(), user_key.user_id(), user_key.zone_id());
      continue;
    }
    auto& matching_manager = user_inst->get_user_matching_manager();
    auto sync_result = matching_manager.acknowledge_matching_sync(get_shared_context(), req_body, source_matchsvr_id);
    if (!sync_result.accepted) {
      continue;
    }

    if (sync_result.confirm_event_id > 0) {
      bool confirm_success = false;
      if (kAutoConfirmMatching) {
        auto confirm_request =
            rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSMatchingConfirmReq>(get_shared_context());
        auto confirm_response =
            rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp>(get_shared_context());
        confirm_request->set_unit_id(matching_manager.get_view().unit().unit_id());
        confirm_request->set_confirmed(true);
        const int32_t result = RPC_AWAIT_CODE_RESULT(
            matching_manager.confirm_matching(get_shared_context(), *confirm_request, *confirm_response));
        confirm_success = result == PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
        if (!confirm_success) {
          FCTXLOGERROR(get_shared_context(),
                       "auto confirm matching failed, unit_id={}, user={}:{}, event_id={}, result={}({})",
                       req_body.unit_id(), user_key.user_id(), user_key.zone_id(), sync_result.confirm_event_id, result,
                       protobuf_mini_dumper_get_error_msg(result));
        } else {
          FCTXLOGDEBUG(get_shared_context(), "auto confirm matching finish, unit_id={}, user={}:{}, event_id={}",
                       req_body.unit_id(), user_key.user_id(), user_key.zone_id(), sync_result.confirm_event_id);
        }
      }
      if (matching_manager.finish_matching_event(get_shared_context(), req_body.unit_id(), sync_result.confirm_event_id,
                                                 confirm_success)) {
        sync_result.acknowledge_event_id = sync_result.confirm_event_id;
        sync_result.has_pending_event = false;
      }
    }

    if (sync_result.has_pending_event) {
      continue;
    }

    auto heartbeat_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingCheckReq>(get_shared_context());
    heartbeat_request->set_unit_id(req_body.unit_id());
    heartbeat_request->set_subscriber_server_id(logic_config::me()->get_local_server_id());
    auto* heartbeat_data = heartbeat_request->mutable_heartbeat_data();
    protobuf_copy_message(*heartbeat_data->mutable_user_key(), user_key);
    heartbeat_data->set_acknowledge_event_id(sync_result.acknowledge_event_id);
    auto heartbeat_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingSnapshot>(get_shared_context());
    if (source_matchsvr_id != 0) {
      const int32_t heartbeat_result = RPC_AWAIT_CODE_RESULT(rpc::matching::matching_heart_bear(
          get_shared_context(), source_matchsvr_id, *heartbeat_request, *heartbeat_response));
      if (heartbeat_result < 0 || heartbeat_response->result() != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
        const int32_t result = heartbeat_result < 0 ? heartbeat_result : heartbeat_response->result();
        FCTXLOGERROR(get_shared_context(), "send matching Unit heartbeat failed, unit_id={}, result={}({})",
                     req_body.unit_id(), result, protobuf_mini_dumper_get_error_msg(result));
      }
    } else {
      FCTXLOGERROR(get_shared_context(), "send matching Unit heartbeat failed without source Matchsvr, unit_id={}",
                   req_body.unit_id());
    }
  }
  FCTXLOGDEBUG(get_shared_context(), "handle matching Unit sync finish, unit_id={}, users={}", req_body.unit_id(),
               req_body.user_keys_size());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_matching_event_sync::on_success() { return get_result(); }

int task_action_matching_event_sync::on_failed() { return get_result(); }

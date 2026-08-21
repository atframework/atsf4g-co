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

namespace {
// 测试阶段由 lobbysvr 模拟客户端确认；联调客户端确认流程后应关闭。
constexpr bool kAutoConfirmMatchingForTest = true;

bool should_auto_confirm(const PROJECT_NAMESPACE_ID::SSMatchingEventSync& sync) {
  if (!kAutoConfirmMatchingForTest) {
    return false;
  }
  for (const auto& event_log : sync.event_logs()) {
    if (event_log.event_case() == PROJECT_NAMESPACE_ID::DMatchingEventLog::kNotifyConfirm) {
      return true;
    }
  }
  return false;
}
}  // namespace

task_action_matching_event_sync::task_action_matching_event_sync(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_matching_event_sync::~task_action_matching_event_sync() {}

const char* task_action_matching_event_sync::name() const { return "task_action_matching_event_sync"; }

task_action_matching_event_sync::result_type task_action_matching_event_sync::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  FCTXLOGDEBUG(
      get_shared_context(), "receive matching event sync, matching_id={}, users={}, view={}, event_count={}",
      req_body.matching_id(), req_body.user_keys_size(), req_body.has_player_view(), req_body.event_logs_size());

  const bool auto_confirm = should_auto_confirm(req_body);

  for (const auto& user_key : req_body.user_keys()) {
    auto user_inst = user_manager::me()->find_as<user>(user_key.user_id(), user_key.zone_id());
    if (!user_inst) {
      // 玩家离线时由持久化游标在下次登录执行 check_matching 恢复。
      FCTXLOGDEBUG(get_shared_context(), "skip matching event sync for offline user, matching_id={}, user={}:{}",
                   req_body.matching_id(), user_key.user_id(), user_key.zone_id());
      continue;
    }
    user_inst->get_user_matching_manager().acknowledge_matching_sync(get_shared_context(), req_body);
    if (!auto_confirm) {
      continue;
    }

    auto confirm_request = rpc::make_shared_message<PROJECT_NAMESPACE_ID::CSMatchingConfirmReq>(get_shared_context());
    auto confirm_response = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp>(get_shared_context());
    confirm_request->set_matching_id(req_body.matching_id());
    confirm_request->set_confirmed(true);
    const int32_t result = RPC_AWAIT_CODE_RESULT(user_inst->get_user_matching_manager().confirm_matching(
        get_shared_context(), *confirm_request, *confirm_response));
    if (result != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
      FCTXLOGERROR(get_shared_context(), "auto confirm matching failed, matching_id={}, user={}:{}, result={}({})",
                   req_body.matching_id(), user_key.user_id(), user_key.zone_id(), result,
                   protobuf_mini_dumper_get_error_msg(result));
    } else {
      FCTXLOGDEBUG(get_shared_context(), "auto confirm matching finish, matching_id={}, user={}:{}",
                   req_body.matching_id(), user_key.user_id(), user_key.zone_id());
    }
  }

  FCTXLOGDEBUG(get_shared_context(), "handle matching event sync finish, matching_id={}, users={}",
               req_body.matching_id(), req_body.user_keys_size());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_matching_event_sync::on_success() { return get_result(); }

int task_action_matching_event_sync::on_failed() { return get_result(); }

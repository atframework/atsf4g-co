// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-30 13:30:46

#include "logic/friend_api/task_action_management_event_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/friend_management_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <rpc/rpc_context.h>

#include <memory>
#include <unordered_map>
#include <utility>

#include "data/user.h"
#include "logic/friend_api/user_friend_api_manager.h"
#include "logic/user_manager.h"

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTNOTIFYSERVICE_API
task_action_management_event_sync::task_action_management_event_sync(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTNOTIFYSERVICE_API
task_action_management_event_sync::~task_action_management_event_sync() {}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTNOTIFYSERVICE_API const char* task_action_management_event_sync::name() const {
  return "task_action_management_event_sync";
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTNOTIFYSERVICE_API task_action_management_event_sync::result_type
task_action_management_event_sync::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  std::unordered_map<const user*, std::shared_ptr<user>> user_map;

  for (const auto& receiver_info : req_body.event_target()) {
    if (!receiver_info.has_snapshot() && receiver_info.increase().event_log_size() <= 0) {
      continue;
    }

    for (const auto& user_key : receiver_info.subscriber_key()) {
      auto user_inst = user_manager::me()->find_as<user>(user_key);
      if (!user_inst) {
        FCTXLOGDEBUG(get_shared_context(), "user {}:{} maybe logout, ignore friend notification and send subscribe",
                     user_key.zone_id(), user_key.user_id());
        continue;
      }

      // 第一次追加数据需要先执行刷新逻辑
      if (user_map.emplace(user_inst.get(), user_inst).second) {
        user_inst->refresh_feature_limit(get_shared_context());
      }

      user_inst->get_user_friend_api_manager().receive_event_sync(get_shared_context(), receiver_info);
    }
  }

  // 下发推送数据
  for (const auto& user_pair : user_map) {
    if (user_pair.second) {
      user_pair.second->send_all_syn_msg(get_shared_context());
    }
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTNOTIFYSERVICE_API int task_action_management_event_sync::on_success() {
  return get_result();
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTNOTIFYSERVICE_API int task_action_management_event_sync::on_failed() {
  return get_result();
}

// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-20 16:47:38

#include "logic/action/task_action_management_unsubscribe.h"

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

#include <router/router_friend_cache.h>
#include <router/router_friend_manager.h>

#include <utility>

#include "data/friend_object.h"

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API
task_action_management_unsubscribe::task_action_management_unsubscribe(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API
task_action_management_unsubscribe::~task_action_management_unsubscribe() {}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API const char* task_action_management_unsubscribe::name() const {
  return "task_action_management_unsubscribe";
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API task_action_management_unsubscribe::result_type
task_action_management_unsubscribe::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  msg_cref_type req_msg = get_request();
  uint64_t user_id = req_msg.head().router().object_inst_id();
  uint32_t zone_id = req_msg.head().router().object_zone_id();

  if (0 == user_id || 0 == zone_id) {
    FWLOGERROR("try to find router cache id from request {} for {} and player {}:{} failed.", "SSFriendUnsubscribeReq",
               name(), zone_id, user_id);
    set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  router_object_base::key_t router_key(atfw::friend_api::router_friend_manager::me()->get_type_id(), zone_id, user_id);
  atfw::friend_api::router_friend_manager::ptr_t router_cache;
  set_response_code(RPC_AWAIT_CODE_RESULT(atfw::friend_api::router_friend_manager::me()->mutable_object(
      get_shared_context(), router_cache, router_key, nullptr)));
  if (get_response_code() < 0 || !router_cache) {
    FWLOGERROR("try to get router object for {} and friend {}:{} failed. res: {}({})", name(), zone_id, user_id,
               get_response_code(), protobuf_mini_dumper_get_error_msg(get_response_code()));
    TASK_ACTION_RETURN_CODE(get_response_code());
  }

  auto friend_cache_obj = router_cache->get_object();
  if (!friend_cache_obj || !friend_cache_obj->is_writable()) {
    FWLOGERROR("try to get friend_object for {} and friend {}:{} failed.", name(), zone_id, user_id);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE);
  }

  atfw::friend_api::friend_object::ptr_t friend_obj =
      std::static_pointer_cast<atfw::friend_api::friend_object>(friend_cache_obj);

  // update router target, router cache maybe offline
  friend_obj->unsubscribe(get_shared_context(), req_body.subscriber());

  friend_obj->refresh_feature_limit(get_shared_context());

  RPC_AWAIT_IGNORE_RESULT(friend_obj->send_notification(get_shared_context()));

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API int task_action_management_unsubscribe::on_success() {
  return get_result();
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API int task_action_management_unsubscribe::on_failed() {
  return get_result();
}

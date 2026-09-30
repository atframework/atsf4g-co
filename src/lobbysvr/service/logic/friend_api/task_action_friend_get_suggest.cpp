// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-28 17:11:31

#include "logic/friend_api/task_action_friend_get_suggest.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>

#include <utility>

#include "data/user.h"
#include "logic/friend_api/user_friend_api_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_get_suggest::task_action_friend_get_suggest(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_get_suggest::~task_action_friend_get_suggest() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_friend_get_suggest::name() const {
  return "task_action_friend_get_suggest";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_get_suggest::result_type
task_action_friend_get_suggest::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

#if 0  // TODO(any): 本地推荐,暂时留空。需要具体推荐的策略，和业务内容相关
  std::vector<std::pair<uint64_t, uint32_t> > suggest_users;
  size_t suggest_count = 100;
  if (excel::get_const_config().friend_suggest_number() > 0) {
    suggest_count = static_cast<size_t>(excel::get_const_config().friend_suggest_number());
  }

  user_level_local_index_manager::me()->get_suggest_friends(*user_inst, suggest_users, suggest_count,
                                                            user_inst->get_user_id());
  for (auto& user_info : suggest_users) {
    PROJECT_NAMESPACE_ID::DPlayerIDKey* user_key = rsp_body.add_suggest_user_keys();
    if (user_key != nullptr) {
      user_key->set_user_id(user_info.first);
      user_key->set_zone_id(user_info.second);
    }
  }
#endif

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_get_suggest::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_get_suggest::on_failed() { return get_result(); }

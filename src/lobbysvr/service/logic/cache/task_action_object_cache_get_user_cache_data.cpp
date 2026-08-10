// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-28 18:16:08

#include "task_action_object_cache_get_user_cache_data.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.cache.pb.h>
#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>
#include <data/player.h>
#include <logic/player_manager.h>
#include <rpc/cache/cache_algorithm.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/user/user_basic.h>

LOBBY_SERVICE_API task_action_object_cache_get_user_cache_data::task_action_object_cache_get_user_cache_data(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

LOBBY_SERVICE_API task_action_object_cache_get_user_cache_data::~task_action_object_cache_get_user_cache_data() {}

LOBBY_SERVICE_API const char* task_action_object_cache_get_user_cache_data::name() const {
  return "task_action_object_cache_get_user_cache_data";
}

LOBBY_SERVICE_API task_action_object_cache_get_user_cache_data::result_type
task_action_object_cache_get_user_cache_data::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  task_action_object_cache_get_user_cache_data::result_type::value_type ret_code =
      PROJECT_NAMESPACE_ID::err::EN_SUCCESS;

  switch (req_body.key().cache_type()) {
    case PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER: {
      player::ptr_t user =
          player_manager::me()->find_as<player>(req_body.key().instance_id(), req_body.key().zone_id());
      if (!user) {
        rsp_body.set_result(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
        TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
      }

      auto cache_meta = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DCacheApiMetaData>(get_shared_context());
      cache_meta->mutable_user_meta()->mutable_user_key()->set_zone_id(user->get_zone_id());
      cache_meta->mutable_user_meta()->mutable_user_key()->set_user_id(user->get_user_id());

      rpc::cache_api::update_cache_meta_from_origin_data(
          get_shared_context(), *cache_meta->mutable_user_meta(), user->get_data_version(), &user->get_login_info(),
          &user->get_user_data(), &user->get_account_info().profile(), &user->get_client_info());

      if (!rpc::cache_api::pack_cache_meta_to_any(get_shared_context(),
                                                  *rsp_body.mutable_cache_meta()->mutable_cache_meta(), *cache_meta)) {
        FWLOGERROR("pack cache meta failed for user {}:{}", user->get_zone_id(), user->get_user_id());
        ret_code = PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
        rsp_body.set_result(ret_code);
      } else {
        rsp_body.set_result(PROJECT_NAMESPACE_ID::EN_SUCCESS);
      }
      break;
    }
    default: {
      FCTXLOGERROR(get_shared_context(), "unsupported cache type: {}",
                   static_cast<uint32_t>(req_body.key().cache_type()));
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
      break;
    }
  }

  TASK_ACTION_RETURN_CODE(ret_code);
}

LOBBY_SERVICE_API int task_action_object_cache_get_user_cache_data::on_success() { return get_result(); }

LOBBY_SERVICE_API int task_action_object_cache_get_user_cache_data::on_failed() { return get_result(); }

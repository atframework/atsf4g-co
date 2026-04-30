// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-24 21:34:18

#include "task_action_object_cache_meta_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.cache.common.pb.h>
#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <rpc/cache/cache_api.h>

#include <data/player.h>
#include <logic/cache/global_cache_manager.h>
#include <logic/cache/user_cache_manager.h>
#include <logic/player_manager.h>

GAME_SERVICE_API task_action_object_cache_meta_sync::task_action_object_cache_meta_sync(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAME_SERVICE_API task_action_object_cache_meta_sync::~task_action_object_cache_meta_sync() {}

GAME_SERVICE_API const char* task_action_object_cache_meta_sync::name() const {
  return "task_action_object_cache_meta_sync";
}

GAME_SERVICE_API task_action_object_cache_meta_sync::result_type task_action_object_cache_meta_sync::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  PROJECT_NAMESPACE_ID::object_cache_key key;
  rpc::cache_api::pick_key_from_meta(get_shared_context(), key, req_body.update_meta().cache_meta());

  for (int i = 0; i < req_body.watchers_size(); ++i) {
    bool need_unwatch = false;

    const PROJECT_NAMESPACE_ID::object_cache_watcher& watcher_key = req_body.watchers(i);
    do {
      if (watcher_key.server_subscribe()) {
        if (watcher_key.instance_id() != logic_config::me()->get_local_server_id()) {
          need_unwatch = true;
          FCTXLOGERROR(
              get_shared_context(), "cache watcher {}:{}:{} is invalid because svr watch instance_id not match",
              static_cast<uint32_t>(watcher_key.cache_type()), watcher_key.zone_id(), watcher_key.instance_id());
          break;
        }

        if (key.cache_type() == PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_UNKNOWN) {
          break;
        }
        global_cache_manager::me()->hot_data_expired(key);
        break;
      }

      if (PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER != watcher_key.cache_type()) {
        need_unwatch = true;
        FCTXLOGERROR(get_shared_context(), "cache watcher {}:{}:{} is invalid because type is not supported",
                     static_cast<uint32_t>(watcher_key.cache_type()), watcher_key.zone_id(), watcher_key.instance_id());
        break;
      }

      player_ptr_t user = player_manager::me()->find_as<player>(watcher_key.instance_id(), watcher_key.zone_id());
      if (!user) {
        need_unwatch = true;
        FCTXLOGDEBUG(get_shared_context(), "cache watcher {}:{}:{} maybe logout",
                     static_cast<uint32_t>(watcher_key.cache_type()), watcher_key.zone_id(), watcher_key.instance_id());
        break;
      }
      if (!user->has_session()) {
        need_unwatch = true;
        FCTXLOGDEBUG(get_shared_context(), "cache watcher {}:{}:{} already logout",
                     static_cast<uint32_t>(watcher_key.cache_type()), watcher_key.zone_id(), watcher_key.instance_id());
        break;
      }

      RPC_AWAIT_IGNORE_RESULT(
          user->get_user_cache_manager().send_update_meta_to_client(get_shared_context(), req_body.update_meta()));
    } while (false);

    do {
      if (!need_unwatch) {
        break;
      }

      PROJECT_NAMESPACE_ID::SSCacheUnwatchSync* sync_body =
          get_shared_context().create<PROJECT_NAMESPACE_ID::SSCacheUnwatchSync>();
      if (nullptr == sync_body) {
        FCTXLOGERROR(get_shared_context(), "{}", "malloc SSCacheUnwatchSync failed");
        break;
      }

      protobuf_copy_message(*sync_body->mutable_watcher(), watcher_key);
      protobuf_copy_message(*sync_body->add_unwatch_keys(), key);

      int res = RPC_AWAIT_CODE_RESULT(rpc::cache::unwatch(get_shared_context(), get_request_node_id(), *sync_body));
      if (res < 0) {
        FCTXLOGERROR(get_shared_context(), "Watcher {}:{}:{} try to unwatch {}:{}:{} failed, res: {}({})",
                     static_cast<uint32_t>(watcher_key.cache_type()), watcher_key.zone_id(), watcher_key.instance_id(),
                     static_cast<uint32_t>(key.cache_type()), key.zone_id(), key.instance_id(), res,
                     protobuf_mini_dumper_get_error_msg(res));
      }
    } while (false);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAME_SERVICE_API int task_action_object_cache_meta_sync::on_success() { return get_result(); }

GAME_SERVICE_API int task_action_object_cache_meta_sync::on_failed() { return get_result(); }

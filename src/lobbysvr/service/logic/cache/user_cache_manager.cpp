// Copyright 2026 atframework

#include "logic/cache/user_cache_manager.h"

#include <log/log_wrapper.h>

#include <common/string_oprs.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.cache.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/player.h>

#include <rpc/cache/cache_algorithm.h>
#include <rpc/cache/cache_api.h>
#include <rpc/cache/cachesvrservice.atfw.gen.h>
#include <rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>

#include <logic/cache/global_cache_manager.h>
#include <logic/logic_server_setup.h>

#include <list>
#include <utility>
#include "rpc/rpc_shared_message.h"

#ifdef max
#  undef max
#endif

std::string user_cache_manager::memory_leak_debug() {
  return atfw::util::log::format("user_cache_manager: {} \n", watch_data_.size());
}

user_cache_manager::user_cache_manager(player& owner)
    : owner_(&owner),
      cachesvr_discovery_version_(0),
      need_notify_user_cache_expired_(false),
      need_notify_user_meta_expired_(false),
      need_notify_user_match_exired_(false),
      fallback_notify_expired_timepoint_(0),
      watch_heartbeat_timepoint_(0),
      is_logout_(false),
      next_time_meta_update_time_(-1) {}

user_cache_manager::~user_cache_manager() {}

rpc::result_code_type user_cache_manager::login_init(rpc::context& /*ctx*/) {
  set_user_cache_expired();

  RPC_RETURN_CODE(0);
}

void user_cache_manager::on_logout(rpc::context& ctx) {
  bool send_expired_right_now = 0 == owner_->get_login_lock().router_server_id();
  if (watch_data_.empty() && !send_expired_right_now) {
    return;
  }

  // 如果玩家尚未保存，记录缓存过期即可，等保存时会进行
  // 否则需要立即发出缓存失效通知
  if (send_expired_right_now) {
    async_send_update_user_basic_meta_to_cachesvr(ctx);
  } else {
    // 置脏后会被其他协议会导致 导致save 之前触发 meta同步，logout数据错误
    // set_user_meta_expired();
    is_logout_ = true;
  }

  // 所有缓存反订阅
  async_unwatch_all(ctx);
}

void user_cache_manager::refresh_feature_limit_second(rpc::context& /*ctx*/) {
  if (next_time_meta_update_time_ != -1 && next_time_meta_update_time_ <= atfw::util::time::time_utility::get_now()) {
    set_user_meta_expired();
  }
}

void user_cache_manager::on_update_session(rpc::context& ctx) {
  cachesvr_discovery_version_ = 0;

  do {
    if (!owner_->is_inited()) {
      break;
    }

    auto sess = owner_->get_session();
    if (!sess) {
      break;
    }

    // 下发给客户端缓存过期
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SCCacheApiAllExpiredSync> msg{ctx};
    rpc::lobbysvrclientservice::send_cache_api_all_expired(ctx, *msg, *sess);

    for (auto& watch_data : watch_data_) {
      watch_data.second.data_version = 0;
    }
  } while (false);
}

void user_cache_manager::on_saved(rpc::context& ctx) {
  if (need_notify_user_meta_expired_ || is_logout_) {
    async_send_update_user_basic_meta_to_cachesvr(ctx);
    is_logout_ = false;
  }
  if (need_notify_user_cache_expired_) {
    send_cache_expired_notify_to_cachesvr(ctx);
  }
}

void user_cache_manager::refresh_feature_limit_minute(rpc::context& ctx) {
  // cachesvr_discovery_version_ = 0;
  bool need_notify_my_cache_expired = false;
  do {
    logic_server_common_module* mod = logic_server_last_common_module();
    if (nullptr == mod) {
      break;
    }

    int64_t current_cachesvr_discovery_version =
        mod->get_discovery_service_version(atfw::component::logic_service_type::kCacheSvr);

    if (cachesvr_discovery_version_ != current_cachesvr_discovery_version) {
      cachesvr_discovery_version_ = current_cachesvr_discovery_version;

      // cachesvr 分布有变化，通知客户端缓存全部失效。下次客户端拉取的时候会自动重新订阅
      need_notify_my_cache_expired = true;

      for (auto& watch_data : watch_data_) {
        watch_data.second.data_version = 0;
      }
    }
  } while (false);

  if (fallback_notify_expired_timepoint_ > 0) {
    if (fallback_notify_expired_timepoint_ <= atfw::util::time::time_utility::get_now()) {
      fallback_notify_expired_timepoint_ = 0;

      if (!rpc::cache_api::has_cachesvr()) {
        // 如果下发给客户端缓存过期
        need_notify_my_cache_expired = true;
      }
    }
  }

  do {
    if (!need_notify_my_cache_expired) {
      break;
    }

    auto sess = owner_->get_session();
    if (!sess) {
      break;
    }

    // 下发给客户端缓存过期
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SCCacheApiAllExpiredSync> msg{ctx};
    rpc::lobbysvrclientservice::send_cache_api_all_expired(ctx, *msg, *sess);
  } while (false);

  // 监听缓存变化的心跳
  maybe_async_watch_heartbeat(ctx);
}

void user_cache_manager::send_cache_expired_notify_to_cachesvr(rpc::context& ctx) {
  auto zone_id = owner_->get_zone_id();
  auto user_id = owner_->get_user_id();
  auto user_ptr = owner_->shared_from_this();
  auto notify_task = rpc::async_invoke(
      ctx, "user_cache_manager.send_cache_expired_notify_to_cachesvr",
      [user_ptr, zone_id, user_id](rpc::context& subctx) -> rpc::result_code_type {
        // 通知cachesvr，缓存过期
        PROJECT_NAMESPACE_ID::SSCacheSetExpiredSync* sync_body =
            subctx.create<PROJECT_NAMESPACE_ID::SSCacheSetExpiredSync>();
        if (nullptr == sync_body) {
          FWLOGERROR("malloc SSCacheSetExpiredSync failed");
          user_ptr->get_user_cache_manager().need_notify_user_cache_expired_ = true;
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }

        PROJECT_NAMESPACE_ID::object_cache_key* cache_key = sync_body->add_expired_keys();
        if (nullptr == cache_key) {
          FWLOGERROR("malloc object_cache_key failed");
          user_ptr->get_user_cache_manager().need_notify_user_cache_expired_ = true;
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }

        cache_key->set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
        cache_key->set_zone_id(zone_id);
        cache_key->set_instance_id(user_id);

        uint64_t server_inst_id = rpc::cache_api::get_cachesvr_server_id(*cache_key);
        if (0 == server_inst_id) {
          RPC_RETURN_CODE(0);
        }

        int res = RPC_AWAIT_CODE_RESULT(rpc::cache::set_expired(subctx, server_inst_id, *sync_body));
        if (res < 0) {
          FWLOGERROR("call rpc::cache::set_expired to server {:#x} with key {}:{}:{} failed, res: {}({})",
                     server_inst_id, static_cast<uint32_t>(cache_key->cache_type()), cache_key->zone_id(),
                     cache_key->instance_id(), res, protobuf_mini_dumper_get_error_msg(res));
          // 任务失败，恢复脏标记以供后续重试
          user_ptr->get_user_cache_manager().need_notify_user_cache_expired_ = true;
        }

        RPC_RETURN_CODE(res);
      });
  if (notify_task.is_error()) {
    FWPLOGERROR(*owner_, "async_invoke task to notify player cache expired failed, res: {}({})",
                *notify_task.get_error(), protobuf_mini_dumper_get_error_msg(*notify_task.get_error()));
  } else {
    need_notify_user_cache_expired_ = false;
  }
}

rpc::result_code_type user_cache_manager::send_update_user_basic_meta_to_cachesvr(rpc::context& ctx) {
  // 通知cachesvr，缓存过期
  PROJECT_NAMESPACE_ID::SSCacheUpdateMetaSync* sync_body = ctx.create<PROJECT_NAMESPACE_ID::SSCacheUpdateMetaSync>();
  if (nullptr == sync_body) {
    FWLOGERROR("malloc SSCacheUpdateMetaSync failed");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  PROJECT_NAMESPACE_ID::object_cache_meta* cache_meta = sync_body->add_object_metas();
  if (nullptr == cache_meta) {
    FWLOGERROR("malloc object_cache_meta failed");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  pack_user_meta_data(ctx, *cache_meta);
  PROJECT_NAMESPACE_ID::object_cache_watch_key watch_key;

  watch_key.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
  watch_key.set_zone_id(owner_->get_zone_id());
  watch_key.set_instance_id(owner_->get_user_id());

  uint64_t server_inst_id = rpc::cache_api::get_cachesvr_server_id(watch_key);
  if (0 == server_inst_id) {
    RPC_RETURN_CODE(0);
  }

  int res = RPC_AWAIT_CODE_RESULT(rpc::cache::update_meta(ctx, server_inst_id, *sync_body));
  if (res < 0) {
    FWLOGERROR("call rpc::cache::update_meta to server {:#x} with key {}:{}:{} failed, res: {}({})", server_inst_id,
               static_cast<uint32_t>(watch_key.cache_type()), watch_key.zone_id(), watch_key.instance_id(), res,
               protobuf_mini_dumper_get_error_msg(res));
  }
  RPC_RETURN_CODE(res);
}

void user_cache_manager::async_send_update_user_basic_meta_to_cachesvr(rpc::context& ctx) {
  auto user_ptr = owner_->shared_from_this();
  auto notify_task = rpc::async_invoke(
      ctx, "user_cache_manager.async_send_update_user_basic_meta_to_cachesvr",
      [user_ptr](rpc::context& subctx) -> rpc::result_code_type {
        int32_t ret =
            RPC_AWAIT_CODE_RESULT(user_ptr->get_user_cache_manager().send_update_user_basic_meta_to_cachesvr(subctx));
        RPC_RETURN_CODE(ret);
      });
  if (notify_task.is_error()) {
    FWPLOGERROR(*owner_, "async_invoke task to notify player cache update meta failed, res: {}({})",
                *notify_task.get_error(), protobuf_mini_dumper_get_error_msg(*notify_task.get_error()));
  } else {
    need_notify_user_meta_expired_ = false;
  }
}

void user_cache_manager::set_user_cache_expired() { need_notify_user_cache_expired_ = true; }

void user_cache_manager::set_user_meta_expired() {
  need_notify_user_meta_expired_ = true;
  next_time_meta_update_time_ = -1;
}

void user_cache_manager::set_user_meta_expired_delay_sync() {
  next_time_meta_update_time_ = atfw::util::time::time_utility::get_now() + 5;
}

rpc::result_code_type user_cache_manager::send_cache_expired_notify_to_client(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::object_cache_key& cache_key) {
  // 下发通知给客户端
  do {
    auto sess = owner_->get_session();
    if (!sess) {
      break;
    }

    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SCCacheApiExpiredSync> sync_body{ctx};

    PROJECT_NAMESPACE_ID::DCacheApiCacheKey* user_key = sync_body->add_keys();
    if (nullptr == user_key) {
      break;
    }

    user_key->set_cache_type(cache_key.cache_type());
    user_key->set_instance_id(cache_key.instance_id());
    user_key->set_zone_id(cache_key.zone_id());

    RPC_AWAIT_IGNORE_RESULT(rpc::lobbysvrclientservice::send_cache_api_expired(ctx, *sync_body, *sess));
  } while (false);

  // 本地清空版本号，以防下次心跳watch的时候重复收到过期通知
  {
    auto watch_iter = watch_data_.find(cache_key);
    if (watch_iter != watch_data_.end()) {
      watch_iter->second.data_version = 0;
    }
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_cache_manager::send_update_meta_to_client(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::object_cache_meta& meta) {
  // 下发通知给客户端
  do {
    // if (PROJECT_NAMESPACE_ID::object_cache_meta::kUserMeta != meta.cache_meta().cache_data_case()) {
    //   break;
    // }

    auto sess = owner_->get_session();
    if (!sess) {
      break;
    }

    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SCCacheApiUpdateMetaSync> sync_body{ctx};

    if (rpc::cache_api::unpack_cache_meta_from_any(ctx, *sync_body->mutable_cache_meta(), meta.cache_meta())) {
      RPC_AWAIT_IGNORE_RESULT(rpc::lobbysvrclientservice::send_cache_api_update_meta(ctx, *sync_body, *sess));
    }
  } while (false);

  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_cache_manager::unwatch_cache_keys(
    rpc::context& ctx, PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type,
    const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectKey>& keys) {
  if (keys.empty()) {
    RPC_RETURN_CODE(0);
  }

  if (!rpc::cache_api::has_cachesvr()) {
    RPC_RETURN_CODE(0);
  }

  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>* cache_keys =
      ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>>();
  if (nullptr == cache_keys) {
    FWLOGERROR("malloc RepeatedPtrField<PROJECT_NAMESPACE_ID::DPlayerIDKey> failed");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  for (int i = 0; i < keys.size(); ++i) {
    PROJECT_NAMESPACE_ID::object_cache_key* cache_key = cache_keys->Add();
    if (nullptr == cache_key) {
      FWLOGERROR("malloc object_cache_key failed");
      break;
    }

    cache_key->set_cache_type(cache_type);
    cache_key->set_zone_id(keys.Get(i).zone_id());
    cache_key->set_instance_id(keys.Get(i).instance_id());
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unwatch_cache_keys(ctx, std::move(*cache_keys))));
}

rpc::result_code_type user_cache_manager::unwatch_cache_keys(
    rpc::context& ctx, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>&& cache_keys) {
  if (cache_keys.empty()) {
    RPC_RETURN_CODE(0);
  }

  if (!rpc::cache_api::has_cachesvr()) {
    // 也要释放监视列表
    for (int i = 0; i < cache_keys.size(); ++i) {
      watch_data_.erase(cache_keys.Get(i));
    }
    RPC_RETURN_CODE(0);
  }

  // 所有缓存反订阅
  std::unordered_map<uint64_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>*>
      watch_keys_by_server_id;
  for (int i = 0; i < cache_keys.size(); ++i) {
    PROJECT_NAMESPACE_ID::object_cache_key* cache_key = cache_keys.Mutable(i);
    if (nullptr == cache_key) {
      continue;
    }

    uint64_t server_inst_id = rpc::cache_api::get_cachesvr_server_id(*cache_key);
    if (0 == server_inst_id) {
      continue;
    }

    PROJECT_NAMESPACE_ID::object_cache_key* watch_key = nullptr;
    auto iter = watch_keys_by_server_id.find(server_inst_id);
    if (iter == watch_keys_by_server_id.end()) {
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>* keys =
          ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>>();
      if (nullptr != keys) {
        keys->Reserve(static_cast<int>(watch_data_.size()));
        watch_key = keys->Add();

        watch_keys_by_server_id[server_inst_id] = keys;
      }
    } else {
      watch_key = iter->second->Add();
    }

    if (nullptr == watch_key) {
      break;
    }

    // 也要释放监视列表
    watch_data_.erase(*cache_key);
    protobuf_move_message(*watch_key, std::move(*cache_key));
  }

  if (watch_keys_by_server_id.empty()) {
    RPC_RETURN_CODE(0);
  }

  for (auto& unwatch_msg : watch_keys_by_server_id) {
    PROJECT_NAMESPACE_ID::SSCacheUnwatchSync* sync_body = ctx.create<PROJECT_NAMESPACE_ID::SSCacheUnwatchSync>();
    if (nullptr == sync_body) {
      FWLOGERROR("malloc SSCacheUnwatchSync failed");
      continue;
    }

    protobuf_move_message(*sync_body->mutable_unwatch_keys(), std::move(*unwatch_msg.second));
    int res = RPC_AWAIT_CODE_RESULT(rpc::cache::unwatch(ctx, unwatch_msg.first, *sync_body));
    if (res < 0) {
      FWLOGERROR("call rpc::cache::unwatch to server {:#x} with {} keys failed, res: {}({})", unwatch_msg.first,
                 sync_body->unwatch_keys_size(), res, protobuf_mini_dumper_get_error_msg(res));
    }
  }

  RPC_RETURN_CODE(0);
}

void user_cache_manager::fill_self_basic_data(PROJECT_NAMESPACE_ID::DUserBasicData& output) {
  output.mutable_user_key()->set_user_id(owner_->get_user_id());
  output.mutable_user_key()->set_zone_id(owner_->get_zone_id());
  // output.set_account_id(owner_->get_account_id());

  // user 表里的这个字段可能没更新，用本地login表里的
  PROJECT_NAMESPACE_ID::DLoginBasicDataCache* login_cache = output.mutable_login_data_cache();
  login_cache->set_business_login_time(owner_->get_login_info().business_login_time());
  login_cache->set_business_logout_time(owner_->get_login_info().business_logout_time());
  login_cache->set_business_register_time(owner_->get_login_info().business_register_time());
  login_cache->set_business_unregister_time(owner_->get_login_info().business_unregister_time());

  output.set_account_type(owner_->get_account_info().account_type());
  output.set_account_login_channel(owner_->get_account_info().channel_id());
  // output.set_package_login_channel(owner_->get_account_info().package_login_channel());
  // output.set_package_register_channel(owner_->get_account_info().package_register_channel());
  output.set_user_data_version(owner_->get_data_version());
  output.set_user_level(owner_->get_user_data().user_level());
  output.set_version_type(owner_->get_account_info().version_type());

  protobuf_copy_message(*output.mutable_profile(), owner_->get_account_info().profile());
  protobuf_copy_message(*output.mutable_shared_options(), owner_->get_user_option_public_data().custom_options());

  //  FIXME 经验、战力等
  //  output.set_combat();
  //  output.set_combat_timepoint();
}

void user_cache_manager::async_unwatch_all(rpc::context& ctx) {
  if (watch_data_.empty()) {
    return;
  }

  if (!rpc::cache_api::has_cachesvr()) {
    return;
  }

  auto user_ptr = owner_->shared_from_this();
  auto invoke_task = rpc::async_invoke(
      ctx, "user_cache_manager.unwatch_all", [user_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto& self = user_ptr->get_user_cache_manager();
        std::unordered_map<uint64_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>*>
            watch_keys_by_server_id;
        for (auto& watch_data : self.watch_data_) {
          uint64_t server_inst_id = rpc::cache_api::get_cachesvr_server_id(watch_data.first);
          if (0 == server_inst_id) {
            continue;
          }

          PROJECT_NAMESPACE_ID::object_cache_key* watch_key = nullptr;
          auto iter = watch_keys_by_server_id.find(server_inst_id);
          if (iter == watch_keys_by_server_id.end()) {
            ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>* keys =
                child_ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>>();
            if (nullptr != keys) {
              keys->Reserve(static_cast<int>(self.watch_data_.size()));
              watch_key = keys->Add();

              watch_keys_by_server_id[server_inst_id] = keys;
            }
          } else {
            watch_key = iter->second->Add();
          }

          if (nullptr == watch_key) {
            break;
          }

          protobuf_copy_message(*watch_key, watch_data.first);
        }
        self.watch_data_.clear();

        if (watch_keys_by_server_id.empty()) {
          RPC_RETURN_CODE(0);
        }

        for (auto& unwatch_msg : watch_keys_by_server_id) {
          PROJECT_NAMESPACE_ID::SSCacheUnwatchSync* sync_body =
              child_ctx.create<PROJECT_NAMESPACE_ID::SSCacheUnwatchSync>();
          if (nullptr == sync_body) {
            FWLOGERROR("malloc SSCacheUnwatchSync failed");
            break;
          }

          sync_body->mutable_watcher()->set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
          sync_body->mutable_watcher()->set_zone_id(user_ptr->get_zone_id());
          sync_body->mutable_watcher()->set_instance_id(user_ptr->get_user_id());
          protobuf_move_message(*sync_body->mutable_unwatch_keys(), std::move(*unwatch_msg.second));
          int32_t res = RPC_AWAIT_CODE_RESULT(rpc::cache::unwatch(child_ctx, unwatch_msg.first, *sync_body));
          if (res < 0) {
            FWLOGERROR("call rpc::cache::unwatch to server {:#x} with {} keys failed, res: {}({})", unwatch_msg.first,
                       sync_body->unwatch_keys_size(), res, protobuf_mini_dumper_get_error_msg(res));
          }
        }

        RPC_RETURN_CODE(0);
      });
  if (invoke_task.is_error()) {
    FWPLOGERROR(*owner_, "invoke task to send watch heartbeat failed, result: {}({})", *invoke_task.get_error(),
                protobuf_mini_dumper_get_error_msg(*invoke_task.get_error()));
  }
}

void user_cache_manager::maybe_async_watch_heartbeat(rpc::context& ctx) {
  if (watch_heartbeat_timepoint_ > atfw::util::time::time_utility::get_now()) {
    return;
  }

  if (!rpc::cache_api::has_cachesvr()) {
    return;
  }

  if (watch_data_.empty()) {
    return;
  }

  auto user_ptr = owner_->shared_from_this();
  auto invoke_task = rpc::async_invoke(
      ctx, "user_cache_manager.watch_heartbeat", [user_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        RPC_AWAIT_IGNORE_VOID(user_ptr->get_user_cache_manager().watch_heartbeat(child_ctx));
        RPC_RETURN_CODE(0);
      });
  if (invoke_task.is_error()) {
    FWPLOGERROR(*owner_, "invoke task to send watch heartbeat failed, result: {}({})", *invoke_task.get_error(),
                protobuf_mini_dumper_get_error_msg(*invoke_task.get_error()));
  }
}

rpc::result_void_type user_cache_manager::watch_heartbeat(rpc::context& ctx) {
  if (watch_heartbeat_timepoint_ > atfw::util::time::time_utility::get_now()) {
    RPC_RETURN_VOID;
  }

  time_t heartbeat_interval = logic_config::me()->get_logic_cfg().cache().watcher().heartbeat_interval().seconds();
  if (heartbeat_interval <= 0) {
    heartbeat_interval = 900;
  }
  watch_heartbeat_timepoint_ = atfw::util::time::time_utility::get_now() + heartbeat_interval;

  if (!rpc::cache_api::has_cachesvr()) {
    RPC_RETURN_VOID;
  }

  // 所有缓存反订阅
  std::unordered_map<uint64_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watch_key>*>
      watch_keys_by_server_id;
  for (auto& watch_data : watch_data_) {
    uint64_t server_inst_id = rpc::cache_api::get_cachesvr_server_id(watch_data.first);
    if (0 == server_inst_id) {
      continue;
    }

    PROJECT_NAMESPACE_ID::object_cache_watch_key* watch_key = nullptr;
    auto iter = watch_keys_by_server_id.find(server_inst_id);
    if (iter == watch_keys_by_server_id.end()) {
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watch_key>* keys =
          ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watch_key>>();
      if (nullptr != keys) {
        keys->Reserve(static_cast<int>(watch_data_.size()));
        watch_key = keys->Add();

        watch_keys_by_server_id[server_inst_id] = keys;
      }
    } else {
      watch_key = iter->second->Add();
    }

    if (nullptr == watch_key) {
      break;
    }

    watch_key->set_cache_type(watch_data.first.cache_type());
    watch_key->set_zone_id(watch_data.first.zone_id());
    watch_key->set_instance_id(watch_data.first.instance_id());
    watch_key->set_data_version(static_cast<int64_t>(watch_data.second.data_version));
  }

  if (watch_keys_by_server_id.empty()) {
    RPC_RETURN_VOID;
  }

  for (auto& watch_msg : watch_keys_by_server_id) {
    PROJECT_NAMESPACE_ID::SSCacheWatchSync* sync_body = ctx.create<PROJECT_NAMESPACE_ID::SSCacheWatchSync>();
    if (nullptr == sync_body) {
      FWLOGERROR("malloc SSCacheWatchSync failed");
      continue;
    }

    sync_body->mutable_watcher()->set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
    sync_body->mutable_watcher()->set_zone_id(owner_->get_zone_id());
    sync_body->mutable_watcher()->set_instance_id(owner_->get_user_id());
    protobuf_move_message(*sync_body->mutable_watch_keys(), std::move(*watch_msg.second));
    int res = RPC_AWAIT_CODE_RESULT(rpc::cache::watch(ctx, watch_msg.first, *sync_body));
    if (res < 0) {
      FWLOGERROR("call rpc::cache::watch to server {:#x} with {} keys failed, res: {}({})", watch_msg.first,
                 sync_body->watch_keys_size(), res, protobuf_mini_dumper_get_error_msg(res));
    }
  }

  RPC_RETURN_VOID;
}

void user_cache_manager::pack_user_meta_data(rpc::context& ctx, PROJECT_NAMESPACE_ID::object_cache_meta& cache_meta) {
  auto meta = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DCacheApiMetaData>(ctx);
  rpc::cache_api::update_cache_meta_from_origin_data(ctx, *meta->mutable_user_meta(), owner_->get_data_version(),
                                                     &owner_->get_login_info(), &owner_->get_user_data(),
                                                     &owner_->get_account_info().profile(), &owner_->get_client_info());

  rpc::cache_api::pack_cache_meta_to_any(ctx, *cache_meta.mutable_cache_meta(), *meta);
}

ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_cache_manager::pull_cache(
    rpc::context& ctx, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>& keys,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>& output, bool filter_unused_id,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiCacheKey>* not_found_keys) {
  std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                     rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>
      player_map;

  for (const auto& key : keys) {
    PROJECT_NAMESPACE_ID::object_cache_key cache_key;
    cache_key.set_cache_type(key.cache_key().cache_type());
    cache_key.set_zone_id(key.cache_key().zone_id());
    cache_key.set_instance_id(key.cache_key().instance_id());
    player_map[cache_key] = key.get_type();
  }

  if (player_map.empty()) {
    RPC_RETURN_CODE(0);
  }
  int32_t ret = 0;
  if (rpc::cache_api::has_cachesvr()) {
    global_cache_manager::me()->fetch_hot_data(player_map, output);
    if (player_map.empty()) {
      RPC_RETURN_CODE(0);
    }

    if (filter_unused_id) {
      for (auto iter = output.begin(); iter != output.end();) {
        if (iter->has_user_cache() && iter->user_cache().login_data_cache().business_unregister_time() != 0) {
          if (not_found_keys != nullptr) {
            auto* key = not_found_keys->Add();
            if (key != nullptr) {
              key->set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
              key->set_zone_id(iter->user_cache().user_key().zone_id());
              key->set_instance_id(iter->user_cache().user_key().user_id());
            }
          }
          iter = output.erase(iter);
        } else {
          ++iter;
        }
      }
    }

    PROJECT_NAMESPACE_ID::object_cache_watcher* watcher = ctx.create<PROJECT_NAMESPACE_ID::object_cache_watcher>();
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>* cache_pull_keys =
        ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>>();
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content>* cache_contents =
        ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content>>();

    if (nullptr == watcher || nullptr == cache_pull_keys || nullptr == cache_contents) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    watcher->set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
    watcher->set_zone_id(owner_->get_zone_id());
    watcher->set_instance_id(owner_->get_user_id());

    cache_pull_keys->Reserve(keys.size());
    cache_contents->Reserve(keys.size());

    for (const auto& data : player_map) {
      PROJECT_NAMESPACE_ID::object_cache_pull_key* pull_key = cache_pull_keys->Add();
      if (nullptr == pull_key) {
        FWLOGERROR("malloc object_cache_key failed");
        continue;
      }
      pull_key->set_get_type(data.second);
      protobuf_copy_message(*pull_key->mutable_cache_key(), data.first);
    }

    ret = RPC_AWAIT_CODE_RESULT(
        rpc::cache_api::batch_get_cache(ctx, *watcher, std::move(*cache_pull_keys), *cache_contents));
    if (ret != 0) {
      FWPLOGERROR(*owner_, "pull cache failed size {}, ret {}", player_map.size(), ret);
      RPC_RETURN_CODE(ret);
    }

    for (int i = 0; i < cache_contents->size(); ++i) {
      PROJECT_NAMESPACE_ID::object_cache_key got_cache_key;
      const PROJECT_NAMESPACE_ID::object_cache_content& basic_data = cache_contents->Get(i);
      PROJECT_NAMESPACE_ID::DCacheApiObjectData* output_data = output.Add();
      if (nullptr == output_data) {
        continue;
      }
      if (!rpc::cache_api::unpack_cache_content_from_any(ctx, *output_data, basic_data.cache_data())) {
        output.RemoveLast();
        continue;
      }

      rpc::cache_api::pick_key_from_content(ctx, got_cache_key, *output_data);
      if (output_data->has_user_cache() && filter_unused_id &&
          output_data->user_cache().login_data_cache().business_unregister_time() != 0) {
        // 注销 的情况不下发
        if (not_found_keys != nullptr) {
          auto* key = not_found_keys->Add();
          if (key != nullptr) {
            key->set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
            key->set_zone_id(output_data->user_cache().user_key().zone_id());
            key->set_instance_id(output_data->user_cache().user_key().user_id());
          }
        }
        output.RemoveLast();
        continue;
      }

      // 检查所有支持的类型
      if (player_map[got_cache_key] == PROJECT_NAMESPACE_ID::EN_CACHE_API_GET_CACHE_TYPE_HOT_DATA) {
        global_cache_manager::me()->update_hot_data(got_cache_key, cache_contents->Get(i));
      }

      // 记录数据版本号
      if (player_map[got_cache_key] == PROJECT_NAMESPACE_ID::EN_CACHE_API_GET_CACHE_TYPE_SUBSCRIBE) {
        watch_data_[got_cache_key].data_version = static_cast<uint64_t>(cache_contents->Get(i).data_version());
      }

      player_map.erase(got_cache_key);
    }
  }
  if (not_found_keys != nullptr) {
    for (const auto& unit : player_map) {
      auto* key = not_found_keys->Add();
      if (key != nullptr) {
        key->set_cache_type(unit.first.cache_type());
        key->set_instance_id(unit.first.instance_id());
        key->set_zone_id(unit.first.zone_id());
      }
    }
  }
  RPC_RETURN_CODE(ret);
}

void user_cache_manager::update_user_cache_info(::rpc::context& ctx) {
  if (need_notify_user_meta_expired_) {
    async_send_update_user_basic_meta_to_cachesvr(ctx);
  }
  if (need_notify_user_cache_expired_) {
    send_cache_expired_notify_to_cachesvr(ctx);
  }
}

ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_cache_manager::check_user_id_valid(rpc::context& ctx,
                                                                                           uint32_t zone_id,
                                                                                           uint64_t user_id) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(get_user_cache(ctx, zone_id, user_id, nullptr)));
}

ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_cache_manager::get_user_cache(
    rpc::context& ctx, uint32_t zone_id, uint64_t user_id, PROJECT_NAMESPACE_ID::DUserBasicData* out) {
  PROJECT_NAMESPACE_ID::object_cache_key key;
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>* cache_contents =
      ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>>();
  key.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
  key.set_zone_id(zone_id);
  key.set_instance_id(user_id);
  int32_t ret = RPC_AWAIT_CODE_RESULT(pull_one_cache(ctx, key, *cache_contents));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  if (cache_contents->empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
  }

  const auto& user_cache = cache_contents->at(0).user_cache();

  if (user_cache.user_key().user_id() != user_id || user_cache.user_key().zone_id() != zone_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
  }

  if (user_cache.login_data_cache().business_unregister_time() != 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
  }
  if (out != nullptr) {
    protobuf_copy_message(*out, user_cache);
  }
  RPC_RETURN_CODE(0);
}

ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_cache_manager::pull_one_cache(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::object_cache_key& key,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>& output) {
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>* cache_pull_keys =
      ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>>();
  auto* unit = cache_pull_keys->Add();
  if (unit != nullptr) {
    unit->set_get_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_GET_CACHE_TYPE_NORMAL);
    protobuf_copy_message(*unit->mutable_cache_key(), key);
  }

  int32_t ret = RPC_AWAIT_CODE_RESULT(pull_cache(ctx, *cache_pull_keys, output, false));

  RPC_RETURN_CODE(ret);
}

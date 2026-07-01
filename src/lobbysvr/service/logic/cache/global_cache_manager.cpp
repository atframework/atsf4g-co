// Copyright 2026 atframework

#include "logic/cache/global_cache_manager.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel_config_const_index.h>
#include <config/logic_config.h>
#include <data/player.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>

#include <rpc/cache/cache_api.h>
#include <rpc/cache/cachesvrservice.atfw.gen.h>

#include <utility/protobuf_mini_dumper.h>
#include "protocol/common/com.struct.cache.common.pb.h"

std::string global_cache_manager::memory_leak_debug() {
  return util::log::format("global_cache_manager: ({}) \n", hot_data_map_.size());
}

void global_cache_manager::tick() {
  if (ss_msg_dispatcher::is_instance_destroyed() || ss_msg_dispatcher::me()->is_closing()) {
    return;
  }

  if (hot_data_map_.empty()) {
    return;
  }

  if (util::time::time_utility::get_now() == last_time_tick_) {
    return;
  }
  last_time_tick_ = util::time::time_utility::get_now();

  if (last_time_tick_ % 60 == 0) {
    hot_data_debug();
  }

  remove_cold_data();
  hot_data_watch();
}

int global_cache_manager::init() {
  watch_heartbeat_timepoint_ = 0;
  last_time_tick_ = 0;
  return 0;
}

void global_cache_manager::hot_data_expired(const PROJECT_NAMESPACE_ID::object_cache_key& expired_key) {
  auto iter = hot_data_map_.find(expired_key, false);
  if (iter == hot_data_map_.end()) {
    return;
  }

  (iter->second)->set_expired();
}

void global_cache_manager::fetch_hot_data(
    std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                       rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>& cache_keys,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content>& output) {
  std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                     rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>
      output_cache_keys;
  output_cache_keys.reserve(cache_keys.size());

  for (const auto& iter : cache_keys) {
    if (iter.second != PROJECT_NAMESPACE_ID::EN_CACHE_API_GET_CACHE_TYPE_HOT_DATA &&
        iter.second != PROJECT_NAMESPACE_ID::EN_CACHE_API_GET_CACHE_TYPE_NORMAL) {
      output_cache_keys[iter.first] = iter.second;
      continue;
    }

    auto tmp_iter = hot_data_map_.find(iter.first, true);
    if (tmp_iter == hot_data_map_.end()) {
      output_cache_keys[iter.first] = iter.second;
      continue;
    }

    // 续期cache
    tmp_iter->second->refresh_cache();
    if (tmp_iter->second->data_version() == 0) {
      output_cache_keys[iter.first] = iter.second;
      continue;
    }

    tmp_iter->second->fetch_data(*output.Add());
  }
  cache_keys.swap(output_cache_keys);
}

void global_cache_manager::fetch_hot_data(
    std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                       rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>& cache_keys,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DUserBasicData>& output) {
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> cache_content;
  fetch_hot_data(cache_keys, cache_content);

  for (auto& data : cache_content) {
    if (data.cache_data().Is<PROJECT_NAMESPACE_ID::DUserBasicData>()) {
      auto* user_cache = output.Add();
      if (nullptr == user_cache) {
        continue;
      }
      if (!data.cache_data().UnpackTo(user_cache)) {
        FWLOGERROR("Unpack DUserBasicData failed: {}", user_cache->InitializationErrorString());
        output.RemoveLast();
        continue;
      }
    }
  }
}

void global_cache_manager::fetch_hot_data(
    std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                       rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>& cache_keys,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>& output) {
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> cache_content;
  fetch_hot_data(cache_keys, cache_content);

  for (auto& data : cache_content) {
    if (data.cache_data().Is<PROJECT_NAMESPACE_ID::DUserBasicData>()) {
      auto* object_data = output.Add();
      if (nullptr == object_data) {
        continue;
      }

      if (!data.cache_data().UnpackTo(object_data->mutable_user_cache())) {
        FWLOGERROR("Unpack DUserBasicData failed: {}", object_data->mutable_user_cache()->InitializationErrorString());
        output.RemoveLast();
        continue;
      }
    }
  }
}

void global_cache_manager::update_hot_data(const PROJECT_NAMESPACE_ID::object_cache_key& cache_key,
                                           const PROJECT_NAMESPACE_ID::object_cache_content& basic_data) {
  auto& data = hot_data_map_[cache_key];
  data.refresh_cache();
  data.update_data(cache_key, basic_data);
}

void global_cache_manager::hot_data_watch() {
  if (watch_heartbeat_timepoint_ > util::time::time_utility::get_now()) {
    return;
  }

  time_t heartbeat_interval = logic_config::me()->get_logic_cfg().cache().watcher().heartbeat_interval().seconds();
  if (heartbeat_interval <= 0) {
    heartbeat_interval = 15 * 60;
  }
  watch_heartbeat_timepoint_ = util::time::time_utility::get_now() + heartbeat_interval;

  if (!rpc::cache_api::has_cachesvr()) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  rpc::context::tracer tracer;
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = true;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(tracer, "global_cache_manager.hot_data_watch", std::move(trace_start_option));

  // 所有缓存订阅
  std::unordered_map<uint64_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watch_key>*>
      watch_keys_by_server_id;
  for (auto& hot_data_iter : hot_data_map_) {
    uint64_t server_inst_id = rpc::cache_api::get_cachesvr_server_id(hot_data_iter.first);
    if (0 == server_inst_id) {
      continue;
    }

    PROJECT_NAMESPACE_ID::object_cache_watch_key* watch_key = nullptr;
    auto iter = watch_keys_by_server_id.find(server_inst_id);
    if (iter == watch_keys_by_server_id.end()) {
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watch_key>* keys =
          ctx.create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watch_key>>();
      if (nullptr != keys) {
        keys->Reserve(static_cast<int>(hot_data_map_.size()));
        watch_key = keys->Add();

        watch_keys_by_server_id[server_inst_id] = keys;
      }
    } else {
      watch_key = iter->second->Add();
    }

    if (nullptr == watch_key) {
      break;
    }
    watch_key->set_cache_type(hot_data_iter.first.cache_type());
    watch_key->set_zone_id(hot_data_iter.first.zone_id());
    watch_key->set_instance_id(hot_data_iter.first.instance_id());
    watch_key->set_data_version(hot_data_iter.second->data_version());
  }

  if (watch_keys_by_server_id.empty()) {
    tracer.finish({0, {}});
    return;
  }

  int ret = 0;
  for (auto& watch_msg : watch_keys_by_server_id) {
    PROJECT_NAMESPACE_ID::SSCacheWatchSync* sync_body = ctx.create<PROJECT_NAMESPACE_ID::SSCacheWatchSync>();
    if (nullptr == sync_body) {
      FWLOGERROR("malloc SSCacheWatchSync failed");
      continue;
    }

    sync_body->mutable_watcher()->set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_SERVICE);
    sync_body->mutable_watcher()->set_zone_id(0);
    sync_body->mutable_watcher()->set_instance_id(logic_config::me()->get_local_server_id());
    sync_body->mutable_watcher()->set_server_subscribe(true);
    protobuf_move_message(*sync_body->mutable_watch_keys(), std::move(*watch_msg.second));
    int res = rpc::cache::watch(ctx, watch_msg.first, *sync_body);
    if (res < 0) {
      ret = res;
      FWLOGERROR("call rpc::cache::watch to server {:#x} with {} keys failed, res: {}({})", watch_msg.first,
                 sync_body->watch_keys_size(), res, protobuf_mini_dumper_get_error_msg(res));
    }
  }

  tracer.finish({ret, {}});
}

void global_cache_manager::remove_cold_data() {
  while (!hot_data_map_.empty()) {
    auto iter = hot_data_map_.begin();

    // 无效数据
    if (!(*iter).second) {
      FWLOGDEBUG("global_cache_manager::remove_cold_data remove");
      hot_data_map_.erase(iter);
      continue;
    }

    if ((*iter).second->is_cache_cold()) {
      // 全是有效的了
      break;
    }

    FWLOGDEBUG("global_cache_manager::remove_cold_data vaild remove {} ({}:{}:{})", (*iter).second->cache_timeout(),
               static_cast<uint32_t>((*iter).second->cache_key().cache_type()), (*iter).second->cache_key().zone_id(),
               (*iter).second->cache_key().instance_id());
    hot_data_map_.erase(iter);
  }
}

void global_cache_manager::hot_data_debug() {
  FWLOGINFO("global_cache_manager::hot_data_debug DATA size:{}", hot_data_map_.size());
  int64_t last_cache_timeout_ = 0;
  for (const auto& data : hot_data_map_) {
    if (last_cache_timeout_ > data.second->cache_timeout()) {
      FWLOGERROR("global_cache_manager::hot_data_debug DATA ERROR:({}:{}:{}) Timeout:{} Version:{}",
                 static_cast<uint32_t>(data.second->cache_key().cache_type()), data.second->cache_key().zone_id(),
                 data.second->cache_key().instance_id(), data.second->cache_timeout(), data.second->data_version());
    }
    last_cache_timeout_ = data.second->cache_timeout();
  }
}

void hot_data::fetch_data(PROJECT_NAMESPACE_ID::object_cache_content& out) const {
  FWLOGDEBUG("global_cache_manager::fetch_data {}:{}:{}", static_cast<uint32_t>(cache_key_.cache_type()),
             cache_key_.zone_id(), cache_key_.instance_id());
  protobuf_copy_message(out, cache_content_);
}

void hot_data::set_expired() noexcept { cache_content_.set_data_version(0); }

void hot_data::update_data(const PROJECT_NAMESPACE_ID::object_cache_key& input_cache_key,
                           const PROJECT_NAMESPACE_ID::object_cache_content& input_cache_content) {
  if (input_cache_content.data_version() >= cache_content_.data_version()) {
    protobuf_copy_message(cache_key_, input_cache_key);
    FWLOGDEBUG("hot_data::update_data version: ({})->({}) {}:{}:{}", cache_content_.data_version(),
               input_cache_content.data_version(), static_cast<uint32_t>(cache_key_.cache_type()), cache_key_.zone_id(),
               cache_key_.instance_id());
    protobuf_copy_message(cache_content_, input_cache_content);
  }
}

void hot_data::refresh_cache() noexcept {
  if (cache_timeout_ == 0) {
    FWLOGDEBUG("hot_data::create");
  } else {
    FWLOGDEBUG("hot_data::refresh_cache old:{} {}:{}:{}", cache_timeout_,
               static_cast<uint32_t>(cache_key_.cache_type()), cache_key_.zone_id(), cache_key_.instance_id());
  }
  int32_t expired_timeout = 20 * 60;
  cache_timeout_ = util::time::time_utility::get_now() + expired_timeout;
}

bool hot_data::is_cache_cold() const noexcept { return cache_timeout_ < util::time::time_utility::get_now(); }

int64_t hot_data::data_version() const noexcept { return cache_content_.data_version(); }

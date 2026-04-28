// Copyright 2026 atframework
// Created by owent

#include "logic/cache_group.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <libcopp/future/storage.h>

#include <config/extern_service_types.h>
#include <config/logic_config.h>

#include <logic/logic_server_setup.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <rpc/lobby/lobbysvrservice.h>

#include <utility>

#include "logic/cache_group_manager.h"

cache_object_base::cache_object_base(const PROJECT_NAMESPACE_ID::object_cache_key &key)
    : cachesvr_version_(0),
      data_version_(0),
      expired_time_(0),
      remove_protect_time_(0),
      next_check_time_(0),
      key_(key) {}

cache_object_base::~cache_object_base() { cleanup_all_watchers(true); }

int cache_object_base::replace_watcher(rpc::context &ctx, cache_group_manager &manager,
                                       const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key,
                                       int64_t data_version, uint64_t server_inst_id) {
  cache_watcher_timer_handle_t timer_handle;
  manager.reset_timer_handle(timer_handle);

  auto iter = watchers_.find(copp::future::make_unique<cache_watcher_t>(server_inst_id, watcher_key));
  if (iter != watchers_.end() && !(*iter)) {
    watchers_.erase(iter);
    iter = watchers_.end();
  }

  if (iter == watchers_.end()) {
    cache_watcher_t::ptr_t new_watcher = copp::future::make_unique<cache_watcher_t>(server_inst_id, watcher_key);
    if (!new_watcher) {
      FWLOGERROR("malloc watcher {}:{}:{} failed", static_cast<uint32_t>(watcher_key.cache_type()),
                 watcher_key.zone_id(), watcher_key.instance_id());
      return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
    }

    // 需要刷新访问时间
    new_watcher->visit_update();
    manager.setup_timer(*this, *new_watcher);
    watchers_.emplace(std::move(new_watcher));

    FWLOGINFO("Insert watcher {}:{}:{} for cache {}:{}:{}", static_cast<uint32_t>(watcher_key.cache_type()),
              watcher_key.zone_id(), watcher_key.instance_id(), static_cast<uint32_t>(get_key().cache_type()),
              get_key().zone_id(), get_key().instance_id());
  } else {
    // 需要刷新访问时间
    (*iter)->visit_update();
    (*iter)->set_server_instance_id(server_inst_id);

    // 检查观察者数据版本号，如果不匹配通知缓存失效
    if (data_version > 0 && data_version != get_data_version()) {
      (*iter)->notify_cache_expired(ctx, get_key());
    }
  }

  return 0;
}

void cache_object_base::remove_watcher(const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key,
                                       const cache_watcher_t *check_watcher) {
  cache_watcher_set_t::iterator iter = watchers_.find(copp::future::make_unique<cache_watcher_t>(0, watcher_key));
  if (iter != watchers_.end() && !(*iter)) {
    watchers_.erase(iter);
    iter = watchers_.end();
  }

  if (iter == watchers_.end()) {
    return;
  }

  if (nullptr != check_watcher && check_watcher != (*iter).get()) {
    return;
  }

  FWLOGINFO("Remove watcher {}:{}:{} for cache {}:{}:{}, watchers_ sz: {}",
            static_cast<uint32_t>(watcher_key.cache_type()), watcher_key.zone_id(), watcher_key.instance_id(),
            static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
            watchers_.size());

  // 支持重入,这里需要和cache_group_manager::remove_timer保持一致
  cache_group_manager *timer_manager = nullptr;
  cache_watcher_timer_handle_t timer_handle;

  // 先用内部的const接口移出定时器。允许重入
  std::tie(timer_manager, timer_handle) = (*iter)->move_timer_out_inner();

  // 解绑定时器
  if (nullptr != timer_manager && timer_manager->is_time_handle_valid(timer_handle)) {
    (*timer_handle).cache_object = nullptr;
    (*timer_handle).watcher_object = nullptr;

    // 移除相关的定时器
    timer_manager->remove_timer(timer_handle);
  }

  // 移除watcher
  watchers_.erase(iter);
}

bool cache_object_base::is_cache_valid() const {
  if (0 == cachesvr_version_ || 0 == data_version_) {
    return false;
  }

  logic_server_common_module *logic_module = logic_server_last_common_module();
  int64_t current_cachesvr_version =
      logic_module != nullptr
          ? logic_module->get_discovery_service_version(atfw::component::logic_service_type::kCacheSvr)
          : 0;
  if (nullptr != logic_module && cachesvr_version_ != current_cachesvr_version) {
    // 缓存分布未变化，则直接刷新版本即可
    if (logic_config::me()->get_local_server_id() == rpc::cache_api::get_cachesvr_server_id(get_key())) {
      const_cast<cache_object_base *>(this)->update_cachesvr_version(current_cachesvr_version);
    } else {
      // 缓存已经不在本节点上了，下次直接返回false即可
      const_cast<cache_object_base *>(this)->data_version_ = 0;
      return false;
    }
  }

  time_t cache_data_expired_timeout =
      logic_config::me()->get_logic_cfg().cache().data().cache_data_expired_timeout().seconds();
  time_t now = util::time::time_utility::get_now();

  // 需要支持改时间，data_version_ 精确到毫秒, @see cache_object_base::update_pull_cache_time
  time_t cache_content_update_time = data_version_ / 1000;
  bool ret = now >= cache_content_update_time && now < cache_content_update_time + cache_data_expired_timeout;
  if (!ret) {
    const_cast<cache_object_base *>(this)->data_version_ = 0;  // 下次直接返回false即可，不需要计算时间了
  }

  return ret;
}

void cache_object_base::visit_update() {
  time_t cache_expired_timeout = logic_config::me()->get_logic_cfg().cache().data().cache_expired_timeout().seconds();
  if (cache_expired_timeout <= 0) {
    cache_expired_timeout = util::time::time_utility::HOUR_SECONDS;
  }

  expired_time_ = util::time::time_utility::get_now() + cache_expired_timeout;
}

void cache_object_base::cleanup_all_watchers(bool /*notify*/) {
  // 要保证析构流程中递归调用到 remove_watcher 时安全
  cache_watcher_set_t watchers;
  watchers_.swap(watchers);
  for (const auto &watcher : watchers) {
    if (!watcher) {
      continue;
    }

    watcher->cleanup_timer();
  }

  // FIXME 正常对象关闭时都应该是失效状态，如果非失效状态且有watcher的情况下析构。说明缓存不够用了而淘汰。
  // 这时候最好通知所有的watcher cache失效.
  // if (!notify || !is_cache_valid() || watchers.empty()) {
  //     watchers.clear();
  //     return;
  // }

  watchers.clear();
}

rpc::result_code_type cache_object_base::set_cache_expired(rpc::context &ctx) {
  visit_update();

  data_version_ = 0;

  // 通知所有的watcher缓存失效
  if (watchers_.empty()) {
    RPC_RETURN_CODE(0);
  }

  std::unordered_map<uint64_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watcher> >
      object_cache_key_watcher;
  object_cache_key_watcher.reserve(watchers_.size());

  for (const auto &watcher : watchers_) {
    if (!watcher) {
      continue;
    }

    if (0 == watcher->get_server_instance_id()) {
      continue;
    }

    auto iter = object_cache_key_watcher.find(watcher->get_server_instance_id());
    if (object_cache_key_watcher.end() == iter) {
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watcher> &keys =
          object_cache_key_watcher[watcher->get_server_instance_id()];
      keys.Reserve(static_cast<int>(watchers_.size()));
      protobuf_copy_message(*keys.Add(), watcher->get_key());
    } else {
      protobuf_copy_message(*iter->second.Add(), watcher->get_key());
    }
  }

  if (object_cache_key_watcher.empty()) {
    RPC_RETURN_CODE(0);
  }

  // 批量通知缓存过期
  int32_t ret = 0;
  logic_server_common_module *logic_module = logic_server_last_common_module();
  for (auto &keys_on_server : object_cache_key_watcher) {
    if (logic_module == nullptr) {
      FWLOGWARNING(
          "send cache {}:{}:{} expired to {} watchers on server {:#x} ignored. logic_server_common_module not ready",
          static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
          keys_on_server.second.size(), keys_on_server.first);
      continue;
    }
    auto node = logic_module->get_discovery_by_id(keys_on_server.first);
    if (!node) {
      FWLOGWARNING("send cache {}:{}:{} expired to {} watchers on server {:#x} ignored. node may shutdown",
                   static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
                   keys_on_server.second.size(), keys_on_server.first);
      continue;
    }
    if (node->get_discovery_info().type_id() != static_cast<uint64_t>(atfw::component::logic_service_type::kLobbySvr)) {
      FWLOGERROR("send cache {}:{}:{} expired to {} watchers on server {:#x} failed. invalid watcher server type {}",
                 static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
                 keys_on_server.second.size(), keys_on_server.first, node->get_discovery_info().type_id());
      continue;
    }

    PROJECT_NAMESPACE_ID::SSObjectCacheExpiredSync *req_body =
        ctx.create<PROJECT_NAMESPACE_ID::SSObjectCacheExpiredSync>();
    if (nullptr == req_body) {
      FWLOGERROR("malloc SSObjectCacheExpiredSync failed");
      continue;
    }

    protobuf_copy_message(*req_body->mutable_expired_key(), get_key());
    protobuf_move_message(*req_body->mutable_watchers(), std::move(keys_on_server.second));

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::lobby::object_cache_expired_sync(ctx, keys_on_server.first, *req_body));
    if (res < 0) {
      FWLOGERROR("send cache {}:{}:{} expired to {} watchers on server {:#x}",
                 static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
                 req_body->watchers_size(), keys_on_server.first, res, protobuf_mini_dumper_get_error_msg(res));
      ret = res;
    } else {
      FWLOGDEBUG("send cache {}:{}:{} expired to {} watchers on server {:#x}",
                 static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
                 req_body->watchers_size(), keys_on_server.first);
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type cache_object_base::notify_update_meta(rpc::context &ctx,
                                                            const PROJECT_NAMESPACE_ID::object_cache_meta &input) {
  visit_update();

  // 通知所有的watcher meta刷新
  if (watchers_.empty()) {
    RPC_RETURN_CODE(0);
  }

  std::unordered_map<uint64_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watcher> >
      object_cache_key_watcher;
  object_cache_key_watcher.reserve(watchers_.size());

  for (const auto &watcher : watchers_) {
    if (!watcher) {
      continue;
    }

    if (0 == watcher->get_server_instance_id()) {
      continue;
    }

    auto iter = object_cache_key_watcher.find(watcher->get_server_instance_id());
    if (object_cache_key_watcher.end() == iter) {
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_watcher> &keys =
          object_cache_key_watcher[watcher->get_server_instance_id()];
      keys.Reserve(static_cast<int>(watchers_.size()));
      protobuf_copy_message(*keys.Add(), watcher->get_key());
    } else {
      protobuf_copy_message(*iter->second.Add(), watcher->get_key());
    }
  }

  if (object_cache_key_watcher.empty()) {
    RPC_RETURN_CODE(0);
  }

  // 批量通知缓存过期
  int32_t ret = 0;
  logic_server_common_module *logic_module = logic_server_last_common_module();
  for (auto &keys_on_server : object_cache_key_watcher) {
    if (logic_module == nullptr) {
      FWLOGWARNING(
          "send cache {}:{}:{} update meta to {} watchers on server {:#x} ignored. logic_server_common_module not "
          "ready",
          static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
          keys_on_server.second.size(), keys_on_server.first);
      continue;
    }
    auto node = logic_module->get_discovery_by_id(keys_on_server.first);
    if (!node) {
      FWLOGWARNING("send cache {}:{}:{} update meta to {} watchers on server {:#x} ignored. node may shutdown",
                   static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
                   keys_on_server.second.size(), keys_on_server.first);
      continue;
    }
    if (node->get_discovery_info().type_id() != static_cast<uint64_t>(atfw::component::logic_service_type::kLobbySvr)) {
      FWLOGERROR(
          "send cache {}:{}:{} update meta to {} watchers on server {:#x} failed. invalid watcher server type {}",
          static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
          keys_on_server.second.size(), keys_on_server.first, node->get_discovery_info().type_id());
      continue;
    }

    PROJECT_NAMESPACE_ID::SSObjectCacheMetaSync *req_body = ctx.create<PROJECT_NAMESPACE_ID::SSObjectCacheMetaSync>();
    if (nullptr == req_body) {
      FWLOGERROR("malloc SSObjectCacheMetaSync failed");
      continue;
    }

    protobuf_copy_message(*req_body->mutable_update_meta(), input);
    protobuf_move_message(*req_body->mutable_watchers(), std::move(keys_on_server.second));

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::lobby::object_cache_meta_sync(ctx, keys_on_server.first, *req_body));
    if (res < 0) {
      FWLOGERROR("send cache {}:{}:{} update meta to {} watchers on server {:#x}",
                 static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
                 req_body->watchers_size(), keys_on_server.first, res, protobuf_mini_dumper_get_error_msg(res));
      ret = res;
    } else {
      FWLOGDEBUG("send cache {}:{}:{} update meta to {} watchers on server {:#x}",
                 static_cast<uint32_t>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(),
                 req_body->watchers_size(), keys_on_server.first);
    }
  }

  RPC_RETURN_CODE(ret);
}

void cache_object_base::update_cachesvr_version(int64_t cachesvr_version) noexcept {
  cachesvr_version_ = cachesvr_version;
}

void cache_object_base::update_pull_cache_time() noexcept {
  // 精确到毫秒就够了
  int64_t next_version =
      (atfw::util::time::time_utility::get_now() * 1000) + (atfw::util::time::time_utility::get_now_usec() / 1000);
  if (next_version <= data_version_) {
    ++data_version_;
  } else {
    data_version_ = next_version;
  }
}

void cache_object_base::update_next_check_time() noexcept {
  time_t cache_check_interval = logic_config::me()->get_logic_cfg().cache().data().cache_check_interval().seconds();
  if (cache_check_interval <= 0) {
    cache_check_interval = 600;
  }

  next_check_time_ = util::time::time_utility::get_now() + cache_check_interval;
}

void cache_object_base::update_remove_protect_time() noexcept {
  time_t remove_protect_interval = logic_config::me()->get_logic_cfg().task().csmsg().timeout().seconds();
  if (remove_protect_interval <= 0) {
    remove_protect_interval = 10;
  }

  remove_protect_time_ = util::time::time_utility::get_now() + remove_protect_interval;
}

cache_group_base::cache_group_base(cache_group_manager &manager) : owner_(&manager) {}

cache_group_base::~cache_group_base() {}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
int64_t cache_group_base::get_local_cachesvr_version() const {
  logic_server_common_module *logic_module = logic_server_last_common_module();
  if (nullptr != logic_module) {
    return logic_module->get_discovery_service_version(atfw::component::logic_service_type::kCacheSvr);
  }

  return 1;
}

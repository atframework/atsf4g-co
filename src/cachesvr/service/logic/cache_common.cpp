// Copyright 2026 atframework
// Created by owent

#include "logic/cache_common.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <algorithm/murmur_hash.h>

#include <config/extern_service_types.h>
#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>

#include <logic/logic_server_setup.h>

#include <rpc/lobby/lobbysvrservice.atfw.gen.h>

#include <cstddef>
#include <utility>

#include "logic/cache_group_manager.h"

// NOLINTNEXTLINE(modernize-pass-by-value)
cache_watcher_t::cache_watcher_t(uint64_t server_id, const PROJECT_NAMESPACE_ID::object_cache_watcher &key)
    : manager_(nullptr), watcher_key_(key), server_inst_id_(server_id), expired_time_(0), next_check_time_(0) {}

cache_watcher_t::~cache_watcher_t() {
  if (nullptr == manager_) {
    return;
  }

  cleanup_timer();
}

cache_watcher_t::cache_watcher_t(cache_watcher_t &&other) noexcept
    : manager_(nullptr), server_inst_id_(0), expired_time_(0), next_check_time_(0) {
  *this = std::move(other);
}

cache_watcher_t &cache_watcher_t::operator=(cache_watcher_t &&other) noexcept {
  manager_ = other.manager_;
  timer_handle_ = other.timer_handle_;
  server_inst_id_ = other.server_inst_id_;
  protobuf_move_message(watcher_key_, std::move(other.watcher_key_));
  expired_time_ = other.expired_time_;
  next_check_time_ = other.next_check_time_;

  other.manager_ = nullptr;
  other.server_inst_id_ = 0;
  other.watcher_key_.Clear();
  other.expired_time_ = 0;
  other.next_check_time_ = 0;

  return *this;
}

std::pair<cache_group_manager *, cache_watcher_timer_handle_t> cache_watcher_t::move_timer_out() {
  std::pair<cache_group_manager *, cache_watcher_timer_handle_t> ret = std::make_pair(manager_, timer_handle_);
  if (nullptr != manager_) {
    manager_->reset_timer_handle(timer_handle_);
  }
  manager_ = nullptr;

  return ret;
}

void cache_watcher_t::move_timer_in(cache_group_manager &manager, cache_watcher_timer_handle_t &&handle) {
  cleanup_timer();

  manager_ = &manager;
  timer_handle_ = handle;

  manager_->reset_timer_handle(handle);
}

void cache_watcher_t::visit_update() {
  time_t expired_timeout = logic_config::me()->get_logic_cfg().cache().watcher().expired_timeout().seconds();
  if (expired_timeout <= 0) {
    expired_timeout = 2400;
  }

  expired_time_ = util::time::time_utility::get_now() + expired_timeout;
}

void cache_watcher_t::update_next_check_time() {
  time_t check_interval = logic_config::me()->get_logic_cfg().cache().watcher().check_interval().seconds();
  if (check_interval <= 0) {
    check_interval = 900;
  }

  next_check_time_ = util::time::time_utility::get_now() + check_interval;
}

void cache_watcher_t::notify_cache_expired(::rpc::context &ctx,
                                           const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) {
  if (0 == server_inst_id_) {
    return;
  }

  auto *mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return;
  }
  auto node = mod->get_discovery_by_id(server_inst_id_);
  if (!node) {
    FWLOGWARNING(
        "send cache {}:{}:{} expired to watcher {}:{}:{} on server {:#x} failed. server not found in discovery",
        static_cast<int>(cache_key.cache_type()), cache_key.zone_id(), cache_key.instance_id(),
        static_cast<int>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(), server_inst_id_);
    return;
  }

  if (node->get_discovery_info().type_id() != static_cast<uint64_t>(atfw::component::logic_service_type::kLobbySvr)) {
    FWLOGERROR("send cache {}:{}:{} expired to watcher {}:{}:{} on server {:#x} failed. invalid watcher server type {}",
               static_cast<int>(cache_key.cache_type()), cache_key.zone_id(), cache_key.instance_id(),
               static_cast<int>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(), server_inst_id_,
               node->get_discovery_info().type_id());
    return;
  }

  // 单独通知缓存过期
  PROJECT_NAMESPACE_ID::SSObjectCacheExpiredSync *req_body =
      ctx.create<PROJECT_NAMESPACE_ID::SSObjectCacheExpiredSync>();
  if (req_body == nullptr) {
    FWLOGERROR("malloc SSObjectCacheExpiredSync error");
    return;
  }

  protobuf_copy_message(*req_body->add_watchers(), get_key());
  protobuf_copy_message(*req_body->mutable_expired_key(), cache_key);

  int32_t res = rpc::lobby::object_cache_expired_sync(ctx, server_inst_id_, *req_body).unwrap();
  if (res < 0) {
    FWLOGERROR("send cache {}:{}:{} expired to watcher {}:{}:{} on server {:#x}",
               static_cast<int>(cache_key.cache_type()), cache_key.zone_id(), cache_key.instance_id(),
               static_cast<int>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(), server_inst_id_,
               res, protobuf_mini_dumper_get_error_msg(res));
  } else {
    FWLOGDEBUG("send cache {}:{}:{} expired to watcher {}:{}:{} on server {:#x}",
               static_cast<int>(cache_key.cache_type()), cache_key.zone_id(), cache_key.instance_id(),
               static_cast<int>(get_key().cache_type()), get_key().zone_id(), get_key().instance_id(), server_inst_id_);
  }
}

void cache_watcher_t::cleanup_timer() {
  if (nullptr == manager_) {
    return;
  }

  // 支持重入,这里需要和cache_group_manager::remove_timer保持一致
  cache_group_manager *timer_manager = nullptr;
  cache_watcher_timer_handle_t timer_handle;

  // 先移出定时器。允许重入
  std::tie(timer_manager, timer_handle) = move_timer_out();

  // 解绑定时器
  if (nullptr != timer_manager && timer_manager->is_time_handle_valid(timer_handle)) {
    (*timer_handle).cache_object = nullptr;
    (*timer_handle).watcher_object = nullptr;

    // 移除相关的定时器
    timer_manager->remove_timer(timer_handle);
  }
}

std::pair<cache_group_manager *, cache_watcher_timer_handle_t> cache_watcher_t::move_timer_out_inner() const {
  return const_cast<cache_watcher_t *>(this)->move_timer_out();
}

// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#include "logic/cache_group_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/arena.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <protocol/pbdesc/com.struct.cache.pb.h>
#include <protocol/pbdesc/svr.struct.cache.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <dispatcher/task_manager.h>

#include <utility/protobuf_mini_dumper.h>

#include <logic/logic_server_setup.h>

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_shared_message.h>

#include <rpc/cache/cache_algorithm.h>

#include <rpc/lobby/lobbysvrservice.atfw.gen.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <memory>

#include "config/server_frame_build_feature.h"

cache_group_manager::cache_group_manager()
    : user_cache_group_(*this) {}

cache_group_manager::~cache_group_manager() {}

int cache_group_manager::tick() {
  time_t now = util::time::time_utility::get_now();

  int ret = 0;
  ret += tick_watcher(now);
  ret += tick_cache_groups(now);
  if (!double_check_cache_objects_.empty()) {
    ret += tick_double_check(now);
  }

  return ret;
}

int cache_group_manager::tick_watcher(time_t now) {
  const auto &watcher_conf = logic_config::me()->get_logic_cfg().cache().watcher();

  size_t max_recycle_count = watcher_conf.max_recycle_count_per_tick();
  if (max_recycle_count <= 0) {
    max_recycle_count = 1000;
  }

  // size_t max_watcher_number = watcher_conf.max_number();
  // if (max_watcher_number <= 0) {
  //     max_watcher_number = 1000000;
  // }
  size_t ret = 0;

  // Watcher GC流程
  for (; !timers_.empty() && (0 == max_recycle_count || ret < max_recycle_count); ++ret) {
    auto iter = timers_.begin();

    cache_object_base *cache_object = (*iter).cache_object;
    cache_watcher_t *watcher_object = (*iter).watcher_object;

    bool need_remove = false;
    do {
      // 无效数据
      if (nullptr == cache_object || nullptr == watcher_object) {
        need_remove = true;
        break;
      }

      // 长时间未访问，缓存可以淘汰。正常watcher会定期刷新访问时间
      if (watcher_object->get_expired_time() <= now) {
        need_remove = true;
        break;
      }

      // TODO 缓存数量超出预期,高负载保护
      // if (max_watcher_number > 0 && timers_size > max_watcher_number) {
      //     need_remove = true;
      //
      //     // TODO OSS日志告警。可能需要扩容缓存服务器
      //     break;
      // }
    } while (false);

    if (need_remove) {
      // 和下面 remove_timer 一样
      // 支持重入,这里需要和cache_watcher_t::cleanup_timer保持一致
      if (nullptr != watcher_object && iter == watcher_object->get_timer_handle()) {
        // 解绑watcher内的定时器，后面会移除
        watcher_object->move_timer_out();
      }

      // 先复制，允许重入
      timers_.erase(iter);

      // 也要移除相关的watcher
      if (nullptr != cache_object && nullptr != watcher_object) {
        // 如果是即将释放最后一个watcher，可以加入double check队列，尽早释放cache对象
        if (1 == cache_object->get_watcher_size()) {
          double_check_cache_objects_.insert(cache_object->get_key());
        }
        cache_object->remove_watcher(watcher_object->get_key(), watcher_object);
      }
      continue;
    }

    if (watcher_object->get_next_check_time() > now) {
      break;
    }

    // 刷新重设定时器
    setup_timer(*cache_object, *watcher_object);
  }

  return static_cast<int>(ret);
}

int cache_group_manager::tick_cache_groups(time_t now) {
  int64_t cachesvr_version = 1;
  logic_server_common_module *logic_module = logic_server_last_common_module();
  if (nullptr != logic_module) {
    cachesvr_version = logic_module->get_discovery_service_version(atfw::component::logic_service_type::kCacheSvr);
  }

  int ret = 0;

  // 缓存组tick - user cache
  {
    int res = user_cache_group_.tick(now, cachesvr_version);
    if (res >= 0) {
      ret += res;
    }
  }
  return ret;
}

int cache_group_manager::tick_double_check(time_t) {
  int ret = 0;
  std::unordered_set<PROJECT_NAMESPACE_ID::object_cache_key, rpc::cache_api::cache_key_hash_t,
                     rpc::cache_api::cache_key_equal_t>
      double_check_cache_objects;
  double_check_cache_objects.swap(double_check_cache_objects_);
  double_check_cache_objects_.reserve(256);

  for (const auto &cache_key : double_check_cache_objects) {
    cache_group_base *group = get_group(cache_key.cache_type());
    if (nullptr == group) {
      continue;
    }

    std::shared_ptr<cache_object_base> cache_object = group->get_cache(cache_key);
    if (!cache_object) {
      continue;
    }

    if (!cache_object->has_watcher() && !cache_object->is_cache_valid()) {
      group->remove_cache(cache_key, false);
      ++ret;
    }
  }

  return ret;
}

void cache_group_manager::setup_timer(cache_object_base &cache_object, cache_watcher_t &watcher) {
  cache_watcher_timer_handle_t handle = timers_.insert(timers_.end(), cache_watcher_timer_t{&cache_object, &watcher});
  watcher.move_timer_in(*this, std::move(handle));
  watcher.update_next_check_time();
}

void cache_group_manager::remove_timer(cache_watcher_timer_handle_t &handle) {
  if (handle == timers_.end()) {
    return;
  }

  cache_object_base *cache_object = (*handle).cache_object;
  cache_watcher_t *watcher_object = (*handle).watcher_object;

  // 和上面 tick_watcher 一样
  // 支持重入,这里需要和cache_watcher_t::cleanup_timer保持一致
  if (nullptr != watcher_object && handle == watcher_object->get_timer_handle()) {
    // 解绑watcher内的定时器，后面会移除
    watcher_object->move_timer_out();
  }

  // 先复制，允许重入
  timers_.erase(handle);
  reset_timer_handle(handle);

  // 也要移除相关的watcher
  if (nullptr != cache_object && nullptr != watcher_object) {
    // 如果是即将释放最后一个watcher，可以加入double check队列，尽早释放cache对象
    if (1 == cache_object->get_watcher_size()) {
      double_check_cache_objects_.insert(cache_object->get_key());
    }
    cache_object->remove_watcher(watcher_object->get_key(), watcher_object);
  }
}

void cache_group_manager::reset_timer_handle(cache_watcher_timer_handle_t &handle) { handle = timers_.end(); }

bool cache_group_manager::is_time_handle_valid(const cache_watcher_timer_handle_t &handle) {
  return handle != timers_.end();
}

cache_group_base *cache_group_manager::get_group(PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type) {
  switch (cache_type) {
    case PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER:
      return &user_cache_group_;
    default:
      return nullptr;
  }
}

// Copyright 2026 atframework
// Created by owent

#pragma once

#include <design_pattern/singleton.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <ctime>
#include <unordered_set>

#include "logic/cache_group.h"
#include "logic/user_cache_group.h"

class cache_group_manager : public util::design_pattern::singleton<cache_group_manager> {
 protected:
  cache_group_manager();
  ~cache_group_manager();

 public:
  int tick();
  int tick_watcher(time_t now);
  int tick_cache_groups(time_t now);
  int tick_double_check(time_t now);

  void setup_timer(cache_object_base &, cache_watcher_t &watcher);
  void remove_timer(cache_watcher_timer_handle_t &handle);
  void reset_timer_handle(cache_watcher_timer_handle_t &handle);
  bool is_time_handle_valid(const cache_watcher_timer_handle_t &handle);

  /**
   * @brief Get the group object by cache type
   *
   * @param cache_type @see PROJECT_NAMESPACE_ID::EnCacheApiCacheType
   * @return nullptr if type is invalid
   */
  cache_group_base *get_group(PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type);

 private:
  cache_watcher_timer_set_t timers_;

  user_cache_group user_cache_group_;

  // Double check cache
  std::unordered_set<PROJECT_NAMESPACE_ID::object_cache_key, rpc::cache_api::cache_key_hash_t,
                     rpc::cache_api::cache_key_equal_t>
      double_check_cache_objects_;
};

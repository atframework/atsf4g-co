// Copyright atframework 2023
// Created by owent on 2019-07-15.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <design_pattern/singleton.h>

#include <stdint.h>
#include <cstddef>
#include <list>
#include <memory>

class router_object_base;
class router_manager_base;
class router_manager_set;

struct UTIL_SYMBOL_VISIBLE router_system_timer_t {
  uint32_t timer_sequence;
  uint32_t type_id;
  time_t timeout;
  std::weak_ptr<router_object_base> obj_watcher;
};

namespace rpc {
class context;
}

struct UTIL_SYMBOL_VISIBLE router_manager_metrics_data {
  std::atomic<int64_t> pull_cache_count;
  std::atomic<int64_t> pull_object_count;
  std::atomic<int64_t> save_count;
  std::atomic<int64_t> cache_count;
};

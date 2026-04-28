// Copyright 2026 atframework
// Created by owent

#pragma once

#include <config/compile_optimize.h>
#include <config/compiler_features.h>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <config/server_frame_build_feature.h>

#include <rpc/cache/cache_algorithm.h>

#include <cstddef>
#include <cstdint>

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rpc {
class context;
}

class cache_object_base;
class cache_group_manager;
class cache_watcher_t;

struct cache_watcher_timer_t {
  cache_object_base *cache_object;  // 所属缓存对象
  cache_watcher_t *watcher_object;  // 观察者
};

using cache_watcher_timer_set_t = std::list<cache_watcher_timer_t>;
using cache_watcher_timer_handle_t = cache_watcher_timer_set_t::iterator;

class cache_watcher_t {
 public:
  using ptr_t = std::unique_ptr<cache_watcher_t>;

  UTIL_DESIGN_PATTERN_NOCOPYABLE(cache_watcher_t)

 public:
  cache_watcher_t(uint64_t, const PROJECT_NAMESPACE_ID::object_cache_watcher &);
  ~cache_watcher_t();

  cache_watcher_t(cache_watcher_t &&other) noexcept;
  cache_watcher_t &operator=(cache_watcher_t &&other) noexcept;

  /**
   * @brief 这是一个unsafe接口，移出定时器索引。
   * @note 使用者要负责检查有效性并调用remove_timer()移除定时器
   *
   * @return std::pair<cache_group_manager*, cache_watcher_timer_handle_t> 绑定的定时器结构
   */
  std::pair<cache_group_manager *, cache_watcher_timer_handle_t> move_timer_out();

  /**
   * @brief 这是一个安全的接口。如果原先存在定时器。先移除老的定时器
   *
   */
  void move_timer_in(cache_group_manager &, cache_watcher_timer_handle_t &&);

  inline cache_group_manager *get_timer_manager() const { return manager_; }
  inline const cache_watcher_timer_handle_t &get_timer_handle() const { return timer_handle_; }
  inline const PROJECT_NAMESPACE_ID::object_cache_watcher &get_key() const { return watcher_key_; }
  inline uint64_t get_server_instance_id() const { return server_inst_id_; }
  inline void set_server_instance_id(uint64_t instance_id) { server_inst_id_ = instance_id; }
  inline time_t get_expired_time() const { return expired_time_; }
  inline time_t get_next_check_time() const { return next_check_time_; }

  void visit_update();
  void update_next_check_time();

  void notify_cache_expired(::rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_key &cache_key);

 private:
  void cleanup_timer();

  /**
   * @brief 这是一个unsafe接口，移出定时器索引不会影响上层的Key-Value索引。所以可以直接移出
   * @note 使用者要负责检查有效性并调用remove_timer()移除定时器
   *
   * @return std::pair<cache_group_manager*, cache_watcher_timer_handle_t> 绑定的定时器结构
   */
  std::pair<cache_group_manager *, cache_watcher_timer_handle_t> move_timer_out_inner() const;
  friend class cache_object_base;

 private:
  mutable cache_group_manager *manager_;
  mutable cache_watcher_timer_handle_t timer_handle_;
  PROJECT_NAMESPACE_ID::object_cache_watcher watcher_key_;
  uint64_t server_inst_id_;
  time_t expired_time_;
  time_t next_check_time_;
};

struct cache_watcher_hash_t : public ::rpc::cache_api::cache_watcher_hash_t {
  using ::rpc::cache_api::cache_watcher_hash_t::operator();
  size_t operator()(const cache_watcher_t::ptr_t &watcher) const;
};

struct cache_watcher_equal_t : public ::rpc::cache_api::cache_watcher_equal_t {
  using ::rpc::cache_api::cache_watcher_equal_t::operator();
  bool operator()(const cache_watcher_t::ptr_t &left, const cache_watcher_t::ptr_t &right) const;
};

using cache_watcher_set_t = std::unordered_set<cache_watcher_t::ptr_t, cache_watcher_hash_t, cache_watcher_equal_t>;

// Copyright 2021 atframework
// Created by owent on 2018/05/01.
//

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <lock/spin_rw_lock.h>

#include <dispatcher/task_type_traits.h>

#include <string>
#include <unordered_map>

#include "router/router_system_defs.h"

class task_action_auto_save_objects;
class router_manager_set {
#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(router_manager_set)
#  else
  UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(router_manager_set)
#  endif
#else
  UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(router_manager_set)
#endif
 public:
  using timer_t = router_system_timer_t;

  enum auto_save_action_t {
    EN_ASA_SAVE = 0,
    EN_ASA_REMOVE_OBJECT,
    EN_ASA_REMOVE_CACHE,
  };

  struct ATFW_UTIL_SYMBOL_VISIBLE pending_action_data {
    auto_save_action_t action;
    uint32_t type_id;

    std::shared_ptr<router_object_base> object;
  };

 public:
  SERVER_FRAME_API router_manager_set();
  SERVER_FRAME_API ~router_manager_set();

  SERVER_FRAME_API int init();

  SERVER_FRAME_API int tick();

  SERVER_FRAME_API int stop();

  SERVER_FRAME_API void force_close();

  SERVER_FRAME_API bool insert_timer(router_manager_base *mgr, const std::shared_ptr<router_object_base> &obj,
                                     bool is_fast = false);

  SERVER_FRAME_API router_manager_base *get_manager(uint32_t type);

  SERVER_FRAME_API int register_manager(router_manager_base *b);
  SERVER_FRAME_API int unregister_manager(router_manager_base *b);

  SERVER_FRAME_API size_t size() const;

  /**
   * @brief 尝试回收缓存对象
   * @param max_count 最大回收数量
   * @note 此操作不会立即回收缓存对象，而是会把待回收的缓存标记为待清理，并放到清理队列中。
   *       清理任务会尽快执行，但是如果在清理前又对这个缓存调用了mutable_cache或者mutable_object则会放弃清理。
   *       清理后缓存对象会被添加到快队列做复检，复检没问题后才会最终移除对象内存结构，这个延迟取决于快队列的tick间隔，一般是5秒左右。
   * @return 错误返回错误码(小于0)，成功返回回收数量，返回0表示没有对象要被清理。
   */
  SERVER_FRAME_API int recycle_caches(int max_count);

  ATFW_UTIL_FORCEINLINE bool is_closing() const { return is_closing_; }

  ATFW_UTIL_FORCEINLINE bool is_closed() const { return is_closed_; }

  SERVER_FRAME_API bool add_save_schedule(const std::shared_ptr<router_object_base> &obj);
  SERVER_FRAME_API bool add_downgrade_schedule(const std::shared_ptr<router_object_base> &obj);
  SERVER_FRAME_API bool mark_fast_save(router_manager_base *mgr, const std::shared_ptr<router_object_base> &obj);

  SERVER_FRAME_API void add_io_schedule_order_task(const std::shared_ptr<router_object_base> &obj,
                                                   task_type_trait::task_type &task);

  ATFW_UTIL_FORCEINLINE void set_pre_closing() { is_pre_closing_ = true; }

  SERVER_FRAME_API std::shared_ptr<router_manager_metrics_data> mutable_metrics_data(const std::string &manager_name);
  SERVER_FRAME_API std::shared_ptr<router_manager_metrics_data> mutable_metrics_data(uint32_t type);

 private:
  bool is_save_task_running() const;

  bool is_closing_task_running() const;

  int tick_timer(time_t cache_expire, time_t object_expire, time_t object_save, std::list<timer_t> &timer_list,
                 bool is_fast);

  void setup_metrics();

 private:
  struct timer_set_t {
    std::list<timer_t> default_timer_list;
    std::list<timer_t> fast_timer_list;
  };
  timer_set_t timers_;
  time_t last_proc_time_;
  router_manager_base *mgrs_[PROJECT_NAMESPACE_ID::EnRouterObjectType_ARRAYSIZE];
  std::list<pending_action_data> pending_action_list_;
  task_type_trait::task_type pending_action_task_;
  task_type_trait::task_type closing_task_;
  bool is_closing_;
  bool is_closed_;
  bool is_pre_closing_;
  bool is_ready_;

  struct metrics_set_data {
    atfw::util::lock::spin_rw_lock metric_lock;
    std::unordered_map<std::string, std::shared_ptr<router_manager_metrics_data>> metric_data;

    std::atomic<int64_t> fast_timer_count;
    std::atomic<int64_t> default_timer_count;
  };
  metrics_set_data metrics_data_;

  friend class task_action_auto_save_objects;
  friend class task_action_router_close_manager_set;
};

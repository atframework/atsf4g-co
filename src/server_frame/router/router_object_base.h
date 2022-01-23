// Copyright 2021 atframework
// Created by owent on 2018/05/07.
//

#ifndef ROUTER_ROUTER_OBJECT_BASE_H
#define ROUTER_ROUTER_OBJECT_BASE_H

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/compiler_features.h>
#include <design_pattern/noncopyable.h>

#include <dispatcher/task_manager.h>

#include <stdint.h>
#include <cstddef>
#include <ctime>
#include <functional>
#include <list>
#include <set>
#include <string>
#include <type_traits>

#include "router/router_system_defs.h"

class router_object_base : public ::util::design_pattern::noncopyable {
 public:
  struct key_t {
    uint32_t type_id;
    uint32_t zone_id;
    uint64_t object_id;

    key_t() : type_id(0), zone_id(0), object_id(0) {}
    key_t(uint32_t tid, uint32_t zid, uint64_t oid) : type_id(tid), zone_id(zid), object_id(oid) {}

    bool operator==(const key_t &r) const noexcept;
    bool operator!=(const key_t &r) const noexcept;
    bool operator<(const key_t &r) const noexcept;
    bool operator<=(const key_t &r) const noexcept;
    bool operator>(const key_t &r) const noexcept;
    bool operator>=(const key_t &r) const noexcept;
  };

  /**
   * @note 基类flag范围是0x00000001-0x00008000
   * @note 子类flag范围是0x00010000-0x40000000
   */
  struct flag_t {
    enum type {
      EN_ROFT_FORCE_PULL_OBJECT = 0x0001,  // 下一次mutable_object时是否强制执行数据拉取
      EN_ROFT_IS_OBJECT = 0x0002,          // 当前对象是否时实体（可写）
      EN_ROFT_FORCE_SAVE_OBJECT = 0x0004,  // 下一次触发定时器时是否强制执行数据保存
      EN_ROFT_CACHE_REMOVED =
          0x0008,  // 当前对象缓存是否已处于实体被移除的状态，缓存被移除意味着已经不在manager的管理中，但是可能临时存在于部分正在进行的任务里
      EN_ROFT_SAVING = 0x0010,               // 是否正在保存
      EN_ROFT_TRANSFERING = 0x0020,          // 是否正在进行数据转移
      EN_ROFT_PULLING_CACHE = 0x0040,        // 是否正在拉取对象缓存
      EN_ROFT_PULLING_OBJECT = 0x0080,       // 是否正在拉取对象实体
      EN_ROFT_SCHED_REMOVE_OBJECT = 0x0100,  // 定时任务 - 实体降级计划任务是否有效
      EN_ROFT_SCHED_REMOVE_CACHE = 0x0200,   // 定时任务 - 移除缓存计划任务是否有效
      EN_ROFT_SCHED_SAVE_OBJECT = 0x0400,    // 定时任务 - 实体保存计划任务是否有效
    };
  };

  class flag_guard {
   public:
    flag_guard(router_object_base &owner, int f);
    ~flag_guard();

    inline operator bool() { return !!f_; }

   private:
    router_object_base *owner_;
    int f_;
  };

 protected:
  explicit router_object_base(const key_t &k);
  explicit router_object_base(key_t &&k);
  virtual ~router_object_base();

 public:
  void refresh_visit_time();
  void refresh_save_time();

  inline const key_t &get_key() const { return key_; }
  inline bool check_flag(int32_t v) const { return (flags_ & v) == v; }
  inline void set_flag(int32_t v) { flags_ |= v; }
  inline void unset_flag(int32_t v) { flags_ &= ~v; }
  inline int32_t get_flags() const { return flags_; }

  inline uint32_t alloc_timer_sequence() { return ++timer_sequence_; }
  inline bool check_timer_sequence(uint32_t seq) const { return seq == timer_sequence_; }

  inline bool is_writable() const {
    return check_flag(flag_t::EN_ROFT_IS_OBJECT) && !check_flag(flag_t::EN_ROFT_FORCE_PULL_OBJECT) &&
           !check_flag(flag_t::EN_ROFT_CACHE_REMOVED);
  }

  inline bool is_io_running() const { return io_task_ && !io_task_->is_exiting(); }
  inline bool is_pulling_cache() const { return check_flag(flag_t::EN_ROFT_PULLING_CACHE); }
  inline bool is_pulling_object() const { return check_flag(flag_t::EN_ROFT_PULLING_OBJECT); }
  inline bool is_transfering() const { return check_flag(flag_t::EN_ROFT_TRANSFERING); }

  inline time_t get_last_visit_time() const { return last_visit_time_; }
  inline time_t get_last_save_time() const { return last_save_time_; }

  /**
   * @brief 获取缓存是否有效
   * @note 如果缓存过期或正在拉取缓存，则缓存无效
   * @return 缓存是否有效
   */
  bool is_cache_available() const;

  /**
   * @brief 获取实体是否有效
   * @note 如果没有实体或实体要强制拉取或正在拉取实体，则实体无效
   * @return 实体是否有效
   */
  bool is_object_available() const;

  /**
   * @brief 获取路由节点ID
   * @return 路由节点ID
   */
  inline uint64_t get_router_server_id() const { return router_svr_id_; }

  /**
   * @brief 移除实体，降级为缓存
   */
  int remove_object(void *priv_data, uint64_t transfer_to_svr_id);

  /**
   * @brief 名字接口
   * @return 获取类型名称
   */
  virtual const char *name() const = 0;

  /**
   * @brief 启动拉取缓存流程
   * @param priv_data 外部传入的私有数据
   * @note 这个接口如果不是默认实现，则要注意如果异步消息回来以后已经有实体数据了，就要已实体数据为准
   * @note 必须要填充数据有：
   *       * 关联的对象的数据
   *       * 版本信息(如果有)
   *       * 路由BUS ID和版本号（调用set_router_server_id）
   *
   *       如果需要处理容灾也可以保存时间并忽略过长时间的不匹配路由信息
   * @return 0或错误码
   */
  virtual int pull_cache(void *priv_data);

  /**
   * @brief 启动拉取实体流程
   * @param priv_data 外部传入的私有数据
   * @note 必须要填充数据有：
   *       * 关联的对象的数据
   *       * 版本信息(如果有)
   *       * 路由BUS ID和版本号（调用set_router_server_id）
   *
   *       如果需要处理容灾也可以保存时间并忽略过长时间的不匹配路由信息
   * @return 0或错误码
   */
  virtual int pull_object(void *priv_data) = 0;

  /**
   * @brief 启动保存实体的流程(这个接口不会设置状态)
   * @param priv_data 外部传入的私有数据
   * @note
   *        * 这个接口里不能使用get_object接口，因为这回导致缓存被续期，不能让定时保存机制无限续期缓存
   *        * 这个接口成功后最好调用一次refresh_save_time，可以减少保存次数
   *        * 如果路由节点发生变化，则必须保证刷新了路由版本号（版本号+1）（调用set_router_server_id）
   *        * 注意get_router_version()的返回值可能在外部被更改，所以不能依赖它做CAS
   *        * 可以保存执行时间用以处理容灾时的过期数据（按需）
   * @return 0或错误码
   */
  virtual int save_object(void *priv_data) = 0;

  /**
   * @brief 启动保存实体的流程(这个接口会设置状态,被router_object<TObj, TChild>覆盖)
   * @param priv_data 外部传入的私有数据
   * @return 0或错误码
   */
  virtual int save(void *priv_data) = 0;

  /**
   * @brief 启动拉取缓存流程
   * @note 必须要填充数据有
   *          关联的对象的数据
   *          路由BUS ID
   * @return 0或错误码
   */
  // virtual int load(PROJECT_NAMESPACE_ID::SSMsg& msg_set) = 0;

  /**
   * @brief 缓存升级为实体
   * @return 0或错误码
   */
  virtual int upgrade();

  /**
   * @brief 为实体降级为缓存
   * @return 0或错误码
   */
  virtual int downgrade();

  /**
   * @brief 获取路由版本号
   * @return 路由版本号
   */
  inline uint64_t get_router_version() const { return router_svr_ver_; }

  inline void set_router_server_id(uint64_t r, uint64_t v) {
    router_svr_id_ = r;
    router_svr_ver_ = v;
  }

  /**
  template <typename TR, typename TV>
  inline void set_router_server_id(TR r, TV v) {
      router_svr_id_  = r;
      router_svr_ver_ = v;
      static_assert(std::is_same<TR, uint64_t>::value && std::is_same<TV, uint64_t>::value, "invalid call");
  }
  **/

  inline std::list<PROJECT_NAMESPACE_ID::SSMsg> &get_transfer_pending_list() { return transfer_pending_; }
  inline const std::list<PROJECT_NAMESPACE_ID::SSMsg> &get_transfer_pending_list() const { return transfer_pending_; }

  /**
   * @brief 根据请求包回发转发失败回包
   * @param req 请求包，在这个接口调用后req的内容将被移入到rsp包。req内容不再可用
   * @return 0或错误码
   */
  int send_transfer_msg_failed(PROJECT_NAMESPACE_ID::SSMsg &&req);

  int await_io_task();

  /**
   * @brief 设置链路跟踪信息到RPC上下文
   *
   * @param ctx RPC上下文
   * @param type_id
   * @param zone_id
   * @param object_id
   */
  static void trace_router(rpc::context &ctx, uint32_t type_id, uint32_t zone_id, uint64_t object_id);
  static inline void trace_router(rpc::context &ctx, const key_t &key) {
    trace_router(ctx, key.type_id, key.zone_id, key.object_id);
  }
  inline void trace_router(rpc::context &ctx) { trace_router(ctx, get_key()); }

 protected:
  void wakeup_io_task_awaiter();
  int await_io_task(task_manager::task_ptr_t &self_task);
  int await_io_task(task_manager::task_ptr_t &self_task, task_manager::task_ptr_t &other_task);

  // 内部接口，拉取缓存。会排队读任务
  int pull_cache_inner(void *priv_data);
  // 内部接口，拉取实体。会排队读任务
  int pull_object_inner(void *priv_data);
  // 内部接口，保存数据。会排队写任务
  int save_object_inner(void *priv_data);

 private:
  void reset_timer_ref(std::list<router_system_timer_t> *timer_list,
                       const std::list<router_system_timer_t>::iterator &it);
  void check_and_remove_timer_ref(std::list<router_system_timer_t> *timer_list,
                                  const std::list<router_system_timer_t>::iterator &it);
  void unset_timer_ref();

  int await_io_schedule_order_task(task_manager::task_ptr_t &self_task);

 private:
  key_t key_;
  time_t last_save_time_;
  time_t last_visit_time_;
  uint64_t router_svr_id_;
  uint64_t router_svr_ver_;
  uint32_t timer_sequence_;
  std::list<router_system_timer_t> *timer_list_;
  std::list<router_system_timer_t>::iterator timer_iter_;

  // 新版排队系统
  task_manager::task_ptr_t io_task_;
  uint64_t saving_sequence_;
  uint64_t saved_sequence_;
  std::set<task_manager::task_t::id_t> io_schedule_order_;
  std::list<task_manager::task_ptr_t> io_task_awaiter_;

  int32_t flags_;
  std::list<PROJECT_NAMESPACE_ID::SSMsg> transfer_pending_;

  friend class router_manager_base;
  template <typename TCache, typename TObj, typename TPrivData>
  friend class router_manager;
  friend class router_manager_set;
};

namespace std {
template <>
struct hash<router_object_base::key_t> {
  size_t operator()(const router_object_base::key_t &k) const noexcept {
    size_t first = hash<uint64_t>()((static_cast<uint64_t>(k.type_id) << 32) | k.zone_id);
    size_t second = hash<uint64_t>()(k.object_id);
    return first ^ (second << 1);
  }
};
}  // namespace std

#endif  //_ROUTER_ROUTER_OBJECT_BASE_H

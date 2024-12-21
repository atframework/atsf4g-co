// Copyright 2021 atframework
// Created by owent on 2018-05-07.
//
// 路由对象基类，用于管理路由相关的缓存和实体

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/compiler_features.h>
#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <config/server_frame_build_feature.h>
#include <dispatcher/task_type_traits.h>

#include <stdint.h>
#include <cstddef>
#include <ctime>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>

#include "rpc/rpc_common_types.h"

#include "router/router_system_defs.h"

// 路由对象基类，支持共享指针
class UTIL_SYMBOL_VISIBLE router_object_base : public std::enable_shared_from_this<router_object_base> {
  // 禁止拷贝构造和赋值操作
  UTIL_DESIGN_PATTERN_NOCOPYABLE(router_object_base)
  UTIL_DESIGN_PATTERN_NOMOVABLE(router_object_base)

 public:
  // 键的结构体，包含类型、区域和对象ID
  struct UTIL_SYMBOL_VISIBLE key_t {
    uint32_t type_id;    // 类型ID
    uint32_t zone_id;    // 区域ID
    uint64_t object_id;  // 对象ID

    // 默认构造函数
    UTIL_FORCEINLINE key_t() : type_id(0), zone_id(0), object_id(0) {}
    // 带参数的构造函数
    UTIL_FORCEINLINE key_t(uint32_t tid, uint32_t zid, uint64_t oid) : type_id(tid), zone_id(zid), object_id(oid) {}

    SERVER_FRAME_API bool operator==(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator!=(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator<(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator<=(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator>(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator>=(const key_t &r) const noexcept;
  };

  /**
   * @note 基类flag范围是0x00000001-0x00008000
   * @note 子类flag范围是0x00010000-0x40000000
   */
  struct UTIL_SYMBOL_VISIBLE flag_t {
    // 定义各种标志位类型
    enum type {
      EN_ROFT_FORCE_PULL_OBJECT = 0x0001,  // 下一次mutable_object时是否强制执行数据拉取
      EN_ROFT_IS_OBJECT = 0x0002,          // 当前对象是否是实体（可写）
      EN_ROFT_FORCE_SAVE_OBJECT = 0x0004,  // 下一次触发定时器时是否强制执行数据保存

      // 当前对象缓存是否已处于实体被移除的状态，缓存被移除意味着已经不在manager的管理中，但是可能临时存在于部分正在进行的任务里
      EN_ROFT_CACHE_REMOVED = 0x0008,
      EN_ROFT_SAVING = 0x0010,               // 是否正在保存
      EN_ROFT_TRANSFERING = 0x0020,          // 是否正在进行数据转移
      EN_ROFT_PULLING_CACHE = 0x0040,        // 是否正在拉取对象缓存
      EN_ROFT_PULLING_OBJECT = 0x0080,       // 是否正在拉取对象实体
      EN_ROFT_SCHED_REMOVE_OBJECT = 0x0100,  // 定时任务 - 实体降级计划任务是否有效
      EN_ROFT_SCHED_REMOVE_CACHE = 0x0200,   // 定时任务 - 移除缓存计划任务是否有效
      EN_ROFT_SCHED_SAVE_OBJECT = 0x0400,    // 定时任务 - 实体保存计划任务是否有效
      EN_ROFT_FORCE_REMOVE_OBJECT = 0x0800,  // 下一次触发定时器时是否强制执行实体降级
      EN_ROFT_REMOVING_CACHE = 0x1000,       // 是否正在移除对象缓存
      EN_ROFT_REMOVING_OBJECT = 0x2000,      // 是否正在移除对象实体
    };
  };

  // 标志位的保护类，自动管理标志位的设置和清除
  class UTIL_SYMBOL_VISIBLE flag_guard {
   public:
    SERVER_FRAME_API flag_guard(router_object_base &owner, int f);
    SERVER_FRAME_API ~flag_guard();

    // 转换为bool类型检测标志位是否为真
    UTIL_FORCEINLINE operator bool() { return !!f_; }

   private:
    router_object_base *owner_;
    int f_;
  };

  // IO任务的保护类，自动管理IO任务的生命周期
  class UTIL_SYMBOL_VISIBLE io_task_guard {
   public:
    SERVER_FRAME_API io_task_guard();
    SERVER_FRAME_API ~io_task_guard();

    UTIL_FORCEINLINE operator bool() { return !owner_.expired() && 0 != await_task_id_; }

    SERVER_FRAME_API rpc::result_code_type take(rpc::context &ctx, router_object_base &owner) noexcept;

   private:
    friend class router_object_base;

    std::weak_ptr<router_object_base> owner_;
    task_type_trait::id_type await_task_id_;
  };

 protected:
  // 构造函数
  SERVER_FRAME_API explicit router_object_base(const key_t &k);
  SERVER_FRAME_API explicit router_object_base(key_t &&k);
  SERVER_FRAME_API virtual ~router_object_base();

 public:
  // 刷新访问时间
  SERVER_FRAME_API void refresh_visit_time();
  // 刷新保存时间
  SERVER_FRAME_API void refresh_save_time();

  // 获取对象键
  UTIL_FORCEINLINE const key_t &get_key() const { return key_; }
  // 检查某个标志位是否被设置
  UTIL_FORCEINLINE bool check_flag(int32_t v) const { return (flags_ & v) == v; }
  // 设置某个标志位
  UTIL_FORCEINLINE void set_flag(int32_t v) { flags_ |= v; }
  // 清除某个标志位
  UTIL_FORCEINLINE void unset_flag(int32_t v) { flags_ &= ~v; }
  // 获取所有标志位
  UTIL_FORCEINLINE int32_t get_flags() const { return flags_; }

  // 分配定时器序列号
  UTIL_FORCEINLINE uint32_t alloc_timer_sequence() { return ++timer_sequence_; }
  // 检查定时器序列号是否匹配
  UTIL_FORCEINLINE bool check_timer_sequence(uint32_t seq) const { return seq == timer_sequence_; }

  // 判断对象是否可写
  UTIL_FORCEINLINE bool is_writable() const {
    return check_flag(flag_t::EN_ROFT_IS_OBJECT) && !check_flag(flag_t::EN_ROFT_FORCE_PULL_OBJECT) &&
           !check_flag(flag_t::EN_ROFT_CACHE_REMOVED);
  }

  // 判断是否有IO任务正在运行
  UTIL_FORCEINLINE bool is_io_running() const { return 0 != io_task_id_; }

  // 判断是否正在拉取缓存
  UTIL_FORCEINLINE bool is_pulling_cache() const { return check_flag(flag_t::EN_ROFT_PULLING_CACHE); }
  // 判断是否正在拉取对象
  UTIL_FORCEINLINE bool is_pulling_object() const { return check_flag(flag_t::EN_ROFT_PULLING_OBJECT); }
  // 判断是否正在进行数据转移
  UTIL_FORCEINLINE bool is_transfering() const { return check_flag(flag_t::EN_ROFT_TRANSFERING); }

  // 获取最后一次访问时间
  UTIL_FORCEINLINE time_t get_last_visit_time() const { return last_visit_time_; }
  // 获取最后一次保存时间
  UTIL_FORCEINLINE time_t get_last_save_time() const { return last_save_time_; }

  // 获取最后一次拉取缓存的任务ID
  UTIL_FORCEINLINE task_type_trait::id_type get_last_pull_cache_task_id() const noexcept {
    return io_last_pull_cache_task_id_;
  }
  // 获取最后一次拉取对象的任务ID
  UTIL_FORCEINLINE task_type_trait::id_type get_last_pull_object_task_id() const noexcept {
    return io_last_pull_object_task_id_;
  }

  /**
   * @brief 获取缓存是否有效
   * @note 如果缓存过期或正在拉取缓存，则缓存无效
   * @return 缓存是否有效
   */
  SERVER_FRAME_API bool is_cache_available() const;

  /**
   * @brief 获取实体是否有效
   * @note 如果没有实体或实体要强制拉取或正在拉取实体，则实体无效
   * @return 实体是否有效
   */
  SERVER_FRAME_API bool is_object_available() const;

  /**
   * @brief 获取路由节点ID
   * @return 路由节点ID
   */
  UTIL_FORCEINLINE uint64_t get_router_server_id() const { return router_svr_id_; }

  /**
   * @brief 获取路由节点名称（尚未接入）
   * @return 路由节点名称
   */
  UTIL_FORCEINLINE const std::string &get_router_server_name() const { return router_svr_name_; }

  /**
   * @brief 移除实体，降级为缓存
   *
   * 该函数用于从服务器中移除指定的对象，并将其降级为缓存。
   *
   * @param ctx            RPC 上下文，其中包含请求的相关信息。
   * @param priv_data      指向私有数据的指针，该数据与当前会话相关。
   * @param transfer_to_svr_id  标识需要转移到的服务器 ID。
   * @param guard          IO 任务保护器，用于确保任务执行期间的安全性。
   *
   * @return rpc::result_code_type 返回表示操作结果的代码。
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type remove_object(rpc::context &ctx, void *priv_data,
                                                                               uint64_t transfer_to_svr_id,
                                                                               io_task_guard &guard);

  /**
   * @brief 名字接口
   * @return 获取类型名称
   */
  virtual const char *name() const = 0;

  /**
   * @brief 启动拉取缓存流程
   * @param ctx RPC上下文
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
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type pull_cache(rpc::context &ctx, void *priv_data);

  /**
   * @brief 启动拉取实体流程
   * @param ctx RPC上下文
   * @param priv_data 外部传入的私有数据
   * @note 必须要填充数据有：
   *       * 关联的对象的数据
   *       * 版本信息(如果有)
   *       * 路由BUS ID和版本号（调用set_router_server_id）
   *
   *       如果需要处理容灾也可以保存时间并忽略过长时间的不匹配路由信息
   * @return 0或错误码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type pull_object(rpc::context &ctx,
                                                                                     void *priv_data) = 0;

  /**
   * @brief 启动保存实体的流程(这个接口不会设置状态)
   * @param ctx RPC上下文
   * @param priv_data 外部传入的私有数据
   * @note
   *        * 这个接口里不能使用get_object接口，因为这会导致缓存被续期，不能让定时保存机制无限续期缓存
   *        * 这个接口成功后最好调用一次refresh_save_time，可以减少保存次数
   *        * 如果路由节点发生变化，则必须保证刷新了路由版本号（版本号+1）（调用set_router_server_id）
   *        * 注意get_router_version()的返回值可能在外部被更改，所以不能依赖它做CAS
   *        * 可以保存执行时间用以处理容灾时的过期数据（按需）
   * @return 0或错误码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type save_object(rpc::context &ctx,
                                                                                     void *priv_data) = 0;

  /**
   * @brief 启动保存实体的流程(这个接口会设置状态,被router_object<TObj, TChild>覆盖)
   * @param ctx RPC上下文
   * @param priv_data 外部传入的私有数据
   * @return 0或错误码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type save(rpc::context &ctx, void *priv_data,
                                                                              io_task_guard &guard) = 0;

  /**
   * @brief 启动保存实体的流程,不继承IO task保护(这个接口会设置状态,被router_object<TObj, TChild>覆盖)
   * @param ctx RPC上下文
   * @param priv_data 外部传入的私有数据
   * @return 0或错误码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type save(rpc::context &ctx, void *priv_data);

  /**
   * @brief 启动拉取缓存流程
   * @note 必须要填充数据有
   *          关联的对象的数据
   *          路由BUS ID
   * @return 0或错误码
   */
  // SERVER_FRAME_API virtual int load(atframework::SSMsg& msg_set) = 0;

  /**
   * @brief 缓存升级为实体
   * @return 0或错误码
   */
  SERVER_FRAME_API virtual int upgrade();

  /**
   * @brief 为实体降级为缓存
   * @return 0或错误码
   */
  SERVER_FRAME_API virtual int downgrade();

  /**
   * @brief 获取路由版本号
   * @return 路由版本号
   */
  UTIL_FORCEINLINE uint64_t get_router_version() const noexcept { return router_svr_ver_; }

  /**
   * @brief 设置路由服务器ID和版本号
   * @param r 路由服务器ID
   * @param v 路由版本号
   * @param node_name 节点名称
   */
  UTIL_FORCEINLINE void set_router_server_id(uint64_t r, uint64_t v, std::string node_name = "") noexcept {
    router_svr_id_ = r;
    router_svr_ver_ = v;
    router_svr_name_ = std::move(node_name);
  }

  /**
   * @brief 获取转发待处理列表
   * @return 转发待处理消息列表
   */
  UTIL_FORCEINLINE std::list<atframework::SSMsg> &get_transfer_pending_list() noexcept { return transfer_pending_; }
  UTIL_FORCEINLINE const std::list<atframework::SSMsg> &get_transfer_pending_list() const noexcept {
    return transfer_pending_;
  }

  /**
   * @brief 根据请求包回发转发失败回包
   * @param req 请求包，在这个接口调用后req的内容将被移入到rsp包。req内容不再可用
   * @return 0或错误码
   */
  SERVER_FRAME_API int send_transfer_msg_failed(atframework::SSMsg &&req);

  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type await_io_task(rpc::context &ctx);

  /**
   * @brief 设置链路跟踪信息到RPC上下文
   *
   * @param ctx RPC上下文
   * @param type_id 类型ID
   * @param zone_id 区域ID
   * @param object_id 对象ID
   */
  SERVER_FRAME_API static void trace_router(rpc::context &ctx, uint32_t type_id, uint32_t zone_id, uint64_t object_id);
  UTIL_FORCEINLINE static void trace_router(rpc::context &ctx, const key_t &key) {
    trace_router(ctx, key.type_id, key.zone_id, key.object_id);
  }
  UTIL_FORCEINLINE void trace_router(rpc::context &ctx) { trace_router(ctx, get_key()); }

 protected:
  /**
   * @brief 唤醒IO任务等待者
   *
   * 该函数用于唤醒当前阻塞等待IO任务完成的任务。
   */
  SERVER_FRAME_API void wakeup_io_task_awaiter();

  /**
   * @brief 等待IO任务，排队执行其他任务
   *
   * 该函数在等待IO任务完成的同时，将其他任务排队以便顺序执行。
   *
   * @param ctx        RPC 上下文，其中包含请求的相关信息。
   * @param other_task 引用类型的任务变量，用于指定需要排队的其他任务。
   *
   * @return rpc::result_code_type 返回表示操作结果的代码。
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type await_io_task(rpc::context &ctx,
                                                                               task_type_trait::task_type &other_task);

  /**
   * @brief 内部接口，拉取缓存，会排队读任务
   *
   * 该函数是一个内部接口，用于从缓存中拉取数据，并将其读任务排队。
   *
   * @param ctx       RPC 上下文，其中包含请求的相关信息。
   * @param priv_data 指向私有数据的指针，该数据与当前会话相关。
   * @param guard     IO 任务保护器，用于确保任务执行期间的安全性。
   *
   * @return rpc::result_code_type 返回表示操作结果的代码。
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type internal_pull_cache(rpc::context &ctx, void *priv_data,
                                                                                     io_task_guard &guard);

  /**
   * @brief 内部接口，拉取实体，会排队读任务
   *
   * 该函数是一个内部接口，用于从数据库或其他存储介质中拉取实体数据，并将其读任务排队。
   *
   * @param ctx       RPC 上下文，其中包含请求的相关信息。
   * @param priv_data 指向私有数据的指针，该数据与当前会话相关。
   * @param guard     IO 任务保护器，用于确保任务执行期间的安全性。
   *
   * @return rpc::result_code_type 返回表示操作结果的代码。
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type internal_pull_object(rpc::context &ctx,
                                                                                      void *priv_data,
                                                                                      io_task_guard &guard);

  /**
   * @brief 内部接口，保存数据，会排队写任务
   *
   * 该函数是一个内部接口，用于将数据保存到数据库或其他存储介质中，并将写任务排队。
   *
   * @param ctx       RPC 上下文，其中包含请求的相关信息。
   * @param priv_data 指向私有数据的指针，该数据与当前会话相关。
   * @param guard     IO 任务保护器，用于确保任务执行期间的安全性。
   *
   * @return rpc::result_code_type 返回表示操作结果的代码。
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type internal_save_object(rpc::context &ctx,
                                                                                      void *priv_data,
                                                                                      io_task_guard &guard);

 private:
  // 重置定时器引用
  void reset_timer_ref(std::list<router_system_timer_t> *timer_list,
                       const std::list<router_system_timer_t>::iterator &it);
  // 检查并移除定时器引用
  void check_and_remove_timer_ref(std::list<router_system_timer_t> *timer_list,
                                  const std::list<router_system_timer_t>::iterator &it);
  // 取消定时器引用
  void unset_timer_ref();

  // 等待IO调度订单任务
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_io_schedule_order_task(rpc::context &ctx);

 private:
  key_t key_;                                              // 对象的键
  time_t last_save_time_;                                  // 最后一次保存时间
  time_t last_visit_time_;                                 // 最后一次访问时间
  uint64_t router_svr_id_;                                 // 路由服务器ID
  std::string router_svr_name_;                            // 路由服务器名称
  uint64_t router_svr_ver_;                                // 路由服务器版本
  uint32_t timer_sequence_;                                // 定时器序列
  std::list<router_system_timer_t> *timer_list_;           // 定时器列表
  std::list<router_system_timer_t>::iterator timer_iter_;  // 定时器迭代器

  // 新版排队系统
  task_type_trait::id_type io_task_id_;  // IO任务ID

  uint64_t saving_sequence_;                               // 保存序列
  uint64_t saved_sequence_;                                // 已保存序列
  std::set<task_type_trait::id_type> io_schedule_order_;   // IO调度订单
  std::list<task_type_trait::task_type> io_task_awaiter_;  // IO任务等待者
  task_type_trait::id_type io_last_pull_cache_task_id_;    // 最后一次拉取缓存的任务ID
  task_type_trait::id_type io_last_pull_object_task_id_;   // 最后一次拉取对象的任务ID

  int32_t flags_;                                   // 标志位
  std::list<atframework::SSMsg> transfer_pending_;  // 待处理的转发消息

  friend class io_task_guard;
  friend class router_manager_base;
  template <typename TCache, typename TObj, typename TPrivData>
  friend class router_manager;
  friend class router_manager_set;
};

// 哈希函数的模板特化，用于计算router_object_base::key_t的哈希值
namespace std {
template <>
struct UTIL_SYMBOL_VISIBLE hash<router_object_base::key_t> {
  UTIL_FORCEINLINE size_t operator()(const router_object_base::key_t &k) const noexcept {
    size_t first = hash<uint64_t>()((static_cast<uint64_t>(k.type_id) << 32) | k.zone_id);
    size_t second = hash<uint64_t>()(k.object_id);
    return first ^ (second << 1);
  }
};
}  // namespace std

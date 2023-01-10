// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <design_pattern/singleton.h>

#include <libcotask/task_manager.h>

#include <config/server_frame_build_feature.h>
#include <utility/environment_helper.h>

#include <memory>
#include <unordered_map>
#include <utility>

#include "dispatcher/dispatcher_type_defines.h"
#include "dispatcher/task_type_traits.h"

/**
 * @brief 协程任务和简单actor的管理创建manager类
 * @note 涉及异步处理的任务全部走协程任务，不涉及异步调用的模块可以直接使用actor。
 *       actor会比task少一次栈初始化开销（大约8us的CPU+栈所占用的内存）,在量大但是无异步调用的模块（比如地图同步行为）可以节省CPU和内存
 */
class task_manager : public ::util::design_pattern::singleton<task_manager> {
 public:
  using task_t = task_type_trait::internal_task_type;
  using id_t = task_type_trait::id_type;

  using actor_action_ptr_t = std::shared_ptr<actor_action_base>;

  struct task_action_maker_base_t {
    atframework::DispatcherOptions options;
    explicit task_action_maker_base_t(const atframework::DispatcherOptions *opt);
    virtual ~task_action_maker_base_t();
    virtual int operator()(task_manager::id_t &task_id, dispatcher_start_data_t ctor_param) = 0;
  };

  struct actor_action_maker_base_t {
    atframework::DispatcherOptions options;
    explicit actor_action_maker_base_t(const atframework::DispatcherOptions *opt);
    virtual ~actor_action_maker_base_t();
    virtual actor_action_ptr_t operator()(dispatcher_start_data_t ctor_param) = 0;
  };

  /// 协程任务创建器
  using task_action_creator_t = std::shared_ptr<task_action_maker_base_t>;
  using actor_action_creator_t = std::shared_ptr<actor_action_maker_base_t>;

  template <typename TAction>
  struct task_action_maker_t : public task_action_maker_base_t {
    explicit task_action_maker_t(const atframework::DispatcherOptions *opt) : task_action_maker_base_t(opt) {}
    int operator()(task_manager::id_t &task_id, dispatcher_start_data_t ctor_param) override {
      if (options.has_timeout() && (options.timeout().seconds() > 0 || options.timeout().nanos() > 0)) {
        return task_manager::me()->create_task_with_timeout<TAction>(
            task_id, options.timeout().seconds(), options.timeout().nanos(), COPP_MACRO_STD_MOVE(ctor_param));
      } else {
        return task_manager::me()->create_task<TAction>(task_id, COPP_MACRO_STD_MOVE(ctor_param));
      }
    };
  };

  template <typename TAction>
  struct actor_action_maker_t : public actor_action_maker_base_t {
    explicit actor_action_maker_t(const atframework::DispatcherOptions *opt) : actor_action_maker_base_t(opt) {}
    actor_action_ptr_t operator()(dispatcher_start_data_t ctor_param) override {
      return task_manager::me()->create_actor<TAction>(COPP_MACRO_STD_MOVE(ctor_param));
    };
  };

 private:
#if (LIBCOPP_VERSION_MAJOR * 1000000 + LIBCOPP_VERSION_MINOR * 1000 + LIBCOPP_VERSION_PATCH) >= 2000001
  using native_task_manager_type = cotask::task_manager<task_type_trait::internal_task_type>;
  using native_task_manager_ptr_type = typename native_task_manager_type::ptr_type;
#else
  using native_task_container_t =
      std::unordered_map<task_type_trait::id_type,
                         cotask::detail::task_manager_node<task_type_trait::internal_task_type> >;
  using native_task_manager_type = cotask::task_manager<task_type_trait::internal_task_type, native_task_container_t>;
  using native_task_manager_ptr_type = typename native_task_manager_type::ptr_t;
#endif

 public:
  using task_ptr_t = task_type_trait::task_type;

 protected:
  task_manager();
  ~task_manager();

 public:
  int init();

  int reload();

  /**
   * 获取栈大小
   */
  size_t get_stack_size() const;

  /**
   * @brief 创建任务并指定超时时间
   * @param task_instance 协程任务
   * @param timeout_sec 超时时间(秒)
   * @param timeout_nsec 超时时间(纳秒), 0-999999999
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  int create_task_with_timeout(task_t::ptr_t &task_instance, time_t timeout_sec, time_t timeout_nsec, TParams &&args) {
    if (!stack_pool_ || !native_mgr_) {
      task_instance = nullptr;
      return PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    }

    task_type_trait::task_macro_coroutine::stack_allocator_type alloc(stack_pool_);

    task_instance = task_t::create_with_delegate<TAction>(COPP_MACRO_STD_FORWARD(TParams, args), alloc,
                                                          get_stack_size(), sizeof(task_private_data_type));
    if (!task_instance) {
      return report_create_error(__FUNCTION__);
    }

    task_private_data_type *task_priv_data = get_private_data(*task_instance);
    if (nullptr != task_priv_data) {
      // initialize private data
      reset_private_data(*task_priv_data);
    }

    return add_task(task_instance, timeout_sec, timeout_nsec);
  }

  /**
   * @brief 创建任务并指定超时时间
   * @param task_id 协程任务的ID
   * @param timeout_sec 超时时间(秒)
   * @param timeout_nsec 超时时间(纳秒), 0-999999999
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  int create_task_with_timeout(id_t &task_id, time_t timeout_sec, time_t timeout_nsec, TParams &&args) {
    task_t::ptr_t task_instance;
    int ret = create_task_with_timeout<TAction>(task_instance, timeout_sec, timeout_nsec,
                                                COPP_MACRO_STD_FORWARD(TParams, args));
    if (task_instance) {
      task_id = task_instance->get_id();
    } else {
      task_id = 0;
    }
    return ret;
  }

  /**
   * @brief 创建任务
   * @param task_instance 协程任务
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  int create_task(task_t::ptr_t &task_instance, TParams &&args) {
    return create_task_with_timeout<TAction>(task_instance, 0, 0, COPP_MACRO_STD_FORWARD(TParams, args));
  }

  /**
   * @brief 创建任务
   * @param task_id 协程任务的ID
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  int create_task(id_t &task_id, TParams &&args) {
    return create_task_with_timeout<TAction>(task_id, 0, 0, COPP_MACRO_STD_FORWARD(TParams, args));
  }

  /**
   * @brief 创建任务并指定超时时间
   * @param task_id 协程任务的ID
   * @param timeout_sec 超时时间(秒)
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  inline int create_task_with_timeout(id_t &task_id, time_t timeout_sec, TParams &&args) {
    return create_task_with_timeout<TAction>(task_id, timeout_sec, 0, COPP_MACRO_STD_FORWARD(TParams, args));
  }

  /**
   * @brief 创建协程任务构造器
   * @return 任务构造器
   */
  template <typename TAction>
  inline task_action_creator_t make_task_creator(const atframework::DispatcherOptions *opt) {
    return std::make_shared<task_action_maker_t<TAction> >(opt);
  }

  /**
   * @brief 开始任务
   * @param task_id 协程任务的ID
   * @param data 启动数据，operator()(void* priv_data)的priv_data指向这个对象的地址
   * @return 0或错误码
   */
  int start_task(id_t task_id, dispatcher_start_data_t &data);

  /**
   * @brief 恢复任务
   * @param task_id 协程任务的ID
   * @param data 恢复时透传的数据，yield返回的指针指向这个对象的地址
   * @return 0或错误码
   */
  int resume_task(id_t task_id, dispatcher_resume_data_t &data);

  /**
   * @brief 创建Actor
   * @note 所有的actor必须使用组合的方式执行，不允许使用协程RPC操作
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TActor, typename... TParams>
  std::shared_ptr<TActor> create_actor(TParams &&...args) {
    return std::make_shared<TActor>(COPP_MACRO_STD_FORWARD(TParams, args)...);
  }

  /**
   * @brief 创建Actor构造器
   * @return Actor构造器
   */
  template <typename TAction>
  inline actor_action_creator_t make_actor_creator(const atframework::DispatcherOptions *opt) {
    return std::make_shared<actor_action_maker_t<TAction> >(opt);
  }

  /**
   * @brief tick，可能会触发任务过期
   */
  int tick(time_t sec, int nsec);

  /**
   * @brief tick，可能会触发任务过期
   * @param task_id 任务id
   * @return 如果存在，返回协程任务的智能指针
   */
  task_ptr_t get_task(id_t task_id);

  inline const task_type_trait::stack_pool_type::ptr_t &get_stack_pool() const { return stack_pool_; }
  inline const native_task_manager_ptr_type &get_native_manager() const { return native_mgr_; }

  bool is_busy() const;

  static void reset_private_data(task_private_data_type &priv_data);
  static task_private_data_type *get_private_data(task_t &task);
  static rpc::context *get_shared_context(task_t &task);

  static int32_t convert_task_status_to_error_code(task_t &task) noexcept;

 private:
  bool check_sys_config() const;

  void setup_metrics();

  /**
   * @brief 创建任务
   * @param task 协程任务
   * @param timeout 超时时间
   * @return 0或错误码
   */
  int add_task(const task_t::ptr_t &task, time_t timeout_sec = 0, time_t timeout_nsec = 0);

  int report_create_error(const char *fn_name);

 private:
  time_t stat_interval_;
  time_t stat_last_checkpoint_;
  size_t conf_busy_count_;
  size_t conf_busy_warn_count_;
  native_task_manager_ptr_type native_mgr_;
  task_type_trait::stack_pool_type::ptr_t stack_pool_;
};

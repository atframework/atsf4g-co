// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>
#include <utility/environment_helper.h>

#include <libcotask/task_manager.h>
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  include <libcopp/coroutine/generator_promise.h>
#endif

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
  using actor_action_ptr_t = std::shared_ptr<actor_action_base>;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  struct start_error_transform {
    std::pair<int32_t, dispatcher_start_data_type *> operator()(copp::promise_status in) const noexcept;
  };

  struct resume_error_transform {
    std::pair<int32_t, dispatcher_resume_data_type *> operator()(copp::promise_status in) const noexcept;
  };

  using generic_start_generator =
      copp::generator_future<std::pair<int32_t, dispatcher_start_data_type *>, start_error_transform>;
  using generic_resume_generator =
      copp::generator_future<std::pair<int32_t, dispatcher_resume_data_type *>, resume_error_transform>;
  struct generic_resume_key {
    std::chrono::system_clock::time_point timeout;
    uintptr_t message_type;
    uint64_t sequence;

    inline explicit generic_resume_key(std::chrono::system_clock::time_point t, uintptr_t m, uint64_t s) noexcept
        : timeout(t), message_type(m), sequence(s) {}

    inline friend bool operator==(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      return self.timeout == other.timeout && self.message_type == other.message_type &&
             self.sequence == other.sequence;
    }

#  ifdef __cpp_impl_three_way_comparison
    inline friend std::strong_ordering operator<=>(const generic_resume_key &self,
                                                   const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout <=> other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type <=> other.message_type;
      }

      return self.sequence <=> other.sequence;
    }
#  else

    inline friend bool operator!=(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      return self.timeout != other.timeout || self.message_type != other.message_type ||
             self.sequence != other.sequence;
    }

    inline friend bool operator<(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout < other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type < other.message_type;
      }

      return self.sequence < other.sequence;
    }

    inline friend bool operator<=(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout <= other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type <= other.message_type;
      }

      return self.sequence <= other.sequence;
    }

    inline friend bool operator>(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout > other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type > other.message_type;
      }

      return self.sequence > other.sequence;
    }

    inline friend bool operator>=(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout >= other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type >= other.message_type;
      }

      return self.sequence >= other.sequence;
    }
#  endif
  };

  struct generic_resume_index {
    uintptr_t message_type;
    uint64_t sequence;

    inline explicit generic_resume_index(uintptr_t m, uint64_t s) noexcept : message_type(m), sequence(s) {}
    inline explicit generic_resume_index(const generic_resume_key &key) noexcept
        : message_type(key.message_type), sequence(key.sequence) {}

    inline friend bool operator==(const generic_resume_index &self, const generic_resume_index &other) noexcept {
      return self.message_type == other.message_type && self.sequence == other.sequence;
    }

#  ifdef __cpp_impl_three_way_comparison
    inline friend std::strong_ordering operator<=>(const generic_resume_index &self,
                                                   const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type <=> other.message_type;
      }

      return self.sequence <=> other.sequence;
    }
#  else

    inline friend bool operator!=(const generic_resume_index &self, const generic_resume_index &other) noexcept {
      return self.message_type != other.message_type || self.sequence != other.sequence;
    }

    inline friend bool operator<(const generic_resume_index &self, const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type < other.message_type;
      }

      return self.sequence < other.sequence;
    }

    inline friend bool operator<=(const generic_resume_index &self, const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type <= other.message_type;
      }

      return self.sequence <= other.sequence;
    }

    inline friend bool operator>(const generic_resume_index &self, const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type > other.message_type;
      }

      return self.sequence > other.sequence;
    }

    inline friend bool operator>=(const generic_resume_index &self, const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type >= other.message_type;
      }

      return self.sequence >= other.sequence;
    }
#  endif
  };

  struct generic_resume_hash {
    template <typename T>
    inline static void _hash_combine(size_t &seed, const T &val) noexcept {
      seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    template <typename... Types>
    inline static size_t hash_combine(const Types &...args) noexcept {
      size_t seed = 0;
      (_hash_combine(seed, args), ...);  // create hash value with seed over all args
      return seed;
    }

    std::size_t operator()(const generic_resume_index &index) const noexcept {
      return hash_combine(index.message_type, index.sequence);
    }

    std::size_t operator()(const generic_resume_key &key) const noexcept {
      return hash_combine(key.timeout.time_since_epoch().count(), key.message_type, key.sequence);
    }
  };

  template <class TAction, class... TParams>
  static typename task_type_trait::task_type internal_create_and_setup_task(TParams &&...args) {
    using internal_task_type = typename task_type_trait::internal_task_type;
    TAction action{std::forward<TParams>(args)...};

    typename task_action_base::task_meta_data_type action_meta;
    // Split the assignment to member and getting the return value of co_yield for GCC BUG
    // @see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108620
    auto current_task_id = co_yield internal_task_type::yield_task_id();
    auto private_data = co_yield internal_task_type::yield_private_data();
    action_meta.task_id = current_task_id;
    action_meta.private_data = private_data;
    if (nullptr != action_meta.private_data) {
      reset_private_data(*action_meta.private_data);
    }

    std::pair<int32_t, dispatcher_start_data_type *> wait_start{PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, nullptr};
    {
      generic_start_generator start_generator = make_start_generator(action_meta.task_id);
      wait_start = co_await start_generator;
    }

    if (wait_start.first < 0 || nullptr == wait_start.second) {
      co_return wait_start.first;
    }

    int32_t result = co_await action(std::move(action_meta), *wait_start.second);
    co_return result;
  }
#endif

  struct task_action_maker_base_t {
    atframework::DispatcherOptions options;
    explicit task_action_maker_base_t(const atframework::DispatcherOptions *opt);
    virtual ~task_action_maker_base_t();
    virtual int operator()(task_type_trait::id_type &task_id, dispatcher_start_data_type ctor_param) = 0;
  };

  struct actor_action_maker_base_t {
    atframework::DispatcherOptions options;
    explicit actor_action_maker_base_t(const atframework::DispatcherOptions *opt);
    virtual ~actor_action_maker_base_t();
    virtual actor_action_ptr_t operator()(dispatcher_start_data_type ctor_param) = 0;
  };

  /// 协程任务创建器
  using task_action_creator_t = std::shared_ptr<task_action_maker_base_t>;
  using actor_action_creator_t = std::shared_ptr<actor_action_maker_base_t>;

  template <typename TAction>
  struct task_action_maker_t : public task_action_maker_base_t {
    explicit task_action_maker_t(const atframework::DispatcherOptions *opt) : task_action_maker_base_t(opt) {}
    int operator()(task_type_trait::id_type &task_id, dispatcher_start_data_type ctor_param) override {
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
    actor_action_ptr_t operator()(dispatcher_start_data_type ctor_param) override {
      return task_manager::me()->create_actor<TAction>(COPP_MACRO_STD_MOVE(ctor_param));
    };
  };

 private:
  using native_task_manager_type = cotask::task_manager<task_type_trait::internal_task_type>;
  using native_task_manager_ptr_type = typename native_task_manager_type::ptr_type;

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
  int create_task_with_timeout(task_type_trait::task_type &task_instance, time_t timeout_sec, time_t timeout_nsec,
                               TParams &&args) {
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    if (!native_mgr_) {
      task_instance.reset();
      return PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    }

    task_instance = internal_create_and_setup_task<TAction>(std::forward<TParams>(args));
    if (!task_instance.get_context()) {
      return report_create_error(__FUNCTION__);
    }
#else
    if (!stack_pool_ || !native_mgr_) {
      task_instance = nullptr;
      return PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    }

    task_type_trait::task_macro_coroutine::stack_allocator_type alloc(stack_pool_);

    task_instance = task_type_trait::internal_task_type::create_with_delegate<TAction>(
        std::forward<TParams>(args), alloc, get_stack_size(), sizeof(task_private_data_type));
    if (task_type_trait::empty(task_instance)) {
      return report_create_error(__FUNCTION__);
    }

    task_private_data_type *task_priv_data = task_type_trait::get_private_data(*task_instance);
    if (nullptr != task_priv_data) {
      // initialize private data
      reset_private_data(*task_priv_data);
    }
#endif

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
  int create_task_with_timeout(task_type_trait::id_type &task_id, time_t timeout_sec, time_t timeout_nsec,
                               TParams &&args) {
    task_type_trait::task_type task_instance;
    int ret = create_task_with_timeout<TAction>(task_instance, timeout_sec, timeout_nsec, std::forward<TParams>(args));
    if (!task_type_trait::empty(task_instance)) {
      task_id = task_type_trait::get_task_id(task_instance);
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
  int create_task(task_type_trait::task_type &task_instance, TParams &&args) {
    return create_task_with_timeout<TAction>(task_instance, 0, 0, std::forward<TParams>(args));
  }

  /**
   * @brief 创建任务
   * @param task_id 协程任务的ID
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  int create_task(task_type_trait::id_type &task_id, TParams &&args) {
    return create_task_with_timeout<TAction>(task_id, 0, 0, std::forward<TParams>(args));
  }

  /**
   * @brief 创建任务并指定超时时间
   * @param task_id 协程任务的ID
   * @param timeout_sec 超时时间(秒)
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  inline int create_task_with_timeout(task_type_trait::id_type &task_id, time_t timeout_sec, TParams &&args) {
    return create_task_with_timeout<TAction>(task_id, timeout_sec, 0, std::forward<TParams>(args));
  }

  /**
   * @brief 创建协程任务构造器
   * @return 任务构造器
   */
  template <typename TAction>
  inline task_action_creator_t make_task_creator(const atframework::DispatcherOptions *opt) {
    return std::make_shared<task_action_maker_t<TAction>>(opt);
  }

  /**
   * @brief 开始任务
   * @param task_id 协程任务的ID
   * @param data 启动数据，operator()(void* priv_data)的priv_data指向这个对象的地址
   * @return 0或错误码
   */
  int start_task(task_type_trait::id_type task_id, dispatcher_start_data_type &data);

  /**
   * @brief 恢复任务
   * @param task_id 协程任务的ID
   * @param data 恢复时透传的数据，yield返回的指针指向这个对象的地址
   * @return 0或错误码
   */
  int resume_task(task_type_trait::id_type task_id, dispatcher_resume_data_type &data);

  /**
   * @brief 创建Actor
   * @note 所有的actor必须使用组合的方式执行，不允许使用协程RPC操作
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TActor, typename... TParams>
  std::shared_ptr<TActor> create_actor(TParams &&...args) {
    return std::make_shared<TActor>(std::forward<TParams>(args)...);
  }

  /**
   * @brief 创建Actor构造器
   * @return Actor构造器
   */
  template <typename TAction>
  inline actor_action_creator_t make_actor_creator(const atframework::DispatcherOptions *opt) {
    return std::make_shared<actor_action_maker_t<TAction>>(opt);
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
  task_type_trait::task_type get_task(task_type_trait::id_type task_id);

#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
  inline const task_type_trait::stack_pool_type::ptr_t &get_stack_pool() const { return stack_pool_; }
#endif
  inline const native_task_manager_ptr_type &get_native_manager() const { return native_mgr_; }

  bool is_busy() const;

  static void reset_private_data(task_private_data_type &priv_data);
  static rpc::context *get_shared_context(task_type_trait::task_type &task);

  static int32_t convert_task_status_to_error_code(task_type_trait::task_status task_status) noexcept;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  static generic_start_generator make_start_generator(task_type_trait::id_type task_id);
  static std::pair<generic_resume_key, generic_resume_generator> make_resume_generator(
      uintptr_t message_type, const dispatcher_await_options &await_options);
#endif

 private:
  bool check_sys_config() const;

  void setup_metrics();

  /**
   * @brief 创建任务
   * @param task 协程任务
   * @param timeout 超时时间
   * @return 0或错误码
   */
  int add_task(const task_type_trait::task_type &task, time_t timeout_sec = 0, time_t timeout_nsec = 0);

  int report_create_error(const char *fn_name);

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  void internal_insert_start_generator(task_type_trait::id_type task_id,
                                       generic_start_generator::context_pointer_type &&);
  void internal_remove_start_generator(task_type_trait::id_type task_id, const generic_start_generator::context_type &);
  void internal_insert_resume_generator(const generic_resume_key &key,
                                        generic_resume_generator::context_pointer_type &&);
  void internal_remove_resume_generator(const generic_resume_key &key, const generic_resume_generator::context_type &);
#endif

 private:
  time_t stat_interval_;
  time_t stat_last_checkpoint_;
  size_t conf_busy_count_;
  size_t conf_busy_warn_count_;
  native_task_manager_ptr_type native_mgr_;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  std::unordered_map<task_type_trait::id_type, generic_start_generator::context_pointer_type> waiting_start_;
  std::multimap<generic_resume_key, generic_resume_generator::context_pointer_type> waiting_resume_timer_;
  std::unordered_map<generic_resume_index, generic_resume_key, generic_resume_hash> waiting_resume_index_;
#else
  task_type_trait::stack_pool_type::ptr_t stack_pool_;
#endif
};

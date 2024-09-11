// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

#if GOOGLE_PROTOBUF_VERSION >= 4022000
#  include <absl/log/internal/log_sink_set.h>
#endif

#include <config/compiler/protobuf_suffix.h>

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <libcotask/task_manager.h>
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  include <libcopp/coroutine/generator_promise.h>
#endif

#include <memory/object_allocator.h>

#include <memory>
#include <unordered_map>
#include <utility>
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  include <map>
#endif

#include "dispatcher/dispatcher_type_defines.h"
#include "dispatcher/task_type_traits.h"
#include "utility/protobuf_mini_dumper.h"

/**
 * @brief 协程任务和简单actor的管理创建manager类
 * @note 涉及异步处理的任务全部走协程任务，不涉及异步调用的模块可以直接使用actor。
 *       actor会比task少一次栈初始化开销（大约8us的CPU+栈所占用的内存）,在量大但是无异步调用的模块（比如地图同步行为）可以节省CPU和内存
 */
class task_manager {
 public:
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  struct start_error_transform {
    SERVER_FRAME_API start_error_transform();
    SERVER_FRAME_API ~start_error_transform();

    SERVER_FRAME_API start_error_transform(const start_error_transform &);
    SERVER_FRAME_API start_error_transform(start_error_transform &&);
    SERVER_FRAME_API start_error_transform &operator=(const start_error_transform &);
    SERVER_FRAME_API start_error_transform &operator=(start_error_transform &&);

    SERVER_FRAME_API std::pair<int32_t, dispatcher_start_data_type *> operator()(
        copp::promise_status in) const noexcept;
  };

  struct resume_error_transform {
    SERVER_FRAME_API resume_error_transform();
    SERVER_FRAME_API ~resume_error_transform();

    SERVER_FRAME_API resume_error_transform(const resume_error_transform &);
    SERVER_FRAME_API resume_error_transform(resume_error_transform &&);
    SERVER_FRAME_API resume_error_transform &operator=(const resume_error_transform &);
    SERVER_FRAME_API resume_error_transform &operator=(resume_error_transform &&);

    SERVER_FRAME_API std::pair<int32_t, dispatcher_resume_data_type *> operator()(
        copp::promise_status in) const noexcept;
  };

  using generic_start_generator =
      copp::generator_future<std::pair<int32_t, dispatcher_start_data_type *>, start_error_transform>;
  using generic_resume_generator =
      copp::generator_future<std::pair<int32_t, dispatcher_resume_data_type *>, resume_error_transform>;

  struct generic_resume_key {
    std::chrono::system_clock::time_point timeout;
    uintptr_t message_type;
    uint64_t sequence;

    SERVER_FRAME_API generic_resume_key();
    SERVER_FRAME_API ~generic_resume_key();

    SERVER_FRAME_API generic_resume_key(const generic_resume_key &);
    SERVER_FRAME_API generic_resume_key(generic_resume_key &&);
    SERVER_FRAME_API generic_resume_key &operator=(const generic_resume_key &);
    SERVER_FRAME_API generic_resume_key &operator=(generic_resume_key &&);

    UTIL_FORCEINLINE explicit generic_resume_key(std::chrono::system_clock::time_point t, uintptr_t m,
                                                 uint64_t s) noexcept
        : timeout(t), message_type(m), sequence(s) {}

    UTIL_FORCEINLINE friend bool operator==(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      return self.timeout == other.timeout && self.message_type == other.message_type &&
             self.sequence == other.sequence;
    }

#  ifdef __cpp_impl_three_way_comparison
    UTIL_FORCEINLINE friend std::strong_ordering operator<=>(const generic_resume_key &self,
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

    UTIL_FORCEINLINE friend bool operator!=(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      return self.timeout != other.timeout || self.message_type != other.message_type ||
             self.sequence != other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator<(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout < other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type < other.message_type;
      }

      return self.sequence < other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator<=(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout <= other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type <= other.message_type;
      }

      return self.sequence <= other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator>(const generic_resume_key &self, const generic_resume_key &other) noexcept {
      if (self.timeout != other.timeout) {
        return self.timeout > other.timeout;
      }

      if (self.message_type != other.message_type) {
        return self.message_type > other.message_type;
      }

      return self.sequence > other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator>=(const generic_resume_key &self, const generic_resume_key &other) noexcept {
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

    SERVER_FRAME_API generic_resume_index();
    SERVER_FRAME_API ~generic_resume_index();

    SERVER_FRAME_API generic_resume_index(const generic_resume_index &);
    SERVER_FRAME_API generic_resume_index(generic_resume_index &&);
    SERVER_FRAME_API generic_resume_index &operator=(const generic_resume_index &);
    SERVER_FRAME_API generic_resume_index &operator=(generic_resume_index &&);

    UTIL_FORCEINLINE explicit generic_resume_index(uintptr_t m, uint64_t s) noexcept : message_type(m), sequence(s) {}
    UTIL_FORCEINLINE explicit generic_resume_index(const generic_resume_key &key) noexcept
        : message_type(key.message_type), sequence(key.sequence) {}

    UTIL_FORCEINLINE friend bool operator==(const generic_resume_index &self,
                                            const generic_resume_index &other) noexcept {
      return self.message_type == other.message_type && self.sequence == other.sequence;
    }

#  ifdef __cpp_impl_three_way_comparison
    UTIL_FORCEINLINE friend std::strong_ordering operator<=>(const generic_resume_index &self,
                                                             const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type <=> other.message_type;
      }

      return self.sequence <=> other.sequence;
    }
#  else

    UTIL_FORCEINLINE friend bool operator!=(const generic_resume_index &self,
                                            const generic_resume_index &other) noexcept {
      return self.message_type != other.message_type || self.sequence != other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator<(const generic_resume_index &self,
                                           const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type < other.message_type;
      }

      return self.sequence < other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator<=(const generic_resume_index &self,
                                            const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type <= other.message_type;
      }

      return self.sequence <= other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator>(const generic_resume_index &self,
                                           const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type > other.message_type;
      }

      return self.sequence > other.sequence;
    }

    UTIL_FORCEINLINE friend bool operator>=(const generic_resume_index &self,
                                            const generic_resume_index &other) noexcept {
      if (self.message_type != other.message_type) {
        return self.message_type >= other.message_type;
      }

      return self.sequence >= other.sequence;
    }
#  endif
  };

  struct UTIL_SYMBOL_VISIBLE generic_resume_hash {
    SERVER_FRAME_API generic_resume_hash();
    SERVER_FRAME_API ~generic_resume_hash();

    SERVER_FRAME_API generic_resume_hash(const generic_resume_hash &);
    SERVER_FRAME_API generic_resume_hash(generic_resume_hash &&);
    SERVER_FRAME_API generic_resume_hash &operator=(const generic_resume_hash &);
    SERVER_FRAME_API generic_resume_hash &operator=(generic_resume_hash &&);

    template <typename T>
    UTIL_FORCEINLINE static void _hash_combine(size_t &seed, const T &val) noexcept {
      seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    template <typename... Types>
    UTIL_FORCEINLINE static size_t hash_combine(const Types &...args) noexcept {
      size_t seed = 0;
      (_hash_combine(seed, args), ...);  // create hash value with seed over all args
      return seed;
    }

    UTIL_FORCEINLINE std::size_t operator()(const generic_resume_index &index) const noexcept {
      return hash_combine(index.message_type, index.sequence);
    }

    UTIL_FORCEINLINE std::size_t operator()(const generic_resume_key &key) const noexcept {
      return hash_combine(key.timeout.time_since_epoch().count(), key.message_type, key.sequence);
    }
  };

  template <class TAction, class... TParams>
  UTIL_SYMBOL_VISIBLE static typename task_type_trait::task_type internal_create_and_setup_task(TParams &&...args) {
    using internal_task_type = typename task_type_trait::internal_task_type;

    // Should not be exiting, task will start immediately after created.
    auto current_status = co_yield internal_task_type::yield_status();
    if (task_type_trait::is_exiting(current_status)) {
      co_return static_cast<int32_t>(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }
    TAction action{std::forward<TParams>(args)...};

    task_action_meta_data_type action_meta;
    // Split the assignment to member and getting the return value of co_yield for GCC BUG
    // @see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108620
    auto current_task_id = co_yield internal_task_type::yield_task_id();
    auto private_data = co_yield internal_task_type::yield_private_data();
    action_meta.task_id = current_task_id;
    action_meta.private_data = private_data;
    if (nullptr != action_meta.private_data) {
      reset_private_data(*action_meta.private_data);
    }

    dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
    std::pair<int32_t, dispatcher_start_data_type *> wait_start{PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM,
                                                                &start_data};
    {
      generic_start_generator start_generator = make_start_generator(
          action_meta.task_id,
          [](const dispatcher_start_data_type *input_start_data, void *stack_data) {
            if (nullptr != stack_data && nullptr != input_start_data) {
              (*reinterpret_cast<dispatcher_start_data_type *>(stack_data)) = *input_start_data;
            }
          },
          reinterpret_cast<void *>(&start_data));
      wait_start = co_await start_generator;
    }

    if (wait_start.first < 0) {
      co_return wait_start.first;
    }

    int32_t result = co_await action(std::move(action_meta), std::move(start_data));
    co_return result;
  }
#endif

  struct UTIL_SYMBOL_VISIBLE task_action_maker_base_t {
    atframework::DispatcherOptions options;
    SERVER_FRAME_API explicit task_action_maker_base_t(const atframework::DispatcherOptions *opt);
    SERVER_FRAME_API virtual ~task_action_maker_base_t();
    virtual int operator()(task_type_trait::task_type &task_inst, dispatcher_start_data_type ctor_param) = 0;
  };

  /// 协程任务创建器
  using task_action_creator_t = std::shared_ptr<task_action_maker_base_t>;

  template <typename TAction>
  struct UTIL_SYMBOL_VISIBLE task_action_maker_t : public task_action_maker_base_t {
    explicit task_action_maker_t(const atframework::DispatcherOptions *opt) : task_action_maker_base_t(opt) {}
    int operator()(task_type_trait::task_type &task_inst, dispatcher_start_data_type ctor_param) override {
      if (options.has_timeout() && (options.timeout().seconds() > 0 || options.timeout().nanos() > 0)) {
        auto timeout = make_timeout_duration(options.timeout());
        timeout += make_timeout_duration(options.timeout_offset());
        return task_manager::me()->create_task_with_timeout<TAction>(task_inst, timeout, std::move(ctor_param));
      } else {
        auto timeout = get_default_timeout();
        if (options.timeout_default_multiple() > 0) {
          timeout *= options.timeout_default_multiple();
        }
        timeout += make_timeout_duration(options.timeout_offset());
        return task_manager::me()->create_task_with_timeout<TAction>(task_inst, timeout, std::move(ctor_param));
      }
    };
  };

 private:
  using native_task_manager_type = cotask::task_manager<task_type_trait::internal_task_type>;
  using native_task_manager_ptr_type = typename native_task_manager_type::ptr_type;

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(task_manager)
#  else
  UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(task_manager)
#  endif
#else
  UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(task_manager)
#endif

 private:
  SERVER_FRAME_API task_manager();

 public:
  SERVER_FRAME_API ~task_manager();

  SERVER_FRAME_API int init();

  SERVER_FRAME_API int reload();

  UTIL_FORCEINLINE static std::chrono::system_clock::duration make_timeout_duration(
      const ::google::protobuf::Duration &dur) {
    return protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dur);
  }

  UTIL_FORCEINLINE static std::chrono::system_clock::duration make_timeout_duration(
      const std::chrono::system_clock::duration &dur) {
    return dur;
  }

  template <typename Rep, typename Period>
  UTIL_FORCEINLINE static std::chrono::system_clock::duration make_timeout_duration(
      const std::chrono::duration<Rep, Period> &timeout) {
    return std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout);
  }

  SERVER_FRAME_API static std::chrono::system_clock::duration get_default_timeout();

  /**
   * 获取栈大小
   */
  SERVER_FRAME_API size_t get_stack_size() const;

  /**
   * @brief 创建任务并指定超时时间
   * @param task_instance 协程任务
   * @param timeout 超时时间
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams, typename Duration>
  UTIL_SYMBOL_VISIBLE int create_task_with_timeout(task_type_trait::task_type &task_instance, Duration &&timeout,
                                                   TParams &&args) {
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    if (!native_mgr_) {
      task_instance.reset();
      return PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    }

    task_instance = internal_create_and_setup_task<TAction>(std::forward<TParams>(args));
    if (task_type_trait::empty(task_instance)) {
      return report_create_error(__FUNCTION__);
    }
    // Start and use args to setup task action
    task_instance.start();
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

    return add_task(task_instance, make_timeout_duration(std::forward<Duration>(timeout)));
  }

  /**
   * @brief 创建任务
   * @param task_instance 协程任务
   * @param args 传入构造函数的参数
   * @return 0或错误码
   */
  template <typename TAction, typename TParams>
  UTIL_SYMBOL_VISIBLE int create_task(task_type_trait::task_type &task_instance, TParams &&args) {
    return create_task_with_timeout<TAction>(task_instance, std::chrono::system_clock::duration::zero(),
                                             std::forward<TParams>(args));
  }

  /**
   * @brief 创建协程任务构造器
   * @return 任务构造器
   */
  template <typename TAction>
  UTIL_FORCEINLINE task_action_creator_t make_task_creator(const atframework::DispatcherOptions *opt) {
    return atfw::memory::stl::make_shared<task_action_maker_t<TAction>>(opt);
  }

  /**
   * @brief 开始任务
   * @param task_id 协程任务的ID
   * @param data 启动数据，operator()(void* priv_data)的priv_data指向这个对象的地址
   * @return 0或错误码
   */
  SERVER_FRAME_API int start_task(task_type_trait::id_type task_id, dispatcher_start_data_type &data);
  SERVER_FRAME_API int start_task(task_type_trait::task_type &task_instance, dispatcher_start_data_type &data);

  /**
   * @brief 恢复任务
   * @param task_id 协程任务的ID
   * @param data 恢复时透传的数据，yield返回的指针指向这个对象的地址
   * @return 0或错误码
   */
  SERVER_FRAME_API int resume_task(task_type_trait::id_type task_id, dispatcher_resume_data_type &data);
  SERVER_FRAME_API int resume_task(task_type_trait::task_type &task_instance, dispatcher_resume_data_type &data);

  /**
   * @brief tick，可能会触发任务过期
   */
  SERVER_FRAME_API int tick(time_t sec, int nsec);

  /**
   * @brief Kill 所有尚未完成的任务
   */
  SERVER_FRAME_API void kill_all();

  /**
   * @brief 按ID获取任务
   * @param task_id 任务id
   * @return 如果存在，返回协程任务的智能指针
   */
  SERVER_FRAME_API task_type_trait::task_type get_task(task_type_trait::id_type task_id);

#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
  UTIL_FORCEINLINE const task_type_trait::stack_pool_type::ptr_t &get_stack_pool() const { return stack_pool_; }
#endif
  UTIL_FORCEINLINE const native_task_manager_ptr_type &get_native_manager() const { return native_mgr_; }

  SERVER_FRAME_API bool is_busy() const;

  SERVER_FRAME_API static void reset_private_data(task_private_data_type &priv_data);
  SERVER_FRAME_API static rpc::context *get_shared_context(task_type_trait::task_type &task);

  SERVER_FRAME_API static int32_t convert_task_status_to_error_code(task_type_trait::task_status task_status) noexcept;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  SERVER_FRAME_API static generic_start_generator make_start_generator(
      task_type_trait::id_type task_id, dispatcher_receive_start_data_callback receive_callback,
      void *callback_private_data);
  SERVER_FRAME_API static std::pair<generic_resume_key, generic_resume_generator> make_resume_generator(
      uintptr_t message_type, const dispatcher_await_options &await_options,
      dispatcher_receive_resume_data_callback receive_callback, void *callback_private_data);
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
  int add_task(const task_type_trait::task_type &task,
               std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero());

  int report_create_error(const char *fn_name);

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  struct generic_start_generator_record {
    dispatcher_receive_start_data_callback callback;
    void *callback_private_data;
    generic_start_generator::context_pointer_type generator_context;
  };

  struct generic_resume_generator_record {
    dispatcher_receive_resume_data_callback callback;
    void *callback_private_data;
    generic_resume_generator::context_pointer_type generator_context;
  };

  void internal_insert_start_generator(task_type_trait::id_type task_id,
                                       generic_start_generator::context_pointer_type &&,
                                       dispatcher_receive_start_data_callback callback, void *callback_private_data);
  void internal_remove_start_generator(task_type_trait::id_type task_id, const generic_start_generator::context_type &);
  void internal_insert_resume_generator(const generic_resume_key &key,
                                        generic_resume_generator::context_pointer_type &&,
                                        dispatcher_receive_resume_data_callback callback, void *callback_private_data);
  void internal_remove_resume_generator(const generic_resume_key &key, const generic_resume_generator::context_type &);

  static void internal_trigger_callback(generic_start_generator_record &start_record,
                                        const dispatcher_start_data_type *start_data);
  static void internal_trigger_callback(generic_resume_generator_record &resume_record, const generic_resume_key &key,
                                        const dispatcher_resume_data_type *resume_data);
#endif

 private:
  time_t stat_interval_;
  time_t stat_last_checkpoint_;
  size_t conf_busy_count_;
  size_t conf_busy_warn_count_;
  native_task_manager_ptr_type native_mgr_;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  std::unordered_map<task_type_trait::id_type, generic_start_generator_record> waiting_start_;
  std::map<generic_resume_key, generic_resume_generator_record> waiting_resume_timer_;
  std::unordered_map<generic_resume_index, generic_resume_key, generic_resume_hash> waiting_resume_index_;
#else
  task_type_trait::stack_pool_type::ptr_t stack_pool_;
#endif

#if GOOGLE_PROTOBUF_VERSION >= 4022000
  std::unique_ptr<absl::LogSink> absl_log_sink_;
#endif
};

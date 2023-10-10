// Copyright 2023 atframework
// Created by owent on 2021-11-05.
//

#pragma once

#include <config/server_frame_build_feature.h>

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  include <libcotask/task_promise.h>
#else
#  include <libcopp/stack/stack_allocator.h>
#  include <libcopp/stack/stack_pool.h>
#  include <libcotask/task.h>
#endif

#include <stdint.h>
#include <unordered_map>

class task_action_base;
struct task_private_data_type {
  task_action_base* action;
};

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
struct task_type_trait {
  using internal_task_type = cotask::task_future<int32_t, task_private_data_type>;
  using id_type = typename internal_task_type::id_type;
  using task_type = internal_task_type;
  using task_status = typename internal_task_type::task_status_type;

  inline static copp::promise_base_type::pick_promise_status_awaitable internal_pick_current_status() noexcept {
    return copp::promise_base_type::pick_current_status();
  }

  inline static id_type get_task_id(const task_type& task) noexcept { return task.get_id(); }

  inline static void reset_task(task_type& task) noexcept { task.reset(); }

  inline static bool empty(const task_type& task) noexcept { return !task.get_context(); }

  inline static int32_t get_result(const task_type& task) noexcept {
    if (task.get_context()) {
      auto data_ptr = task.get_context()->data();
      if (nullptr == data_ptr) {
        return 0;
      }
      return *data_ptr;
    }

    return 0;
  }

  inline static bool is_exiting(task_status status) noexcept {
    return status >= task_status::kDone || status == task_status::kInvalid;
  }

  inline static bool is_timeout(task_status status) noexcept { return status == task_status::kTimeout; }

  inline static bool is_cancel(task_status status) noexcept { return status == task_status::kCancle; }

  inline static bool is_fault(task_status status) noexcept { return status >= task_status::kKilled; }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_exiting(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return is_exiting(future.get_status()) || future.is_ready();
  }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_timeout(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return is_timeout(future.get_status());
  }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_cancel(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return is_cancel(future.get_status());
  }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_fault(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return is_fault(future.get_status());
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_exiting(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& task) noexcept {
    return empty(task) || task.is_exiting();
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_timeout(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& task) noexcept {
    return task.is_timeout();
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_cancel(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& task) noexcept {
    return is_cancel(task.get_status());
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_fault(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& task) noexcept {
    return task.is_faulted();
  }

  inline static task_private_data_type* get_private_data(task_type& task) noexcept {
    if (empty(task)) {
      return nullptr;
    }

    return task.get_private_data();
  }
};

struct task_action_meta_data_type {
  task_type_trait::id_type task_id = 0;
  task_private_data_type* private_data = nullptr;
};

// Compatibility
// C++20 coroutine use return type to check if it's in a coroutine, just do nothing here
// GCC Problems:
//  + task_type_trait::is_timeout((co_yield task_type_trait::internal_pick_current_status())):
//    no matching function for call to ‘task_type_trait::is_timeout(<unresolved overloaded function type>)’
//    | We TASK_COMPAT_ASSIGN_CURRENT_STATUS(VAR_NAME) to get status and then use task_type_trait::is_XXX to check it
#  define TASK_COMPAT_CHECK_TASK_ACTION_RETURN(...)
#  define TASK_COMPAT_ASSIGN_CURRENT_STATUS(VAR_NAME) \
    task_type_trait::task_status VAR_NAME = co_yield task_type_trait::internal_pick_current_status()

#else
struct task_type_trait {
  using stack_pool_type = copp::stack_pool<copp::allocator::default_statck_allocator>;

  struct task_macro_coroutine {
    using stack_allocator_type = copp::allocator::stack_allocator_pool<stack_pool_type>;
    using coroutine_type = copp::coroutine_context_container<stack_allocator_type>;
    using value_type = int;
  };

  using internal_task_type = cotask::task<task_macro_coroutine>;
  using id_type = typename internal_task_type::id_t;
  using task_type = typename internal_task_type::ptr_t;
  using task_status = cotask::EN_TASK_STATUS;

  inline static id_type get_task_id(const task_type& task) noexcept {
    if (!task) {
      return 0;
    }

    return task->get_id();
  }

  inline static void reset_task(task_type& task) noexcept { task.reset(); }

  inline static bool empty(const task_type& task) noexcept { return !task; }

  inline static int32_t get_result(const task_type& task) noexcept {
    if (!task) {
      return 0;
    }

    return task->get_ret_code();
  }

  inline static bool is_exiting(cotask::EN_TASK_STATUS status) noexcept {
    return status >= cotask::EN_TS_DONE || status == task_status::EN_TS_INVALID;
  }

  inline static bool is_timeout(cotask::EN_TASK_STATUS status) noexcept { return status == cotask::EN_TS_TIMEOUT; }

  inline static bool is_cancel(cotask::EN_TASK_STATUS status) noexcept { return status == cotask::EN_TS_CANCELED; }

  inline static bool is_fault(cotask::EN_TASK_STATUS status) noexcept { return status >= cotask::EN_TS_KILLED; }

  inline static bool is_exiting(const task_type& task) noexcept {
    if (!task) {
      return true;
    }

    return task->is_exiting();
  }

  inline static bool is_timeout(const task_type& task) noexcept {
    if (!task) {
      return false;
    }

    return task->is_timeout();
  }

  inline static bool is_cancel(const task_type& task) noexcept {
    if (!task) {
      return false;
    }

    return task->is_canceled();
  }

  inline static bool is_fault(const task_type& task) noexcept {
    if (!task) {
      return false;
    }

    return task->is_faulted();
  }

  inline static task_status get_status(const task_type& task) noexcept {
    if (!task) {
      return task_status::EN_TS_INVALID;
    }

    return task->get_status();
  }

  inline static task_private_data_type* get_private_data(internal_task_type& task) noexcept {
    if (task.get_private_buffer_size() < sizeof(task_private_data_type)) {
      return nullptr;
    }

    return reinterpret_cast<task_private_data_type*>(task.get_private_buffer());
  }

  inline static task_private_data_type* get_private_data(task_type& task) noexcept {
    if (!task) {
      return nullptr;
    }

    return get_private_data(*task);
  }
};

// Compatibility
#  define TASK_COMPAT_CHECK_TASK_ACTION_RETURN(...)                    \
    if (nullptr == task_type_trait::internal_task_type::this_task()) { \
      FWLOGERROR(__VA_ARGS__);                                         \
      RPC_RETURN_TYPE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);  \
    }

#  define TASK_COMPAT_ASSIGN_CURRENT_STATUS(VAR_NAME) \
    task_type_trait::task_status VAR_NAME =           \
        task_type_trait::get_status(task_type_trait::internal_task_type::this_task())

#endif

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

  inline static id_type get_task_id(const task_type& task) noexcept { return task.get_id(); }

  inline static void reset_task(task_type& task) noexcept { task.reset(); }

  inline static bool empty(const task_type& task) noexcept { return !task.get_context(); }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_exiting(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return future.get_status() >= task_type_trait::task_type::task_status_type::kDone || future.is_ready();
  }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_timeout(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return future.get_status() == task_type_trait::task_type::task_status_type::kTimeout;
  }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_cancel(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return future.get_status() == task_type_trait::task_type::task_status_type::kCancle;
  }

  template <class TVALUE, class TERROR_TRANSFORM>
  inline static bool is_fault(const copp::callable_future<TVALUE, TERROR_TRANSFORM>& future) noexcept {
    return future.get_status() >= task_type_trait::task_type::task_status_type::kKilled;
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_exiting(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& task) noexcept {
    return task.is_exiting();
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_timeout(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& future) noexcept {
    return task.is_timeout();
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_cancel(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& future) noexcept {
    return task.get_status() == task_type_trait::task_type::task_status_type::kCancle;
  }

  template <class TVALUE, class TPRIVATE_DATA, class TERROR_TRANSFORM>
  inline static bool is_fault(const cotask::task_future<TVALUE, TPRIVATE_DATA, TERROR_TRANSFORM>& future) noexcept {
    return task.is_faulted();
  }
};

// Compatibility
// C++20 coroutine use return type to check if it's in a coroutine, just do nothing here
#  define TASK_COMPAT_CHECK_TASK_ACTION_RETURN(...)
#  define TASK_COMPAT_CHECK_IS_EXITING() \
    ((co_yield copp::promise_base_type::pick_current_status()) >= task_type_trait::task_type::task_status_type::kDone)
#  define TASK_COMPAT_CHECK_IS_TIMEOUT()                          \
    ((co_yield copp::promise_base_type::pick_current_status()) == \
     task_type_trait::task_type::task_status_type::kTimeout)
#  define TASK_COMPAT_CHECK_IS_CANCEL() \
    ((co_yield copp::promise_base_type::pick_current_status()) == task_type_trait::task_type::task_status_type::kCancle)
#  define TASK_COMPAT_CHECK_IS_FAULT() \
    ((co_yield copp::promise_base_type::pick_current_status()) >= task_type_trait::task_type::task_status_type::kKilled)

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

  inline static id_type get_task_id(const task_type& task) noexcept {
    if (!task) {
      return 0;
    }

    return task->get_id();
  }

  inline static void reset_task(task_type& task) noexcept { task.reset(); }

  inline static bool empty(const task_type& task) noexcept { return !task; }

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
};

// Compatibility
#  define TASK_COMPAT_CHECK_TASK_ACTION_RETURN(...)                    \
    if (nullptr == task_type_trait::internal_task_type::this_task()) { \
      FWLOGERROR(__VA_ARGS__);                                         \
      RPC_RETURN_TYPE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);  \
    }

#  define TASK_COMPAT_CHECK_IS_EXITING() task_type_trait::is_exiting(task_type_trait::internal_task_type::this_task())
#  define TASK_COMPAT_CHECK_IS_TIMEOUT() task_type_trait::is_timeout(task_type_trait::internal_task_type::this_task())
#  define TASK_COMPAT_CHECK_IS_CANCEL() task_type_trait::is_cancel(task_type_trait::internal_task_type::this_task())
#  define TASK_COMPAT_CHECK_IS_FAULT() task_type_trait::is_fault(task_type_trait::internal_task_type::this_task())

#endif

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

  static id_type get_task_id(const task_type& task) noexcept;
};
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

  static id_type get_task_id(const task_type& task) noexcept;
};
#endif

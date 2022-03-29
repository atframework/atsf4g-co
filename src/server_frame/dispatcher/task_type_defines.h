// Copyright 2021 atframework
// Created by owent on 2021-11-05.
//

#pragma once

#include <libcopp/stack/stack_allocator.h>
#include <libcopp/stack/stack_pool.h>
#include <libcotask/task.h>

#include <unordered_map>

struct task_types {
  using stack_pool_type = copp::stack_pool<copp::allocator::default_statck_allocator>;

  struct task_macro_coroutine {
    using stack_allocator_t = copp::allocator::stack_allocator_pool<stack_pool_type>;
    using coroutine_t = copp::coroutine_context_container<stack_allocator_t>;
  };

  using task_type = cotask::task<task_macro_coroutine>;
  using id_type = typename task_type::id_t;
  using task_ptr_type = typename task_type::ptr_t;
};

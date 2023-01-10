// Copyright 2023 atframework
// Created by owent on 2023-01-10.
//

#include "dispatcher/task_type_traits.h"

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
task_type_trait::id_type task_type_trait::get_task_id(const task_type& task) noexcept { return task.get_id(); }
#else
task_type_trait::id_type task_type_trait::get_task_id(const task_type& task) noexcept {
  if (!task) {
    return 0;
  }

  return task->get_id();
}
#endif

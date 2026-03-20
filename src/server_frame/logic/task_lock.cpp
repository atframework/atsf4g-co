#include "logic/task_lock.h"

#include <dispatcher/cs_msg_dispatcher.h>
#include <dispatcher/task_action_base.h>
#include <dispatcher/task_manager.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>

#include <config/logic_config.h>

#include <data/player_cache.h>

SERVER_FRAME_API rpc::result_code_type task_lock::wait_task(rpc::context &ctx) {
  if (id_.empty()) {
    FWLOGDEBUG("[TASK_LOCK]:({}) task empty", user_id_);
    RPC_RETURN_CODE(0);
  }

  auto invoke_task = rpc::async_invoke(
      ctx, "task_lock::wait_task",
      [ptr = shared_from_this()](rpc::context &sub_ctx) -> rpc::result_code_type {
        int32_t loop_count = 0;
        while (!ptr->id_.empty()) {
          std::vector<task_type_trait::task_type> tasks;
          for (uint64_t tid : ptr->id_) {
            if (tid == sub_ctx.get_task_context().task_id) {
              continue;
            }

            task_type_trait::task_type subtask = task_manager::me()->get_task(tid);
            if (task_type_trait::empty(subtask) || task_type_trait::is_exiting(subtask)) {
              continue;
            }

            auto &action = task_type_trait::get_private_data(subtask)->action;
            if (action == nullptr) {
              continue;
            }

            if (action->get_dispatcher() == std::static_pointer_cast<dispatcher_implement>(cs_msg_dispatcher::me())) {
              tasks.push_back(subtask);
            }
          }
          ptr->id_.clear();
          if (tasks.empty()) {
            break;
          }

          FWLOGINFO("[TASK_LOCK]:({}) task:({}) not empty. loop :({})", ptr->user_id_,
                    sub_ctx.get_task_context().task_id, ++loop_count);
          int ret = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(sub_ctx, tasks));
          if (ret != 0) {
            FWLOGERROR("[TASK_LOCK]:({}) task:({}) await_task FAILED. RET:({})", ptr->user_id_,
                       sub_ctx.get_task_context().task_id, ret);
            RPC_RETURN_CODE(ret);
          }
        }
        RPC_RETURN_CODE(0);
      },
      std::chrono::duration<time_t, std::milli>(2000));
  if (invoke_task.is_success()) {
    if (!task_type_trait::is_exiting(*invoke_task.get_success())) {
      RPC_AWAIT_IGNORE_RESULT(rpc::wait_task(ctx, *invoke_task.get_success()));
    }
  }
  RPC_RETURN_CODE(0);
}

SERVER_FRAME_API void task_lock::init(uint64_t user_id) {
  user_id_ = user_id;
  FWLOGDEBUG("[TASK_LOCK]:({}) task init user success", user_id_);
}

SERVER_FRAME_API void task_lock::init_task(uint64_t id) {
  id_.insert(id);
  FWLOGDEBUG("[TASK_LOCK]:({}) task init:({}). current size:({})", user_id_, id, id_.size());
}

SERVER_FRAME_API void task_lock::remove_task(uint64_t id) {
  id_.erase(id);
  FWLOGDEBUG("[TASK_LOCK]:({}) task remove:({}). current size:({})", user_id_, id, id_.size());
}

SERVER_FRAME_API task_lock_guard::task_lock_guard(std::shared_ptr<player_cache> player, uint64_t task_id) {
  if (player != nullptr) {
    ptr_ = player;
    task_id_ = task_id;
    player->task_lock_init_task(task_id_);
  }
}

SERVER_FRAME_API task_lock_guard::~task_lock_guard() {
  if (!ptr_.expired()) {
    ptr_.lock()->task_lock_remove_task(task_id_);
  }
}
// Copyright 2021 atframework
// Created by owent on 2016/10/4.
//

#include "rpc/rpc_utils.h"

#include <log/log_wrapper.h>

#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.global.table.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <google/protobuf/util/time_util.h>

#include <config/compiler/protobuf_suffix.h>

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  include <libcopp/coroutine/algorithm.h>
#endif

#include <opentelemetry/trace/semantic_conventions.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "logic/logic_server_setup.h"

namespace rpc {

namespace detail {
static result_code_type wait(context &ctx, uintptr_t check_type, const dispatcher_await_options &options,
                             dispatcher_receive_resume_data_callback receive_callback,
                             void *receive_callback_private_data) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in a task");
  if (0 == options.sequence) {
    FCTXLOGERROR(ctx, "can not wait for type {}, sequence {}", check_type, options.sequence);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  {
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_exiting(current_task_status)) {
      if (task_type_trait::is_timeout(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
      } else if (task_type_trait::is_fault(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
      } else if (task_type_trait::is_cancel(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
      }
    }
  }

  bool is_continue = true;
  for (int retry_times = 0; is_continue && retry_times < PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES + 1;
       ++retry_times) {
    is_continue = false;
    // 协程 swap out
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    auto generator_kv =
        task_manager::make_resume_generator(check_type, options, receive_callback, receive_callback_private_data);
    auto [await_rsult, resume_data] = co_await generator_kv.second;
    if (await_rsult < 0) {
      RPC_RETURN_CODE(await_rsult);
    }
#else
    void *result = nullptr;
    task_type_trait::internal_task_type::this_task()->yield(&result);

    dispatcher_resume_data_type *resume_data = reinterpret_cast<dispatcher_resume_data_type *>(result);

    // 协程 swap in
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_exiting(current_task_status)) {
      if (task_type_trait::is_timeout(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
      }

      if (task_type_trait::is_fault(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
      }

      if (task_type_trait::is_cancel(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
      }
    }

    if (nullptr == resume_data) {
      FCTXLOGINFO(ctx, "task {} resume data is empty, maybe resumed by await_task", ctx.get_task_context().task_id);
      is_continue = true;
      continue;
    }

    if (resume_data->message.message_type != check_type) {
      FCTXLOGINFO(ctx, "task {} resume and expect message type {:#x} but real is {:#x}, ignore this message",
                  ctx.get_task_context().task_id, check_type, resume_data->message.message_type);

      is_continue = true;
      continue;
    }

    if (0 != resume_data->sequence && options.sequence != resume_data->sequence) {
      FCTXLOGINFO(ctx, "task {} resume and expect message sequence {:#x} but real is {:#x}, ignore this message",
                  ctx.get_task_context().task_id, options.sequence, resume_data->sequence);
      is_continue = true;
      continue;
    }

    if (nullptr != receive_callback) {
      (*receive_callback)(resume_data, receive_callback_private_data);
    }
#endif
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

template <typename TMSG>
static inline void wait_swap_message(std::shared_ptr<TMSG> &output, void *input) {
  if (output && input) {
    output->Swap(reinterpret_cast<TMSG *>(input));
  }
}
template <typename TMSG>
static inline void wait_swap_message(std::unique_ptr<TMSG> &output, void *input) {
  if (output && input) {
    output->Swap(reinterpret_cast<TMSG *>(input));
  }
}
template <typename TMSG>
static inline void wait_swap_message(TMSG *output, void *input) {
  if (output && input) {
    output->Swap(reinterpret_cast<TMSG *>(input));
  }
}
template <typename TMSG>
static inline void wait_swap_message(TMSG &output, void *input) {
  if (input) {
    output.Swap(reinterpret_cast<TMSG *>(input));
  }
}

template <typename TMSG>
struct batch_wait_private_type {
  const std::unordered_set<dispatcher_await_options> *waiters;
  std::unordered_map<uint64_t, TMSG> *received;
  std::unordered_set<uint64_t> *received_sequences;
};

template <typename TMSG>
static result_code_type wait(context &ctx, uintptr_t check_type,
                             const std::unordered_set<dispatcher_await_options> &waiters,
                             std::unordered_map<uint64_t, TMSG> &received, size_t wakeup_count) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in a task");
  if (0 == check_type) {
    FCTXLOGERROR(ctx, "can not wait for type {}", check_type);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (waiters.empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  {
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_exiting(current_task_status)) {
      if (task_type_trait::is_timeout(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
      } else if (task_type_trait::is_fault(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
      } else if (task_type_trait::is_cancel(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
      }
    }
  }

  std::unordered_set<uint64_t> received_sequences;
  received_sequences.reserve(waiters.size());
  received.reserve(waiters.size());

  using multiple_wait_private_type = batch_wait_private_type<TMSG>;

  multiple_wait_private_type callback_private_data;
  callback_private_data.waiters = &waiters;
  callback_private_data.received = &received;
  callback_private_data.received_sequences = &received_sequences;

  void *receive_callback_private_data = reinterpret_cast<void *>(&callback_private_data);
  dispatcher_receive_resume_data_callback receive_callback = [](const dispatcher_resume_data_type *resume_data,
                                                                void *stack_data) {
    multiple_wait_private_type *stack_received = reinterpret_cast<multiple_wait_private_type *>(stack_data);
    if (nullptr == resume_data || nullptr == stack_received) {
      return;
    }
    if (nullptr != stack_received->waiters) {
      dispatcher_await_options one_wait_option = dispatcher_make_default<dispatcher_await_options>();
      one_wait_option.sequence = resume_data->sequence;
      if (stack_received->waiters->end() == stack_received->waiters->find(one_wait_option)) {
        FWLOGINFO("resume and expect message type {:#x} but sequence not found, ignore this message",
                  resume_data->message.message_type, one_wait_option.sequence);
        return;
      }
    }

    if (nullptr != stack_received->received_sequences) {
      stack_received->received_sequences->insert(resume_data->sequence);
    }

    if (nullptr != stack_received->received) {
      wait_swap_message((*stack_received->received)[resume_data->sequence], resume_data->message.msg_addr);
    }
  };

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  std::vector<task_manager::generic_resume_generator> generators;
  generators.reserve(waiters.size());
  for (auto &waiter_option : waiters) {
    if (0 == waiter_option.sequence) {
      FCTXLOGERROR(ctx, "can not wait for type {}, sequence {}", check_type, waiter_option.sequence);
      continue;
    }

    generators.emplace_back(std::move(
        task_manager::make_resume_generator(check_type, waiter_option, receive_callback, receive_callback_private_data)
            .second));
  }
  if (wakeup_count > generators.size()) {
    wakeup_count = generators.size();
  }
  if (generators.empty()) {
    FCTXLOGERROR(ctx, "can not wait for type {}, all waiter(s) have not valid sequence", check_type);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
#else
  size_t waiter_count = 0;
  for (auto &waiter_option : waiters) {
    if (0 == waiter_option.sequence) {
      FCTXLOGERROR(ctx, "can not wait for type {}, sequence {}", check_type, waiter_option.sequence);
      continue;
    }
    ++waiter_count;
  }
  if (wakeup_count > waiter_count) {
    wakeup_count = waiter_count;
  }
  if (waiter_count <= 0) {
    FCTXLOGERROR(ctx, "can not wait for type {}, all waiter(s) have not valid sequence", check_type);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
#endif
  for (size_t retry_times = 0;
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
       received_sequences.size() < wakeup_count &&
       retry_times < PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES + 1;
#else
       received_sequences.size() < wakeup_count &&
       retry_times < waiter_count + PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES;
#endif
       ++retry_times) {
    // 协程 swap out
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    copp::some_ready<task_manager::generic_resume_generator>::type readys;
    auto await_result = co_await copp::some(readys, wakeup_count - received_sequences.size(), generators);
    if (await_result != copp::promise_status::kDone) {
      RPC_RETURN_CODE(task_manager::convert_task_status_to_error_code(await_result));
    }
#else
    void *result = nullptr;
    task_type_trait::internal_task_type::this_task()->yield(&result);

    dispatcher_resume_data_type *resume_data = reinterpret_cast<dispatcher_resume_data_type *>(result);

    // 协程 swap in
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_exiting(current_task_status)) {
      if (task_type_trait::is_timeout(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
      }

      if (task_type_trait::is_fault(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
      }

      if (task_type_trait::is_cancel(current_task_status)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
      }
    }

    if (nullptr == resume_data) {
      FCTXLOGINFO(ctx, "task {} resume data is empty, maybe resumed by await_task", ctx.get_task_context().task_id);
      continue;
    }

    if (resume_data->message.message_type != check_type) {
      FCTXLOGINFO(ctx, "task {} resume and expect message type {:#x} but real is {:#x}, ignore this message",
                  ctx.get_task_context().task_id, check_type, resume_data->message.message_type);

      continue;
    }

    if (nullptr != receive_callback) {
      (*receive_callback)(resume_data, receive_callback_private_data);
    }
#endif
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
}  // namespace detail

SERVER_FRAME_API result_code_type wait(context &ctx, std::chrono::system_clock::duration timeout) {
  logic_server_common_module *mod = logic_server_last_common_module();
  if (nullptr == mod) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_COMMON_LOGIC_TIMER_NEED_COMMON_MODULE);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in a task");
  rpc::context child_ctx(ctx);
  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.timer"},
      {opentelemetry::trace::SemanticConventions::kRpcService, "rpc.timer"},
      {opentelemetry::trace::SemanticConventions::kRpcMethod, "rpc.wait"}};
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  trace_start_option.attributes = trace_attributes;

  rpc::context::tracer tracer = child_ctx.make_tracer("rpc.wait.timer", std::move(trace_start_option));

  logic_server_timer timer;
  mod->insert_timer(ctx.get_task_context().task_id, timeout, timer);

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = timer.sequence;
  await_options.timeout = timeout;

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(detail::wait(ctx, timer.message_type, await_options, nullptr, nullptr)));
}

SERVER_FRAME_API result_code_type wait(context &ctx, atframework::SSMsg &msg, const dispatcher_await_options &options) {
  result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(detail::wait(
      ctx, ss_msg_dispatcher::me()->get_instance_ident(), options,
      [](const dispatcher_resume_data_type *resume_data, void *stack_data) {
        atframework::SSMsg *stack_msg = reinterpret_cast<atframework::SSMsg *>(stack_data);
        if (nullptr == stack_msg || nullptr == resume_data) {
          return;
        }

        detail::wait_swap_message(stack_msg, resume_data->message.msg_addr);
      },
      reinterpret_cast<void *>(&msg)));
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(msg.head().error_code());
}

SERVER_FRAME_API result_code_type wait(context &ctx, PROJECT_NAMESPACE_ID::table_all_message &msg,
                                       const dispatcher_await_options &options) {
  int ret = RPC_AWAIT_CODE_RESULT(detail::wait(
      ctx, db_msg_dispatcher::me()->get_instance_ident(), options,
      [](const dispatcher_resume_data_type *resume_data, void *stack_data) {
        PROJECT_NAMESPACE_ID::table_all_message *stack_msg =
            reinterpret_cast<PROJECT_NAMESPACE_ID::table_all_message *>(stack_data);
        if (nullptr == stack_msg || nullptr == resume_data) {
          return;
        }

        detail::wait_swap_message(stack_msg, resume_data->message.msg_addr);
      },
      reinterpret_cast<void *>(&msg)));
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(msg.error_code());
}

SERVER_FRAME_API result_code_type wait(context &ctx, const std::unordered_set<dispatcher_await_options> &waiters,
                                       std::unordered_map<uint64_t, atframework::SSMsg> &received,
                                       size_t wakeup_count) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(detail::wait(ctx, ss_msg_dispatcher::me()->get_instance_ident(), waiters,
                                                     received, 0 == wakeup_count ? waiters.size() : wakeup_count)));
}

SERVER_FRAME_API result_code_type wait(context &ctx, const std::unordered_set<dispatcher_await_options> &waiters,
                                       std::unordered_map<uint64_t, atframework::SSMsg *> &received,
                                       size_t wakeup_count) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(detail::wait(ctx, ss_msg_dispatcher::me()->get_instance_ident(), waiters,
                                                     received, 0 == wakeup_count ? waiters.size() : wakeup_count)));
}

SERVER_FRAME_API result_code_type custom_wait(context &ctx, const void *type_address,
                                              const dispatcher_await_options &options,
                                              dispatcher_receive_resume_data_callback receive_callback,
                                              void *receive_callback_private_data) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(detail::wait(ctx, reinterpret_cast<uintptr_t>(type_address), options,
                                                     receive_callback, receive_callback_private_data)));
}

SERVER_FRAME_API int32_t custom_resume(const task_type_trait::task_type &task,
                                       dispatcher_resume_data_type &resume_data) {
  if (task_type_trait::empty(task)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  int32_t res = task_manager::me()->resume_task(task_type_trait::get_task_id(task), resume_data);
  if (res < 0) {
    FWLOGERROR("resume task {} failed, res: {}.", task_type_trait::get_task_id(task), res);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

SERVER_FRAME_API int32_t custom_resume(task_type_trait::id_type task_id, dispatcher_resume_data_type &resume_data) {
  if (0 == task_id) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  return task_manager::me()->resume_task(task_id, resume_data);
}

}  // namespace rpc

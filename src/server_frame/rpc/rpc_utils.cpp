// Copyright 2021 atframework
// Created by owent on 2016/10/4.
//

#include "rpc/rpc_utils.h"

#include <log/log_wrapper.h>

#include <atframe/atapp.h>

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

#include <rpc/db/uuid.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "rpc/telemetry/rpc_global_service.h"

namespace rpc {

context::context() {
  task_manager::task_t *task = task_manager::task_t::this_task();
  if (task) {
    rpc::context *parent = task_manager::get_shared_context(*task);
    if (parent) {
      set_parent_context(*parent);
    }
  }
}

context::context(context &&other) {
  allocator_.swap(other.allocator_);
  trace_span_.swap(other.trace_span_);
  parent_span_.swap(other.parent_span_);
}

context::context(context &parent) {
  // Set parent tracer and arena allocator
  set_parent_context(parent);
}

context::~context() {}

void context::setup_tracer(tracer &tracer_instance, string_view name, trace_option &&options) {
  if (!tracer_instance.start(name, std::move(options), parent_span_)) {
    return;
  }

  if (trace_span_) {
    trace_span_->End();
    trace_span_.reset();
  }
  trace_span_ = tracer_instance.get_trace_span();
}

std::shared_ptr<::google::protobuf::Arena> context::mutable_protobuf_arena() {
  if (allocator_) {
    return allocator_;
  }

  ::google::protobuf::ArenaOptions arena_options;
  arena_options.start_block_size = 512;  // 链路跟踪可能就占了200字节，起始可以大一点
  arena_options.max_block_size = 65536;  // 数据库的数据块比较大。最大值可以大一点

  allocator_ = std::make_shared<::google::protobuf::Arena>(arena_options);
  return allocator_;
}

const std::shared_ptr<::google::protobuf::Arena> &context::get_protobuf_arena() const { return allocator_; }

bool context::try_reuse_protobuf_arena(const std::shared_ptr<::google::protobuf::Arena> &arena) {
  if (!arena || allocator_) {
    return false;
  }

  allocator_ = arena;
  return true;
}

void context::set_parent_context(rpc::context &parent) {
  if (nullptr == allocator_) {
    try_reuse_protobuf_arena(parent.mutable_protobuf_arena());
  }

  parent_span_ = parent.get_trace_span();
}

void context::set_current_service(atapp::app &app, const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &telemetry) {
  telemetry::global_service::set_current_service(app, telemetry);
}

namespace detail {
static result_code_type wait(void **output_msg, uintptr_t check_type, uint64_t check_sequence) {
  if (nullptr != output_msg) {
    *output_msg = nullptr;
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK};
  }

  if (task->is_timeout()) {
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  } else if (task->is_faulted()) {
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED};
  } else if (task->is_canceled()) {
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED};
  }

  bool is_continue = true;
  for (int retry_times = 0; is_continue && retry_times < PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES + 1;
       ++retry_times) {
    is_continue = false;
    // 协程 swap out
    void *result = nullptr;
    task->yield(&result);

    dispatcher_resume_data_t *resume_data = reinterpret_cast<dispatcher_resume_data_t *>(result);

    // 协程 swap in

    if (task->is_timeout()) {
      return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
    }

    if (task->is_faulted()) {
      return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED};
    }

    if (task->is_canceled()) {
      return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED};
    }

    if (nullptr == resume_data) {
      FWLOGINFO("task {} resume data is empty, maybe resumed by await_task", task->get_id());
      is_continue = true;
      continue;
    }

    if (resume_data->message.msg_type != check_type) {
      FWLOGINFO("task {} resume and expect message type {:#x} but real is {:#x}, ignore this message", task->get_id(),
                check_type, resume_data->message.msg_type);

      is_continue = true;
      continue;
    }

    if (0 != check_sequence && 0 != resume_data->sequence && check_sequence != resume_data->sequence) {
      FWLOGINFO("task {} resume and expect message sequence {:#x} but real is {:#x}, ignore this message",
                task->get_id(), check_sequence, resume_data->sequence);
      is_continue = true;
      continue;
    }

    if (nullptr != output_msg) {
      *output_msg = resume_data->message.msg_addr;
    }
  }

  return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SUCCESS};
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
static result_code_type wait(uintptr_t check_type, const std::unordered_set<uint64_t> &waiters,
                             std::unordered_map<uint64_t, TMSG> &received, size_t wakeup_count) {
  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK};
  }

  if (task->is_timeout()) {
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  }

  if (task->is_faulted()) {
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED};
  }

  if (task->is_canceled()) {
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED};
  }

  received.reserve(waiters.size());
  for (size_t retry_times = 0; received.size() < wakeup_count &&
                               retry_times < waiters.size() + PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES;
       ++retry_times) {
    // 协程 swap out
    void *result = nullptr;
    task->yield(&result);

    dispatcher_resume_data_t *resume_data = reinterpret_cast<dispatcher_resume_data_t *>(result);

    // 协程 swap in

    if (task->is_timeout()) {
      return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
    }

    if (task->is_faulted()) {
      return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED};
    }

    if (task->is_canceled()) {
      return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED};
    }

    if (nullptr == resume_data) {
      FWLOGINFO("task {} resume data is empty, maybe resumed by await_task", task->get_id());
      continue;
    }

    if (resume_data->message.msg_type != check_type) {
      FWLOGINFO("task {} resume and expect message type {:#x} but real is {:#x}, ignore this message", task->get_id(),
                check_type, resume_data->message.msg_type);

      continue;
    }

    auto rsp_iter = waiters.find(resume_data->sequence);
    if (rsp_iter == waiters.end()) {
      FWLOGINFO("task {} resume and with message sequence {} but not found in waiters, ignore this message",
                task->get_id(), resume_data->sequence);
      continue;
    }

    wait_swap_message(received[resume_data->sequence], resume_data->message.msg_addr);
  }

  return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SUCCESS};
}
}  // namespace detail

result_code_type wait(atframework::SSMsg &msg, uint64_t check_sequence) {
  void *result = nullptr;
  int ret = RPC_AWAIT_CODE_RESULT(detail::wait(&result, ss_msg_dispatcher::me()->get_instance_ident(), check_sequence));
  if (0 != ret) {
    return result_code_type{ret};
  }

  if (nullptr != result) {
    msg.Swap(reinterpret_cast<atframework::SSMsg *>(result));
  }

  return result_code_type{msg.head().error_code()};
}

result_code_type wait(PROJECT_NAMESPACE_ID::table_all_message &msg, uint64_t check_sequence) {
  void *result = nullptr;
  int ret = RPC_AWAIT_CODE_RESULT(detail::wait(&result, db_msg_dispatcher::me()->get_instance_ident(), check_sequence));
  if (0 != ret) {
    return result_code_type{ret};
  }

  if (nullptr != result) {
    msg.Swap(reinterpret_cast<PROJECT_NAMESPACE_ID::table_all_message *>(result));
  }

  return result_code_type{msg.error_code()};
}

result_code_type wait(const std::unordered_set<uint64_t> &waiters,
                      std::unordered_map<uint64_t, atframework::SSMsg> &received, size_t wakeup_count) {
  return detail::wait(ss_msg_dispatcher::me()->get_instance_ident(), waiters, received,
                      0 == wakeup_count ? waiters.size() : wakeup_count);
}

result_code_type wait(const std::unordered_set<uint64_t> &waiters,
                      std::unordered_map<uint64_t, atframework::SSMsg *> &received, size_t wakeup_count) {
  return detail::wait(ss_msg_dispatcher::me()->get_instance_ident(), waiters, received,
                      0 == wakeup_count ? waiters.size() : wakeup_count);
}

result_code_type custom_wait(const void *type_address, void **received, uint64_t check_sequence) {
  return detail::wait(received, reinterpret_cast<uintptr_t>(type_address), check_sequence);
}

result_code_type custom_resume(task_types::task_type &task, const void *type_address, uint64_t sequence,
                               void *received) {
  dispatcher_resume_data_t resume_data = dispatcher_make_default<dispatcher_resume_data_t>();
  resume_data.message.msg_type = reinterpret_cast<uintptr_t>(type_address);
  resume_data.message.msg_addr = received;
  resume_data.sequence = sequence;

  int res = task.resume(&resume_data);
  if (res < 0) {
    FWLOGERROR("resume task {:#x} failed, res: {}.", task.get_id(), res);
    return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND};
  }

  return result_code_type{PROJECT_NAMESPACE_ID::err::EN_SUCCESS};
}

}  // namespace rpc

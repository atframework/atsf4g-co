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

void context::set_current_service(const atapp::app &app,
                                  const PROJECT_SERVER_FRAME_NAMESPACE_ID::config::logic_telemetry_cfg &telemetry) {
  telemetry::global_service::set_current_service(app, telemetry);
}

namespace detail {
template <typename TMSG>
static int wait(TMSG &msg, uintptr_t check_type, uint64_t check_sequence) {
  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK;
  }

  if (task->is_timeout()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  } else if (task->is_faulted()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
  } else if (task->is_canceled()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
  }

  bool is_continue = true;
  for (int retry_times = 0; is_continue && retry_times < 5; ++retry_times) {
    is_continue = false;
    // 协程 swap out
    void *result = nullptr;
    task->yield(&result);

    dispatcher_resume_data_t *resume_data = reinterpret_cast<dispatcher_resume_data_t *>(result);

    // 协程 swap in

    if (task->is_timeout()) {
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    }

    if (task->is_faulted()) {
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
    }

    if (task->is_canceled()) {
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
    }

    if (nullptr == resume_data) {
      FWLOGERROR("task {} resume data con not be empty", task->get_id());
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_PARAM;
    }

    if (resume_data->message.msg_type != check_type) {
      FWLOGERROR("task {} resume and expect message type {:#x} but real is {:#x}", task->get_id(), check_type,
                 resume_data->message.msg_type);

      is_continue = true;
      continue;
    }

    if (0 != check_sequence && 0 != resume_data->sequence && check_sequence != resume_data->sequence) {
      FWLOGERROR("task {} resume and expect message sequence {:#x} but real is {:#x}", task->get_id(), check_sequence,
                 resume_data->sequence);
      is_continue = true;
      continue;
    }

    msg.Swap(reinterpret_cast<TMSG *>(resume_data->message.msg_addr));
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
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
static int wait(uintptr_t check_type, std::unordered_map<uint64_t, TMSG> &msg_waiters) {
  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK;
  }

  if (task->is_timeout()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  }

  if (task->is_faulted()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
  }

  if (task->is_canceled()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
  }

  std::unordered_set<uint64_t> received;
  received.reserve(msg_waiters.size());
  for (size_t retry_times = 0; received.size() < msg_waiters.size() && retry_times < msg_waiters.size() * 3;
       ++retry_times) {
    // 协程 swap out
    void *result = nullptr;
    task->yield(&result);

    dispatcher_resume_data_t *resume_data = reinterpret_cast<dispatcher_resume_data_t *>(result);

    // 协程 swap in

    if (task->is_timeout()) {
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    }

    if (task->is_faulted()) {
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
    }

    if (task->is_canceled()) {
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
    }

    if (nullptr == resume_data) {
      FWLOGERROR("task {} resume data con not be empty", task->get_id());
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_PARAM;
    }

    if (resume_data->message.msg_type != check_type) {
      FWLOGERROR("task {} resume and expect message type {:#x} but real is {:#x}", task->get_id(), check_type,
                 resume_data->message.msg_type);

      continue;
    }

    auto rsp_iter = msg_waiters.find(resume_data->sequence);
    if (rsp_iter == msg_waiters.end()) {
      FWLOGERROR("task {} resume and with message sequence {} but not found in waiters", task->get_id(),
                 resume_data->sequence);
      continue;
    }

    wait_swap_message(rsp_iter->second, resume_data->message.msg_addr);
    received.insert(resume_data->sequence);
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}
}  // namespace detail

int wait(PROJECT_SERVER_FRAME_NAMESPACE_ID::SSMsg &msg, uint64_t check_sequence) {
  int ret = detail::wait(msg, ss_msg_dispatcher::me()->get_instance_ident(), check_sequence);
  if (0 != ret) {
    return ret;
  }

  return msg.head().error_code();
}

int wait(PROJECT_SERVER_FRAME_NAMESPACE_ID::table_all_message &msg, uint64_t check_sequence) {
  int ret = detail::wait(msg, db_msg_dispatcher::me()->get_instance_ident(), check_sequence);
  if (0 != ret) {
    return ret;
  }

  return msg.error_code();
}

int wait(std::unordered_map<uint64_t, PROJECT_SERVER_FRAME_NAMESPACE_ID::SSMsg> &msg_waiters) {
  return detail::wait(ss_msg_dispatcher::me()->get_instance_ident(), msg_waiters);
}

int wait(std::unordered_map<uint64_t, PROJECT_SERVER_FRAME_NAMESPACE_ID::SSMsg *> &msg_waiters) {
  return detail::wait(ss_msg_dispatcher::me()->get_instance_ident(), msg_waiters);
}

}  // namespace rpc

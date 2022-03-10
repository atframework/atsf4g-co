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

#include "logic/logic_server_setup.h"

namespace rpc {

context::context() : parent_link_mode_(false) {
  task_manager::task_t *task = task_manager::task_t::this_task();
  if (task) {
    rpc::context *parent = task_manager::get_shared_context(*task);
    if (parent) {
      set_parent_context(*parent);
    }
  }
}

context::context(context &&other) : parent_link_mode_(false) {
  using std::swap;

  allocator_.swap(other.allocator_);
  trace_span_.swap(other.trace_span_);
  parent_span_.swap(other.parent_span_);
  link_spans_.swap(other.link_spans_);
  swap(parent_link_mode_, other.parent_link_mode_);
}

context::context(context &parent, bool link_mode) {
  // Set parent tracer and arena allocator
  set_parent_context(parent, link_mode);
}

context::~context() {}

void context::setup_tracer(
    tracer &tracer_instance, string_view name, trace_option &&options,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attributes) {
  if (!parent_link_mode_ && parent_span_ && !options.parent_memory_span) {
    options.parent_memory_span = parent_span_;
  }

  tracer::links_type tracer_links;
  if ((parent_link_mode_ && parent_span_) || !link_spans_.empty()) {
    tracer_links.reserve(link_spans_.size() + 1);
    options.links = &tracer_links;
    if (parent_link_mode_ && parent_span_) {
      tracer_links.push_back(tracer::link_pair_type(parent_span_->GetContext(), {}));
    }
    for (auto &link_span : link_spans_) {
      tracer_links.push_back(tracer::link_pair_type(link_span->GetContext(), {}));
    }
  }

  if (!tracer_instance.start(name, std::move(options), attributes)) {
    return;
  }

  if (trace_span_) {
    trace_span_->End();
    trace_span_ = tracer::span_ptr_type();
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

bool context::try_reuse_protobuf_arena(const std::shared_ptr<::google::protobuf::Arena> &arena) noexcept {
  if (!arena || allocator_) {
    return false;
  }

  allocator_ = arena;
  return true;
}

void context::set_parent_context(rpc::context &parent, bool link_mode) noexcept {
  if (nullptr == allocator_) {
    try_reuse_protobuf_arena(parent.mutable_protobuf_arena());
  }

  parent_span_ = parent.get_trace_span();
  parent_link_mode_ = link_mode;
}

void context::add_link_span(const tracer::span_ptr_type &span_ptr) noexcept {
  if (!span_ptr) {
    return;
  }

  link_spans_.push_back(span_ptr);
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
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }

  if (task->is_timeout()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  } else if (task->is_faulted()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
  } else if (task->is_canceled()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
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
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    }

    if (task->is_faulted()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    }

    if (task->is_canceled()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
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
static result_code_type wait(uintptr_t check_type, const std::unordered_set<uint64_t> &waiters,
                             std::unordered_map<uint64_t, TMSG> &received, size_t wakeup_count) {
  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }

  if (task->is_timeout()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  }

  if (task->is_faulted()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
  }

  if (task->is_canceled()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
  }

  std::unordered_set<uint64_t> received_sequences;
  received_sequences.reserve(waiters.size());
  received.reserve(waiters.size());
  for (size_t retry_times = 0; received_sequences.size() < wakeup_count &&
                               retry_times < waiters.size() + PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES;
       ++retry_times) {
    // 协程 swap out
    void *result = nullptr;
    task->yield(&result);

    dispatcher_resume_data_t *resume_data = reinterpret_cast<dispatcher_resume_data_t *>(result);

    // 协程 swap in

    if (task->is_timeout()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    }

    if (task->is_faulted()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    }

    if (task->is_canceled()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
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

    received_sequences.insert(resume_data->sequence);
    wait_swap_message(received[resume_data->sequence], resume_data->message.msg_addr);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
}  // namespace detail

result_code_type wait(context &ctx, std::chrono::system_clock::duration timeout) {
  logic_server_common_module *mod = logic_server_last_common_module();
  if (nullptr == mod) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_COMMON_LOGIC_TIMER_NEED_COMMON_MODULE);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("current not in a task");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }
  rpc::context child_ctx(ctx);
  rpc::context::tracer tracer;
  ::rpc::context::trace_option trace_option;
  trace_option.dispatcher = nullptr;
  trace_option.is_remote = false;
  trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_ctx.setup_tracer(tracer, "rpc.wait.timer", std::move(trace_option));

  logic_server_timer timer;
  mod->insert_timer(task->get_id(), timeout, timer);

  return detail::wait(nullptr, timer.message_type, timer.sequence);
}

result_code_type wait(atframework::SSMsg &msg, uint64_t check_sequence) {
  void *result = nullptr;
  int ret = RPC_AWAIT_CODE_RESULT(detail::wait(&result, ss_msg_dispatcher::me()->get_instance_ident(), check_sequence));
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  if (nullptr != result) {
    msg.Swap(reinterpret_cast<atframework::SSMsg *>(result));
  }

  RPC_RETURN_CODE(msg.head().error_code());
}

result_code_type wait(PROJECT_NAMESPACE_ID::table_all_message &msg, uint64_t check_sequence) {
  void *result = nullptr;
  int ret = RPC_AWAIT_CODE_RESULT(detail::wait(&result, db_msg_dispatcher::me()->get_instance_ident(), check_sequence));
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  if (nullptr != result) {
    msg.Swap(reinterpret_cast<PROJECT_NAMESPACE_ID::table_all_message *>(result));
  }

  RPC_RETURN_CODE(msg.error_code());
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
    FWLOGERROR("resume task {} failed, res: {}.", task.get_id(), res);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace rpc

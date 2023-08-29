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

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  include <libcopp/coroutine/algorithm.h>
#endif

#include <rpc/db/uuid.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "rpc/telemetry/rpc_global_service.h"

#include "logic/logic_server_setup.h"

namespace rpc {

SERVER_FRAME_API context::context() noexcept {
  trace_context_.caller_mode = parent_mode::kParent;
  task_context_.task_id = 0;
}

SERVER_FRAME_API context::context(context &&other) noexcept {
  trace_context_.caller_mode = parent_mode::kParent;
  task_context_.task_id = 0;
  using std::swap;

  allocator_.swap(other.allocator_);
  trace_context_.trace_span.swap(other.trace_context_.trace_span);
  trace_context_.parent_span.swap(other.trace_context_.parent_span);
  trace_context_.link_spans.swap(other.trace_context_.link_spans);
  swap(trace_context_.caller_mode, other.trace_context_.caller_mode);

  swap(task_context_.task_id, other.task_context_.task_id);
}

SERVER_FRAME_API context::context(context &parent, inherit_options options) noexcept {
  task_context_.task_id = 0;

  // Set parent tracer and arena allocator
  set_parent_context(parent, options);
}

SERVER_FRAME_API context::~context() {}

SERVER_FRAME_API context context::create_without_task(create_options) noexcept { return {}; }

SERVER_FRAME_API context context::create_temporary_child(inherit_options options) noexcept { return {*this, options}; }

SERVER_FRAME_API std::shared_ptr<context> context::create_shared_child(inherit_options options) noexcept {
  return std::make_shared<context>(*this, options);
}

SERVER_FRAME_API void context::setup_tracer(
    tracer &tracer_instance, string_view name, trace_option &&options,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attributes) {
  tracer::links_type tracer_links;
  std::unique_ptr<opentelemetry::trace::SpanContext> parent_span_context;
  tracer_links.reserve(trace_context_.link_spans.size() + 1);

  switch (trace_context_.caller_mode) {
    case parent_mode::kLink: {
      if (nullptr != options.parent_memory_span) {
        tracer_links.push_back(tracer::link_pair_type(options.parent_memory_span->GetContext(), {}));
        options.parent_memory_span = tracer::span_ptr_type();
        break;
      }

      if (nullptr != options.parent_network_span &&
          options.parent_network_span->trace_id().size() == tracer::trace_id_span::extent &&
          options.parent_network_span->span_id().size() == tracer::span_id_span::extent) {
        const uint8_t *parent_trace_id =
            reinterpret_cast<const uint8_t *>(options.parent_network_span->trace_id().c_str());
        const uint8_t *parent_span_id =
            reinterpret_cast<const uint8_t *>(options.parent_network_span->span_id().c_str());
        parent_span_context.reset(new opentelemetry::trace::SpanContext{
            opentelemetry::trace::TraceId{tracer::trace_id_span{parent_trace_id, tracer::trace_id_span::extent}},
            opentelemetry::trace::SpanId{tracer::span_id_span{parent_span_id, tracer::span_id_span::extent}},
            opentelemetry::trace::TraceFlags{opentelemetry::trace::TraceFlags::kIsSampled}, options.is_remote});
        if (parent_span_context) {
          tracer_links.push_back(tracer::link_pair_type(*parent_span_context, {}));
          options.parent_network_span = nullptr;
          break;
        }
      }

      if (trace_context_.parent_span) {
        tracer_links.push_back(tracer::link_pair_type(trace_context_.parent_span->GetContext(), {}));
      }

      break;
    }
    // parent_mode::kParent by default
    default: {
      if (nullptr != options.parent_memory_span || nullptr != options.parent_network_span) {
        break;
      }

      if (trace_context_.parent_span) {
        options.parent_memory_span = trace_context_.parent_span;
        break;
      }
      break;
    }
  }

  // Add links
  for (auto &link_span : trace_context_.link_spans) {
    tracer_links.push_back(tracer::link_pair_type(link_span->GetContext(), {}));
  }
  if (!tracer_links.empty()) {
    options.links = &tracer_links;
  }

  if (!tracer_instance.start(name, std::move(options), attributes)) {
    return;
  }

  if (trace_context_.trace_span) {
    trace_context_.trace_span->End();
    trace_context_.trace_span = tracer::span_ptr_type();
  }
  trace_context_.trace_span = tracer_instance.get_trace_span();
}

SERVER_FRAME_API context::tracer context::make_tracer(
    string_view name, trace_option &&options,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attributes) {
  tracer ret;
  setup_tracer(ret, name, std::move(options), attributes);

  return tracer{std::move(ret)};
}

SERVER_FRAME_API std::shared_ptr<::google::protobuf::Arena> context::mutable_protobuf_arena() {
  if (allocator_) {
    return allocator_;
  }

  ::google::protobuf::ArenaOptions arena_options;
  arena_options.start_block_size = 512;  // 链路跟踪可能就占了200字节，起始可以大一点
  arena_options.max_block_size = 65536;  // 数据库的数据块比较大。最大值可以大一点

  allocator_ = std::make_shared<::google::protobuf::Arena>(arena_options);
  return allocator_;
}

SERVER_FRAME_API const std::shared_ptr<::google::protobuf::Arena> &context::get_protobuf_arena() const {
  return allocator_;
}

SERVER_FRAME_API bool context::try_reuse_protobuf_arena(
    const std::shared_ptr<::google::protobuf::Arena> &arena) noexcept {
  if (!arena || allocator_) {
    return false;
  }

  allocator_ = arena;
  return true;
}

SERVER_FRAME_API const context::tracer::span_ptr_type &context::get_trace_span() const noexcept {
  if (trace_context_.trace_span) {
    return trace_context_.trace_span;
  }

  if (trace_context_.parent_span) {
    return trace_context_.parent_span;
  }

  return trace_context_.trace_span;
}

SERVER_FRAME_API void context::set_parent_context(rpc::context &parent, inherit_options options) noexcept {
  if (nullptr == allocator_ && options.inherit_allocator) {
    try_reuse_protobuf_arena(parent.mutable_protobuf_arena());
  }

  if (options.inherit_parent_span) {
    trace_context_.parent_span = parent.get_trace_span();
  }
  trace_context_.caller_mode = options.mode;

  task_context_ = parent.task_context_;
}

SERVER_FRAME_API void context::add_link_span(const tracer::span_ptr_type &span_ptr) noexcept {
  if (!span_ptr) {
    return;
  }

  trace_context_.link_spans.push_back(span_ptr);
}

SERVER_FRAME_API void context::set_current_service(atapp::app &app,
                                                   const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &telemetry) {
  telemetry::global_service::set_current_service(app, telemetry);
}

SERVER_FRAME_API void context::set_task_context(const task_context_data &task_ctx) noexcept {
  task_context_ = task_ctx;
}

namespace detail {
static result_code_type wait(context &ctx, dispatcher_resume_data_type *output, uintptr_t check_type,
                             const dispatcher_await_options &options) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in a task");

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
    auto generator_kv = task_manager::make_resume_generator(check_type, options);
    auto [await_rsult, resume_data] = co_await generator_kv.second;
    if (await_rsult < 0) {
      RPC_RETURN_CODE(await_rsult);
    }
#else
    void *result = nullptr;
    task_type_trait::internal_task_type::this_task()->yield(&result);

    dispatcher_resume_data_type *resume_data = reinterpret_cast<dispatcher_resume_data_type *>(result);
#endif

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
      FWLOGINFO("task {} resume data is empty, maybe resumed by await_task", ctx.get_task_context().task_id);
      is_continue = true;
      continue;
    }

    if (resume_data->message.msg_type != check_type) {
      FWLOGINFO("task {} resume and expect message type {:#x} but real is {:#x}, ignore this message",
                ctx.get_task_context().task_id, check_type, resume_data->message.msg_type);

      is_continue = true;
      continue;
    }

    if (0 != options.sequence && 0 != resume_data->sequence && options.sequence != resume_data->sequence) {
      FWLOGINFO("task {} resume and expect message sequence {:#x} but real is {:#x}, ignore this message",
                ctx.get_task_context().task_id, options.sequence, resume_data->sequence);
      is_continue = true;
      continue;
    }

    if (nullptr != output) {
      *output = *resume_data;
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
static result_code_type wait(context &ctx, uintptr_t check_type,
                             const std::unordered_set<dispatcher_await_options> &waiters,
                             std::unordered_map<uint64_t, TMSG> &received, size_t wakeup_count) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in a task");

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

  dispatcher_await_options one_wait_option = dispatcher_make_default<dispatcher_await_options>();
  std::unordered_set<uint64_t> received_sequences;
  received_sequences.reserve(waiters.size());
  received.reserve(waiters.size());
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  std::vector<task_manager::generic_resume_generator> generators;
  generators.reserve(waiters.size());
  for (auto &waiter_option : waiters) {
    generators.emplace_back(std::move(task_manager::make_resume_generator(check_type, waiter_option).second));
  }
#endif
  for (size_t retry_times = 0;
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
       received_sequences.size() < wakeup_count &&
       retry_times < PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES + 1;
#else
       received_sequences.size() < wakeup_count &&
       retry_times < waiters.size() + PROJECT_NAMESPACE_ID::EN_SL_RPC_MAX_MISMATCH_RETRY_TIMES;
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
#endif

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

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    for (auto &ready_generator : readys) {
      dispatcher_resume_data_type *resume_data = ready_generator->get_context()->data()->second;
#endif
      if (nullptr == resume_data) {
        FWLOGINFO("task {} resume data is empty, maybe resumed by await_task", ctx.get_task_context().task_id);
        continue;
      }

      if (resume_data->message.msg_type != check_type) {
        FWLOGINFO("task {} resume and expect message type {:#x} but real is {:#x}, ignore this message",
                  ctx.get_task_context().task_id, check_type, resume_data->message.msg_type);

        continue;
      }

      one_wait_option.sequence = resume_data->sequence;
      auto rsp_iter = waiters.find(one_wait_option);
      if (rsp_iter == waiters.end()) {
        FWLOGINFO("task {} resume and with message sequence {} but not found in waiters, ignore this message",
                  ctx.get_task_context().task_id, resume_data->sequence);
        continue;
      }

      received_sequences.insert(resume_data->sequence);
      wait_swap_message(received[resume_data->sequence], resume_data->message.msg_addr);
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
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
  rpc::context::tracer tracer;
  rpc::context::trace_option trace_option;
  trace_option.dispatcher = nullptr;
  trace_option.is_remote = false;
  trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_ctx.setup_tracer(tracer, "rpc.wait.timer", std::move(trace_option),
                         {{"rpc.system", "atrpc.timer"}, {"rpc.method", "rpc.wait"}});

  logic_server_timer timer;
  mod->insert_timer(ctx.get_task_context().task_id, timeout, timer);

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = timer.sequence;
  await_options.timeout = timeout;

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(detail::wait(ctx, nullptr, timer.message_type, await_options)));
}

SERVER_FRAME_API result_code_type wait(context &ctx, atframework::SSMsg &msg, const dispatcher_await_options &options) {
  dispatcher_resume_data_type result = dispatcher_make_default<dispatcher_resume_data_type>();
  int ret = RPC_AWAIT_CODE_RESULT(detail::wait(ctx, &result, ss_msg_dispatcher::me()->get_instance_ident(), options));
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  if (nullptr != result.message.msg_addr) {
    msg.Swap(reinterpret_cast<atframework::SSMsg *>(result.message.msg_addr));
  }

  RPC_RETURN_CODE(msg.head().error_code());
}

SERVER_FRAME_API result_code_type wait(context &ctx, PROJECT_NAMESPACE_ID::table_all_message &msg,
                                       const dispatcher_await_options &options) {
  dispatcher_resume_data_type result = dispatcher_make_default<dispatcher_resume_data_type>();
  int ret = RPC_AWAIT_CODE_RESULT(detail::wait(ctx, &result, db_msg_dispatcher::me()->get_instance_ident(), options));
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  if (nullptr != result.message.msg_addr) {
    msg.Swap(reinterpret_cast<PROJECT_NAMESPACE_ID::table_all_message *>(result.message.msg_addr));
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
                                              dispatcher_resume_data_type *received,
                                              const dispatcher_await_options &options) {
  RPC_RETURN_CODE(
      RPC_AWAIT_CODE_RESULT(detail::wait(ctx, received, reinterpret_cast<uintptr_t>(type_address), options)));
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

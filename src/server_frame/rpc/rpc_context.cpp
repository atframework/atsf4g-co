// Copyright 2023 atframework
// Created by owent on 2023-09-07.
//

#include "rpc/rpc_context.h"

#include <atframe/atapp.h>
#include <atframe/atapp_conf_rapidjson.h>

#include <memory/object_allocator.h>
#include <utility/random_engine.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.protocol.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include "rpc/telemetry/opentelemetry_utility.h"
#include "rpc/telemetry/rpc_global_service.h"
#include "rpc/telemetry/rpc_trace.h"

namespace rpc {

namespace {
struct ATFW_UTIL_SYMBOL_LOCAL rpc_context_mertrics_data {
  bool metrics_installed = false;
  int64_t sample_rate = 0;

  int64_t configure_max_count_per_second = 0;
  int64_t configure_max_count_per_minute = 0;

  time_t last_collect_per_second_timepoint = 0;
  time_t last_collect_per_minute_timepoint = 0;

  int64_t total_drop_trace_span_count = 0;
  int64_t total_sample_trace_span_count = 0;

  int64_t last_second_timepoint_drop_trace_span_count = 0;
  int64_t last_second_timepoint_sample_trace_span_count = 0;
  int64_t last_minute_timepoint_drop_trace_span_count = 0;
  int64_t last_minute_timepoint_sample_trace_span_count = 0;

  inline rpc_context_mertrics_data() noexcept {}
};

static rpc_context_mertrics_data &get_rpc_context_mertrics_data() noexcept {
  static rpc_context_mertrics_data g_rpc_context_mertrics_data;
  return g_rpc_context_mertrics_data;
}

static void calculate_trace_span_permillage(time_t now) {
  int64_t current_rate = 0x1000000;
  auto &trace_configure = logic_config::me()->get_logic().task().trace();
  auto &metrics_data = get_rpc_context_mertrics_data();

  metrics_data.configure_max_count_per_second = trace_configure.max_count_per_second();
  metrics_data.configure_max_count_per_minute = trace_configure.max_count_per_minute();

  if (trace_configure.sample_permillage() > 0 && trace_configure.sample_permillage() < 1000) {
    current_rate = (static_cast<int64_t>(trace_configure.sample_permillage()) * 0x1000000 + 999) / 1000;
  }

  int64_t current_span_count = metrics_data.total_sample_trace_span_count + metrics_data.total_drop_trace_span_count;
  int64_t last_per_second_span_count = metrics_data.last_second_timepoint_drop_trace_span_count +
                                       metrics_data.last_second_timepoint_sample_trace_span_count;
  if (metrics_data.configure_max_count_per_second > 0 &&
      last_per_second_span_count + metrics_data.configure_max_count_per_second < current_span_count) {
    int64_t base = current_span_count - last_per_second_span_count;
    int64_t rate = (metrics_data.configure_max_count_per_second * 0x1000000 + base - 1) / base;
    if (rate > 0 && rate < current_rate) {
      current_rate = rate;
    }
  }

  metrics_data.last_collect_per_second_timepoint = now;
  metrics_data.last_second_timepoint_drop_trace_span_count = metrics_data.total_drop_trace_span_count;
  metrics_data.last_second_timepoint_sample_trace_span_count = metrics_data.total_sample_trace_span_count;

  if (metrics_data.last_collect_per_minute_timepoint / atfw::util::time::time_utility::MINITE_SECONDS ==
      now / atfw::util::time::time_utility::MINITE_SECONDS) {
    return;
  }

  int64_t last_per_minute_span_count = metrics_data.last_minute_timepoint_drop_trace_span_count +
                                       metrics_data.last_minute_timepoint_sample_trace_span_count;
  if (metrics_data.configure_max_count_per_minute > 0 &&
      last_per_minute_span_count + metrics_data.configure_max_count_per_minute < current_span_count) {
    int64_t base = current_span_count - last_per_minute_span_count;
    int64_t rate = (metrics_data.configure_max_count_per_minute * 0x1000000 + base - 1) / base;
    if (rate > 0 && rate < current_rate) {
      current_rate = rate;
    }
  }

  metrics_data.last_collect_per_minute_timepoint = now;
  metrics_data.last_minute_timepoint_drop_trace_span_count = metrics_data.total_drop_trace_span_count;
  metrics_data.last_minute_timepoint_sample_trace_span_count = metrics_data.total_sample_trace_span_count;
}

static bool should_sample_trace_span() {
  time_t now = atfw::util::time::time_utility::get_sys_now();

  auto &metrics_data = get_rpc_context_mertrics_data();
  if (now != metrics_data.last_collect_per_second_timepoint) {
    calculate_trace_span_permillage(now);
  }

  if (metrics_data.configure_max_count_per_second > 0 &&
      metrics_data.configure_max_count_per_second + metrics_data.last_second_timepoint_sample_trace_span_count <
          metrics_data.total_sample_trace_span_count) {
    return false;
  }

  if (metrics_data.configure_max_count_per_minute > 0 &&
      metrics_data.configure_max_count_per_minute + metrics_data.last_minute_timepoint_sample_trace_span_count <
          metrics_data.total_sample_trace_span_count) {
    return false;
  }

  if (metrics_data.sample_rate <= 0 || metrics_data.sample_rate > 0xFFFFFF) {
    return true;
  }

  return atfw::util::random_engine::fast_random_between<int64_t>(0, 0x1000000) < metrics_data.sample_rate;
}

}  // namespace

SERVER_FRAME_API context::context() noexcept {
  trace_context_.dynamic_policy = trace_dynamic_policy::kUnset;
  trace_context_.caller_mode = parent_mode::kParent;
  task_context_.task_id = 0;
}

SERVER_FRAME_API context::context(context &&other) noexcept {
  trace_context_.dynamic_policy = trace_dynamic_policy::kUnset;
  trace_context_.caller_mode = parent_mode::kParent;
  task_context_.task_id = 0;
  using std::swap;

  allocator_.swap(other.allocator_);
  trace_context_.trace_span.swap(other.trace_context_.trace_span);
  trace_context_.parent_span.swap(other.trace_context_.parent_span);
  trace_context_.link_spans.swap(other.trace_context_.link_spans);
  swap(trace_context_.caller_mode, other.trace_context_.caller_mode);
  swap(trace_context_.dynamic_policy, other.trace_context_.dynamic_policy);

  swap(task_context_.task_id, other.task_context_.task_id);
}

SERVER_FRAME_API context::context(context &parent, inherit_options options) noexcept {
  trace_context_.dynamic_policy = trace_dynamic_policy::kUnset;
  task_context_.task_id = 0;

  // Set parent tracer and arena allocator
  set_parent_context(parent, options);
}

SERVER_FRAME_API context::~context() {}

SERVER_FRAME_API context context::create_without_task(create_options) noexcept { return {}; }

SERVER_FRAME_API context context::create_temporary_child(inherit_options options) noexcept { return {*this, options}; }

SERVER_FRAME_API std::shared_ptr<context> context::create_shared_child(inherit_options options) noexcept {
  return atfw::memory::stl::make_shared<context>(*this, options);
}

SERVER_FRAME_API void context::setup_tracer(tracer &tracer_instance, string_view name, trace_start_option &&options) {
  // 动态采样率
  if (trace_dynamic_policy::kUnset == trace_context_.dynamic_policy) {
    if (options.parent_memory_span) {
      if (!options.parent_memory_span->IsRecording()) {
        trace_context_.dynamic_policy = trace_dynamic_policy::kDrop;
      }
    } else if (options.parent_network_span) {
      if (options.parent_network_span->dynamic_ignore()) {
        trace_context_.dynamic_policy = trace_dynamic_policy::kDrop;
      }
    }
  }

  if (trace_dynamic_policy::kDrop == get_trace_policy()) {
    ++get_rpc_context_mertrics_data().total_drop_trace_span_count;
    return;
  }

  std::vector<telemetry::trace_link_pair_type> tracer_links;
  std::unique_ptr<opentelemetry::trace::SpanContext> parent_span_context;
  tracer_links.reserve(trace_context_.link_spans.size() + options.links.size() + 1);

  switch (trace_context_.caller_mode) {
    case parent_mode::kLink: {
      if (nullptr != options.parent_memory_span) {
        tracer_links.emplace_back(telemetry::trace_link_pair_type(options.parent_memory_span->GetContext(), {}));
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
          tracer_links.emplace_back(telemetry::trace_link_pair_type(*parent_span_context, {}));
          options.parent_network_span = nullptr;
          break;
        }
      }

      if (trace_context_.parent_span) {
        tracer_links.emplace_back(telemetry::trace_link_pair_type(trace_context_.parent_span->GetContext(), {}));
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
    tracer_links.emplace_back(telemetry::trace_link_pair_type(link_span->GetContext(), {}));
  }
  if (!tracer_links.empty()) {
    for (auto &input_link : options.links) {
      tracer_links.push_back(input_link);
    }
    options.links = tracer_links;
  }

  if (!tracer_instance.start(name, std::move(options))) {
    ++get_rpc_context_mertrics_data().total_drop_trace_span_count;
    return;
  }

  if (trace_context_.trace_span) {
    trace_context_.trace_span->End();
    trace_context_.trace_span = tracer::span_ptr_type();
  }
  trace_context_.trace_span = tracer_instance.get_trace_span();

  ++get_rpc_context_mertrics_data().total_sample_trace_span_count;
}

SERVER_FRAME_API context::tracer context::make_tracer(string_view name, trace_start_option &&options) {
  tracer ret;
  setup_tracer(ret, name, std::move(options));

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

  if (allocator_ == arena) {
    return true;
  }

  if (allocator_ && !arena) {
    return true;
  }

  // 如果Arena已被使用，不能再替换。否则已有数据可能会出现生命周期问题
  if (allocator_ && allocator_->SpaceUsed() > 0) {
    bool already_stored = false;
    for (auto &alloc : used_allocators_) {
      if (alloc == allocator_) {
        already_stored = true;
        break;
      }
    }
    if (!already_stored) {
      used_allocators_.emplace_back(std::move(allocator_));
    }
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

  return ::rpc::telemetry::tracer::get_shared_noop_trace_span();
}

SERVER_FRAME_API context::trace_dynamic_policy context::get_trace_policy() noexcept {
  // 动态策略
  if (trace_dynamic_policy::kUnset == trace_context_.dynamic_policy) {
    if (should_sample_trace_span()) {
      trace_context_.dynamic_policy = trace_dynamic_policy::kRecording;
    } else {
      trace_context_.dynamic_policy = trace_dynamic_policy::kDrop;
    }
  }
  return trace_context_.dynamic_policy;
}

SERVER_FRAME_API void context::set_parent_context(rpc::context &parent, inherit_options options) noexcept {
  if (nullptr == allocator_ && options.inherit_allocator) {
    try_reuse_protobuf_arena(parent.mutable_protobuf_arena());
  }

  if (options.inherit_parent_span) {
    trace_context_.parent_span = parent.get_trace_span();
    trace_context_.dynamic_policy = parent.get_trace_policy();
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
                                                   const PROJECT_NAMESPACE_ID::config::logic_section_cfg &logic_cfg) {
  telemetry::global_service::set_current_service(app, logic_cfg.telemetry());

  if (!get_rpc_context_mertrics_data().metrics_installed) {
    get_rpc_context_mertrics_data().metrics_installed = true;

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "atframework_rpc_context",
        {"atframework_rpc_context_trace_sample_count", "", ""},
        [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
          rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
              result, get_rpc_context_mertrics_data().total_sample_trace_span_count);
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "atframework_rpc_context",
        {"atframework_rpc_context_trace_drop_count", "", ""},
        [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
          rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
              result, get_rpc_context_mertrics_data().total_drop_trace_span_count);
        });
  }
}

SERVER_FRAME_API void context::set_task_context(const task_context_data &task_ctx) noexcept {
  task_context_ = task_ctx;
}
}  // namespace rpc

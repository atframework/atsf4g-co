// Copyright 2021 atframework
// Created by owent on 2021/10/18.
//

#include "rpc/telemetry/rpc_global_service.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/util/time_util.h>

#include <google/protobuf/timestamp.pb.h>

#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/atframework.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframe/atapp.h>

#include <lock/lock_holder.h>
#include <lock/spin_rw_lock.h>
#include <log/log_wrapper.h>

#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <fstream>
#include <string>
#include <vector>

#include "opentelemetry/exporters/ostream/span_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/trace/batch_span_processor.h"
#include "opentelemetry/sdk/trace/multi_span_processor.h"
#include "opentelemetry/sdk/trace/samplers/always_off.h"
#include "opentelemetry/sdk/trace/samplers/always_on.h"
#include "opentelemetry/sdk/trace/samplers/parent.h"
#include "opentelemetry/sdk/trace/samplers/trace_id_ratio.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/provider.h"

namespace rpc {

namespace telemetry {

namespace details {

struct local_caller_info_t {
  uint64_t server_id;
  std::string server_id_string;
  std::string server_identity;
  std::string server_name;
  uint64_t server_type_id;
  std::string server_type_name;
  std::string app_version;
  std::string build_version;
  atapp::protocol::atapp_area app_area;
  atapp::protocol::atapp_metadata app_metadata;

  util::log::log_wrapper::ptr_t logger;
  ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer> tracer;
  ::opentelemetry::nostd::shared_ptr<std::ofstream> debug_ostream_exportor;
};

static std::shared_ptr<local_caller_info_t> g_global_service_cache;
static util::lock::spin_rw_lock g_global_service_lock;

class opentelemetry_internal_log_handler : public opentelemetry::sdk::common::internal_log::LogHandler {
 public:
  void Handle(opentelemetry::sdk::common::internal_log::LogLevel level, const char *file, int line, const char *msg,
              const opentelemetry::sdk::common::AttributeMap &attributes) noexcept override {
    util::log::log_wrapper::caller_info_t caller;
    caller.file_path = file;
    caller.func_name = nullptr;
    caller.line_number = static_cast<uint32_t>(line);
    caller.rotate_index = 0;
    switch (level) {
      case opentelemetry::sdk::common::internal_log::LogLevel::Error: {
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_ERROR;
        caller.level_name = "Error";
        break;
      }
      case opentelemetry::sdk::common::internal_log::LogLevel::Warning: {
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_WARNING;
        caller.level_name = "Warning";
        break;
      }
      case opentelemetry::sdk::common::internal_log::LogLevel::Info: {
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_INFO;
        caller.level_name = "Info";
        break;
      }
      case opentelemetry::sdk::common::internal_log::LogLevel::Debug: {
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_DEBUG;
        caller.level_name = "Debug";
        break;
      }
      default: {
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_DEBUG;
        caller.level_name = "Debug";
        break;
      }
    }

    ::util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
    if (!details::g_global_service_cache) {
      return;
    }

    if (!details::g_global_service_cache->logger) {
      return;
    }

    if (nullptr != msg) {
      details::g_global_service_cache->logger->format_log(caller, "{}", msg);
    }
  }
};

}  // namespace details

::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer> global_service::get_current_default_tracer() {
  ::util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  if (details::g_global_service_cache) {
    return details::g_global_service_cache->tracer;
  }

  return ::opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer>();
}

static std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> _opentelemetry_create_exporter(
    ::rpc::telemetry::details::local_caller_info_t &caller,
    const hello::config::opentelemetry_exporter_cfg &exporter_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> ret;
  ret.reserve(2);

  if (!exporter_cfg.ostream().empty()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP("stdout", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
          new opentelemetry::exporter::trace::OStreamSpanExporter(std::cout)));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP("stderr", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
          new opentelemetry::exporter::trace::OStreamSpanExporter(std::cerr)));
    } else {
      ::opentelemetry::nostd::shared_ptr<std::ofstream> fout{
          new std::ofstream(exporter_cfg.ostream().c_str(), std::ios::out | std::ios::trunc | std::ios::binary)};
      if (fout && fout->is_open()) {
        caller.debug_ostream_exportor = fout;
        ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
            new opentelemetry::exporter::trace::OStreamSpanExporter(*fout)));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc_trace() && !exporter_cfg.otlp_grpc_trace().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions options;
    options.endpoint = exporter_cfg.otlp_grpc_trace().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc_trace().insecure();
    options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc_trace().ca_file();

    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
        new opentelemetry::exporter::otlp::OtlpGrpcExporter(options)));
  }

  if (exporter_cfg.has_otlp_http_trace() && !exporter_cfg.otlp_http_trace().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpHttpExporterOptions options;
    options.url = exporter_cfg.otlp_http_trace().endpoint();
    options.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(exporter_cfg.otlp_http_trace().timeout().seconds()) +
        std::chrono::nanoseconds(exporter_cfg.otlp_http_trace().timeout().nanos()));

    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
        new opentelemetry::exporter::otlp::OtlpHttpExporter(options)));
  }

  return ret;
}

static std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> _opentelemetry_create_processor(
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> &&exporters,
    const hello::config::opentelemetry_processor_cfg &processor_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> ret;
  ret.reserve(exporters.size());
  if (processor_cfg.has_simple() && !processor_cfg.has_batch()) {
    for (auto &exporter : exporters) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>(
          new opentelemetry::sdk::trace::SimpleSpanProcessor(std::move(exporter))));
    }
    return ret;
  }

  opentelemetry::sdk::trace::BatchSpanProcessorOptions options{};
  if (processor_cfg.has_batch()) {
    options.max_export_batch_size = static_cast<size_t>(processor_cfg.batch().send_batch_max_size());
    options.max_queue_size = static_cast<size_t>(processor_cfg.batch().send_batch_size());
    options.schedule_delay_millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(processor_cfg.batch().timeout().seconds()) +
        std::chrono::nanoseconds(processor_cfg.batch().timeout().nanos()));
  }

  for (auto &exporter : exporters) {
    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>(
        new opentelemetry::sdk::trace::BatchSpanProcessor(std::move(exporter), options)));
  }
  return ret;
}

static std::unique_ptr<opentelemetry::sdk::trace::Sampler> _opentelemetry_create_sampler(
    const hello::config::opentelemetry_sampler_cfg &sampler_cfg) {
  switch (sampler_cfg.sampler_type_case()) {
    case hello::config::opentelemetry_sampler_cfg::kAlwaysOff: {
      return std::unique_ptr<opentelemetry::sdk::trace::Sampler>(new opentelemetry::sdk::trace::AlwaysOffSampler());
    }
    case hello::config::opentelemetry_sampler_cfg::kTraceIdRatio: {
      return std::unique_ptr<opentelemetry::sdk::trace::Sampler>(
          new opentelemetry::sdk::trace::ParentBasedSampler(std::unique_ptr<opentelemetry::sdk::trace::Sampler>(
              new opentelemetry::sdk::trace::TraceIdRatioBasedSampler{sampler_cfg.trace_id_ratio()})));
    }
    // case hello::config::opentelemetry_sampler_cfg::kAlwaysOn: {}
    default: {
      return std::unique_ptr<opentelemetry::sdk::trace::Sampler>(new opentelemetry::sdk::trace::AlwaysOnSampler());
    }
  }
}

static opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> _opentelemetry_create_provider(
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> &&processors,
    std::unique_ptr<opentelemetry::sdk::trace::Sampler> &&sampler, opentelemetry::sdk::resource::Resource resource) {
  auto provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
      new opentelemetry::sdk::trace::TracerProvider(std::move(processors), resource, std::move(sampler)));
  if (provider) {
    // Set the global trace provider.
    opentelemetry::trace::Provider::SetTracerProvider(provider);
  }

  return provider;
}

static void _opentelemetry_set_global_provider(
    const atapp::app &app, opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> provider,
    std::shared_ptr<details::local_caller_info_t> app_info_cache,
    const hello::config::opentelemetry_cfg &opentelemetry_cfg) {
  // Default tracer
  if (!opentelemetry_cfg.tracer_name().empty()) {
    app_info_cache->tracer = provider->GetTracer(opentelemetry_cfg.tracer_name(), app.get_app_version());
  } else if (!app.get_type_name().empty()) {
    app_info_cache->tracer = provider->GetTracer(app.get_type_name(), app.get_app_version());
  } else {
    app_info_cache->tracer = provider->GetTracer(app.get_app_name(), app.get_app_version());
  }
  app_info_cache->logger = util::log::log_wrapper::create_user_logger();

  // Logger
  ::atapp::protocol::atapp_log opentelemetry_log_conf;
  app.parse_log_configures_into(opentelemetry_log_conf,
                                std::vector<gsl::string_view>{"logic", "telemetry", "opentelemetry", "log"});
  if (app_info_cache->logger && opentelemetry_log_conf.category_size() > 0) {
    app_info_cache->logger->init(util::log::log_formatter::get_level_by_name(opentelemetry_log_conf.level().c_str()));
    app.setup_logger(*app_info_cache->logger, opentelemetry_log_conf.level(), opentelemetry_log_conf.category(0));
  }

  ::util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  // Set the global trace provider and service cache.
  opentelemetry::trace::Provider::SetTracerProvider(provider);
  if (!details::g_global_service_cache) {
    // Setup global log handle for opentelemetry for first startup
    opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogHandler(
        nostd::shared_ptr<opentelemetry::sdk::common::internal_log::LogHandler>{
            new details::opentelemetry_internal_log_handler()});
  }
  details::g_global_service_cache.swap(app_info_cache);
}

static opentelemetry::sdk::resource::ResourceAttributes _create_opentelemetry_app_resource(const atapp::app &app) {
  // @see
  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/resource/semantic_conventions/README.md

  // basic
  opentelemetry::sdk::resource::ResourceAttributes resource_values{
      {"service.instance.id", app.get_id()},  {"service.instance.name", app.get_app_name()},
      {"service.name", app.get_type_name()},  {"service.identity", app.get_app_identity()},
      {"service.type_id", app.get_type_id()}, {"service.version", server_frame_project_get_version()},
  };

  // area
  if (0 != app.get_area().zone_id()) {
    resource_values.SetAttribute("service.area.zone_id", app.get_area().zone_id());
  }
  if (!app.get_area().region().empty()) {
    resource_values.SetAttribute("service.area.region", app.get_area().region());
  }
  if (!app.get_area().district().empty()) {
    resource_values.SetAttribute("service.area.district", app.get_area().district());
  }

  // metadata
  if (!app.get_metadata().namespace_name().empty()) {
    resource_values.SetAttribute("service.namespace", app.get_metadata().namespace_name());
  }

  {
    auto iter = app.get_metadata().labels().find("env");
    if (iter != app.get_metadata().labels().end()) {
      resource_values.SetAttribute("deployment.environment", iter->second);
    }
  }

  // process
  // @see
  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/resource/semantic_conventions/process.md
  resource_values.SetAttribute("process.pid", atbus::node::get_pid());

  return resource_values;
}

void global_service::set_current_service(const atapp::app &app, const hello::config::logic_telemetry_cfg &telemetry) {
  std::shared_ptr<details::local_caller_info_t> app_info_cache = std::make_shared<details::local_caller_info_t>();
  if (!app_info_cache) {
    return;
  }

  app_info_cache->server_id = app.get_id();
  app_info_cache->server_id_string = app.convert_app_id_to_string(app.get_id());
  app_info_cache->server_identity = app.get_app_identity();
  app_info_cache->server_name = app.get_app_name();
  app_info_cache->server_type_id = app.get_type_id();
  app_info_cache->server_type_name = app.get_type_name();
  app_info_cache->app_version = app.get_app_version();
  app_info_cache->build_version = app.get_build_version();

  protobuf_copy_message(app_info_cache->app_area, app.get_area());
  protobuf_copy_message(app_info_cache->app_metadata, app.get_metadata());

  // Setup telemetry
  auto &opentelemetry_cfg = telemetry.opentelemetry();
  opentelemetry::sdk::resource::ResourceAttributes resource_values = _create_opentelemetry_app_resource(app);

  for (auto &ext_res : opentelemetry_cfg.resource()) {
    if (ext_res.second.empty()) {
      continue;
    }
    resource_values.SetAttribute(ext_res.first, ext_res.second);
  }

  auto resource = opentelemetry::sdk::resource::Resource::Create(resource_values);
  auto exporter = _opentelemetry_create_exporter(*app_info_cache, opentelemetry_cfg.exporters());
  auto sampler = _opentelemetry_create_sampler(opentelemetry_cfg.samplers());
  if (!sampler) {
    return;
  }
  auto processor = _opentelemetry_create_processor(std::move(exporter), opentelemetry_cfg.processors());
  auto provider = _opentelemetry_create_provider(std::move(processor), std::move(sampler), std::move(resource));
  if (!provider) {
    return;
  }

  _opentelemetry_set_global_provider(app, provider, app_info_cache, opentelemetry_cfg);
}

}  // namespace telemetry

}  // namespace rpc

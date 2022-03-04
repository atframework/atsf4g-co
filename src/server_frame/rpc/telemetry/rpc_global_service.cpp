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

#include <std/explicit_declare.h>

#include <atframe/atapp.h>

#include <lock/lock_holder.h>
#include <lock/spin_rw_lock.h>
#include <log/log_wrapper.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "opentelemetry/exporters/ostream/log_exporter.h"
#include "opentelemetry/exporters/ostream/span_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_exporter.h"
#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/noop.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/logs/batch_log_processor.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/simple_log_processor.h"
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

  util::log::log_wrapper::ptr_t internal_logger;

  std::shared_ptr<opentelemetry::sdk::trace::TracerContext> tracer_context;
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> default_tracer;
  opentelemetry::nostd::shared_ptr<std::ofstream> debug_tracer_ostream_exportor;
  opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider> logs_provider;
  std::shared_ptr<opentelemetry::sdk::logs::LoggerContext> logs_context;
  opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> default_logger;
  opentelemetry::nostd::shared_ptr<std::ofstream> debug_logger_ostream_exportor;
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

    std::shared_ptr<local_caller_info_t> current_service_cache;
    {
      ::util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
      current_service_cache = details::g_global_service_cache;
    }
    if (!current_service_cache) {
      return;
    }

    if (!current_service_cache->internal_logger) {
      return;
    }

    if (nullptr != msg) {
      current_service_cache->internal_logger->format_log(caller, "{}", msg);
    }
  }
};

}  // namespace details

opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer> global_service::get_current_default_tracer() {
  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  if (details::g_global_service_cache) {
    return details::g_global_service_cache->default_tracer;
  }

  return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
}

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> global_service::get_tracer(
    opentelemetry::nostd::string_view library_name, opentelemetry::nostd::string_view library_version,
    opentelemetry::nostd::string_view schema_url) {
  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  auto current_service_cache = details::g_global_service_cache;
  if (!current_service_cache) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  auto provider = opentelemetry::trace::Provider::GetTracerProvider();
  if (!provider) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  if (library_name.empty()) {
    library_name = current_service_cache->server_name;
    if (library_version.empty()) {
      library_version = current_service_cache->app_version;
    }
  }
  if (schema_url.empty()) {
    schema_url = logic_config::me()->get_cfg_telemetry().opentelemetry().logs().schema_url();
  }

  return provider->GetTracer(library_name, library_version, schema_url);
}

opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> global_service::get_current_default_logger() {
  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  if (details::g_global_service_cache) {
    return details::g_global_service_cache->default_logger;
  }

  return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
}

opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> global_service::get_logger(
    opentelemetry::nostd::string_view logger_name, opentelemetry::nostd::string_view options,
    opentelemetry::nostd::string_view library_name, opentelemetry::nostd::string_view library_version,
    opentelemetry::nostd::string_view schema_url) {
  auto current_service_cache = details::g_global_service_cache;
  if (!current_service_cache) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
  }

  if (!current_service_cache->logs_provider) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
  }

  if (library_name.empty()) {
    library_name = current_service_cache->server_name;
    if (library_version.empty()) {
      library_version = current_service_cache->app_version;
    }
  }
  if (schema_url.empty()) {
    schema_url = logic_config::me()->get_cfg_telemetry().opentelemetry().logs().schema_url();
  }

  return current_service_cache->logs_provider->GetLogger(logger_name, options, library_name, library_version,
                                                         schema_url);
}

static std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> _opentelemetry_create_trace_exporter(
    ::rpc::telemetry::details::local_caller_info_t &caller,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_exporter_cfg &exporter_cfg) {
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
        caller.debug_tracer_ostream_exportor = fout;
        ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
            new opentelemetry::exporter::trace::OStreamSpanExporter(*fout)));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc() && !exporter_cfg.otlp_grpc().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions options;
    options.endpoint = exporter_cfg.otlp_grpc().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc().insecure();
    options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc().ca_file();

    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
        new opentelemetry::exporter::otlp::OtlpGrpcExporter(options)));
  }

  if (exporter_cfg.has_otlp_http() && !exporter_cfg.otlp_http().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpHttpExporterOptions options;
    options.url = exporter_cfg.otlp_http().endpoint();
    options.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(exporter_cfg.otlp_http().timeout().seconds()) +
        std::chrono::nanoseconds(exporter_cfg.otlp_http().timeout().nanos()));

    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
        new opentelemetry::exporter::otlp::OtlpHttpExporter(options)));
  }

  return ret;
}

static std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> _opentelemetry_create_trace_processor(
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> &&exporters,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_processor_cfg &processor_cfg) {
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

static std::unique_ptr<opentelemetry::sdk::trace::Sampler> _opentelemetry_create_trace_sampler(
    const PROJECT_NAMESPACE_ID::config::opentelemetry_sampler_cfg &sampler_cfg) {
  switch (sampler_cfg.sampler_type_case()) {
    case PROJECT_NAMESPACE_ID::config::opentelemetry_sampler_cfg::kAlwaysOff: {
      return std::unique_ptr<opentelemetry::sdk::trace::Sampler>(new opentelemetry::sdk::trace::AlwaysOffSampler());
    }
    case PROJECT_NAMESPACE_ID::config::opentelemetry_sampler_cfg::kTraceIdRatio: {
      return std::unique_ptr<opentelemetry::sdk::trace::Sampler>(
          new opentelemetry::sdk::trace::ParentBasedSampler(std::unique_ptr<opentelemetry::sdk::trace::Sampler>(
              new opentelemetry::sdk::trace::TraceIdRatioBasedSampler{sampler_cfg.trace_id_ratio()})));
    }
    // case PROJECT_NAMESPACE_ID::config::opentelemetry_sampler_cfg::kAlwaysOn: {}
    default: {
      return std::unique_ptr<opentelemetry::sdk::trace::Sampler>(new opentelemetry::sdk::trace::AlwaysOnSampler());
    }
  }
}

static std::pair<opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>,
                 std::shared_ptr<opentelemetry::sdk::trace::TracerContext>>
_opentelemetry_create_trace_provider(
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> &&processors,
    std::unique_ptr<opentelemetry::sdk::trace::Sampler> &&sampler, opentelemetry::sdk::resource::Resource resource) {
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> provider;
  std::shared_ptr<opentelemetry::sdk::trace::TracerContext> context;
  context = std::make_shared<opentelemetry::sdk::trace::TracerContext>(
      std::move(processors), resource, std::move(sampler),
      std::unique_ptr<opentelemetry::sdk::trace::IdGenerator>(new opentelemetry::sdk::trace::RandomIdGenerator()));
  provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
      new opentelemetry::sdk::trace::TracerProvider(context));
  return std::make_pair(provider, context);
}

static std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogExporter>> _opentelemetry_create_logs_exporter(
    ::rpc::telemetry::details::local_caller_info_t &caller,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_exporter_cfg &exporter_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogExporter>> ret;
  ret.reserve(2);

  if (!exporter_cfg.ostream().empty()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP("stdout", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogExporter>(
          new opentelemetry::exporter::logs::OStreamLogExporter(std::cout)));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP("stderr", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogExporter>(
          new opentelemetry::exporter::logs::OStreamLogExporter(std::cerr)));
    } else {
      ::opentelemetry::nostd::shared_ptr<std::ofstream> fout{
          new std::ofstream(exporter_cfg.ostream().c_str(), std::ios::out | std::ios::trunc | std::ios::binary)};
      if (fout && fout->is_open()) {
        caller.debug_logger_ostream_exportor = fout;
        ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogExporter>(
            new opentelemetry::exporter::logs::OStreamLogExporter(*fout)));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc() && !exporter_cfg.otlp_grpc().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions options;
    options.endpoint = exporter_cfg.otlp_grpc().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc().insecure();
    options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc().ca_file();

    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogExporter>(
        new opentelemetry::exporter::otlp::OtlpGrpcLogExporter(options)));
  }

  if (exporter_cfg.has_otlp_http() && !exporter_cfg.otlp_http().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpHttpLogExporterOptions options;
    options.url = exporter_cfg.otlp_http().endpoint();
    options.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(exporter_cfg.otlp_http().timeout().seconds()) +
        std::chrono::nanoseconds(exporter_cfg.otlp_http().timeout().nanos()));

    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogExporter>(
        new opentelemetry::exporter::otlp::OtlpHttpLogExporter(options)));
  }

  return ret;
}

static std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogProcessor>> _opentelemetry_create_logs_processor(
    std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogExporter>> &&exporters,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_processor_cfg &processor_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogProcessor>> ret;
  ret.reserve(exporters.size());
  if (processor_cfg.has_simple() && !processor_cfg.has_batch()) {
    for (auto &exporter : exporters) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogProcessor>(
          new opentelemetry::sdk::logs::SimpleLogProcessor(std::move(exporter))));
    }
    return ret;
  }

  for (auto &exporter : exporters) {
    ret.emplace_back(
        std::unique_ptr<opentelemetry::sdk::logs::LogProcessor>(new opentelemetry::sdk::logs::BatchLogProcessor(
            std::move(exporter), static_cast<size_t>(processor_cfg.batch().send_batch_size()),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::seconds(processor_cfg.batch().timeout().seconds()) +
                std::chrono::nanoseconds(processor_cfg.batch().timeout().nanos())),
            static_cast<size_t>(processor_cfg.batch().send_batch_max_size()))));
  }
  return ret;
}

static std::pair<opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>,
                 std::shared_ptr<opentelemetry::sdk::logs::LoggerContext>>
_opentelemetry_create_logs_provider(std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogProcessor>> &&processors,
                                    opentelemetry::sdk::resource::Resource resource) {
  opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider> provider;
  std::shared_ptr<opentelemetry::sdk::logs::LoggerContext> context;

  context = std::make_shared<opentelemetry::sdk::logs::LoggerContext>(std::move(processors), std::move(resource));
  provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>(
      new opentelemetry::sdk::logs::LoggerProvider(context));
  return std::make_pair(provider, context);
}

static void _opentelemetry_cleanup_local_caller_info_t(
    std::shared_ptr<details::local_caller_info_t> app_info_cache,
    EXPLICIT_UNUSED_ATTR opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> tracer_provider) {
  if (app_info_cache) {
    // Context must be destroy bnefore logger
    if (app_info_cache->logs_context) {
      app_info_cache->logs_context->ForceFlush();
      app_info_cache->logs_context.reset();
    }
    app_info_cache->default_logger = opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();

    // Context must be destroy bnefore tracer
    if (app_info_cache->tracer_context) {
      app_info_cache->tracer_context->ForceFlush();
      app_info_cache->tracer_context.reset();
    }
    app_info_cache->default_tracer = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }
}

static void _opentelemetry_cleanup_global_provider(atapp::app &app) {
  std::shared_ptr<details::local_caller_info_t> current_service_cache;
  {
    ::util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
    current_service_cache = details::g_global_service_cache;
    details::g_global_service_cache.reset();
  }

  // Hold provider in case of start another span again
  auto provider = opentelemetry::trace::Provider::GetTracerProvider();
  opentelemetry::trace::Provider::SetTracerProvider(
      opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
          new opentelemetry::trace::NoopTracerProvider()));
  if (current_service_cache) {
    if (current_service_cache->default_tracer) {
      current_service_cache->default_tracer->Close(
          std::chrono::seconds(app.get_origin_configure().timer().stop_timeout().seconds()) +
          std::chrono::nanoseconds(app.get_origin_configure().timer().stop_timeout().nanos()));
      opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer> swap_out;
      current_service_cache->default_tracer.swap(swap_out);
    }
  }

  _opentelemetry_cleanup_local_caller_info_t(std::move(current_service_cache), std::move(provider));
}

static void _opentelemetry_set_global_provider(
    atapp::app &app, std::shared_ptr<details::local_caller_info_t> app_info_cache,
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> tracer_provider,
    std::shared_ptr<opentelemetry::sdk::trace::TracerContext> tracer_context,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_tracer_cfg &tracer_config,
    opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider> logs_provider,
    std::shared_ptr<opentelemetry::sdk::logs::LoggerContext> logs_context,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_cfg &logs_config) {
  // Default tracer
  if (!tracer_provider) {
    tracer_provider = opentelemetry::trace::Provider::GetTracerProvider();
  }
  app_info_cache->tracer_context = tracer_context;
  if (!tracer_config.default_name().empty()) {
    app_info_cache->default_tracer =
        tracer_provider->GetTracer(tracer_config.default_name(), app.get_app_version(), tracer_config.schema_url());
  } else if (!app.get_type_name().empty()) {
    app_info_cache->default_tracer =
        tracer_provider->GetTracer(app.get_type_name(), app.get_app_version(), tracer_config.schema_url());
  } else {
    app_info_cache->default_tracer =
        tracer_provider->GetTracer(app.get_app_name(), app.get_app_version(), tracer_config.schema_url());
  }

  // Default logger
  if (!logs_provider) {
    logs_provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>{
        new opentelemetry::logs::NoopLoggerProvider()};
  }
  app_info_cache->logs_provider = logs_provider;
  app_info_cache->logs_context = logs_context;
  if (!logs_config.default_name().empty()) {
    app_info_cache->default_logger = logs_provider->GetLogger(logs_config.default_name(), "", app.get_app_name(),
                                                              app.get_app_version(), logs_config.schema_url());
  } else if (!app.get_type_name().empty()) {
    app_info_cache->default_logger = logs_provider->GetLogger(app.get_type_name(), "", app.get_app_name(),
                                                              app.get_app_version(), logs_config.schema_url());
  } else {
    app_info_cache->default_logger = logs_provider->GetLogger(app.get_app_name(), "", app.get_app_name(),
                                                              app.get_app_version(), logs_config.schema_url());
  }

  // Internal Logger
  app_info_cache->internal_logger = util::log::log_wrapper::create_user_logger();
  ::atapp::protocol::atapp_log opentelemetry_log_conf;
  app.parse_log_configures_into(opentelemetry_log_conf,
                                std::vector<gsl::string_view>{"logic", "telemetry", "opentelemetry", "app_log"},
                                "ATAPP_LOGIC_TELEMETRY_OPENTELEMETRY_LOG");
  if (app_info_cache->internal_logger && opentelemetry_log_conf.category_size() > 0) {
    app_info_cache->internal_logger->init(
        util::log::log_formatter::get_level_by_name(opentelemetry_log_conf.level().c_str()));
    app.setup_logger(*app_info_cache->internal_logger, opentelemetry_log_conf.level(),
                     opentelemetry_log_conf.category(0));
  }

  ::util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  // Set the global trace provider and service cache.
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> old_tracer_provider =
      opentelemetry::trace::Provider::GetTracerProvider();
  opentelemetry::trace::Provider::SetTracerProvider(tracer_provider);
  if (!details::g_global_service_cache) {
    // Setup global log handle for opentelemetry for first startup
    opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogHandler(
        opentelemetry::nostd::shared_ptr<opentelemetry::sdk::common::internal_log::LogHandler>{
            new details::opentelemetry_internal_log_handler()});
    app.add_evt_on_finally(_opentelemetry_cleanup_global_provider);
  }
  details::g_global_service_cache.swap(app_info_cache);

  // Shutdown in another thread to avoid blocking
  do {
    if (!app_info_cache) {
      break;
    }
    if (app_info_cache->logs_context == details::g_global_service_cache->logs_context) {
      app_info_cache->logs_context.reset();
    }
    if (app_info_cache->tracer_context == details::g_global_service_cache->tracer_context) {
      app_info_cache->tracer_context.reset();
    }
    std::thread cleanup_thread([app_info_cache, old_tracer_provider]() {
      _opentelemetry_cleanup_local_caller_info_t(std::move(app_info_cache), std::move(old_tracer_provider));
    });
    cleanup_thread.detach();
  } while (false);
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

void global_service::set_current_service(atapp::app &app,
                                         const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &telemetry) {
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

  opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> tracer_provider;
  std::shared_ptr<opentelemetry::sdk::trace::TracerContext> tracer_context;
  if (opentelemetry_cfg.has_tracer()) {
    auto exporter = _opentelemetry_create_trace_exporter(*app_info_cache, opentelemetry_cfg.tracer().exporters());
    auto sampler = _opentelemetry_create_trace_sampler(opentelemetry_cfg.tracer().samplers());
    if (!sampler) {
      return;
    }
    auto processor =
        _opentelemetry_create_trace_processor(std::move(exporter), opentelemetry_cfg.tracer().processors());

    opentelemetry::sdk::resource::ResourceAttributes trace_resource_values = resource_values;
    for (auto &ext_res : opentelemetry_cfg.tracer().resource()) {
      if (ext_res.second.empty()) {
        continue;
      }
      trace_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }
    std::tie(tracer_provider, tracer_context) =
        _opentelemetry_create_trace_provider(std::move(processor), std::move(sampler),
                                             opentelemetry::sdk::resource::Resource::Create(trace_resource_values));
  }

  opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider> logs_provider;
  std::shared_ptr<opentelemetry::sdk::logs::LoggerContext> logs_context;
  if (opentelemetry_cfg.has_logs()) {
    auto exporter = _opentelemetry_create_logs_exporter(*app_info_cache, opentelemetry_cfg.logs().exporters());
    auto processor = _opentelemetry_create_logs_processor(std::move(exporter), opentelemetry_cfg.logs().processors());

    opentelemetry::sdk::resource::ResourceAttributes logs_resource_values = resource_values;
    for (auto &ext_res : opentelemetry_cfg.logs().resource()) {
      if (ext_res.second.empty()) {
        continue;
      }
      logs_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }
    std::tie(logs_provider, logs_context) = _opentelemetry_create_logs_provider(
        std::move(processor), opentelemetry::sdk::resource::Resource::Create(logs_resource_values));
  }
  _opentelemetry_set_global_provider(app, app_info_cache, tracer_provider, tracer_context, opentelemetry_cfg.tracer(),
                                     logs_provider, logs_context, opentelemetry_cfg.logs());
}

}  // namespace telemetry

}  // namespace rpc

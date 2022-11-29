// Copyright 2022 atframework
// Created by owent on 2021/10/18.
//

#include "rpc/telemetry/rpc_global_service.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/util/time_util.h>

#include <google/protobuf/timestamp.pb.h>

#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/atframework.pb.h>

#include <opentelemetry/exporters/ostream/metric_exporter.h>
#include <opentelemetry/exporters/ostream/span_exporter.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter.h>
#include <opentelemetry/exporters/prometheus/exporter.h>
#include <opentelemetry/logs/logger_provider.h>
#include <opentelemetry/logs/noop.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/common/global_log_handler.h>
#include <opentelemetry/sdk/logs/logger_provider.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h>
#include <opentelemetry/sdk/metrics/meter.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/trace/batch_span_processor.h>
#include <opentelemetry/sdk/trace/multi_span_processor.h>
#include <opentelemetry/sdk/trace/samplers/always_off.h>
#include <opentelemetry/sdk/trace/samplers/always_on.h>
#include <opentelemetry/sdk/trace/samplers/parent.h>
#include <opentelemetry/sdk/trace/samplers/trace_id_ratio.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/provider.h>

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1008
#  include <opentelemetry/exporters/ostream/log_record_exporter.h>
#  include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h>
#  include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#  include <opentelemetry/sdk/logs/batch_log_record_processor.h>
#  include <opentelemetry/sdk/logs/simple_log_record_processor.h>
#else
#  include <opentelemetry/exporters/ostream/log_exporter.h>
#  include <opentelemetry/exporters/otlp/otlp_grpc_log_exporter_factory.h>
#  include <opentelemetry/exporters/otlp/otlp_http_log_exporter_factory.h>
#  include <opentelemetry/sdk/logs/batch_log_processor.h>
#  include <opentelemetry/sdk/logs/simple_log_processor.h>
#endif

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
#include <unordered_map>
#include <vector>

#include "rpc/telemetry/exporter/prometheus_push_exporter.h"

namespace rpc {

namespace telemetry {

namespace details {

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1008
template <class U>
using log_unique_ptr = opentelemetry::nostd::unique_ptr<U>;

using LogRecordExporter = opentelemetry::sdk::logs::LogRecordExporter;
using LogRecordProcessor = opentelemetry::sdk::logs::LogRecordProcessor;
using SimpleLogRecordProcessor = opentelemetry::sdk::logs::SimpleLogRecordProcessor;
using BatchLogRecordProcessor = opentelemetry::sdk::logs::BatchLogRecordProcessor;
using OStreamLogRecordExporter = opentelemetry::exporter::logs::OStreamLogRecordExporter;
using OtlpHttpLogRecordExporterOptions = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions;
using OtlpGrpcLogRecordExporterFactory = opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterFactory;
using OtlpHttpLogRecordExporterFactory = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory;

#else
template <class U>
using log_unique_ptr = std::unique_ptr<U>;

using LogRecordExporter = opentelemetry::sdk::logs::LogExporter;
using LogRecordProcessor = opentelemetry::sdk::logs::LogProcessor;
using SimpleLogRecordProcessor = opentelemetry::sdk::logs::SimpleLogProcessor;
using BatchLogRecordProcessor = opentelemetry::sdk::logs::BatchLogProcessor;
using OStreamLogRecordExporter = opentelemetry::exporter::logs::OStreamLogExporter;
using OtlpHttpLogRecordExporterOptions = opentelemetry::exporter::otlp::OtlpHttpLogExporterOptions;
using OtlpGrpcLogRecordExporterFactory = opentelemetry::exporter::otlp::OtlpGrpcLogExporterFactory;
using OtlpHttpLogRecordExporterFactory = opentelemetry::exporter::otlp::OtlpHttpLogExporterFactory;

#endif

template <class ElementType, class DeleterType>
inline static opentelemetry::nostd::shared_ptr<ElementType> share_smart_ptr(
    std::unique_ptr<ElementType, DeleterType> &&input) {
  return opentelemetry::nostd::shared_ptr<ElementType>{input.release()};
}

template <class ElementType>
inline static opentelemetry::nostd::shared_ptr<ElementType> share_smart_ptr(
    opentelemetry::nostd::shared_ptr<ElementType> &&input) {
  return input;
}

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
template <class ElementType>
inline static opentelemetry::nostd::shared_ptr<ElementType> share_smart_ptr(
    opentelemetry::nostd::unique_ptr<ElementType> &&input) {
  return opentelemetry::nostd::shared_ptr<ElementType>{input.release()};
}
#endif

struct local_meter_info_t {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter;

  // sync instruments
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>>
      sync_counter_uint64;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>>
      sync_histogram_uint64;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>>
      sync_up_down_counter_int64;
#else
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<long>>>
      sync_counter_long;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<long>>>
      sync_histogram_long;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<long>>>
      sync_up_down_counter_long;
#endif
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>>
      sync_counter_double;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>>
      sync_histogram_double;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>>
      sync_up_down_counter_double;

  // async instruments
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>>
      async_instruments;
};

template <class ProviderType>
struct local_provider_handle_t {
  using shutdown_callback_t = std::function<void(const opentelemetry::nostd::shared_ptr<ProviderType> &)>;
  opentelemetry::nostd::shared_ptr<ProviderType> provider;
  shutdown_callback_t shutdown_callback;

  inline void reset_shutdown_callback() { shutdown_callback = shutdown_callback_t(); }

  inline void reset() {
    provider = opentelemetry::nostd::shared_ptr<ProviderType>();
    reset_shutdown_callback();
  }
};

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

  opentelemetry::sdk::common::AttributeMap common_owned_attributes;
  std::unordered_map<std::string, opentelemetry::common::AttributeValue> common_attributes;
  std::unordered_map<std::string, opentelemetry::common::AttributeValue> metrics_attributes;

  local_provider_handle_t<opentelemetry::trace::TracerProvider> tracer_handle;
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> default_tracer;
  opentelemetry::nostd::shared_ptr<std::ofstream> debug_tracer_ostream_exportor;

  local_provider_handle_t<opentelemetry::metrics::MeterProvider> metrics_handle;
  std::shared_ptr<local_meter_info_t> default_metrics_meter;
  std::unordered_map<std::string, std::shared_ptr<local_meter_info_t>> metrics_meters;
  opentelemetry::nostd::shared_ptr<std::ofstream> debug_metrics_ostream_exportor;

  local_provider_handle_t<opentelemetry::logs::LoggerProvider> logs_handle;
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

template <class TValue>
static TValue optimize_search_in_hash_map(std::unordered_map<std::string, TValue> &container,
                                          const opentelemetry::nostd::string_view &key) {
  if (container.size() < 16) {
    for (auto &element : container) {
      if (element.first == key) {
        return element.second;
      }
    }
  } else {
    auto iter = container.find(static_cast<std::string>(key));
    if (iter != container.end()) {
      return iter->second;
    }
  }

  return TValue();
}

static std::shared_ptr<local_meter_info_t> get_meter_info(const opentelemetry::nostd::string_view &meter_name) {
  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{g_global_service_lock};
  if (!g_global_service_cache) {
    return nullptr;
  }

  if (!g_global_service_cache->metrics_handle.provider) {
    return nullptr;
  }

  if (meter_name.empty() ||
      meter_name == logic_config::me()->get_logic().telemetry().opentelemetry().metrics().default_name()) {
    return g_global_service_cache->default_metrics_meter;
  }

  auto ret = optimize_search_in_hash_map(g_global_service_cache->metrics_meters, meter_name);
  if (ret) {
    return ret;
  }

  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter =
      g_global_service_cache->metrics_handle.provider->GetMeter(
          meter_name, g_global_service_cache->app_version,
          logic_config::me()->get_logic().telemetry().opentelemetry().metrics().schema_url());
  if (!meter) {
    return nullptr;
  }

  ret = std::make_shared<local_meter_info_t>();
  if (!ret) {
    return ret;
  }
  ret->meter = std::move(meter);
  g_global_service_cache->metrics_meters[static_cast<std::string>(meter_name)] = ret;

  return ret;
}

static opentelemetry::common::AttributeValue rebuild_attributes_map_value(
    const opentelemetry::sdk::common::OwnedAttributeValue &value) {
  if (opentelemetry::nostd::holds_alternative<bool>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<bool>(value)};
  } else if (opentelemetry::nostd::holds_alternative<int32_t>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<int32_t>(value)};
  } else if (opentelemetry::nostd::holds_alternative<int64_t>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<int64_t>(value)};
  } else if (opentelemetry::nostd::holds_alternative<uint32_t>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<uint32_t>(value)};
  } else if (opentelemetry::nostd::holds_alternative<uint64_t>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<uint64_t>(value)};
  } else if (opentelemetry::nostd::holds_alternative<double>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<double>(value)};
  } else if (opentelemetry::nostd::holds_alternative<std::string>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<std::string>(value)};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<bool>>(value)) {
    // 暂无低开销解决方案，目前公共属性中没有数组类型，故而不处理所有的数组类型也是没有问题的
    // 参见 https://github.com/open-telemetry/opentelemetry-cpp/pull/1154 里的讨论
    return opentelemetry::common::AttributeValue{};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<int32_t>>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<std::vector<int32_t>>(value)};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<uint32_t>>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<std::vector<uint32_t>>(value)};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<int64_t>>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<std::vector<int64_t>>(value)};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<uint64_t>>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<std::vector<uint64_t>>(value)};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<double>>(value)) {
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::get<std::vector<double>>(value)};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<std::string>>(value)) {
    // 暂无低开销解决方案，目前公共属性中没有数组类型，故而不处理所有的数组类型也是没有问题的
    // 参见 https://github.com/open-telemetry/opentelemetry-cpp/pull/1154 里的讨论
    return opentelemetry::common::AttributeValue{};
  }

  return opentelemetry::common::AttributeValue{};
}

static void rebuild_attributes_map(const opentelemetry::sdk::common::AttributeMap &src,
                                   std::unordered_map<std::string, opentelemetry::common::AttributeValue> &dst) {
  dst.clear();
  dst.reserve(src.size());
  for (auto &kv : src) {
    dst[kv.first] = rebuild_attributes_map_value(kv.second);
  }
}

}  // namespace details

const opentelemetry::sdk::common::AttributeMap &global_service::get_common_owned_attributes() {
  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  if (details::g_global_service_cache) {
    return details::g_global_service_cache->common_owned_attributes;
  }

  static opentelemetry::sdk::common::AttributeMap empty;
  return empty;
}

const std::unordered_map<std::string, opentelemetry::common::AttributeValue> &global_service::get_common_attributes() {
  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  if (details::g_global_service_cache) {
    return details::g_global_service_cache->common_attributes;
  }

  static std::unordered_map<std::string, opentelemetry::common::AttributeValue> empty;
  return empty;
}

const std::unordered_map<std::string, opentelemetry::common::AttributeValue> &global_service::get_metrics_labels() {
  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{details::g_global_service_lock};
  if (details::g_global_service_cache) {
    return details::g_global_service_cache->metrics_attributes;
  }

  static std::unordered_map<std::string, opentelemetry::common::AttributeValue> empty;
  return empty;
}

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

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>
global_service::mutable_metrics_counter_uint64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key) {
#else
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<long>> global_service::mutable_metrics_counter_long(
    opentelemetry::nostd::string_view meter_name, meter_instrument_key key) {
#endif
  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>();
#else
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<long>>();
#endif
  }

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  auto ret = details::optimize_search_in_hash_map(meter_info->sync_counter_uint64, key.name);
  if (ret) {
    return ret;
  }
  ret = details::share_smart_ptr(meter_info->meter->CreateUInt64Counter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_counter_uint64[static_cast<std::string>(key.name)] = ret;
  }
#else
  auto ret = details::optimize_search_in_hash_map(meter_info->sync_counter_long, key.name);
  if (ret) {
    return ret;
  }
  ret = details::share_smart_ptr(meter_info->meter->CreateLongCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_counter_long[static_cast<std::string>(key.name)] = ret;
  }
#endif

  return ret;
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>
global_service::mutable_metrics_counter_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key) {
  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>();
  }

  auto ret = details::optimize_search_in_hash_map(meter_info->sync_counter_double, key.name);
  if (ret) {
    return ret;
  }

  ret = details::share_smart_ptr(meter_info->meter->CreateDoubleCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_counter_double[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>
global_service::mutable_metrics_histogram_uint64(opentelemetry::nostd::string_view meter_name,
                                                 meter_instrument_key key) {
#else
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<long>>
global_service::mutable_metrics_histogram_long(opentelemetry::nostd::string_view meter_name, meter_instrument_key key) {
#endif
  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>();
#else
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<long>>();
#endif
  }

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  auto ret = details::optimize_search_in_hash_map(meter_info->sync_histogram_uint64, key.name);
  if (ret) {
    return ret;
  }
  ret = details::share_smart_ptr(meter_info->meter->CreateUInt64Histogram(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_histogram_uint64[static_cast<std::string>(key.name)] = ret;
  }
#else
  auto ret = details::optimize_search_in_hash_map(meter_info->sync_histogram_long, key.name);
  if (ret) {
    return ret;
  }
  ret = details::share_smart_ptr(meter_info->meter->CreateLongHistogram(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_histogram_long[static_cast<std::string>(key.name)] = ret;
  }
#endif

  return ret;
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>
global_service::mutable_metrics_histogram_double(opentelemetry::nostd::string_view meter_name,
                                                 meter_instrument_key key) {
  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>();
  }

  auto ret = details::optimize_search_in_hash_map(meter_info->sync_histogram_double, key.name);
  if (ret) {
    return ret;
  }

  ret = details::share_smart_ptr(meter_info->meter->CreateDoubleHistogram(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_histogram_double[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>
global_service::mutable_metrics_up_down_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                      meter_instrument_key key) {
#else
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<long>>
global_service::mutable_metrics_up_down_counter_long(opentelemetry::nostd::string_view meter_name,
                                                     meter_instrument_key key) {
#endif
  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>();
#else
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<long>>();
#endif
  }

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  auto ret = details::optimize_search_in_hash_map(meter_info->sync_up_down_counter_int64, key.name);
  if (ret) {
    return ret;
  }
  ret = details::share_smart_ptr(meter_info->meter->CreateInt64UpDownCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_up_down_counter_int64[static_cast<std::string>(key.name)] = ret;
  }
#else
  auto ret = details::optimize_search_in_hash_map(meter_info->sync_up_down_counter_long, key.name);
  if (ret) {
    return ret;
  }
  ret = details::share_smart_ptr(meter_info->meter->CreateLongUpDownCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_up_down_counter_long[static_cast<std::string>(key.name)] = ret;
  }
#endif

  return ret;
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>
global_service::mutable_metrics_up_down_counter_double(opentelemetry::nostd::string_view meter_name,
                                                       meter_instrument_key key) {
  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>();
  }

  auto ret = details::optimize_search_in_hash_map(meter_info->sync_up_down_counter_double, key.name);
  if (ret) {
    return ret;
  }

  ret = details::share_smart_ptr(meter_info->meter->CreateDoubleUpDownCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->sync_up_down_counter_double[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> global_service::get_metrics_observable(
    opentelemetry::nostd::string_view meter_name, meter_instrument_key key) {
  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  return details::optimize_search_in_hash_map(meter_info->async_instruments, key.name);
}

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                         meter_instrument_key key) {
#else
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_counter_long(opentelemetry::nostd::string_view meter_name,
                                                        meter_instrument_key key) {
#endif
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key);
  if (ret) {
    return ret;
  }

  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  ret = details::share_smart_ptr(meter_info->meter->CreateInt64ObservableCounter(key.name, key.description, key.unit));
#else
  ret = details::share_smart_ptr(meter_info->meter->CreateLongObservableCounter(key.name, key.description, key.unit));
#endif
  if (ret) {
    meter_info->async_instruments[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_counter_double(opentelemetry::nostd::string_view meter_name,
                                                          meter_instrument_key key) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key);
  if (ret) {
    return ret;
  }

  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  ret = details::share_smart_ptr(meter_info->meter->CreateDoubleObservableCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_gauge_int64(opentelemetry::nostd::string_view meter_name,
                                                       meter_instrument_key key) {
#else
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_gauge_long(opentelemetry::nostd::string_view meter_name,
                                                      meter_instrument_key key) {
#endif
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key);
  if (ret) {
    return ret;
  }

  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  ret = details::share_smart_ptr(meter_info->meter->CreateInt64ObservableGauge(key.name, key.description, key.unit));
#else
  ret = details::share_smart_ptr(meter_info->meter->CreateLongObservableGauge(key.name, key.description, key.unit));
#endif
  if (ret) {
    meter_info->async_instruments[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_gauge_double(opentelemetry::nostd::string_view meter_name,
                                                        meter_instrument_key key) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key);
  if (ret) {
    return ret;
  }

  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  ret = details::share_smart_ptr(meter_info->meter->CreateDoubleObservableGauge(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_up_down_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                                 meter_instrument_key key) {
#else
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_up_down_counter_long(opentelemetry::nostd::string_view meter_name,
                                                                meter_instrument_key key) {
#endif
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key);
  if (ret) {
    return ret;
  }

  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
  ret = details::share_smart_ptr(
      meter_info->meter->CreateInt64ObservableUpDownCounter(key.name, key.description, key.unit));
#else
  ret = details::share_smart_ptr(
      meter_info->meter->CreateLongObservableUpDownCounter(key.name, key.description, key.unit));
#endif
  if (ret) {
    meter_info->async_instruments[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
}

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_up_down_counter_double(opentelemetry::nostd::string_view meter_name,
                                                                  meter_instrument_key key) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key);
  if (ret) {
    return ret;
  }

  auto meter_info = details::get_meter_info(meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  ret = details::share_smart_ptr(
      meter_info->meter->CreateDoubleObservableUpDownCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[static_cast<std::string>(key.name)] = ret;
  }

  return ret;
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

  if (!current_service_cache->logs_handle.provider) {
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

  return current_service_cache->logs_handle.provider->GetLogger(logger_name, options, library_name, library_version,
                                                                schema_url);
}

namespace {
static std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> _opentelemetry_create_trace_exporter(
    ::rpc::telemetry::details::local_caller_info_t &caller,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_trace_exporter_cfg &exporter_cfg) {
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
    if (exporter_cfg.otlp_grpc().timeout().seconds() > 0 || exporter_cfg.otlp_grpc().timeout().nanos() > 0) {
      options.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::seconds(exporter_cfg.otlp_grpc().timeout().seconds()) +
          std::chrono::nanoseconds(exporter_cfg.otlp_grpc().timeout().nanos()));
    }
    for (auto &header : exporter_cfg.otlp_grpc().headers()) {
      if (header.key().empty()) {
        continue;
      }
      options.metadata.emplace(opentelemetry::exporter::otlp::OtlpHeaders::value_type(header.key(), header.value()));
    }

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(options));
  }

  if (exporter_cfg.has_otlp_http() && !exporter_cfg.otlp_http().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpHttpExporterOptions options;
    options.url = exporter_cfg.otlp_http().endpoint();
    options.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds(exporter_cfg.otlp_http().timeout().seconds()) +
        std::chrono::nanoseconds(exporter_cfg.otlp_http().timeout().nanos()));
    for (auto &header : exporter_cfg.otlp_http().headers()) {
      if (header.key().empty()) {
        continue;
      }

      options.http_headers.emplace(
          opentelemetry::exporter::otlp::OtlpHeaders::value_type(header.key(), header.value()));
    }
    options.max_concurrent_requests = exporter_cfg.otlp_http().max_concurrent_requests();
    options.max_requests_per_connection = exporter_cfg.otlp_http().max_requests_per_connection();

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(options));
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

static details::local_provider_handle_t<opentelemetry::trace::TracerProvider> _opentelemetry_create_trace_provider(
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> &&processors,
    std::unique_ptr<opentelemetry::sdk::trace::Sampler> &&sampler, opentelemetry::sdk::resource::Resource resource) {
  details::local_provider_handle_t<opentelemetry::trace::TracerProvider> ret;
  std::shared_ptr<opentelemetry::sdk::trace::TracerContext> context;
  context = std::make_shared<opentelemetry::sdk::trace::TracerContext>(
      std::move(processors), resource, std::move(sampler),
      std::unique_ptr<opentelemetry::sdk::trace::IdGenerator>(new opentelemetry::sdk::trace::RandomIdGenerator()));
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
      new opentelemetry::sdk::trace::TracerProvider(context));
  ret.shutdown_callback = [](const opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> &provider) {
    if (!provider) {
      return;
    }

    static_cast<opentelemetry::sdk::trace::TracerProvider *>(provider.get())->Shutdown();
  };
  return ret;
}

#if (OPENTELEMTRY_CPP_MAJOR_VERSION * 1000 + OPENTELEMTRY_CPP_MINOR_VERSION) >= 1007
using PushMetricExporter = opentelemetry::sdk::metrics::PushMetricExporter;
#else
using PushMetricExporter = opentelemetry::sdk::metrics::MetricExporter;
#endif
static std::vector<std::unique_ptr<PushMetricExporter>> _opentelemetry_create_metrics_exporter(
    ::rpc::telemetry::details::local_caller_info_t &caller,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_exporter_cfg &exporter_cfg) {
  std::vector<std::unique_ptr<PushMetricExporter>> ret;
  ret.reserve(2);

  if (!exporter_cfg.ostream().empty()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP("stdout", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(
          std::unique_ptr<PushMetricExporter>(new opentelemetry::exporter::metrics::OStreamMetricExporter(std::cout)));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP("stderr", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(
          std::unique_ptr<PushMetricExporter>(new opentelemetry::exporter::metrics::OStreamMetricExporter(std::cerr)));
    } else {
      ::opentelemetry::nostd::shared_ptr<std::ofstream> fout{
          new std::ofstream(exporter_cfg.ostream().c_str(), std::ios::out | std::ios::trunc | std::ios::binary)};
      if (fout && fout->is_open()) {
        caller.debug_metrics_ostream_exportor = fout;
        ret.emplace_back(
            std::unique_ptr<PushMetricExporter>(new opentelemetry::exporter::metrics::OStreamMetricExporter(*fout)));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc() && !exporter_cfg.otlp_grpc().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpGrpcMetricExporterOptions options;
    options.endpoint = exporter_cfg.otlp_grpc().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc().insecure();
    options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc().ca_file();
    if (exporter_cfg.otlp_grpc().timeout().seconds() > 0 || exporter_cfg.otlp_grpc().timeout().nanos() > 0) {
      options.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::seconds(exporter_cfg.otlp_grpc().timeout().seconds()) +
          std::chrono::nanoseconds(exporter_cfg.otlp_grpc().timeout().nanos()));
    }
    for (auto &header : exporter_cfg.otlp_grpc().headers()) {
      if (header.key().empty()) {
        continue;
      }

      options.metadata.emplace(opentelemetry::exporter::otlp::OtlpHeaders::value_type(header.key(), header.value()));
    }
    options.aggregation_temporality = opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;

    ret.emplace_back(
        std::unique_ptr<PushMetricExporter>(new opentelemetry::exporter::otlp::OtlpGrpcMetricExporter(options)));
  }

  if (exporter_cfg.has_otlp_http() && !exporter_cfg.otlp_http().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions options;
    options.url = exporter_cfg.otlp_http().endpoint();
    options.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds(exporter_cfg.otlp_http().timeout().seconds()) +
        std::chrono::nanoseconds(exporter_cfg.otlp_http().timeout().nanos()));
    for (auto &header : exporter_cfg.otlp_http().headers()) {
      if (header.key().empty()) {
        continue;
      }

      options.http_headers.emplace(
          opentelemetry::exporter::otlp::OtlpHeaders::value_type(header.key(), header.value()));
    }
    options.aggregation_temporality = opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;

    options.max_concurrent_requests = exporter_cfg.otlp_http().max_concurrent_requests();
    options.max_requests_per_connection = exporter_cfg.otlp_http().max_requests_per_connection();

    ret.emplace_back(
        std::unique_ptr<PushMetricExporter>(new opentelemetry::exporter::otlp::OtlpHttpMetricExporter(options)));
  }

  if (exporter_cfg.has_prometheus_pull() && !exporter_cfg.prometheus_pull().url().empty()) {
    opentelemetry::exporter::metrics::PrometheusExporterOptions options;
    options.url = exporter_cfg.prometheus_pull().url();
    ret.emplace_back(
        std::unique_ptr<PushMetricExporter>(new opentelemetry::exporter::metrics::PrometheusExporter(options)));
  }

  if (exporter_cfg.has_prometheus_push() && !exporter_cfg.prometheus_push().host().empty() &&
      !exporter_cfg.prometheus_push().port().empty() && !exporter_cfg.prometheus_push().jobname().empty()) {
    exporter::metrics::PrometheusPushExporterOptions options;
    options.host = exporter_cfg.prometheus_push().host();
    options.port = exporter_cfg.prometheus_push().port();
    options.jobname = exporter_cfg.prometheus_push().jobname();
    for (auto &kv : exporter_cfg.prometheus_push().labels()) {
      options.labels[kv.first] = kv.second;
    }
    options.username = exporter_cfg.prometheus_push().username();
    options.password = exporter_cfg.prometheus_push().password();

    ret.emplace_back(std::unique_ptr<PushMetricExporter>(new exporter::metrics::PrometheusPushExporter(options)));
  }

  return ret;
}

static std::vector<std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>> _opentelemetry_create_metrics_reader(
    std::vector<std::unique_ptr<PushMetricExporter>> &&exporters,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_reader_cfg &reader_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>> ret;
  opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions options;
  if (exporters.empty()) {
    return ret;
  }

  if (reader_cfg.export_interval().seconds() > 0 || reader_cfg.export_interval().nanos() > 0) {
    options.export_interval_millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(reader_cfg.export_interval().seconds()) +
        std::chrono::nanoseconds(reader_cfg.export_interval().nanos()));
  }

  if (reader_cfg.export_timeout().seconds() > 0 || reader_cfg.export_timeout().nanos() > 0) {
    options.export_timeout_millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(reader_cfg.export_timeout().seconds()) +
        std::chrono::nanoseconds(reader_cfg.export_timeout().nanos()));
  }

  ret.reserve(exporters.size());
  for (auto &exporter : exporters) {
    ret.emplace_back(std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>(
        new opentelemetry::sdk::metrics::PeriodicExportingMetricReader(std::move(exporter), options)));
  }

  return ret;
}

static details::local_provider_handle_t<opentelemetry::metrics::MeterProvider> _opentelemetry_create_metrics_provider(
    std::vector<std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>> &&readers,
    const opentelemetry::sdk::resource::ResourceAttributes &metrics_resource_values) {
  details::local_provider_handle_t<opentelemetry::metrics::MeterProvider> ret;
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>(
      new opentelemetry::sdk::metrics::MeterProvider(
          std::unique_ptr<opentelemetry::sdk::metrics::ViewRegistry>(new opentelemetry::sdk::metrics::ViewRegistry()),
          opentelemetry::sdk::resource::Resource::Create(metrics_resource_values)));

  if (ret.provider) {
    for (auto &reader : readers) {
      static_cast<opentelemetry::sdk::metrics::MeterProvider *>(ret.provider.get())->AddMetricReader(std::move(reader));
    }
  }

  ret.shutdown_callback = [](const opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider> &provider) {
    if (!provider) {
      return;
    }

    static_cast<opentelemetry::sdk::metrics::MeterProvider *>(provider.get())->Shutdown();
  };
  return ret;
}

static std::vector<details::log_unique_ptr<details::LogRecordExporter>> _opentelemetry_create_logs_exporter(
    ::rpc::telemetry::details::local_caller_info_t &caller,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_exporter_cfg &exporter_cfg) {
  std::vector<details::log_unique_ptr<details::LogRecordExporter>> ret;
  ret.reserve(2);

  if (!exporter_cfg.ostream().empty()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP("stdout", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(
          details::log_unique_ptr<details::LogRecordExporter>(new details::OStreamLogRecordExporter(std::cout)));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP("stderr", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(
          details::log_unique_ptr<details::LogRecordExporter>(new details::OStreamLogRecordExporter(std::cerr)));
    } else {
      ::opentelemetry::nostd::shared_ptr<std::ofstream> fout{
          new std::ofstream(exporter_cfg.ostream().c_str(), std::ios::out | std::ios::trunc | std::ios::binary)};
      if (fout && fout->is_open()) {
        caller.debug_logger_ostream_exportor = fout;
        ret.emplace_back(
            details::log_unique_ptr<details::LogRecordExporter>(new details::OStreamLogRecordExporter(*fout)));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc() && !exporter_cfg.otlp_grpc().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions options;
    options.endpoint = exporter_cfg.otlp_grpc().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc().insecure();
    options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc().ca_file();
    if (exporter_cfg.otlp_grpc().timeout().seconds() > 0 || exporter_cfg.otlp_grpc().timeout().nanos() > 0) {
      options.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::seconds(exporter_cfg.otlp_grpc().timeout().seconds()) +
          std::chrono::nanoseconds(exporter_cfg.otlp_grpc().timeout().nanos()));
    }
    for (auto &header : exporter_cfg.otlp_grpc().headers()) {
      if (header.key().empty()) {
        continue;
      }

      options.metadata.emplace(opentelemetry::exporter::otlp::OtlpHeaders::value_type(header.key(), header.value()));
    }

    ret.emplace_back(details::OtlpGrpcLogRecordExporterFactory::Create(options));
  }

  if (exporter_cfg.has_otlp_http() && !exporter_cfg.otlp_http().endpoint().empty()) {
    details::OtlpHttpLogRecordExporterOptions options;
    options.url = exporter_cfg.otlp_http().endpoint();
    options.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds(exporter_cfg.otlp_http().timeout().seconds()) +
        std::chrono::nanoseconds(exporter_cfg.otlp_http().timeout().nanos()));
    for (auto &header : exporter_cfg.otlp_http().headers()) {
      if (header.key().empty()) {
        continue;
      }

      options.http_headers.emplace(
          opentelemetry::exporter::otlp::OtlpHeaders::value_type(header.key(), header.value()));
    }
    options.max_concurrent_requests = exporter_cfg.otlp_http().max_concurrent_requests();
    options.max_requests_per_connection = exporter_cfg.otlp_http().max_requests_per_connection();

    ret.emplace_back(details::OtlpHttpLogRecordExporterFactory::Create(options));
  }

  return ret;
}

static std::vector<details::log_unique_ptr<details::LogRecordProcessor>> _opentelemetry_create_logs_processor(
    std::vector<details::log_unique_ptr<details::LogRecordExporter>> &&exporters,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_processor_cfg &processor_cfg) {
  std::vector<details::log_unique_ptr<details::LogRecordProcessor>> ret;
  ret.reserve(exporters.size());
  if (processor_cfg.has_simple() && !processor_cfg.has_batch()) {
    for (auto &exporter : exporters) {
      ret.emplace_back(details::log_unique_ptr<details::LogRecordProcessor>(
          new details::SimpleLogRecordProcessor(std::move(exporter))));
    }
    return ret;
  }

  for (auto &exporter : exporters) {
    ret.emplace_back(details::log_unique_ptr<details::LogRecordProcessor>(new details::BatchLogRecordProcessor(
        std::move(exporter), static_cast<size_t>(processor_cfg.batch().send_batch_size()),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::seconds(processor_cfg.batch().timeout().seconds()) +
            std::chrono::nanoseconds(processor_cfg.batch().timeout().nanos())),
        static_cast<size_t>(processor_cfg.batch().send_batch_max_size()))));
  }
  return ret;
}

static details::local_provider_handle_t<opentelemetry::logs::LoggerProvider> _opentelemetry_create_logs_provider(
    std::vector<details::log_unique_ptr<details::LogRecordProcessor>> &&processors,
    opentelemetry::sdk::resource::Resource resource) {
  details::local_provider_handle_t<opentelemetry::logs::LoggerProvider> ret;
  std::shared_ptr<opentelemetry::sdk::logs::LoggerContext> context;

  context = std::make_shared<opentelemetry::sdk::logs::LoggerContext>(std::move(processors), std::move(resource));
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>(
      new opentelemetry::sdk::logs::LoggerProvider(context));
  ret.shutdown_callback =
      [context](const opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider> &provider) {
        if (!provider) {
          return;
        }

        // Patch for BUGs [#1591](https://github.com/open-telemetry/opentelemetry-cpp/issues/1591)
        if (context) {
          context->GetProcessor().Shutdown();
        }
        static_cast<opentelemetry::sdk::logs::LoggerProvider *>(provider.get())->Shutdown();
      };
  return ret;
}

static void _opentelemetry_cleanup_local_caller_info_t(std::shared_ptr<details::local_caller_info_t> app_info_cache) {
  if (app_info_cache) {
    // Provider must be destroy before logger
    if (app_info_cache->logs_handle.provider) {
      if (app_info_cache->logs_handle.shutdown_callback) {
        (app_info_cache->logs_handle.shutdown_callback)(app_info_cache->logs_handle.provider);
      }
      app_info_cache->logs_handle.reset();
    }
    app_info_cache->default_logger = opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();

    // Provider must be destroy before meter
    if (app_info_cache->metrics_handle.provider) {
      if (app_info_cache->metrics_handle.shutdown_callback) {
        (app_info_cache->metrics_handle.shutdown_callback)(app_info_cache->metrics_handle.provider);
      }
      app_info_cache->metrics_handle.reset();
    }
    app_info_cache->default_metrics_meter.reset();
    app_info_cache->metrics_meters.clear();

    // Provider must be destroy before tracer
    if (app_info_cache->tracer_handle.provider) {
      if (app_info_cache->tracer_handle.shutdown_callback) {
        (app_info_cache->tracer_handle.shutdown_callback)(app_info_cache->tracer_handle.provider);
      }
      app_info_cache->tracer_handle.reset();
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

  // Hold provider in case of start another span/meter/logger again
  auto trace_provider = opentelemetry::trace::Provider::GetTracerProvider();
  auto metrics_provider = opentelemetry::metrics::Provider::GetMeterProvider();
  auto logs_provider = opentelemetry::logs::Provider::GetLoggerProvider();

  opentelemetry::trace::Provider::SetTracerProvider(
      opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
          new opentelemetry::trace::NoopTracerProvider()));
  opentelemetry::metrics::Provider::SetMeterProvider(
      opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>(
          new opentelemetry::metrics::NoopMeterProvider()));
  opentelemetry::logs::Provider::SetLoggerProvider(
      opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>(
          new opentelemetry::logs::NoopLoggerProvider()));

  if (current_service_cache) {
    if (current_service_cache->default_tracer) {
      current_service_cache->default_tracer->Close(
          std::chrono::seconds(app.get_origin_configure().timer().stop_timeout().seconds()) +
          std::chrono::nanoseconds(app.get_origin_configure().timer().stop_timeout().nanos()));
      opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer> swap_out;
      current_service_cache->default_tracer.swap(swap_out);
    }
  }

  _opentelemetry_cleanup_local_caller_info_t(std::move(current_service_cache));
}

static void _opentelemetry_set_global_provider(
    atapp::app &app, std::shared_ptr<details::local_caller_info_t> app_info_cache,
    details::local_provider_handle_t<opentelemetry::trace::TracerProvider> tracer_handle,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_trace_cfg &tracer_config,
    details::local_provider_handle_t<opentelemetry::metrics::MeterProvider> metrics_handle,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_cfg &metrics_config,
    details::local_provider_handle_t<opentelemetry::logs::LoggerProvider> logs_handle,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_cfg &logs_config) {
  // Default tracer
  if (!tracer_handle.provider) {
    tracer_handle.provider = opentelemetry::trace::Provider::GetTracerProvider();
    tracer_handle.reset_shutdown_callback();
  }
  app_info_cache->tracer_handle = tracer_handle;
  if (!tracer_config.default_name().empty()) {
    app_info_cache->default_tracer = tracer_handle.provider->GetTracer(
        tracer_config.default_name(), app.get_app_version(), tracer_config.schema_url());
  } else if (!app.get_type_name().empty()) {
    app_info_cache->default_tracer =
        tracer_handle.provider->GetTracer(app.get_type_name(), app.get_app_version(), tracer_config.schema_url());
  } else {
    app_info_cache->default_tracer =
        tracer_handle.provider->GetTracer(app.get_app_name(), app.get_app_version(), tracer_config.schema_url());
  }

  // Default meter
  if (!metrics_handle.provider) {
    metrics_handle.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>{
        new opentelemetry::metrics::NoopMeterProvider()};
    metrics_handle.reset_shutdown_callback();
  }
  app_info_cache->metrics_handle = metrics_handle;
  do {
    if (!metrics_handle.provider) {
      break;
    }

    if (metrics_config.default_name().empty()) {
      break;
    }

    auto default_metrics_meter = std::make_shared<details::local_meter_info_t>();
    if (!default_metrics_meter) {
      break;
    }

    default_metrics_meter->meter = metrics_handle.provider->GetMeter(
        metrics_config.default_name(), app.get_app_version(), metrics_config.schema_url());

    if (!default_metrics_meter->meter) {
      break;
    }

    app_info_cache->default_metrics_meter = default_metrics_meter;
    app_info_cache->metrics_meters[metrics_config.default_name()] = default_metrics_meter;
  } while (false);

  // Default logger
  if (!logs_handle.provider) {
    logs_handle.provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>{
        new opentelemetry::logs::NoopLoggerProvider()};
    logs_handle.reset_shutdown_callback();
  }
  app_info_cache->logs_handle = logs_handle;
  if (!logs_config.default_name().empty()) {
    app_info_cache->default_logger = logs_handle.provider->GetLogger(logs_config.default_name(), "", app.get_app_name(),
                                                                     app.get_app_version(), logs_config.schema_url());
  } else if (!app.get_type_name().empty()) {
    app_info_cache->default_logger = logs_handle.provider->GetLogger(app.get_type_name(), "", app.get_app_name(),
                                                                     app.get_app_version(), logs_config.schema_url());
  } else {
    app_info_cache->default_logger = logs_handle.provider->GetLogger(app.get_app_name(), "", app.get_app_name(),
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
  // Set the global trace/metrics provider and service cache.
  opentelemetry::trace::Provider::SetTracerProvider(tracer_handle.provider);
  opentelemetry::metrics::Provider::SetMeterProvider(metrics_handle.provider);
  opentelemetry::logs::Provider::SetLoggerProvider(logs_handle.provider);
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
    if (app_info_cache->logs_handle.provider == details::g_global_service_cache->logs_handle.provider) {
      app_info_cache->logs_handle.reset();
    }
    if (app_info_cache->metrics_handle.provider == details::g_global_service_cache->metrics_handle.provider) {
      app_info_cache->metrics_handle.reset();
    }
    if (app_info_cache->tracer_handle.provider == details::g_global_service_cache->tracer_handle.provider) {
      app_info_cache->tracer_handle.reset();
    }
    std::thread cleanup_thread(
        [app_info_cache]() { _opentelemetry_cleanup_local_caller_info_t(std::move(app_info_cache)); });
    cleanup_thread.detach();
  } while (false);
}

static opentelemetry::sdk::resource::ResourceAttributes _create_opentelemetry_app_resource(
    details::local_caller_info_t &app_info_cache, const atapp::app &app) {
  // @see
  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/resource/semantic_conventions/README.md

  // metrics 维度不能保护无关属性
  app_info_cache.metrics_attributes.clear();

  // basic
  app_info_cache.common_owned_attributes.SetAttribute("service.instance.id", app.get_id());
  app_info_cache.common_owned_attributes.SetAttribute("service.instance.name", app.get_app_name());
  app_info_cache.common_owned_attributes.SetAttribute("service.name", app.get_type_name());
  app_info_cache.common_owned_attributes.SetAttribute("service.identity", app.get_app_identity());
  app_info_cache.common_owned_attributes.SetAttribute("service.type_id", app.get_type_id());
  app_info_cache.common_owned_attributes.SetAttribute("service.version", server_frame_project_get_version());

  // metrics
  app_info_cache.metrics_attributes["service.instance.name"] = app.get_app_name();
  app_info_cache.metrics_attributes["service.name"] = app.get_type_name();
  app_info_cache.metrics_attributes["service.version"] = server_frame_project_get_version();

  // area
  if (0 != app.get_area().zone_id()) {
    app_info_cache.common_owned_attributes.SetAttribute("service.area.zone_id", app.get_area().zone_id());
    app_info_cache.metrics_attributes["service.area.zone_id"] = app.get_area().zone_id();
  }
  if (!app.get_area().region().empty()) {
    app_info_cache.common_owned_attributes.SetAttribute("service.area.region", app.get_area().region());
    app_info_cache.metrics_attributes["service.area.region"] = app.get_area().region();
  }
  if (!app.get_area().district().empty()) {
    app_info_cache.common_owned_attributes.SetAttribute("service.area.district", app.get_area().district());
    app_info_cache.metrics_attributes["service.area.district"] = app.get_area().district();
  }

  // metadata
  if (!app.get_metadata().namespace_name().empty()) {
    app_info_cache.common_owned_attributes.SetAttribute("service.namespace", app.get_metadata().namespace_name());
    app_info_cache.metrics_attributes["service.namespace"] = app.get_metadata().namespace_name();
  }

  {
    auto iter = app.get_metadata().labels().find("deployment.environment");
    if (iter != app.get_metadata().labels().end()) {
      app_info_cache.common_owned_attributes.SetAttribute("deployment.environment", iter->second);
      app_info_cache.metrics_attributes["deployment.environment"] = iter->second;
    }
  }

  // process
  // @see
  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/resource/semantic_conventions/process.md
  app_info_cache.common_owned_attributes.SetAttribute("process.pid", atbus::node::get_pid());

  // Other common resource should be set by configure generator

  details::rebuild_attributes_map(app_info_cache.common_owned_attributes, app_info_cache.common_attributes);
  return app_info_cache.common_owned_attributes;
}

static details::local_provider_handle_t<opentelemetry::trace::TracerProvider>
_opentelemetry_create_opentelemetry_trace_provider(
    details::local_caller_info_t &app_info_cache,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_cfg &opentelemetry_cfg,
    const opentelemetry::sdk::resource::ResourceAttributes &resource_values) {
  if (opentelemetry_cfg.has_trace()) {
    auto exporter = _opentelemetry_create_trace_exporter(app_info_cache, opentelemetry_cfg.trace().exporters());
    auto sampler = _opentelemetry_create_trace_sampler(opentelemetry_cfg.trace().samplers());
    if (!sampler) {
      details::local_provider_handle_t<opentelemetry::trace::TracerProvider> ret;
      ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>();
      ret.reset_shutdown_callback();
      return ret;
    }
    auto processor = _opentelemetry_create_trace_processor(std::move(exporter), opentelemetry_cfg.trace().processors());

    opentelemetry::sdk::resource::ResourceAttributes trace_resource_values = resource_values;
    for (auto &ext_res : opentelemetry_cfg.trace().resource()) {
      if (ext_res.second.empty()) {
        continue;
      }
      trace_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }
    return _opentelemetry_create_trace_provider(std::move(processor), std::move(sampler),
                                                opentelemetry::sdk::resource::Resource::Create(trace_resource_values));
  }

  details::local_provider_handle_t<opentelemetry::trace::TracerProvider> ret;
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>();
  ret.reset_shutdown_callback();
  return ret;
}

static details::local_provider_handle_t<opentelemetry::metrics::MeterProvider>
_opentelemetry_create_opentelemetry_metrics_provider(
    details::local_caller_info_t &app_info_cache,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_cfg &opentelemetry_cfg,
    const opentelemetry::sdk::resource::ResourceAttributes &resource_values) {
  if (opentelemetry_cfg.has_metrics()) {
    auto exporters = _opentelemetry_create_metrics_exporter(app_info_cache, opentelemetry_cfg.metrics().exporters());
    auto readers = _opentelemetry_create_metrics_reader(std::move(exporters), opentelemetry_cfg.metrics().reader());

    opentelemetry::sdk::resource::ResourceAttributes metrics_resource_values = resource_values;
    for (auto &ext_res : opentelemetry_cfg.metrics().resource()) {
      if (ext_res.second.empty()) {
        continue;
      }
      metrics_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }

    if (readers.empty()) {
      details::local_provider_handle_t<opentelemetry::metrics::MeterProvider> ret;
      ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>();
      ret.reset_shutdown_callback();
      return ret;
    }

    return _opentelemetry_create_metrics_provider(std::move(readers), metrics_resource_values);
  }

  details::local_provider_handle_t<opentelemetry::metrics::MeterProvider> ret;
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>();
  ret.reset_shutdown_callback();
  return ret;
}

static details::local_provider_handle_t<opentelemetry::logs::LoggerProvider>
_opentelemetry_create_opentelemetry_logs_provider(
    details::local_caller_info_t &app_info_cache,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_cfg &opentelemetry_cfg,
    const opentelemetry::sdk::resource::ResourceAttributes &resource_values) {
  if (opentelemetry_cfg.has_logs()) {
    auto exporter = _opentelemetry_create_logs_exporter(app_info_cache, opentelemetry_cfg.logs().exporters());
    auto processor = _opentelemetry_create_logs_processor(std::move(exporter), opentelemetry_cfg.logs().processors());

    opentelemetry::sdk::resource::ResourceAttributes logs_resource_values = resource_values;
    for (auto &ext_res : opentelemetry_cfg.logs().resource()) {
      if (ext_res.second.empty()) {
        continue;
      }
      logs_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }
    return _opentelemetry_create_logs_provider(std::move(processor),
                                               opentelemetry::sdk::resource::Resource::Create(logs_resource_values));
  }

  details::local_provider_handle_t<opentelemetry::logs::LoggerProvider> ret;
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>();
  ret.reset_shutdown_callback();
  return ret;
}

}  // namespace

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
  opentelemetry::sdk::resource::ResourceAttributes resource_values =
      _create_opentelemetry_app_resource(*app_info_cache, app);

  for (auto &ext_res : opentelemetry_cfg.resource()) {
    if (ext_res.second.empty()) {
      continue;
    }
    resource_values.SetAttribute(ext_res.first, ext_res.second);
  }

  // Trace
  details::local_provider_handle_t<opentelemetry::trace::TracerProvider> tracer_handle =
      _opentelemetry_create_opentelemetry_trace_provider(*app_info_cache, opentelemetry_cfg, resource_values);

  // Metrics
  details::local_provider_handle_t<opentelemetry::metrics::MeterProvider> metrics_handle =
      _opentelemetry_create_opentelemetry_metrics_provider(*app_info_cache, opentelemetry_cfg, resource_values);

  // Logs
  details::local_provider_handle_t<opentelemetry::logs::LoggerProvider> logs_handle =
      _opentelemetry_create_opentelemetry_logs_provider(*app_info_cache, opentelemetry_cfg, resource_values);

  _opentelemetry_set_global_provider(app, app_info_cache, tracer_handle, opentelemetry_cfg.trace(), metrics_handle,
                                     opentelemetry_cfg.metrics(), logs_handle, opentelemetry_cfg.logs());
}

}  // namespace telemetry

}  // namespace rpc

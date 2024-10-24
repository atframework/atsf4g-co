// Copyright 2023 atframework
// Created by owent on 2021/10/18.
//

#include "rpc/telemetry/rpc_global_service.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/util/time_util.h>

#include <google/protobuf/timestamp.pb.h>

#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/atframework.pb.h>

#include <opentelemetry/exporters/ostream/log_record_exporter.h>
#include <opentelemetry/exporters/ostream/metric_exporter_factory.h>
#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_file_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_file_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_file_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_file_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_file_metric_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/exporters/prometheus/exporter_factory.h>
#include <opentelemetry/exporters/prometheus/exporter_options.h>
#include <opentelemetry/logs/logger_provider.h>
#include <opentelemetry/logs/noop.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/metrics/noop.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/sdk/common/global_log_handler.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_options.h>
#include <opentelemetry/sdk/logs/logger_provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/processor.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>
#include <opentelemetry/sdk/metrics/aggregation/aggregation_config.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/metrics/meter.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>
#include <opentelemetry/sdk/resource/semantic_conventions.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/multi_span_processor.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/samplers/always_off.h>
#include <opentelemetry/sdk/trace/samplers/always_on.h>
#include <opentelemetry/sdk/trace/samplers/parent.h>
#include <opentelemetry/sdk/trace/samplers/trace_id_ratio.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/trace/provider.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <std/explicit_declare.h>

#include <atframe/atapp.h>

#include <design_pattern/singleton.h>
#include <gsl/select-gsl.h>
#include <lock/lock_holder.h>
#include <lock/spin_rw_lock.h>
#include <log/log_wrapper.h>

#include <memory/object_allocator.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "rpc/telemetry/exporter/prometheus_file_exporter_factory.h"
#include "rpc/telemetry/exporter/prometheus_file_exporter_options.h"
#include "rpc/telemetry/exporter/prometheus_push_exporter_factory.h"
#include "rpc/telemetry/exporter/prometheus_push_exporter_options.h"
#include "rpc/telemetry/opentelemetry_utility.h"

namespace rpc {

namespace telemetry {

namespace {
struct local_meter_info_type {
  util::lock::spin_rw_lock lock;

  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter;

  // sync instruments
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>>
      sync_counter_uint64;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>>
      sync_histogram_uint64;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>>
      sync_up_down_counter_int64;
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
struct local_provider_handle_type {
  using shutdown_callback_t = std::function<void(const opentelemetry::nostd::shared_ptr<ProviderType> &)>;
  opentelemetry::nostd::shared_ptr<ProviderType> provider;
  shutdown_callback_t shutdown_callback;

  inline void reset_shutdown_callback() { shutdown_callback = shutdown_callback_t(); }

  inline void reset() {
    provider = opentelemetry::nostd::shared_ptr<ProviderType>();
    reset_shutdown_callback();
  }
};

static void _opentelemetry_initialize_group(const std::shared_ptr<group_type> &);

static bool _opentelemetry_validate_meter_instrument_name(opentelemetry::nostd::string_view name) {
  // @see InstrumentMetaDataValidator::ValidateName in
  // <opentelemetry-cpp>/sdk/src/metrics/instrument_metadata_validator.cc

  using std::isalnum;
  using std::isalpha;

  const size_t kMaxSize = 255;
  // size atmost 255 chars
  if (name.size() > kMaxSize) {
    return false;
  }

  if (name.empty()) {
    return false;
  }

  // first char should be alpha
  if (!isalpha(name[0])) {
    return false;
  }
  // subsequent chars should be either of alphabets, digits, underscore,
  // minus, dot, slash
  return !std::any_of(std::next(name.begin()), name.end(),
                      [](char c) { return !isalnum(c) && (c != '-') && (c != '_') && (c != '.') && (c != '/'); });
}

static bool _opentelemetry_validate_meter_instrument_unit(opentelemetry::nostd::string_view unit) {
  // @see InstrumentMetaDataValidator::ValidateUnit in
  // <opentelemetry-cpp>/sdk/src/metrics/instrument_metadata_validator.cc
  const size_t kMaxSize = 63;
  // length atmost 63 chars
  if (unit.size() > kMaxSize) {
    return false;
  }
  // all should be ascii chars.
  return !std::any_of(unit.begin(), unit.end(), [](char c) { return static_cast<unsigned char>(c) > 127; });
}

static bool _opentelemetry_validate_meter_instrument_description(opentelemetry::nostd::string_view /*desc*/) {
  // @see InstrumentMetaDataValidator::ValidateDescription in
  // <opentelemetry-cpp>/sdk/src/metrics/instrument_metadata_validator.cc
  return true;
}

static bool _opentelemetry_validate_meter_instrument_key(const meter_instrument_key &key) {
  return _opentelemetry_validate_meter_instrument_name(key.name) &&
         _opentelemetry_validate_meter_instrument_unit(key.unit) &&
         _opentelemetry_validate_meter_instrument_description(key.description);
}

}  // namespace

struct group_type {
  util::lock::spin_rw_lock lock;
  bool initialized = false;
  std::string group_name;

  // Replicate
  uint64_t server_id = 0;
  std::string server_name;
  std::string app_version;
  PROJECT_NAMESPACE_ID::config::opentelemetry_group_cfg group_configure;

  std::string trace_default_library_name;
  std::string trace_default_library_version;
  std::string trace_default_schema_url;
  std::unordered_map<std::string, std::string> trace_additional_resource;

  std::string metrics_default_library_name;
  std::string metrics_default_library_version;
  std::string metrics_default_schema_url;
  std::unordered_map<std::string, std::string> metrics_additional_resource;

  std::string logs_default_logger_name;
  std::string logs_default_library_name;
  std::string logs_default_library_version;
  std::string logs_default_schema_url;
  std::unordered_map<std::string, std::string> logs_additional_resource;

  opentelemetry::sdk::common::AttributeMap common_owned_attributes;
  std::unordered_map<std::string, opentelemetry::common::AttributeValue> common_attributes;
  std::unordered_map<std::string, opentelemetry::common::AttributeValue> metrics_attributes;
  std::vector<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
      metrics_attributes_view;

  local_provider_handle_type<opentelemetry::trace::TracerProvider> tracer_handle;
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> default_tracer;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>> tracer_cache;
  opentelemetry::nostd::shared_ptr<std::ofstream> debug_tracer_ostream_exportor;
  size_t tracer_exporter_count = 0;

  local_provider_handle_type<opentelemetry::metrics::MeterProvider> metrics_handle;
  std::shared_ptr<local_meter_info_type> default_metrics_meter;
  std::unordered_map<std::string, std::shared_ptr<local_meter_info_type>> metrics_meters;
  opentelemetry::nostd::shared_ptr<std::ofstream> debug_metrics_ostream_exportor;
  size_t metrics_exporter_count = 0;

  local_provider_handle_type<opentelemetry::logs::LoggerProvider> logs_handle;
  opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> default_logger;
  std::unordered_map<std::string, opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>> logger_cache;
  opentelemetry::nostd::shared_ptr<std::ofstream> debug_logger_ostream_exportor;
  size_t logger_exporter_count = 0;

  group_type() {}
};

namespace {
struct global_service_data_type : public util::design_pattern::local_singleton<global_service_data_type> {
  util::log::log_wrapper::ptr_t internal_logger;

  std::list<global_service::group_event_callback_type> on_group_destroy_callbacks;
  util::lock::spin_rw_lock on_group_destroy_callback_lock;
  std::list<global_service::group_event_callback_type> on_group_create_callbacks;
  util::lock::spin_rw_lock on_group_create_callback_lock;
  std::list<global_service::global_event_callback_type> on_ready_callbacks;
  util::lock::spin_rw_lock on_ready_callback_lock;

  util::lock::spin_rw_lock group_lock;
  std::shared_ptr<group_type> default_group;
  std::unordered_map<std::string, std::shared_ptr<group_type>> named_groups;

  bool shutdown = false;
  inline global_service_data_type() {}
};

static std::shared_ptr<global_service_data_type> get_global_service_data() {
  if (global_service_data_type::is_instance_destroyed()) {
    return nullptr;
  }

  if (global_service_data_type::me()->shutdown) {
    return nullptr;
  }

  return global_service_data_type::me();
}

class opentelemetry_internal_log_handler : public opentelemetry::sdk::common::internal_log::LogHandler {
 public:
  void Handle(opentelemetry::sdk::common::internal_log::LogLevel level, const char *file, int line, const char *msg,
              const opentelemetry::sdk::common::AttributeMap &) noexcept override {
    util::log::log_wrapper::caller_info_t caller;
    caller.file_path = file;
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

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
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
static TValue optimize_search_in_hash_map(std::unordered_map<std::string, TValue> &container, const std::string &key) {
  if (container.size() < 4) {
    for (auto &element : container) {
      if (element.first == key) {
        return element.second;
      }
    }
  } else {
    auto iter = container.find(key);
    if (iter != container.end()) {
      return iter->second;
    }
  }

  return TValue();
}

static std::string get_metrics_key(meter_instrument_key metrics_key) {
  // opentelemetry only use metrics name as key of metric storage
  return {metrics_key.name.data(), metrics_key.name.size()};
}

static std::shared_ptr<local_meter_info_type> get_meter_info(std::shared_ptr<group_type> &group,
                                                             const opentelemetry::nostd::string_view &meter_name) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return std::shared_ptr<local_meter_info_type>();
  }
  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }
  // It will not be initialized when closing
  if (!group->initialized) {
    return std::shared_ptr<local_meter_info_type>();
  }

  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{group->lock};

    if (!group->metrics_handle.provider) {
      return nullptr;
    }

    if (meter_name.empty() ||
        meter_name == logic_config::me()->get_logic().telemetry().opentelemetry().metrics().default_name()) {
      return group->default_metrics_meter;
    }

    auto ret = optimize_search_in_hash_map(group->metrics_meters, static_cast<std::string>(meter_name));
    if (ret) {
      return ret;
    }
  }

  {
    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{group->lock};

    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter = group->metrics_handle.provider->GetMeter(
        meter_name, group->app_version,
        logic_config::me()->get_logic().telemetry().opentelemetry().metrics().schema_url());
    if (!meter) {
      return nullptr;
    }

    auto ret = atfw::memory::stl::make_shared<local_meter_info_type>();
    if (!ret) {
      return ret;
    }
    ret->meter = std::move(meter);
    group->metrics_meters[static_cast<std::string>(meter_name)] = ret;

    return ret;
  }
}

static void rebuild_attributes_map(const opentelemetry::sdk::common::AttributeMap &src,
                                   std::unordered_map<std::string, opentelemetry::common::AttributeValue> &dst) {
  dst.clear();
  dst.reserve(src.size());
  for (auto &kv : src) {
    dst[kv.first] = opentelemetry_utility::convert_attribute_value_wihtout_array(kv.second);
  }
}

static void set_attributes_map_item(const opentelemetry::sdk::common::AttributeMap &src,
                                    std::unordered_map<std::string, opentelemetry::common::AttributeValue> &dst,
                                    const std::string &key) {
  auto iter = src.GetAttributes().find(key);
  if (iter == src.GetAttributes().end()) {
    return;
  }
  dst[iter->first] = opentelemetry_utility::convert_attribute_value_wihtout_array(iter->second);
}

}  // namespace

SERVER_FRAME_API std::pair<bool, global_service::global_event_callback_handle> global_service::add_on_ready(
    global_event_callback_type fn) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return {false, global_event_callback_handle()};
  }

  bool already_ready = false;
  std::pair<bool, global_service::global_event_callback_handle> ret;
  {
    bool hold_write_lock = current_service_cache->on_ready_callback_lock.try_write_lock();
    if (!hold_write_lock) {
      return {false, global_event_callback_handle()};
    }
    auto final_unlock =
        gsl::finally([current_service_cache]() { current_service_cache->on_ready_callback_lock.try_write_unlock(); });

    already_ready = !!current_service_cache->default_group;

    if (!fn) {
      return {false, current_service_cache->on_ready_callbacks.end()};
    }

    ret.second = current_service_cache->on_ready_callbacks.insert(current_service_cache->on_ready_callbacks.end(), fn);
    ret.first = true;
  }

  if (already_ready && fn) {
    fn();
  }

  return ret;
}

SERVER_FRAME_API bool global_service::remove_on_ready(global_event_callback_handle &handle) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return false;
  }

  bool hold_write_lock = current_service_cache->on_ready_callback_lock.try_write_lock();
  if (!hold_write_lock) {
    return false;
  }
  auto final_unlock =
      gsl::finally([current_service_cache]() { current_service_cache->on_ready_callback_lock.try_write_unlock(); });

  if (handle == current_service_cache->on_ready_callbacks.end()) {
    return false;
  }

  current_service_cache->on_ready_callbacks.erase(handle);
  handle = current_service_cache->on_ready_callbacks.end();
  return true;
}

SERVER_FRAME_API std::pair<bool, global_service::group_event_callback_handle> global_service::add_on_group_destroy(
    group_event_callback_type fn) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return {false, group_event_callback_handle()};
  }

  std::pair<bool, global_service::group_event_callback_handle> ret;
  {
    bool hold_write_lock = current_service_cache->on_group_destroy_callback_lock.try_write_lock();
    if (!hold_write_lock) {
      return {false, group_event_callback_handle()};
    }
    auto final_unlock = gsl::finally(
        [current_service_cache]() { current_service_cache->on_group_destroy_callback_lock.try_write_unlock(); });

    if (!fn) {
      return {false, current_service_cache->on_group_destroy_callbacks.end()};
    }

    ret.second = current_service_cache->on_group_destroy_callbacks.insert(
        current_service_cache->on_group_destroy_callbacks.end(), fn);
    ret.first = true;
  }

  return ret;
}

SERVER_FRAME_API bool global_service::remove_on_group_destroy(group_event_callback_handle &handle) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return false;
  }

  bool hold_write_lock = current_service_cache->on_group_destroy_callback_lock.try_write_lock();
  if (!hold_write_lock) {
    return false;
  }
  auto final_unlock = gsl::finally(
      [current_service_cache]() { current_service_cache->on_group_destroy_callback_lock.try_write_unlock(); });

  if (handle == current_service_cache->on_group_destroy_callbacks.end()) {
    return false;
  }

  current_service_cache->on_group_destroy_callbacks.erase(handle);
  handle = current_service_cache->on_group_destroy_callbacks.end();
  return true;
}

SERVER_FRAME_API std::pair<bool, global_service::group_event_callback_handle> global_service::add_on_group_create(
    group_event_callback_type fn) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return {false, group_event_callback_handle()};
  }

  std::pair<bool, global_service::group_event_callback_handle> ret;
  {
    bool hold_write_lock = current_service_cache->on_group_create_callback_lock.try_write_lock();
    if (!hold_write_lock) {
      return {false, group_event_callback_handle()};
    }
    auto final_unlock = gsl::finally(
        [current_service_cache]() { current_service_cache->on_group_create_callback_lock.try_write_unlock(); });

    if (!fn) {
      return {false, current_service_cache->on_group_create_callbacks.end()};
    }

    ret.second = current_service_cache->on_group_create_callbacks.insert(
        current_service_cache->on_group_create_callbacks.end(), fn);
    ret.first = true;
  }

  return ret;
}

SERVER_FRAME_API bool global_service::remove_on_group_create(group_event_callback_handle &handle) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return false;
  }

  bool hold_write_lock = current_service_cache->on_group_create_callback_lock.try_write_lock();
  if (!hold_write_lock) {
    return false;
  }
  auto final_unlock = gsl::finally(
      [current_service_cache]() { current_service_cache->on_group_create_callback_lock.try_write_unlock(); });

  if (handle == current_service_cache->on_group_create_callbacks.end()) {
    return false;
  }

  current_service_cache->on_group_create_callbacks.erase(handle);
  handle = current_service_cache->on_group_create_callbacks.end();
  return true;
}

SERVER_FRAME_API const opentelemetry::sdk::common::AttributeMap &global_service::get_common_owned_attributes(
    std::shared_ptr<group_type> &group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (nullptr != group) {
    return group->common_owned_attributes;
  }

  static opentelemetry::sdk::common::AttributeMap empty;
  return empty;
}

SERVER_FRAME_API const std::unordered_map<std::string, opentelemetry::common::AttributeValue> &
global_service::get_common_attributes(std::shared_ptr<group_type> &group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (nullptr != group) {
    return group->common_attributes;
  }

  static std::unordered_map<std::string, opentelemetry::common::AttributeValue> empty;
  return empty;
}

SERVER_FRAME_API const std::unordered_map<std::string, opentelemetry::common::AttributeValue> &
global_service::get_metrics_labels(std::shared_ptr<group_type> &group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (nullptr != group) {
    return group->metrics_attributes;
  }

  static std::unordered_map<std::string, opentelemetry::common::AttributeValue> empty;
  return empty;
}

SERVER_FRAME_API
opentelemetry::nostd::span<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
global_service::get_metrics_labels_view(std::shared_ptr<group_type> &group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (nullptr != group) {
    return group->metrics_attributes_view;
  }

  return {};
}

SERVER_FRAME_API const PROJECT_NAMESPACE_ID::config::opentelemetry_cfg &global_service::get_configure(
    std::shared_ptr<group_type> &group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (nullptr != group) {
    return group->group_configure.configure();
  }

  return PROJECT_NAMESPACE_ID::config::opentelemetry_cfg::default_instance();
}

SERVER_FRAME_API bool global_service::has_agent_configure(std::shared_ptr<group_type> &group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (nullptr != group) {
    return group->group_configure.has_agent();
  }

  return false;
}

SERVER_FRAME_API const PROJECT_NAMESPACE_ID::config::opentelemetry_agent_cfg &global_service::get_agent_configure(
    std::shared_ptr<group_type> &group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (nullptr != group) {
    return group->group_configure.agent();
  }

  return PROJECT_NAMESPACE_ID::config::opentelemetry_agent_cfg::default_instance();
}

SERVER_FRAME_API
const PROJECT_NAMESPACE_ID::config::opentelemetry_trace_cfg &global_service::get_trace_configure(
    std::shared_ptr<group_type> &group) {
  return get_configure(group).trace();
}

SERVER_FRAME_API
const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_cfg &global_service::get_metrics_configure(
    std::shared_ptr<group_type> &group) {
  return get_configure(group).metrics();
}

SERVER_FRAME_API
const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_cfg &global_service::get_logs_configure(
    std::shared_ptr<group_type> &group) {
  return get_configure(group).logs();
}

SERVER_FRAME_API size_t global_service::get_trace_exporter_count(std::shared_ptr<group_type> group) noexcept {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return 0;
  }

  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }

  if (group) {
    return group->tracer_exporter_count;
  }

  return 0;
}

SERVER_FRAME_API size_t global_service::get_metrics_exporter_count(std::shared_ptr<group_type> group) noexcept {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return 0;
  }

  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }

  if (group) {
    return group->metrics_exporter_count;
  }

  return 0;
}

SERVER_FRAME_API size_t global_service::get_logs_exporter_count(std::shared_ptr<group_type> group) noexcept {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return 0;
  }

  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }

  if (group) {
    return group->logger_exporter_count;
  }

  return 0;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<::opentelemetry::trace::Tracer>
global_service::get_current_default_tracer(std::shared_ptr<group_type> group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }
  // It will not be initialized when closing
  if (!group->initialized) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  if (group) {
    return group->default_tracer;
  }

  return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> global_service::get_tracer(
    opentelemetry::nostd::string_view library_name, opentelemetry::nostd::string_view library_version,
    opentelemetry::nostd::string_view schema_url) {
  return get_tracer(nullptr, library_name, library_version, schema_url);
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> global_service::get_tracer(
    std::shared_ptr<group_type> group, opentelemetry::nostd::string_view library_name,
    opentelemetry::nostd::string_view library_version, opentelemetry::nostd::string_view schema_url) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }
  // It will not be initialized when closing
  if (!group->initialized) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  std::string cache_key = util::log::format("{}:{}", gsl::string_view{library_name.data(), library_name.size()},
                                            gsl::string_view{library_version.data(), library_version.size()});
  {
    auto iter = group->tracer_cache.find(cache_key);
    if (iter != group->tracer_cache.end()) {
      return iter->second;
    }
  }

  auto provider = group->tracer_handle.provider;
  if (!provider) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  if (library_name.empty()) {
    library_name = group->server_name;
    if (library_version.empty()) {
      library_version = group->app_version;
    }
  }
  if (schema_url.empty()) {
    schema_url = logic_config::me()->get_cfg_telemetry().opentelemetry().logs().schema_url();
  }

  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> ret =
      provider->GetTracer(library_name, library_version, schema_url);
  if (ret) {
    group->tracer_cache[cache_key] = ret;
  }
  return ret;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>
global_service::mutable_metrics_counter_uint64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                               std::shared_ptr<group_type> group) {
  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>();
  }

  std::string metrics_storage_key = get_metrics_key(key);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    auto ret = optimize_search_in_hash_map(meter_info->sync_counter_uint64, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics counter {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>();
  }

  {
    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
    auto ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<uint64_t>>(
        meter_info->meter->CreateUInt64Counter(key.name, key.description, key.unit));
    if (ret) {
      meter_info->sync_counter_uint64[metrics_storage_key] = ret;
    }

    return ret;
  }
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>
global_service::mutable_metrics_counter_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                               std::shared_ptr<group_type> group) {
  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>();
  }

  std::string metrics_storage_key = get_metrics_key(key);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    auto ret = optimize_search_in_hash_map(meter_info->sync_counter_double, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics counter {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>();
  }

  {
    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
    auto ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>>(
        meter_info->meter->CreateDoubleCounter(key.name, key.description, key.unit));
    if (ret) {
      meter_info->sync_counter_double[metrics_storage_key] = ret;
    }

    return ret;
  }
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>
global_service::mutable_metrics_histogram_uint64(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                                 std::shared_ptr<group_type> group) {
  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>();
  }

  std::string metrics_storage_key = get_metrics_key(key);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    auto ret = optimize_search_in_hash_map(meter_info->sync_histogram_uint64, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics histogram {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>();
  }

  {
    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
    auto ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<uint64_t>>(
        meter_info->meter->CreateUInt64Histogram(key.name, key.description, key.unit));
    if (ret) {
      meter_info->sync_histogram_uint64[metrics_storage_key] = ret;
    }

    return ret;
  }
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>
global_service::mutable_metrics_histogram_double(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                                 std::shared_ptr<group_type> group) {
  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>();
  }

  std::string metrics_storage_key = get_metrics_key(key);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    auto ret = optimize_search_in_hash_map(meter_info->sync_histogram_double, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics histogram {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>();
  }

  {
    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
    auto ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>>(
        meter_info->meter->CreateDoubleHistogram(key.name, key.description, key.unit));
    if (ret) {
      meter_info->sync_histogram_double[metrics_storage_key] = ret;
    }

    return ret;
  }
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>
global_service::mutable_metrics_up_down_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                      meter_instrument_key key, std::shared_ptr<group_type> group) {
  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>();
  }

  std::string metrics_storage_key = get_metrics_key(key);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    auto ret = optimize_search_in_hash_map(meter_info->sync_up_down_counter_int64, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics up_down_counter {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>();
  }

  {
    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
    auto ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>(
        meter_info->meter->CreateInt64UpDownCounter(key.name, key.description, key.unit));
    if (ret) {
      meter_info->sync_up_down_counter_int64[metrics_storage_key] = ret;
    }

    return ret;
  }
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>
global_service::mutable_metrics_up_down_counter_double(opentelemetry::nostd::string_view meter_name,
                                                       meter_instrument_key key, std::shared_ptr<group_type> group) {
  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>();
  }

  std::string metrics_storage_key = get_metrics_key(key);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    auto ret = optimize_search_in_hash_map(meter_info->sync_up_down_counter_double, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics up_down_counter {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>();
  }

  {
    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
    auto ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::UpDownCounter<double>>(
        meter_info->meter->CreateDoubleUpDownCounter(key.name, key.description, key.unit));
    if (ret) {
      meter_info->sync_up_down_counter_double[metrics_storage_key] = ret;
    }

    return ret;
  }
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::get_metrics_observable(opentelemetry::nostd::string_view meter_name, meter_instrument_key key,
                                       std::shared_ptr<group_type> group) {
  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  std::string metrics_storage_key = get_metrics_key(key);

  util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
  return optimize_search_in_hash_map(meter_info->async_instruments, metrics_storage_key);
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                         meter_instrument_key key, std::shared_ptr<group_type> group) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key, group);
  if (ret) {
    return ret;
  }

  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  std::string metrics_storage_key = static_cast<std::string>(key.name);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    ret = optimize_search_in_hash_map(meter_info->async_instruments, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics observable {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
  ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>(
      meter_info->meter->CreateInt64ObservableCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[metrics_storage_key] = ret;
  }

  return ret;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_counter_double(opentelemetry::nostd::string_view meter_name,
                                                          meter_instrument_key key, std::shared_ptr<group_type> group) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key, group);
  if (ret) {
    return ret;
  }

  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  std::string metrics_storage_key = static_cast<std::string>(key.name);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    ret = optimize_search_in_hash_map(meter_info->async_instruments, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics observable {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
  ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>(
      meter_info->meter->CreateDoubleObservableCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[metrics_storage_key] = ret;
  }

  return ret;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_gauge_int64(opentelemetry::nostd::string_view meter_name,
                                                       meter_instrument_key key, std::shared_ptr<group_type> group) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key, group);
  if (ret) {
    return ret;
  }

  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  std::string metrics_storage_key = static_cast<std::string>(key.name);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    ret = optimize_search_in_hash_map(meter_info->async_instruments, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics observable {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
  ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>(
      meter_info->meter->CreateInt64ObservableGauge(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[metrics_storage_key] = ret;
  }

  return ret;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_gauge_double(opentelemetry::nostd::string_view meter_name,
                                                        meter_instrument_key key, std::shared_ptr<group_type> group) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key, group);
  if (ret) {
    return ret;
  }

  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  std::string metrics_storage_key = static_cast<std::string>(key.name);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    ret = optimize_search_in_hash_map(meter_info->async_instruments, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics observable {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
  ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>(
      meter_info->meter->CreateDoubleObservableGauge(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[metrics_storage_key] = ret;
  }

  return ret;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_up_down_counter_int64(opentelemetry::nostd::string_view meter_name,
                                                                 meter_instrument_key key,
                                                                 std::shared_ptr<group_type> group) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key, group);
  if (ret) {
    return ret;
  }

  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  std::string metrics_storage_key = static_cast<std::string>(key.name);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    ret = optimize_search_in_hash_map(meter_info->async_instruments, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics observable {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
  ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>(
      meter_info->meter->CreateInt64ObservableUpDownCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[metrics_storage_key] = ret;
  }

  return ret;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
global_service::mutable_metrics_observable_up_down_counter_double(opentelemetry::nostd::string_view meter_name,
                                                                  meter_instrument_key key,
                                                                  std::shared_ptr<group_type> group) {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> ret =
      get_metrics_observable(meter_name, key, group);
  if (ret) {
    return ret;
  }

  auto meter_info = get_meter_info(group, meter_name);
  if (!meter_info) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  std::string metrics_storage_key = static_cast<std::string>(key.name);
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};

    ret = optimize_search_in_hash_map(meter_info->async_instruments, metrics_storage_key);
    if (ret) {
      return ret;
    }
  }

  if (!_opentelemetry_validate_meter_instrument_key(key)) {
    FWLOGERROR("Create metrics observable {}(instrument={}, {}, {}) failed, invalid instrument key",
               gsl::string_view{meter_name.data(), meter_name.size()},
               gsl::string_view{key.name.data(), key.name.size()}, gsl::string_view{key.unit.data(), key.unit.size()},
               gsl::string_view{key.description.data(), key.description.size()}

    );
    return opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>();
  }

  util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{meter_info->lock};
  ret = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>(
      meter_info->meter->CreateDoubleObservableUpDownCounter(key.name, key.description, key.unit));
  if (ret) {
    meter_info->async_instruments[metrics_storage_key] = ret;
  }

  return ret;
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>
global_service::get_current_default_logger(std::shared_ptr<group_type> group) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
  }

  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }
  // It will not be initialized when closing
  if (!group->initialized) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
  }

  if (group) {
    return group->default_logger;
  }

  return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> global_service::get_logger(
    opentelemetry::nostd::string_view logger_name, opentelemetry::nostd::string_view library_name,
    opentelemetry::nostd::string_view library_version, opentelemetry::nostd::string_view schema_url) {
  return get_logger(nullptr, logger_name, library_name, library_version, schema_url);
}

SERVER_FRAME_API opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> global_service::get_logger(
    std::shared_ptr<group_type> group, opentelemetry::nostd::string_view logger_name,
    opentelemetry::nostd::string_view library_name, opentelemetry::nostd::string_view library_version,
    opentelemetry::nostd::string_view schema_url) {
  do {
    if (group) {
      break;
    }

    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      break;
    }

    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    group = current_service_cache->default_group;
  } while (false);

  if (!group) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
  }

  if (!group->initialized) {
    _opentelemetry_initialize_group(group);
  }
  // It will not be initialized when closing
  if (!group->initialized) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
  }

  std::string cache_key = util::log::format("{}:{}:{}", gsl::string_view{logger_name.data(), logger_name.size()},
                                            gsl::string_view{library_name.data(), library_name.size()},
                                            gsl::string_view{library_version.data(), library_version.size()});
  {
    auto iter = group->logger_cache.find(cache_key);
    if (iter != group->logger_cache.end()) {
      return iter->second;
    }
  }

  auto provider = group->logs_handle.provider;

  if (!provider) {
    return opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();
  }

  if (library_name.empty()) {
    library_name = group->server_name;
    if (library_version.empty()) {
      library_version = group->app_version;
    }
  }
  if (schema_url.empty()) {
    schema_url = logic_config::me()->get_cfg_telemetry().opentelemetry().logs().schema_url();
  }

  opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> ret =
      provider->GetLogger(logger_name, library_name, library_version, schema_url);
  if (ret) {
    group->logger_cache[cache_key] = ret;
  }
  return ret;
}

namespace {
static std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> _opentelemetry_create_trace_exporter(
    ::rpc::telemetry::group_type &group,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_trace_exporter_cfg &exporter_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> ret;
  ret.reserve(2);

  if (!exporter_cfg.ostream().empty()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP("stdout", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create(std::cout));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP("stderr", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create(std::cerr));
    } else {
      ::opentelemetry::nostd::shared_ptr<std::ofstream> fout{
          new std::ofstream(exporter_cfg.ostream().c_str(), std::ios::out | std::ios::trunc | std::ios::binary)};
      if (fout && fout->is_open()) {
        group.debug_tracer_ostream_exportor = fout;
        ret.emplace_back(opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create(*fout));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc() && !exporter_cfg.otlp_grpc().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions options;
    options.endpoint = exporter_cfg.otlp_grpc().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc().insecure();
    if (!exporter_cfg.otlp_grpc().user_agent().empty()) {
      options.user_agent = exporter_cfg.otlp_grpc().user_agent();
    }
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

    options.max_concurrent_requests = exporter_cfg.otlp_grpc().max_concurrent_requests();
    if (!exporter_cfg.otlp_grpc().ssl_ca_cert_path().empty()) {
      options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc().ssl_ca_cert_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_ca_cert_string().empty()) {
      options.ssl_credentials_cacert_as_string = exporter_cfg.otlp_grpc().ssl_ca_cert_string();
    }
#if defined(ENABLE_OTLP_GRPC_SSL_MTLS_PREVIEW)
    if (!exporter_cfg.otlp_grpc().ssl_client_key_path().empty()) {
      options.ssl_client_key_path = exporter_cfg.otlp_grpc().ssl_client_key_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_key_string().empty()) {
      options.ssl_client_key_string = exporter_cfg.otlp_grpc().ssl_client_key_string();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_cert_path().empty()) {
      options.ssl_client_cert_path = exporter_cfg.otlp_grpc().ssl_client_cert_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_cert_string().empty()) {
      options.ssl_client_cert_string = exporter_cfg.otlp_grpc().ssl_client_cert_string();
    }
#endif

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

    if (!exporter_cfg.otlp_http().ssl_ca_cert_path().empty()) {
      options.ssl_ca_cert_path = exporter_cfg.otlp_http().ssl_ca_cert_path();
    }
    if (!exporter_cfg.otlp_http().ssl_ca_cert_string().empty()) {
      options.ssl_ca_cert_string = exporter_cfg.otlp_http().ssl_ca_cert_string();
    }
    if (!exporter_cfg.otlp_http().ssl_client_key_path().empty()) {
      options.ssl_client_key_path = exporter_cfg.otlp_http().ssl_client_key_path();
    }
    if (!exporter_cfg.otlp_http().ssl_client_key_string().empty()) {
      options.ssl_client_key_string = exporter_cfg.otlp_http().ssl_client_key_string();
    }
    if (!exporter_cfg.otlp_http().ssl_client_cert_path().empty()) {
      options.ssl_client_cert_path = exporter_cfg.otlp_http().ssl_client_cert_path();
    }
    if (!exporter_cfg.otlp_http().ssl_client_cert_string().empty()) {
      options.ssl_client_cert_string = exporter_cfg.otlp_http().ssl_client_cert_string();
    }
    if (!exporter_cfg.otlp_http().ssl_min_tls().empty()) {
      options.ssl_min_tls = exporter_cfg.otlp_http().ssl_min_tls();
    }
    if (!exporter_cfg.otlp_http().ssl_max_tls().empty()) {
      options.ssl_max_tls = exporter_cfg.otlp_http().ssl_max_tls();
    }
    if (!exporter_cfg.otlp_http().ssl_cipher().empty()) {
      options.ssl_cipher = exporter_cfg.otlp_http().ssl_cipher();
    }
    if (!exporter_cfg.otlp_http().ssl_cipher_suite().empty()) {
      options.ssl_cipher_suite = exporter_cfg.otlp_http().ssl_cipher_suite();
    }

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(options));
  }

  if (exporter_cfg.has_otlp_file() && !exporter_cfg.otlp_file().file_pattern().empty()) {
    opentelemetry::exporter::otlp::OtlpFileExporterOptions options;
    if (exporter_cfg.otlp_file().file_pattern() == "@stdout") {
      options.backend_options = std::ref(std::cout);
    } else if (exporter_cfg.otlp_file().file_pattern() == "@stderr") {
      options.backend_options = std::ref(std::cerr);
    } else {
      opentelemetry::exporter::otlp::OtlpFileClientFileSystemOptions file_backend;
      file_backend.file_pattern = exporter_cfg.otlp_file().file_pattern();
      file_backend.alias_pattern = exporter_cfg.otlp_file().alias_pattern();
      file_backend.flush_interval =
          std::chrono::microseconds{exporter_cfg.otlp_file().flush_interval().seconds() * 1000000};
      file_backend.flush_interval += std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::nanoseconds{exporter_cfg.otlp_file().flush_interval().nanos()});
      file_backend.file_size = exporter_cfg.otlp_file().file_size();
      file_backend.rotate_size = exporter_cfg.otlp_file().rotate_size();

      options.backend_options = std::move(file_backend);
    }

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpFileExporterFactory::Create(options));
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
      ret.emplace_back(opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter)));
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
    ret.emplace_back(opentelemetry::sdk::trace::BatchSpanProcessorFactory::Create(std::move(exporter), options));
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

static local_provider_handle_type<opentelemetry::trace::TracerProvider> _opentelemetry_create_trace_provider(
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> &&processors,
    std::unique_ptr<opentelemetry::sdk::trace::Sampler> &&sampler, opentelemetry::sdk::resource::Resource resource) {
  local_provider_handle_type<opentelemetry::trace::TracerProvider> ret;
  auto processors_ptr = atfw::memory::stl::make_shared<std::vector<opentelemetry::sdk::trace::SpanProcessor *>>();
  if (processors_ptr) {
    processors_ptr->reserve(processors.size());
    for (auto &processor : processors) {
      if (processor) {
        processors_ptr->push_back(processor.get());
      }
    }
  }

  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
      opentelemetry::sdk::trace::TracerProviderFactory::Create(
          std::move(processors), resource, std::move(sampler),
          std::unique_ptr<opentelemetry::sdk::trace::IdGenerator>(new opentelemetry::sdk::trace::RandomIdGenerator()))
          .release());
  ret.shutdown_callback =
      [processors_ptr](const opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> &provider) {
        if (!provider) {
          return;
        }

        auto trace_provider = opentelemetry::trace::Provider::GetTracerProvider();
        if (trace_provider == provider) {
          opentelemetry::trace::Provider::SetTracerProvider(
              opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
                  new opentelemetry::trace::NoopTracerProvider()));
        }

        // 处理优雅退出时无限等待的问题
        if (processors_ptr && !processors_ptr->empty()) {
          std::chrono::microseconds timeout =
              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds{10});
          auto app = atapp::app::get_last_instance();
          if (nullptr != app) {
            timeout = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::seconds{app->get_origin_configure().timer().stop_timeout().seconds() / 2});
            timeout += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::nanoseconds{app->get_origin_configure().timer().stop_timeout().nanos() / 2});
          }
          for (auto &processor : *processors_ptr) {
            processor->Shutdown(timeout);
          }
          processors_ptr->clear();
        }

        static_cast<opentelemetry::sdk::trace::TracerProvider *>(provider.get())->Shutdown();
      };
  return ret;
}

using PushMetricExporter = opentelemetry::sdk::metrics::PushMetricExporter;
static std::vector<std::unique_ptr<PushMetricExporter>> _opentelemetry_create_metrics_exporter(
    ::rpc::telemetry::group_type &group,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_exporter_cfg &exporter_cfg) {
  std::vector<std::unique_ptr<PushMetricExporter>> ret;
  ret.reserve(2);

  if (!exporter_cfg.ostream().empty()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP("stdout", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(opentelemetry::exporter::metrics::OStreamMetricExporterFactory::Create(std::cout));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP("stderr", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(opentelemetry::exporter::metrics::OStreamMetricExporterFactory::Create(std::cerr));
    } else {
      ::opentelemetry::nostd::shared_ptr<std::ofstream> fout{
          new std::ofstream(exporter_cfg.ostream().c_str(), std::ios::out | std::ios::trunc | std::ios::binary)};
      if (fout && fout->is_open()) {
        group.debug_metrics_ostream_exportor = fout;
        ret.emplace_back(opentelemetry::exporter::metrics::OStreamMetricExporterFactory::Create(*fout));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc() && !exporter_cfg.otlp_grpc().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpGrpcMetricExporterOptions options;
    options.endpoint = exporter_cfg.otlp_grpc().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc().insecure();
    if (!exporter_cfg.otlp_grpc().user_agent().empty()) {
      options.user_agent = exporter_cfg.otlp_grpc().user_agent();
    }
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
    switch (exporter_cfg.otlp_grpc().aggregation_temporality()) {
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_UNSPECIFIED: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kUnspecified;
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_DELTA: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kDelta;
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_CUMULATIVE: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kCumulative;
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_LOW_MEMORY: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kLowMemory;
        break;
      }
      default: {
        break;
      }
    }

    options.max_concurrent_requests = exporter_cfg.otlp_grpc().max_concurrent_requests();
    if (!exporter_cfg.otlp_grpc().ssl_ca_cert_path().empty()) {
      options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc().ssl_ca_cert_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_ca_cert_string().empty()) {
      options.ssl_credentials_cacert_as_string = exporter_cfg.otlp_grpc().ssl_ca_cert_string();
    }
#if defined(ENABLE_OTLP_GRPC_SSL_MTLS_PREVIEW)
    if (!exporter_cfg.otlp_grpc().ssl_client_key_path().empty()) {
      options.ssl_client_key_path = exporter_cfg.otlp_grpc().ssl_client_key_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_key_string().empty()) {
      options.ssl_client_key_string = exporter_cfg.otlp_grpc().ssl_client_key_string();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_cert_path().empty()) {
      options.ssl_client_cert_path = exporter_cfg.otlp_grpc().ssl_client_cert_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_cert_string().empty()) {
      options.ssl_client_cert_string = exporter_cfg.otlp_grpc().ssl_client_cert_string();
    }
#endif

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpGrpcMetricExporterFactory::Create(options));
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
    switch (exporter_cfg.otlp_http().aggregation_temporality()) {
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_UNSPECIFIED: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kUnspecified;
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_DELTA: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kDelta;
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_CUMULATIVE: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kCumulative;
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_OPENTELEMETRY_AGGREGATION_LOW_MEMORY: {
        options.aggregation_temporality = opentelemetry::exporter::otlp::PreferredAggregationTemporality::kLowMemory;
        break;
      }
      default: {
        break;
      }
    }

    if (!exporter_cfg.otlp_http().ssl_ca_cert_path().empty()) {
      options.ssl_ca_cert_path = exporter_cfg.otlp_http().ssl_ca_cert_path();
    }
    if (!exporter_cfg.otlp_http().ssl_ca_cert_string().empty()) {
      options.ssl_ca_cert_string = exporter_cfg.otlp_http().ssl_ca_cert_string();
    }
    if (!exporter_cfg.otlp_http().ssl_client_key_path().empty()) {
      options.ssl_client_key_path = exporter_cfg.otlp_http().ssl_client_key_path();
    }
    if (!exporter_cfg.otlp_http().ssl_client_key_string().empty()) {
      options.ssl_client_key_string = exporter_cfg.otlp_http().ssl_client_key_string();
    }
    if (!exporter_cfg.otlp_http().ssl_client_cert_path().empty()) {
      options.ssl_client_cert_path = exporter_cfg.otlp_http().ssl_client_cert_path();
    }
    if (!exporter_cfg.otlp_http().ssl_client_cert_string().empty()) {
      options.ssl_client_cert_string = exporter_cfg.otlp_http().ssl_client_cert_string();
    }
    if (!exporter_cfg.otlp_http().ssl_min_tls().empty()) {
      options.ssl_min_tls = exporter_cfg.otlp_http().ssl_min_tls();
    }
    if (!exporter_cfg.otlp_http().ssl_max_tls().empty()) {
      options.ssl_max_tls = exporter_cfg.otlp_http().ssl_max_tls();
    }
    if (!exporter_cfg.otlp_http().ssl_cipher().empty()) {
      options.ssl_cipher = exporter_cfg.otlp_http().ssl_cipher();
    }
    if (!exporter_cfg.otlp_http().ssl_cipher_suite().empty()) {
      options.ssl_cipher_suite = exporter_cfg.otlp_http().ssl_cipher_suite();
    }

    options.max_concurrent_requests = exporter_cfg.otlp_http().max_concurrent_requests();
    options.max_requests_per_connection = exporter_cfg.otlp_http().max_requests_per_connection();

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(options));
  }

  if (exporter_cfg.has_otlp_file() && !exporter_cfg.otlp_file().file_pattern().empty()) {
    opentelemetry::exporter::otlp::OtlpFileMetricExporterOptions options;
    if (exporter_cfg.otlp_file().file_pattern() == "@stdout") {
      options.backend_options = std::ref(std::cout);
    } else if (exporter_cfg.otlp_file().file_pattern() == "@stderr") {
      options.backend_options = std::ref(std::cerr);
    } else {
      opentelemetry::exporter::otlp::OtlpFileClientFileSystemOptions file_backend;
      file_backend.file_pattern = exporter_cfg.otlp_file().file_pattern();
      file_backend.alias_pattern = exporter_cfg.otlp_file().alias_pattern();
      file_backend.flush_interval =
          std::chrono::microseconds{exporter_cfg.otlp_file().flush_interval().seconds() * 1000000};
      file_backend.flush_interval += std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::nanoseconds{exporter_cfg.otlp_file().flush_interval().nanos()});
      file_backend.file_size = exporter_cfg.otlp_file().file_size();
      file_backend.rotate_size = exporter_cfg.otlp_file().rotate_size();

      options.backend_options = std::move(file_backend);
    }

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpFileMetricExporterFactory::Create(options));
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

    options.populate_target_info = exporter_cfg.prometheus_push().populate_target_info();
    options.without_otel_scope = exporter_cfg.prometheus_push().without_otel_scope();

    ret.emplace_back(exporter::metrics::PrometheusPushExporterFactory::Create(options));
  }

  if (exporter_cfg.has_prometheus_file() && !exporter_cfg.prometheus_file().file_pattern().empty()) {
    exporter::metrics::PrometheusFileExporterOptions options;
    options.file_pattern = exporter_cfg.prometheus_file().file_pattern();
    options.alias_pattern = exporter_cfg.prometheus_file().alias_pattern();
    options.file_size = static_cast<std::size_t>(exporter_cfg.prometheus_file().file_size());
    options.rotate_size = static_cast<std::size_t>(exporter_cfg.prometheus_file().rotate_size());
    options.flush_count = static_cast<std::size_t>(exporter_cfg.prometheus_file().flush_count());
    options.flush_interval = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::seconds{exporter_cfg.prometheus_file().flush_interval().seconds()} +
        std::chrono::nanoseconds{exporter_cfg.prometheus_file().flush_interval().nanos()});

    options.populate_target_info = exporter_cfg.prometheus_push().populate_target_info();
    options.without_otel_scope = exporter_cfg.prometheus_push().without_otel_scope();

    ret.emplace_back(exporter::metrics::PrometheusFileExporterFactory::Create(options));
  }

  return ret;
}

static std::vector<std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>> _opentelemetry_create_metrics_reader(
    std::vector<std::unique_ptr<PushMetricExporter>> &&exporters,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_reader_cfg &reader_cfg,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_exporter_cfg &exporter_cfg) {
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

  ret.reserve(exporters.size() + 1);
  for (auto &exporter : exporters) {
    ret.emplace_back(
        opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), options));
  }

  if (exporter_cfg.has_prometheus_pull() && !exporter_cfg.prometheus_pull().url().empty()) {
    opentelemetry::exporter::metrics::PrometheusExporterOptions exporter_options;
    exporter_options.url = exporter_cfg.prometheus_pull().url();
    exporter_options.populate_target_info = exporter_cfg.prometheus_push().populate_target_info();
    exporter_options.without_otel_scope = exporter_cfg.prometheus_push().without_otel_scope();
    ret.emplace_back(opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(exporter_options));
  }

  return ret;
}

static local_provider_handle_type<opentelemetry::metrics::MeterProvider> _opentelemetry_create_metrics_provider(
    std::vector<std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>> &&readers,
    const opentelemetry::sdk::resource::ResourceAttributes &metrics_resource_values,
    opentelemetry::nostd::string_view trace_metrics_name) {
  local_provider_handle_type<opentelemetry::metrics::MeterProvider> ret;
  auto readers_ptr = atfw::memory::stl::make_shared<std::vector<opentelemetry::sdk::metrics::MetricReader *>>();
  if (readers_ptr) {
    readers_ptr->reserve(readers.size());
    for (auto &reader : readers) {
      if (reader) {
        readers_ptr->push_back(reader.get());
      }
    }
  }

  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>(
      new opentelemetry::sdk::metrics::MeterProvider(
          std::unique_ptr<opentelemetry::sdk::metrics::ViewRegistry>(new opentelemetry::sdk::metrics::ViewRegistry()),
          opentelemetry::sdk::resource::Resource::Create(metrics_resource_values)));

  if (ret.provider) {
    for (auto &reader : readers) {
      static_cast<opentelemetry::sdk::metrics::MeterProvider *>(ret.provider.get())->AddMetricReader(std::move(reader));
    }

    // Special trace view for default group
    if (!trace_metrics_name.empty()) {
      std::shared_ptr<opentelemetry::sdk::metrics::HistogramAggregationConfig> config(
          new opentelemetry::sdk::metrics::HistogramAggregationConfig());
      config->boundaries_ = {0, 2000, 8000, 16000, 32000, 128000, 1024000, 3000000, 8000000};
      std::unique_ptr<opentelemetry::sdk::metrics::View> view = opentelemetry::sdk::metrics::ViewFactory::Create(
          static_cast<std::string>(trace_metrics_name) + "_view", "",
#if (OPENTELEMETRY_VERSION_MAJOR * 1000 + OPENTELEMETRY_VERSION_MINOR) >= 1011
          "us",
#endif
          opentelemetry::sdk::metrics::AggregationType::kHistogram, config);
      std::unique_ptr<opentelemetry::sdk::metrics::InstrumentSelector> instrument_selector =
          opentelemetry::sdk::metrics::InstrumentSelectorFactory::Create(
              opentelemetry::sdk::metrics::InstrumentType::kHistogram, "*"
#if (OPENTELEMETRY_VERSION_MAJOR * 1000 + OPENTELEMETRY_VERSION_MINOR) >= 1011
              ,
              ""
#endif
          );
      std::unique_ptr<opentelemetry::sdk::metrics::MeterSelector> meter_selector =
          opentelemetry::sdk::metrics::MeterSelectorFactory::Create(static_cast<std::string>(trace_metrics_name), "",
                                                                    "");
      static_cast<opentelemetry::sdk::metrics::MeterProvider *>(ret.provider.get())
          ->AddView(std::move(instrument_selector), std::move(meter_selector), std::move(view));
    }
  }

  ret.shutdown_callback =
      [readers_ptr](const opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider> &provider) {
        if (!provider) {
          return;
        }

        auto metrics_provider = opentelemetry::metrics::Provider::GetMeterProvider();
        if (metrics_provider == provider) {
          opentelemetry::metrics::Provider::SetMeterProvider(
              opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>(
                  new opentelemetry::metrics::NoopMeterProvider()));
        }

        // 处理优雅退出时无限等待的问题
        if (readers_ptr && !readers_ptr->empty()) {
          std::chrono::microseconds timeout =
              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds{10});
          auto app = atapp::app::get_last_instance();
          if (nullptr != app) {
            timeout = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::seconds{app->get_origin_configure().timer().stop_timeout().seconds() / 2});
            timeout += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::nanoseconds{app->get_origin_configure().timer().stop_timeout().nanos() / 2});
          }
          for (auto &reader : *readers_ptr) {
            reader->Shutdown(timeout);
          }
          readers_ptr->clear();
        }

        static_cast<opentelemetry::sdk::metrics::MeterProvider *>(provider.get())->Shutdown();
      };
  return ret;
}

static std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>> _opentelemetry_create_logs_exporter(
    ::rpc::telemetry::group_type &group,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_exporter_cfg &exporter_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>> ret;
  ret.reserve(2);

  if (!exporter_cfg.ostream().empty()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP("stdout", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>(
          new opentelemetry::exporter::logs::OStreamLogRecordExporter(std::cout)));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP("stderr", exporter_cfg.ostream().c_str())) {
      ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>(
          new opentelemetry::exporter::logs::OStreamLogRecordExporter(std::cerr)));
    } else {
      ::opentelemetry::nostd::shared_ptr<std::ofstream> fout{
          new std::ofstream(exporter_cfg.ostream().c_str(), std::ios::out | std::ios::trunc | std::ios::binary)};
      if (fout && fout->is_open()) {
        group.debug_logger_ostream_exportor = fout;
        ret.emplace_back(std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>(
            new opentelemetry::exporter::logs::OStreamLogRecordExporter(*fout)));
      }
    }
  }

  if (exporter_cfg.has_otlp_grpc() && !exporter_cfg.otlp_grpc().endpoint().empty()) {
#if (OPENTELEMETRY_VERSION_MAJOR * 1000 + OPENTELEMETRY_VERSION_MINOR) >= 1013
    opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterOptions options;
#else
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions options;
#endif
    options.endpoint = exporter_cfg.otlp_grpc().endpoint();
    options.use_ssl_credentials = !exporter_cfg.otlp_grpc().insecure();
    if (!exporter_cfg.otlp_grpc().user_agent().empty()) {
      options.user_agent = exporter_cfg.otlp_grpc().user_agent();
    }
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

    options.max_concurrent_requests = exporter_cfg.otlp_grpc().max_concurrent_requests();
    if (!exporter_cfg.otlp_grpc().ssl_ca_cert_path().empty()) {
      options.ssl_credentials_cacert_path = exporter_cfg.otlp_grpc().ssl_ca_cert_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_ca_cert_string().empty()) {
      options.ssl_credentials_cacert_as_string = exporter_cfg.otlp_grpc().ssl_ca_cert_string();
    }
#if defined(ENABLE_OTLP_GRPC_SSL_MTLS_PREVIEW)
    if (!exporter_cfg.otlp_grpc().ssl_client_key_path().empty()) {
      options.ssl_client_key_path = exporter_cfg.otlp_grpc().ssl_client_key_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_key_string().empty()) {
      options.ssl_client_key_string = exporter_cfg.otlp_grpc().ssl_client_key_string();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_cert_path().empty()) {
      options.ssl_client_cert_path = exporter_cfg.otlp_grpc().ssl_client_cert_path();
    }
    if (!exporter_cfg.otlp_grpc().ssl_client_cert_string().empty()) {
      options.ssl_client_cert_string = exporter_cfg.otlp_grpc().ssl_client_cert_string();
    }
#endif

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterFactory::Create(options));
  }

  if (exporter_cfg.has_otlp_http() && !exporter_cfg.otlp_http().endpoint().empty()) {
    opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions options;
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

    if (!exporter_cfg.otlp_http().ssl_ca_cert_path().empty()) {
      options.ssl_ca_cert_path = exporter_cfg.otlp_http().ssl_ca_cert_path();
    }
    if (!exporter_cfg.otlp_http().ssl_ca_cert_string().empty()) {
      options.ssl_ca_cert_string = exporter_cfg.otlp_http().ssl_ca_cert_string();
    }
    if (!exporter_cfg.otlp_http().ssl_client_key_path().empty()) {
      options.ssl_client_key_path = exporter_cfg.otlp_http().ssl_client_key_path();
    }
    if (!exporter_cfg.otlp_http().ssl_client_key_string().empty()) {
      options.ssl_client_key_string = exporter_cfg.otlp_http().ssl_client_key_string();
    }
    if (!exporter_cfg.otlp_http().ssl_client_cert_path().empty()) {
      options.ssl_client_cert_path = exporter_cfg.otlp_http().ssl_client_cert_path();
    }
    if (!exporter_cfg.otlp_http().ssl_client_cert_string().empty()) {
      options.ssl_client_cert_string = exporter_cfg.otlp_http().ssl_client_cert_string();
    }
    if (!exporter_cfg.otlp_http().ssl_min_tls().empty()) {
      options.ssl_min_tls = exporter_cfg.otlp_http().ssl_min_tls();
    }
    if (!exporter_cfg.otlp_http().ssl_max_tls().empty()) {
      options.ssl_max_tls = exporter_cfg.otlp_http().ssl_max_tls();
    }
    if (!exporter_cfg.otlp_http().ssl_cipher().empty()) {
      options.ssl_cipher = exporter_cfg.otlp_http().ssl_cipher();
    }
    if (!exporter_cfg.otlp_http().ssl_cipher_suite().empty()) {
      options.ssl_cipher_suite = exporter_cfg.otlp_http().ssl_cipher_suite();
    }

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(options));
  }

  if (exporter_cfg.has_otlp_file() && !exporter_cfg.otlp_file().file_pattern().empty()) {
    opentelemetry::exporter::otlp::OtlpFileLogRecordExporterOptions options;
    if (exporter_cfg.otlp_file().file_pattern() == "@stdout") {
      options.backend_options = std::ref(std::cout);
    } else if (exporter_cfg.otlp_file().file_pattern() == "@stderr") {
      options.backend_options = std::ref(std::cerr);
    } else {
      opentelemetry::exporter::otlp::OtlpFileClientFileSystemOptions file_backend;
      file_backend.file_pattern = exporter_cfg.otlp_file().file_pattern();
      file_backend.alias_pattern = exporter_cfg.otlp_file().alias_pattern();
      file_backend.flush_interval =
          std::chrono::microseconds{exporter_cfg.otlp_file().flush_interval().seconds() * 1000000};
      file_backend.flush_interval += std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::nanoseconds{exporter_cfg.otlp_file().flush_interval().nanos()});
      file_backend.file_size = exporter_cfg.otlp_file().file_size();
      file_backend.rotate_size = exporter_cfg.otlp_file().rotate_size();

      options.backend_options = std::move(file_backend);
    }

    ret.emplace_back(opentelemetry::exporter::otlp::OtlpFileLogRecordExporterFactory::Create(options));
  }

  return ret;
}

static std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor>> _opentelemetry_create_logs_processor(
    std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>> &&exporters,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_processor_cfg &processor_cfg) {
  std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor>> ret;
  ret.reserve(exporters.size());
  if (processor_cfg.has_simple() && !processor_cfg.has_batch()) {
    for (auto &exporter : exporters) {
      ret.emplace_back(opentelemetry::sdk::logs::SimpleLogRecordProcessorFactory::Create(std::move(exporter)));
    }
    return ret;
  }

  opentelemetry::v1::sdk::logs::BatchLogRecordProcessorOptions options;
  options.max_export_batch_size = static_cast<size_t>(processor_cfg.batch().send_batch_size());
  options.max_queue_size = static_cast<size_t>(processor_cfg.batch().send_batch_max_size());
  options.schedule_delay_millis = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::seconds(processor_cfg.batch().timeout().seconds()) +
      std::chrono::nanoseconds(processor_cfg.batch().timeout().nanos()));

  for (auto &exporter : exporters) {
    ret.emplace_back(opentelemetry::sdk::logs::BatchLogRecordProcessorFactory::Create(std::move(exporter), options));
  }
  return ret;
}

static local_provider_handle_type<opentelemetry::logs::LoggerProvider> _opentelemetry_create_logs_provider(
    std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor>> &&processors,
    opentelemetry::sdk::resource::Resource resource) {
  local_provider_handle_type<opentelemetry::logs::LoggerProvider> ret;
  auto processors_ptr = atfw::memory::stl::make_shared<std::vector<opentelemetry::sdk::logs::LogRecordProcessor *>>();
  if (processors_ptr) {
    processors_ptr->reserve(processors.size());
    for (auto &processor : processors) {
      if (processor) {
        processors_ptr->push_back(processor.get());
      }
    }
  }

  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>(
      opentelemetry::sdk::logs::LoggerProviderFactory::Create(std::move(processors), std::move(resource)).release());
  ret.shutdown_callback =
      [processors_ptr](const opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider> &provider) {
        if (!provider) {
          return;
        }

        auto logs_provider = opentelemetry::logs::Provider::GetLoggerProvider();
        if (logs_provider == provider) {
          opentelemetry::logs::Provider::SetLoggerProvider(
              opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>(
                  new opentelemetry::logs::NoopLoggerProvider()));
        }

        // 处理优雅退出时无限等待的问题
        if (processors_ptr && !processors_ptr->empty()) {
          std::chrono::microseconds timeout =
              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds{10});
          auto app = atapp::app::get_last_instance();
          if (nullptr != app) {
            timeout = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::seconds{app->get_origin_configure().timer().stop_timeout().seconds() / 2});
            timeout += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::nanoseconds{app->get_origin_configure().timer().stop_timeout().nanos() / 2});
          }
          for (auto &processor : *processors_ptr) {
            processor->Shutdown(timeout);
          }
          processors_ptr->clear();
        }

        static_cast<opentelemetry::sdk::logs::LoggerProvider *>(provider.get())->Shutdown();
      };
  return ret;
}

static void _opentelemetry_cleanup_group(std::shared_ptr<::rpc::telemetry::group_type> group,
                                         const std::shared_ptr<global_service_data_type> &current_service_cache) {
  std::list<std::shared_ptr<std::thread>> shutdown_threads;

  if (current_service_cache && group && group->initialized) {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{
        current_service_cache->on_group_destroy_callback_lock};
    for (auto &callback_fn : current_service_cache->on_group_destroy_callbacks) {
      if (callback_fn) {
        callback_fn(group);
      }
    }

    group->initialized = false;
  }

  if (group) {
    // Provider must be destroy before logger
    if (group->logs_handle.provider) {
      if (group->logs_handle.shutdown_callback) {
        auto handle = group->logs_handle;
        shutdown_threads.push_back(
            atfw::memory::stl::make_shared<std::thread>([handle]() { handle.shutdown_callback(handle.provider); }));
      }
      group->logs_handle.reset();
    }
    group->default_logger = opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger>();

    // Provider must be destroy before meter
    if (group->metrics_handle.provider) {
      if (group->metrics_handle.shutdown_callback) {
        auto handle = group->metrics_handle;
        shutdown_threads.push_back(
            atfw::memory::stl::make_shared<std::thread>([handle]() { handle.shutdown_callback(handle.provider); }));
      }
      group->metrics_handle.reset();
    }
    group->default_metrics_meter.reset();
    group->metrics_meters.clear();

    // Provider must be destroy before tracer
    if (group->tracer_handle.provider) {
      if (group->tracer_handle.shutdown_callback) {
        auto handle = group->tracer_handle;
        shutdown_threads.push_back(
            atfw::memory::stl::make_shared<std::thread>([handle]() { handle.shutdown_callback(handle.provider); }));
      }
      group->tracer_handle.reset();
    }

    group->default_tracer = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>();
  }

  while (!shutdown_threads.empty()) {
    if (!shutdown_threads.front()) {
      shutdown_threads.pop_front();
      continue;
    }

    if (!shutdown_threads.front()->joinable()) {
      shutdown_threads.pop_front();
      continue;
    }

    shutdown_threads.front()->join();
    shutdown_threads.pop_front();
  }
}

static void _opentelemetry_cleanup_global_provider(atapp::app & /*app*/) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  std::list<std::shared_ptr<std::thread>> cleanup_threads;
  if (current_service_cache) {
    current_service_cache->shutdown = true;

    std::shared_ptr<group_type> default_group;
    std::unordered_map<std::string, std::shared_ptr<group_type>> named_groups;

    {
      util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
      default_group.swap(current_service_cache->default_group);
      named_groups.swap(current_service_cache->named_groups);
    }

    cleanup_threads.push_back(atfw::memory::stl::make_shared<std::thread>([default_group, current_service_cache]() {
      _opentelemetry_cleanup_group(default_group, current_service_cache);
    }));

    for (auto &named_group : named_groups) {
      std::shared_ptr<group_type> named_group_ptr = named_group.second;
      if (!named_group_ptr) {
        continue;
      }

      cleanup_threads.push_back(atfw::memory::stl::make_shared<std::thread>([named_group_ptr, current_service_cache]() {
        _opentelemetry_cleanup_group(named_group_ptr, current_service_cache);
      }));
    }
  }

  while (!cleanup_threads.empty()) {
    if (!cleanup_threads.front()) {
      cleanup_threads.pop_front();
      continue;
    }

    if (!cleanup_threads.front()->joinable()) {
      cleanup_threads.pop_front();
      continue;
    }

    cleanup_threads.front()->join();
    cleanup_threads.pop_front();
  }
}

static void _opentelemetry_prepare_group(
    atapp::app &app, std::shared_ptr<group_type> group, std::string group_name,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_cfg &group_config,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_agent_cfg *group_agent_config,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_trace_cfg &default_tracer_config,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_cfg &default_metrics_config,
    const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_cfg &default_logs_config) {
  group->group_configure.set_name(group_name);
  *group->group_configure.mutable_configure() = group_config;
  if (nullptr != group_agent_config) {
    *group->group_configure.mutable_agent() = *group_agent_config;
  }

  // Trace
  {
    const PROJECT_NAMESPACE_ID::config::opentelemetry_trace_cfg &group_tracer_config =
        group->group_configure.configure().trace();

    group->trace_default_library_name = group_tracer_config.default_name();
    if (group->trace_default_library_name.empty()) {
      group->trace_default_library_name = default_tracer_config.default_name();
    }
    if (group->trace_default_library_name.empty()) {
      group->trace_default_library_name = app.get_type_name();
    }
    if (group->trace_default_library_name.empty()) {
      group->trace_default_library_name = app.get_app_name();
    }

    group->trace_default_library_version = app.get_app_version();

    group->trace_default_schema_url = group_tracer_config.schema_url();
    if (group->trace_default_schema_url.empty()) {
      group->trace_default_schema_url = default_tracer_config.schema_url();
    }

    if (&group_tracer_config == &default_tracer_config) {
      group->trace_additional_resource.reserve(static_cast<std::size_t>(group_tracer_config.resource_size()));
      for (auto &kv : group_tracer_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->trace_additional_resource[kv.first] = kv.second;
      }
    } else {
      group->trace_additional_resource.reserve(
          static_cast<std::size_t>(group_tracer_config.resource_size() + default_tracer_config.resource_size()));
      for (auto &kv : default_tracer_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->trace_additional_resource[kv.first] = kv.second;
      }
      for (auto &kv : group_tracer_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->trace_additional_resource[kv.first] = kv.second;
      }
    }
  }

  // Metrics
  {
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_cfg &group_metrics_config =
        group->group_configure.configure().metrics();

    group->metrics_default_library_name = group_metrics_config.default_name();
    group->metrics_default_library_version = app.get_app_version();
    group->metrics_default_schema_url = group_metrics_config.schema_url();
    if (group->metrics_default_schema_url.empty()) {
      group->metrics_default_schema_url = default_metrics_config.schema_url();
    }

    if (&group_metrics_config == &default_metrics_config) {
      group->metrics_additional_resource.reserve(static_cast<std::size_t>(group_metrics_config.resource_size()));
      for (auto &kv : group_metrics_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->metrics_additional_resource[kv.first] = kv.second;
      }
    } else {
      group->metrics_additional_resource.reserve(
          static_cast<std::size_t>(group_metrics_config.resource_size() + default_metrics_config.resource_size()));
      for (auto &kv : default_metrics_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->metrics_additional_resource[kv.first] = kv.second;
      }
      for (auto &kv : group_metrics_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->metrics_additional_resource[kv.first] = kv.second;
      }
    }
  }

  // Logs
  {
    const PROJECT_NAMESPACE_ID::config::opentelemetry_logs_cfg &group_logs_config =
        group->group_configure.configure().logs();

    group->logs_default_logger_name = group_logs_config.default_name();
    if (group->logs_default_logger_name.empty()) {
      group->logs_default_logger_name = default_logs_config.default_name();
    }
    if (group->logs_default_logger_name.empty()) {
      group->logs_default_logger_name = app.get_type_name();
    }
    if (group->logs_default_logger_name.empty()) {
      group->logs_default_logger_name = app.get_app_name();
    }
    group->logs_default_library_name = app.get_app_name();
    group->logs_default_library_version = app.get_app_version();
    group->logs_default_schema_url = group_logs_config.schema_url();
    if (group->logs_default_schema_url.empty()) {
      group->logs_default_schema_url = default_logs_config.schema_url();
    }

    if (&group_logs_config == &default_logs_config) {
      group->logs_additional_resource.reserve(static_cast<std::size_t>(group_logs_config.resource_size()));
      for (auto &kv : group_logs_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->logs_additional_resource[kv.first] = kv.second;
      }
    } else {
      group->logs_additional_resource.reserve(
          static_cast<std::size_t>(group_logs_config.resource_size() + default_logs_config.resource_size()));
      for (auto &kv : default_logs_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->logs_additional_resource[kv.first] = kv.second;
      }
      for (auto &kv : group_logs_config.resource()) {
        if (kv.first.empty() || kv.second.empty()) {
          continue;
        }
        group->logs_additional_resource[kv.first] = kv.second;
      }
    }
  }
}

static void _opentelemetry_setup_group(atapp::app &app, std::shared_ptr<group_type> group,
                                       local_provider_handle_type<opentelemetry::trace::TracerProvider> tracer_handle,
                                       local_provider_handle_type<opentelemetry::metrics::MeterProvider> metrics_handle,
                                       local_provider_handle_type<opentelemetry::logs::LoggerProvider> logs_handle) {
  if (group->initialized || app.is_closing()) {
    return;
  }

  // Default tracer
  if (!tracer_handle.provider) {
    tracer_handle.provider = opentelemetry::trace::Provider::GetTracerProvider();
    tracer_handle.reset_shutdown_callback();
  }
  group->tracer_handle = tracer_handle;
  group->default_tracer = tracer_handle.provider->GetTracer(
      group->trace_default_library_name, group->trace_default_library_version, group->trace_default_schema_url);

  // Default meter
  if (!metrics_handle.provider) {
    metrics_handle.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>{
        new opentelemetry::metrics::NoopMeterProvider()};
    metrics_handle.reset_shutdown_callback();
  }
  group->metrics_handle = metrics_handle;
  do {
    if (!metrics_handle.provider) {
      break;
    }

    if (group->metrics_default_library_name.empty()) {
      break;
    }

    auto default_metrics_meter = atfw::memory::stl::make_shared<local_meter_info_type>();
    if (!default_metrics_meter) {
      break;
    }

    default_metrics_meter->meter = metrics_handle.provider->GetMeter(
        group->metrics_default_library_name, group->metrics_default_library_version, group->metrics_default_schema_url);

    if (!default_metrics_meter->meter) {
      break;
    }

    group->default_metrics_meter = default_metrics_meter;
    group->metrics_meters[group->metrics_default_library_name] = default_metrics_meter;
  } while (false);

  // Default logger
  if (!logs_handle.provider) {
    logs_handle.provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>{
        new opentelemetry::logs::NoopLoggerProvider()};
    logs_handle.reset_shutdown_callback();
  }
  group->logs_handle = logs_handle;
  group->default_logger =
      logs_handle.provider->GetLogger(group->logs_default_logger_name, group->logs_default_library_name,
                                      group->logs_default_library_version, group->logs_default_schema_url);

  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return;
  }

  std::shared_ptr<group_type> previous_group = group;
  if (group->group_name.empty()) {
    current_service_cache->default_group.swap(previous_group);
  } else {
    current_service_cache->named_groups[group->group_name].swap(previous_group);
  }

  // Shutdown in another thread to avoid blocking
  do {
    if (!previous_group || previous_group == group) {
      break;
    }
    if (previous_group->logs_handle.provider == group->logs_handle.provider) {
      previous_group->logs_handle.reset();
    }
    if (previous_group->metrics_handle.provider == group->metrics_handle.provider) {
      previous_group->metrics_handle.reset();
    }
    if (previous_group->tracer_handle.provider == group->tracer_handle.provider) {
      previous_group->tracer_handle.reset();
    }
    std::thread cleanup_thread([previous_group, current_service_cache]() {
      _opentelemetry_cleanup_group(previous_group, current_service_cache);
    });
    cleanup_thread.detach();
  } while (false);

  group->initialized = true;
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{
        current_service_cache->on_group_create_callback_lock};
    for (auto &callback_fn : current_service_cache->on_group_create_callbacks) {
      if (callback_fn) {
        callback_fn(group);
      }
    }
  }

  // Skip set singlethon instance if it's not a default group
  if (!group->group_name.empty()) {
    return;
  }

  // Internal Logger
  current_service_cache->internal_logger = util::log::log_wrapper::create_user_logger();
  ::atapp::protocol::atapp_log opentelemetry_log_conf;
  app.parse_log_configures_into(opentelemetry_log_conf,
                                std::vector<gsl::string_view>{"logic", "telemetry", "opentelemetry", "app_log"},
                                "ATAPP_LOGIC_TELEMETRY_OPENTELEMETRY_LOG");
  util::log::log_formatter::level_t::type opentelemetry_log_level =
      util::log::log_formatter::get_level_by_name(opentelemetry_log_conf.level().c_str());
  if (current_service_cache->internal_logger && opentelemetry_log_conf.category_size() > 0) {
    current_service_cache->internal_logger->init(opentelemetry_log_level);
    app.setup_logger(*current_service_cache->internal_logger, opentelemetry_log_conf.level(),
                     opentelemetry_log_conf.category(0));
  }

  static std::once_flag setup_global_handle_flag;
  std::call_once(setup_global_handle_flag, [&app]() {
    // Setup global log handle for opentelemetry for first startup
    opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogHandler(
        opentelemetry::nostd::shared_ptr<opentelemetry::sdk::common::internal_log::LogHandler>{
            new opentelemetry_internal_log_handler()});
    app.add_evt_on_finally(_opentelemetry_cleanup_global_provider);
  });

  // Set the global trace/metrics provider and service cache.
  opentelemetry::trace::Provider::SetTracerProvider(tracer_handle.provider);
  opentelemetry::metrics::Provider::SetMeterProvider(metrics_handle.provider);
  opentelemetry::logs::Provider::SetLoggerProvider(logs_handle.provider);

  if (opentelemetry_log_level <= util::log::log_formatter::level_t::LOG_LW_ERROR) {
    opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogLevel(
        opentelemetry::sdk::common::internal_log::LogLevel::Error);
  } else if (opentelemetry_log_level <= util::log::log_formatter::level_t::LOG_LW_WARNING) {
    opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogLevel(
        opentelemetry::sdk::common::internal_log::LogLevel::Warning);
  } else if (opentelemetry_log_level <= util::log::log_formatter::level_t::LOG_LW_INFO) {
    opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogLevel(
        opentelemetry::sdk::common::internal_log::LogLevel::Info);
  } else {
    opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogLevel(
        opentelemetry::sdk::common::internal_log::LogLevel::Debug);
  }
}

static void _create_opentelemetry_app_resource(
    group_type &group, const atapp::app &app,
    std::initializer_list<const ::google::protobuf::Map<std::string, std::string> *> resource_configures) {
  // @see
  // https://github.com/open-telemetry/semantic-conventions/blob/main/docs/resource/README.md

  // metrics 维度不能保护无关属性
  group.metrics_attributes.clear();

  // basic
  group.common_owned_attributes.SetAttribute(opentelemetry::sdk::resource::SemanticConventions::kServiceInstanceId,
                                             app.get_id());
  group.common_owned_attributes.SetAttribute("service.instance.name", app.get_app_name());
  group.common_owned_attributes.SetAttribute(opentelemetry::sdk::resource::SemanticConventions::kServiceName,
                                             app.get_type_name());
  group.common_owned_attributes.SetAttribute("service.identity", app.get_app_identity());
  group.common_owned_attributes.SetAttribute("service.type_id", app.get_type_id());
  group.common_owned_attributes.SetAttribute(opentelemetry::sdk::resource::SemanticConventions::kServiceVersion,
                                             server_frame_project_get_version());

  // area
  if (0 != app.get_area().zone_id()) {
    group.common_owned_attributes.SetAttribute("service.area.zone_id", app.get_area().zone_id());
    set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes, "service.area.zone_id");
  }
  if (!app.get_area().region().empty()) {
    group.common_owned_attributes.SetAttribute("service.area.region", app.get_area().region());
    set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes, "service.area.region");
  }
  if (!app.get_area().district().empty()) {
    group.common_owned_attributes.SetAttribute("service.area.district", app.get_area().district());
    set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes, "service.area.district");
  }

  // metadata
  if (!app.get_metadata().namespace_name().empty()) {
    group.common_owned_attributes.SetAttribute(opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace,
                                               app.get_metadata().namespace_name());
    set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes,
                            opentelemetry::sdk::resource::SemanticConventions::kServiceNamespace);
  }

  {
    auto iter =
        app.get_metadata().labels().find(opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName);
    if (iter != app.get_metadata().labels().end()) {
      group.common_owned_attributes.SetAttribute(
          opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName, iter->second);
      set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes,
                              opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName);
    }
  }

  // process
  // @see
  // https://github.com/open-telemetry/semantic-conventions/blob/main/docs/resource/process.md#process
  group.common_owned_attributes.SetAttribute(opentelemetry::sdk::resource::SemanticConventions::kProcessPid,
                                             atbus::node::get_pid());
  {
    if (!app.get_origin_configure().hostname().empty()) {
      group.common_owned_attributes.SetAttribute(opentelemetry::sdk::resource::SemanticConventions::kHostName,
                                                 app.get_origin_configure().hostname());
      set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes,
                              opentelemetry::sdk::resource::SemanticConventions::kHostName);
    } else {
      group.common_owned_attributes.SetAttribute(opentelemetry::sdk::resource::SemanticConventions::kHostName,
                                                 atbus::node::get_hostname());
      set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes,
                              opentelemetry::sdk::resource::SemanticConventions::kHostName);
    }
  }

  for (auto &resource_configure : resource_configures) {
    if (nullptr == resource_configure) {
      continue;
    }

    for (auto &kv : *resource_configure) {
      if (kv.second.empty() || kv.first.empty()) {
        continue;
      }

      group.common_owned_attributes.SetAttribute(kv.first, kv.second);
      set_attributes_map_item(group.common_owned_attributes, group.metrics_attributes, kv.first);
    }
  }

  // Other common resource should be set by configure generator
  rebuild_attributes_map(group.common_owned_attributes, group.common_attributes);

  group.metrics_attributes_view.clear();
  group.metrics_attributes_view.reserve(group.metrics_attributes.size());
  for (auto &metric_data : group.metrics_attributes) {
    group.metrics_attributes_view.push_back(metric_data);
  }
}

static local_provider_handle_type<opentelemetry::trace::TracerProvider>
_opentelemetry_create_opentelemetry_trace_provider(::rpc::telemetry::group_type &group) {
  if (group.group_configure.configure().trace().has_exporters() &&
      group.group_configure.configure().trace().has_processors()) {
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>> exporter;
    if (group.group_configure.has_agent() && group.group_configure.agent().enable_trace()) {
      exporter = _opentelemetry_create_trace_exporter(group, group.group_configure.agent().trace_exporters());
    }
    if (exporter.empty()) {
      exporter = _opentelemetry_create_trace_exporter(group, group.group_configure.configure().trace().exporters());
    }
    auto sampler = _opentelemetry_create_trace_sampler(group.group_configure.configure().trace().samplers());
    if (!sampler) {
      local_provider_handle_type<opentelemetry::trace::TracerProvider> ret;
      ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>();
      ret.reset_shutdown_callback();
      return ret;
    }
    group.tracer_exporter_count = exporter.size();
    auto processor = _opentelemetry_create_trace_processor(std::move(exporter),
                                                           group.group_configure.configure().trace().processors());

    opentelemetry::sdk::resource::ResourceAttributes trace_resource_values = group.common_owned_attributes;
    for (auto &ext_res : group.trace_additional_resource) {
      trace_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }
    return _opentelemetry_create_trace_provider(std::move(processor), std::move(sampler),
                                                opentelemetry::sdk::resource::Resource::Create(trace_resource_values));
  }

  local_provider_handle_type<opentelemetry::trace::TracerProvider> ret;
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>();
  ret.reset_shutdown_callback();
  return ret;
}

static local_provider_handle_type<opentelemetry::metrics::MeterProvider>
_opentelemetry_create_opentelemetry_metrics_provider(::rpc::telemetry::group_type &group) {
  if (group.group_configure.configure().metrics().has_exporters()) {
    std::vector<std::unique_ptr<PushMetricExporter>> exporters;
    std::vector<std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>> readers;
    const PROJECT_NAMESPACE_ID::config::opentelemetry_metrics_exporter_cfg *exporters_cfg =
        &group.group_configure.configure().metrics().exporters();
    if (group.group_configure.has_agent() && group.group_configure.agent().enable_metrics()) {
      exporters = _opentelemetry_create_metrics_exporter(group, group.group_configure.agent().metrics_exporters());
    }
    if (exporters.empty()) {
      exporters =
          _opentelemetry_create_metrics_exporter(group, group.group_configure.configure().metrics().exporters());
    } else {
      exporters_cfg = &group.group_configure.agent().metrics_exporters();
    }
    group.metrics_exporter_count = exporters.size();
    if (group.group_configure.has_agent() && group.group_configure.agent().enable_metrics()) {
      readers = _opentelemetry_create_metrics_reader(
          std::move(exporters), group.group_configure.configure().metrics().reader(), *exporters_cfg);
    }
    if (readers.empty()) {
      readers = _opentelemetry_create_metrics_reader(
          std::move(exporters), group.group_configure.configure().metrics().reader(), *exporters_cfg);
    }

    opentelemetry::sdk::resource::ResourceAttributes metrics_resource_values = group.common_owned_attributes;
    for (auto &ext_res : group.metrics_additional_resource) {
      metrics_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }

    if (readers.empty()) {
      local_provider_handle_type<opentelemetry::metrics::MeterProvider> ret;
      ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>();
      ret.reset_shutdown_callback();
      return ret;
    }

    opentelemetry::nostd::string_view trace_metrics_name;
    if (group.group_configure.name().empty()) {
      trace_metrics_name = group.group_configure.configure().trace().additional_metrics_name();
    }
    return _opentelemetry_create_metrics_provider(std::move(readers), metrics_resource_values, trace_metrics_name);
  }

  local_provider_handle_type<opentelemetry::metrics::MeterProvider> ret;
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>();
  ret.reset_shutdown_callback();
  return ret;
}

static local_provider_handle_type<opentelemetry::logs::LoggerProvider>
_opentelemetry_create_opentelemetry_logs_provider(::rpc::telemetry::group_type &group) {
  if (group.group_configure.configure().logs().has_exporters()) {
    std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>> exporter;
    if (group.group_configure.has_agent() && group.group_configure.agent().enable_logs()) {
      exporter = _opentelemetry_create_logs_exporter(group, group.group_configure.agent().logs_exporters());
    }
    if (exporter.empty()) {
      exporter = _opentelemetry_create_logs_exporter(group, group.group_configure.configure().logs().exporters());
    }
    group.logger_exporter_count = exporter.size();
    auto processor = _opentelemetry_create_logs_processor(std::move(exporter),
                                                          group.group_configure.configure().logs().processors());

    opentelemetry::sdk::resource::ResourceAttributes logs_resource_values = group.common_owned_attributes;
    for (auto &ext_res : group.logs_additional_resource) {
      logs_resource_values.SetAttribute(ext_res.first, ext_res.second);
    }
    return _opentelemetry_create_logs_provider(std::move(processor),
                                               opentelemetry::sdk::resource::Resource::Create(logs_resource_values));
  }

  local_provider_handle_type<opentelemetry::logs::LoggerProvider> ret;
  ret.provider = opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>();
  ret.reset_shutdown_callback();
  return ret;
}

static void _opentelemetry_initialize_group(const std::shared_ptr<group_type> &group) {
  if (!group) {
    return;
  }
  if (group->initialized) {
    return;
  }

  atapp::app *app_inst = atapp::app::get_last_instance();
  if (nullptr == app_inst) {
    return;
  }
  if (app_inst->is_closing()) {
    return;
  }

  // Trace
  local_provider_handle_type<opentelemetry::trace::TracerProvider> tracer_handle =
      _opentelemetry_create_opentelemetry_trace_provider(*group);

  // Metrics
  local_provider_handle_type<opentelemetry::metrics::MeterProvider> metrics_handle =
      _opentelemetry_create_opentelemetry_metrics_provider(*group);

  // Logs
  local_provider_handle_type<opentelemetry::logs::LoggerProvider> logs_handle =
      _opentelemetry_create_opentelemetry_logs_provider(*group);

  _opentelemetry_setup_group(*app_inst, group, tracer_handle, metrics_handle, logs_handle);
}

}  // namespace

SERVER_FRAME_API void global_service::set_current_service(
    atapp::app &app, const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &telemetry) {
  std::shared_ptr<group_type> default_group = atfw::memory::stl::make_shared<group_type>();
  if (!default_group || app.is_closing()) {
    return;
  }

  // Setup telemetry
  default_group->initialized = false;
  default_group->tracer_exporter_count = 0;
  default_group->metrics_exporter_count = 0;
  default_group->logger_exporter_count = 0;
  auto &opentelemetry_cfg = telemetry.opentelemetry();
  _create_opentelemetry_app_resource(*default_group, app, {&opentelemetry_cfg.resource()});

  _opentelemetry_prepare_group(app, default_group, "", opentelemetry_cfg,
                               telemetry.has_agent() ? &telemetry.agent() : nullptr, opentelemetry_cfg.trace(),
                               opentelemetry_cfg.metrics(), opentelemetry_cfg.logs());

  // Trace
  local_provider_handle_type<opentelemetry::trace::TracerProvider> tracer_handle =
      _opentelemetry_create_opentelemetry_trace_provider(*default_group);

  // Metrics
  local_provider_handle_type<opentelemetry::metrics::MeterProvider> metrics_handle =
      _opentelemetry_create_opentelemetry_metrics_provider(*default_group);

  // Logs
  local_provider_handle_type<opentelemetry::logs::LoggerProvider> logs_handle =
      _opentelemetry_create_opentelemetry_logs_provider(*default_group);

  _opentelemetry_setup_group(app, default_group, tracer_handle, metrics_handle, logs_handle);

  std::unordered_map<std::string, std::shared_ptr<group_type>> named_groups;
  named_groups.reserve(static_cast<std::size_t>(telemetry.group_size()));
  // Prepare named groups
  for (auto &named_group_cfg : telemetry.group()) {
    if (named_group_cfg.name().empty()) {
      continue;
    }

    std::shared_ptr<group_type> named_group = atfw::memory::stl::make_shared<group_type>();
    if (!named_group) {
      continue;
    }

    // Setup telemetry
    named_group->group_name = named_group_cfg.name();
    named_group->initialized = false;
    named_group->tracer_exporter_count = 0;
    named_group->metrics_exporter_count = 0;
    named_group->logger_exporter_count = 0;

    _create_opentelemetry_app_resource(*named_group, app,
                                       {&opentelemetry_cfg.resource(), &named_group_cfg.configure().resource()});

    _opentelemetry_prepare_group(app, named_group, named_group_cfg.name(), named_group_cfg.configure(),
                                 named_group_cfg.has_agent() ? &named_group_cfg.agent() : nullptr,
                                 opentelemetry_cfg.trace(), opentelemetry_cfg.metrics(), opentelemetry_cfg.logs());
    named_groups[named_group_cfg.name()] = named_group;
  }

  {
    std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
    if (!current_service_cache) {
      return;
    }

    {
      util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
      named_groups.swap(current_service_cache->named_groups);
    }

    // Swap named group
    std::thread cleanup_thread([current_service_cache, named_groups]() {
      for (auto &named_group : named_groups) {
        _opentelemetry_cleanup_group(named_group.second, current_service_cache);
        if (!named_group.second) {
          continue;
        }
      }
    });
    cleanup_thread.detach();

    // Retrigger ready callbacks
    {
      util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->on_ready_callback_lock};
      for (auto &callback_fn : current_service_cache->on_ready_callbacks) {
        if (callback_fn) {
          callback_fn();
        }
      }
    }
  }
}

SERVER_FRAME_API std::shared_ptr<group_type> global_service::get_group(gsl::string_view group_name) {
  std::shared_ptr<global_service_data_type> current_service_cache = get_global_service_data();
  if (!current_service_cache) {
    return nullptr;
  }

  if (group_name.empty()) {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    return current_service_cache->default_group;
  }

  std::string group_name_string = static_cast<std::string>(group_name);

  std::shared_ptr<group_type> ret;
  {
    util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard{current_service_cache->group_lock};
    auto iter = current_service_cache->named_groups.find(group_name_string);
    if (iter == current_service_cache->named_groups.end()) {
      return ret;
    }

    ret = iter->second;
  }

  return ret;
}

SERVER_FRAME_API std::shared_ptr<group_type> global_service::get_default_group() { return get_group({}); }

}  // namespace telemetry

}  // namespace rpc

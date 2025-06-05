// Copyright 2022 atframework
// Created by owent on 2022-03-03.
//

#include "rpc/telemetry/opentelemetry_utility.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/reflection.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/trace/semantic_conventions.h>

#include <gsl/select-gsl.h>
#include <log/log_stacktrace.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <tbb/concurrent_queue.h>

#include <memory/object_allocator.h>

#include <atframe/atapp.h>

#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>
#include <utility/tls_buffers.h>

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "rpc/rpc_context.h"
#include "rpc/telemetry/rpc_global_service.h"
#include "rpc/telemetry/semantic_conventions.h"

// Patch for Windows SDK
#if defined(GetMessage)
#  undef GetMessage
#endif

namespace rpc {

namespace telemetry {

static constexpr const char* kNotificationNotice = "atframework.notifaction.notice";
static constexpr const char* kkNotificationWarning = "atframework.notifaction.warning";
static constexpr const char* kkNotificationError = "atframework.notifaction.error";
static constexpr const char* kkNotificationCritical = "atframework.notifaction.critial";

struct ATFW_UTIL_SYMBOL_VISIBLE opentelemetry_utility::metrics_attributes_with_lifetime {
  std::unordered_map<std::string, opentelemetry::common::AttributeValue> attributes;

  std::list<std::string> lifetime_string;
  std::list<std::vector<opentelemetry::nostd::string_view>> lifetime_string_view;
  std::list<std::unique_ptr<unsigned char[]>> lifetime_buffer;
};

struct ATFW_UTIL_SYMBOL_VISIBLE opentelemetry_utility::metrics_record {
  opentelemetry::nostd::variant<int64_t, double> value;

  metrics_attributes_with_lifetime attributes;
};

struct ATFW_UTIL_SYMBOL_VISIBLE opentelemetry_utility::metrics_observer {
  std::string key;
  metrics_observable_type type;

  std::string meter_name;
  std::string meter_instrument_name;
  std::string meter_instrument_description;
  std::string meter_instrument_unit;

  opentelemetry::metrics::ObservableCallbackPtr origin_callback;

  std::function<void(metrics_observer&)> callback;

  tbb::concurrent_queue<atfw::util::memory::strong_rc_ptr<opentelemetry_utility::metrics_record>> records;

  std::chrono::system_clock::duration collect_interval;
  std::chrono::system_clock::time_point collected_timepoint;
  std::recursive_mutex collected_lock;
  std::list<atfw::util::memory::strong_rc_ptr<opentelemetry_utility::metrics_record>> collected_records;
  std::atomic<size_t> collect_version;
  std::atomic<size_t> export_version;

  std::atomic<size_t> stat_push_record_counter_sum;

  inline metrics_observer()
      : type(metrics_observable_type::kGauge),
        origin_callback(nullptr),
        collect_interval(std::chrono::seconds{15}),
        collected_timepoint(std::chrono::system_clock::from_time_t(0)),
        collect_version(0),
        export_version(0),
        stat_push_record_counter_sum(0) {}
};

namespace {

static int64_t get_opentelemetry_utility_metrics_record_value_as_int64(
    const opentelemetry::nostd::variant<int64_t, double>& value) {
  if (opentelemetry::nostd::holds_alternative<int64_t>(value)) {
    return opentelemetry::nostd::get<int64_t>(value);
  } else if (opentelemetry::nostd::holds_alternative<double>(value)) {
    return static_cast<int64_t>(opentelemetry::nostd::get<double>(value) + std::numeric_limits<float>::epsilon());
  }

  return 0;
}

static double get_opentelemetry_utility_metrics_record_value_as_double(
    const opentelemetry::nostd::variant<int64_t, double>& value) {
  if (opentelemetry::nostd::holds_alternative<int64_t>(value)) {
    return static_cast<double>(opentelemetry::nostd::get<int64_t>(value));
  } else if (opentelemetry::nostd::holds_alternative<double>(value)) {
    return opentelemetry::nostd::get<double>(value);
  }

  return 0.0;
}

struct ATFW_UTIL_SYMBOL_LOCAL opentelemetry_utility_attribute_converter {
  opentelemetry_utility::metrics_attributes_with_lifetime* record;

  explicit inline opentelemetry_utility_attribute_converter(
      opentelemetry_utility::metrics_attributes_with_lifetime& rec)
      : record(&rec) {}

  opentelemetry::common::AttributeValue operator()(bool v) { return v; }
  opentelemetry::common::AttributeValue operator()(int32_t v) { return v; }
  opentelemetry::common::AttributeValue operator()(uint32_t v) { return v; }
  opentelemetry::common::AttributeValue operator()(int64_t v) { return v; }
  opentelemetry::common::AttributeValue operator()(uint64_t v) { return v; }
  opentelemetry::common::AttributeValue operator()(double v) { return v; }
  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::string_view v) {
    record->lifetime_string.push_back(static_cast<std::string>(v));
    return opentelemetry::nostd::string_view{*record->lifetime_string.rbegin()};
  }
  opentelemetry::common::AttributeValue operator()(std::string v) {
    record->lifetime_string.push_back(std::move(v));
    return opentelemetry::nostd::string_view{*record->lifetime_string.rbegin()};
  }
  opentelemetry::common::AttributeValue operator()(const char* v) {
    record->lifetime_string.push_back(v);
    return opentelemetry::nostd::string_view{*record->lifetime_string.rbegin()};
  }

  template <class T, class = atfw::util::nostd::enable_if_t<
#if ((defined(_MSVC_LANG) && _MSVC_LANG >= 201402L)) || \
    (defined(__cplusplus) && __cplusplus >= 201402L &&  \
     !(!defined(__clang__) && defined(__GNUC__) && defined(__GNUC_MINOR__) && __GNUC__ * 100 + __GNUC_MINOR__ <= 409))
                         std::is_trivially_copyable<T>::value
#elif (defined(__cplusplus) && __cplusplus >= 201103L) || ((defined(_MSVC_LANG) && _MSVC_LANG >= 201103L))
                         std::is_trivial<T>::value
#else
                         std::is_pod<T>::value
#endif
                         >>
  opentelemetry::common::AttributeValue copy_trivial_values(opentelemetry::nostd::span<const T> v) {
    size_t buffer_size = sizeof(T) * v.size();
    if (buffer_size <= 0) {
      return opentelemetry::nostd::span<const T>{};
    }

    std::unique_ptr<unsigned char[]> buffer{new unsigned char[buffer_size]};
    record->lifetime_buffer.emplace_back(std::move(buffer));

    T* copy_begin = reinterpret_cast<T*>(buffer.get());
    for (size_t i = 0; i < v.size(); ++i) {
      *(copy_begin + i) = v[i];
    }

    return opentelemetry::nostd::span<const T>{copy_begin, v.size()};
  }

  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::span<const uint8_t> v) {
    return copy_trivial_values(v);
  }

  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::span<const bool> v) {
    return copy_trivial_values(v);
  }
  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::span<const int32_t> v) {
    return copy_trivial_values(v);
  }
  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::span<const uint32_t> v) {
    return copy_trivial_values(v);
  }
  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::span<const int64_t> v) {
    return copy_trivial_values(v);
  }
  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::span<const uint64_t> v) {
    return copy_trivial_values(v);
  }
  opentelemetry::common::AttributeValue operator()(opentelemetry::nostd::span<const double> v) {
    return copy_trivial_values(v);
  }
  opentelemetry::common::AttributeValue operator()(
      opentelemetry::nostd::span<const opentelemetry::nostd::string_view> v) {
    record->lifetime_string_view.push_back(std::vector<opentelemetry::nostd::string_view>());
    std::vector<opentelemetry::nostd::string_view>& ret = *record->lifetime_string_view.rbegin();
    ret.reserve(v.size());
    for (auto& ov : v) {
      record->lifetime_string.push_back(static_cast<std::string>(ov));
      ret.emplace_back(opentelemetry::nostd::string_view{*record->lifetime_string.rbegin()});
    }

    return opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{ret.data(), ret.size()};
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL opentelemetry_utility_attribute_value_to_string_converter {
  std::string operator()(bool v) { return atfw::util::string::format("{}", v); }  // namespace
  std::string operator()(int32_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(uint32_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(int64_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(uint64_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(double v) { return atfw::util::string::format("{}", v); }
  std::string operator()(opentelemetry::nostd::string_view v) { return static_cast<std::string>(v); }
  std::string operator()(const char* v) { return v == nullptr ? "" : v; }

  template <class T>
  inline void dump_array_value(std::ostream& os, const T& v) {
    os << v;
  }
  inline void dump_array_value(std::ostream& os, const opentelemetry::nostd::string_view& v) {
    os.write(v.data(), static_cast<std::streamsize>(v.size()));
  }

  template <class T>
  std::string to_array(opentelemetry::nostd::span<const T> v) {
    if (v.empty()) {
      return "[]";
    }

    std::stringstream ss;
    ss << "[";
    dump_array_value(ss, v[0]);

    for (size_t i = 1; i < v.size(); ++i) {
      ss << ", ";
      dump_array_value(ss, v[i]);
    }

    ss << "]";

    return ss.str();
  }

  std::string operator()(opentelemetry::nostd::span<const uint8_t> v) { return to_array(v); }
  std::string operator()(opentelemetry::nostd::span<const bool> v) { return to_array(v); }
  std::string operator()(opentelemetry::nostd::span<const int32_t> v) { return to_array(v); }
  std::string operator()(opentelemetry::nostd::span<const uint32_t> v) { return to_array(v); }
  std::string operator()(opentelemetry::nostd::span<const int64_t> v) { return to_array(v); }
  std::string operator()(opentelemetry::nostd::span<const uint64_t> v) { return to_array(v); }
  std::string operator()(opentelemetry::nostd::span<const double> v) { return to_array(v); }
  std::string operator()(opentelemetry::nostd::span<const opentelemetry::nostd::string_view> v) { return to_array(v); }
};  // namespace telemetry

struct ATFW_UTIL_SYMBOL_LOCAL opentelemetry_utility_attribute_owned_value_to_string_converter {
  std::string operator()(bool v) { return atfw::util::string::format("{}", v); }  // namespace rpc
  std::string operator()(int32_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(uint32_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(int64_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(uint64_t v) { return atfw::util::string::format("{}", v); }
  std::string operator()(double v) { return atfw::util::string::format("{}", v); }
  std::string operator()(const std::string& v) { return static_cast<std::string>(v); }

  template <class T>
  inline void dump_array_value(std::ostream& os, const T& v) {
    os << v;
  }
  inline void dump_array_value(std::ostream& os, const std::string& v) {
    os.write(v.data(), static_cast<std::streamsize>(v.size()));
  }

  template <class T>
  std::string to_array(const std::vector<T>& v) {
    if (v.empty()) {
      return "[]";
    }

    std::stringstream ss;
    ss << "[";
    dump_array_value(ss, v[0]);

    for (size_t i = 1; i < v.size(); ++i) {
      ss << ", ";
      dump_array_value(ss, v[i]);
    }

    ss << "]";

    return ss.str();
  }

  std::string operator()(const std::vector<bool>& v) { return to_array(v); }
  std::string operator()(const std::vector<int32_t>& v) { return to_array(v); }
  std::string operator()(const std::vector<uint32_t>& v) { return to_array(v); }
  std::string operator()(const std::vector<int64_t>& v) { return to_array(v); }
  std::string operator()(const std::vector<uint64_t>& v) { return to_array(v); }
  std::string operator()(const std::vector<double>& v) { return to_array(v); }
  std::string operator()(const std::vector<std::string>& v) { return to_array(v); }
  std::string operator()(const std::vector<uint8_t>& v) { return to_array(v); }
};

static opentelemetry::nostd::string_view get_notification_event_log_domain(notification_domain domain) {
  switch (domain) {
    case notification_domain::kCritical:
    case notification_domain::kCriticalWithStackTrace: {
      return kkNotificationCritical;
    }
    case notification_domain::kError:
    case notification_domain::kErrorWithStackTrace: {
      return kkNotificationError;
    }
    case notification_domain::kWarning:
    case notification_domain::kWarningWithStackTrace: {
      return kkNotificationWarning;
    }
    default: {
      return kNotificationNotice;
    }
  }
}

static opentelemetry::logs::Severity get_notification_log_level(notification_domain domain) {
  switch (domain) {
    case notification_domain::kCritical:
    case notification_domain::kCriticalWithStackTrace: {
      return opentelemetry::logs::Severity::kFatal;
    }
    case notification_domain::kError:
    case notification_domain::kErrorWithStackTrace: {
      return opentelemetry::logs::Severity::kError;
    }
    case notification_domain::kWarning:
    case notification_domain::kWarningWithStackTrace: {
      return opentelemetry::logs::Severity::kWarn;
    }
    default: {
      return opentelemetry::logs::Severity::kInfo;
    }
  }
}

static void opentelemetry_utility_protobuf_to_otel_attributes_message(
    const google::protobuf::Reflection* reflection, const google::protobuf::Message& message,
    opentelemetry_utility::attributes_map_type& output, gsl::string_view key_prefix);

template <class TAttr, class TPb>
static inline void opentelemetry_utility_protobuf_to_otel_attributes_assign_vector(
    const std::string& key, opentelemetry_utility::attributes_map_type& output,
    const google::protobuf::RepeatedFieldRef<TPb>& values) {
  auto& owned_variant_values = output.owned_attributes[key];
  owned_variant_values = std::vector<TAttr>();
  std::vector<TAttr>& owned_type_values = opentelemetry::nostd::get<std::vector<TAttr>>(owned_variant_values);

  owned_type_values.reserve(static_cast<size_t>(values.size()));
  for (const auto& value : values) {
    owned_type_values.push_back(value);
  }

  output.attributes[key] = owned_type_values;
}

static inline void opentelemetry_utility_protobuf_to_otel_attributes_assign_vector(
    const std::string& key, opentelemetry_utility::attributes_map_type& output,
    const google::protobuf::RepeatedFieldRef<std::string>& values) {
  output.string_view_storages.push_back(opentelemetry_utility::attributes_map_type::string_view_vec_type());
  opentelemetry_utility::attributes_map_type::string_view_vec_type& owned_type_values =
      *output.string_view_storages.rbegin();

  owned_type_values.reserve(static_cast<size_t>(values.size()));
  for (auto& value : values) {
    owned_type_values.push_back(value);
  }

  output.attributes[key] = owned_type_values;
}

static inline void opentelemetry_utility_protobuf_to_otel_attributes_assign_vector(
    const std::string& key, opentelemetry_utility::attributes_map_type& output,
    const google::protobuf::RepeatedFieldRef<bool>& values) {
  output.bool_view_storages.push_back(std::unique_ptr<bool[]>());
  std::unique_ptr<bool[]>& owned_type_values = *output.bool_view_storages.rbegin();

  owned_type_values.reset(new bool[static_cast<size_t>(values.size())]);
  for (int i = 0; i < values.size(); ++i) {
    *(owned_type_values.get() + i) = values.Get(i);
  }

  output.attributes[key] =
      opentelemetry::nostd::span<const bool>{owned_type_values.get(), static_cast<size_t>(values.size())};
}

static void opentelemetry_utility_protobuf_to_otel_attributes_field(const google::protobuf::Reflection* reflection,
                                                                    const google::protobuf::Message& message,
                                                                    const google::protobuf::FieldDescriptor* fds,
                                                                    opentelemetry_utility::attributes_map_type& output,
                                                                    gsl::string_view key_prefix) {
  if (nullptr == fds) {
    return;
  }

  switch (fds->cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<int32_t>(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::int32>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetInt32(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<int64_t>(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::int64>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetInt64(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<uint32_t>(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::uint32>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetUInt32(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<uint64_t>(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::uint64>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetUInt64(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<std::string>(message, fds));
      } else {
        std::string empty;
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetStringReference(message, fds, &empty);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
      if (fds->is_repeated()) {
        int size = reflection->FieldSize(message, fds);
        for (int i = 0; i < size; ++i) {
          auto& sub_message = reflection->GetRepeatedMessage(message, fds, i);
          opentelemetry_utility_protobuf_to_otel_attributes_message(
              sub_message.GetReflection(), sub_message, output,
              atfw::util::string::format("{}{}[{}].", key_prefix, fds->name(), i));
        }
      } else {
        if (reflection->HasField(message, fds)) {
          auto& sub_message = reflection->GetMessage(message, fds);
          opentelemetry_utility_protobuf_to_otel_attributes_message(
              sub_message.GetReflection(), sub_message, output,
              atfw::util::string::format("{}{}.", key_prefix, fds->name()));
        }
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<double>(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<double>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetDouble(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<double>(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<float>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetFloat(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<bool>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetBool(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<int32_t>(
            atfw::util::string::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<int32_t>(message, fds));
      } else {
        output.attributes[atfw::util::string::format("{}{}", key_prefix, fds->name())] =
            reflection->GetEnumValue(message, fds);
      }
      break;
    }
    default: {
      break;
    }
  }
}

static void opentelemetry_utility_protobuf_to_otel_attributes_message(
    const google::protobuf::Reflection* reflection, const google::protobuf::Message& message,
    opentelemetry_utility::attributes_map_type& output, gsl::string_view key_prefix) {
  if (nullptr == reflection) {
    FWLOGERROR("Reflection for message {} is nullptr", message.GetTypeName());
    return;
  }

  std::vector<const google::protobuf::FieldDescriptor*> fds_set;
  reflection->ListFields(message, &fds_set);
  for (const auto& fds : fds_set) {
    opentelemetry_utility_protobuf_to_otel_attributes_field(reflection, message, fds, output, key_prefix);
  }
}

struct ATFW_UTIL_SYMBOL_LOCAL opentelemetry_utility_global_metrics_set {
  std::atomic<bool> initialized;
  std::atomic<bool> closing;
  std::atomic<size_t> collecting_version;
  std::atomic<size_t> collected_version;

  std::unordered_map<std::string, std::shared_ptr<opentelemetry_utility::metrics_observer>> int64_observable_by_key;
  std::unordered_map<void*, std::shared_ptr<opentelemetry_utility::metrics_observer>> int64_observable_by_pointer;

  std::unordered_map<std::string, std::shared_ptr<opentelemetry_utility::metrics_observer>> double_observable_by_key;
  std::unordered_map<void*, std::shared_ptr<opentelemetry_utility::metrics_observer>> double_observable_by_pointer;

  inline opentelemetry_utility_global_metrics_set()
      : initialized{false}, closing{false}, collecting_version{0}, collected_version{0} {}
};

static std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> get_global_metrics_set() {
  static std::recursive_mutex lock;
  static opentelemetry_utility_global_metrics_set ret;

  return {lock, ret};
}

static bool internal_add_global_metrics_observable_int64(opentelemetry_utility::metrics_observer& observer) {
  meter_instrument_key metrics_key{observer.meter_instrument_name, observer.meter_instrument_description,
                                   observer.meter_instrument_unit};

  std::shared_ptr<::rpc::telemetry::group_type> telemetry_lifetime =
      rpc::telemetry::global_service::get_default_group();
  auto instrument =
      rpc::telemetry::global_service::get_metrics_observable(observer.meter_name, metrics_key, telemetry_lifetime);
  if (instrument) {
    FWLOGERROR(
        "Add metrics observable int64 {}(instrument={}, {}, {}) failed, already exists", observer.meter_name,
        gsl::string_view{observer.meter_instrument_name.data(), observer.meter_instrument_name.size()},
        gsl::string_view{observer.meter_instrument_description.data(), observer.meter_instrument_description.size()},
        gsl::string_view{observer.meter_instrument_unit.data(), observer.meter_instrument_unit.size()});
    return false;
  }

  if (observer.type == metrics_observable_type::kGauge) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_gauge_int64(
        observer.meter_name, metrics_key, telemetry_lifetime);
  } else if (observer.type == metrics_observable_type::kCounter) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_counter_int64(
        observer.meter_name, metrics_key, telemetry_lifetime);
  } else {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_up_down_counter_int64(
        observer.meter_name, metrics_key, telemetry_lifetime);
  }

  if (!instrument) {
    FWLOGERROR(
        "Malloc metrics observable int64 {}(instrument={}, {}, {}) failed", observer.meter_name,
        gsl::string_view{observer.meter_instrument_name.data(), observer.meter_instrument_name.size()},
        gsl::string_view{observer.meter_instrument_description.data(), observer.meter_instrument_description.size()},
        gsl::string_view{observer.meter_instrument_unit.data(), observer.meter_instrument_unit.size()});
    return false;
  }

  observer.origin_callback = [](opentelemetry::metrics::ObserverResult result, void* callback) {
    std::shared_ptr<opentelemetry_utility::metrics_observer> metrics_item;
    auto now = std::chrono::system_clock::now();
    {
      std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> callback_data_set =
          get_global_metrics_set();
      std::lock_guard<std::recursive_mutex> lock_guard{callback_data_set.first};
      auto iter = callback_data_set.second.int64_observable_by_pointer.find(callback);
      if (iter != callback_data_set.second.int64_observable_by_pointer.end()) {
        metrics_item = iter->second;
      }
      if (!metrics_item) {
        return;
      }

      if (now - metrics_item->collected_timepoint > metrics_item->collect_interval) {
        metrics_item->export_version.store(metrics_item->collect_version.load(std::memory_order_acquire),
                                           std::memory_order_release);
        callback_data_set.second.collecting_version.fetch_add(1, std::memory_order_release);

        metrics_item->collected_timepoint = now;

        std::lock_guard<std::recursive_mutex> collected_lock_guard{metrics_item->collected_lock};
        metrics_item->collected_records.clear();
      } else if (now < metrics_item->collected_timepoint) {
        metrics_item->collected_timepoint = now;
      }
    }

    size_t export_record_count = 0;
    std::lock_guard<std::recursive_mutex> collected_lock_guard{metrics_item->collected_lock};
    {
      atfw::util::memory::strong_rc_ptr<opentelemetry_utility::metrics_record> record;
      while (metrics_item->records.try_pop(record)) {
        if (!record) {
          continue;
        }

        metrics_item->collected_records.emplace_back(std::move(record));
      }
    }

    for (auto& record : metrics_item->collected_records) {
      if (!record) {
        continue;
      }
      ++export_record_count;

      if (opentelemetry::nostd::holds_alternative<
              opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
        auto real_observer = opentelemetry::nostd::get<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
        if (real_observer) {
          real_observer->Observe(get_opentelemetry_utility_metrics_record_value_as_int64(record->value),
                                 opentelemetry_utility::get_attributes(record->attributes));
        }
      } else if (opentelemetry::nostd::holds_alternative<
                     opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
        auto real_observer = opentelemetry::nostd::get<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
        if (real_observer) {
          real_observer->Observe(get_opentelemetry_utility_metrics_record_value_as_double(record->value),
                                 opentelemetry_utility::get_attributes(record->attributes));
        }
      }
    }

    FWLOGDEBUG("[Telemetry]: Export metric meter instrument {}(@{}) with {} record(s) to version {}", metrics_item->key,
               callback, export_record_count, metrics_item->export_version.load(std::memory_order_acquire));
  };

  instrument->AddCallback(observer.origin_callback, reinterpret_cast<void*>(&observer));

  return true;
}

static bool internal_add_global_metrics_observable_double(opentelemetry_utility::metrics_observer& observer) {
  meter_instrument_key metrics_key{observer.meter_instrument_name, observer.meter_instrument_description,
                                   observer.meter_instrument_unit};

  std::shared_ptr<::rpc::telemetry::group_type> telemetry_lifetime =
      rpc::telemetry::global_service::get_default_group();
  auto instrument =
      rpc::telemetry::global_service::get_metrics_observable(observer.meter_name, metrics_key, telemetry_lifetime);
  if (instrument) {
    FWLOGERROR(
        "Add metrics observable double {}(instrument={}, {}, {}) failed, already exists", observer.meter_name,
        gsl::string_view{observer.meter_instrument_name.data(), observer.meter_instrument_name.size()},
        gsl::string_view{observer.meter_instrument_description.data(), observer.meter_instrument_description.size()},
        gsl::string_view{observer.meter_instrument_unit.data(), observer.meter_instrument_unit.size()});
    return false;
  }

  if (observer.type == metrics_observable_type::kGauge) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_gauge_double(
        observer.meter_name, metrics_key, telemetry_lifetime);
  } else if (observer.type == metrics_observable_type::kCounter) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_counter_double(
        observer.meter_name, metrics_key, telemetry_lifetime);
  } else {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_up_down_counter_double(
        observer.meter_name, metrics_key, telemetry_lifetime);
  }

  if (!instrument) {
    FWLOGERROR(
        "Malloc metrics observable double {}(instrument={}, {}, {}) failed", observer.meter_name,
        gsl::string_view{observer.meter_instrument_name.data(), observer.meter_instrument_name.size()},
        gsl::string_view{observer.meter_instrument_description.data(), observer.meter_instrument_description.size()},
        gsl::string_view{observer.meter_instrument_unit.data(), observer.meter_instrument_unit.size()});
    return false;
  }

  observer.origin_callback = [](opentelemetry::metrics::ObserverResult result, void* callback) {
    std::shared_ptr<opentelemetry_utility::metrics_observer> metrics_item;
    auto now = std::chrono::system_clock::now();
    {
      std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> callback_data_set =
          get_global_metrics_set();
      std::lock_guard<std::recursive_mutex> lock_guard{callback_data_set.first};
      auto iter = callback_data_set.second.double_observable_by_pointer.find(callback);
      if (iter != callback_data_set.second.double_observable_by_pointer.end()) {
        metrics_item = iter->second;
      }

      if (!metrics_item) {
        return;
      }

      if (now - metrics_item->collected_timepoint > metrics_item->collect_interval) {
        metrics_item->export_version.store(metrics_item->collect_version.load(std::memory_order_acquire),
                                           std::memory_order_release);
        callback_data_set.second.collecting_version.fetch_add(1, std::memory_order_release);

        metrics_item->collected_timepoint = now;

        std::lock_guard<std::recursive_mutex> collected_lock_guard{metrics_item->collected_lock};
        metrics_item->collected_records.clear();
      } else if (now < metrics_item->collected_timepoint) {
        metrics_item->collected_timepoint = now;
      }
    }

    size_t export_record_count = 0;
    if (metrics_item->records.empty()) {
      FWLOGDEBUG("[Telemetry]: Export metric meter instrument {}(@{}) with {} record(s) to version {}",
                 metrics_item->key, callback, export_record_count,
                 metrics_item->export_version.load(std::memory_order_acquire));
      return;
    }

    std::lock_guard<std::recursive_mutex> collected_lock_guard{metrics_item->collected_lock};
    {
      atfw::util::memory::strong_rc_ptr<opentelemetry_utility::metrics_record> record;
      while (metrics_item->records.try_pop(record)) {
        if (!record) {
          continue;
        }

        metrics_item->collected_records.emplace_back(std::move(record));
      }
    }

    for (auto& record : metrics_item->collected_records) {
      if (!record) {
        continue;
      }
      ++export_record_count;

      if (opentelemetry::nostd::holds_alternative<
              opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
        auto real_observer = opentelemetry::nostd::get<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
        if (real_observer) {
          real_observer->Observe(get_opentelemetry_utility_metrics_record_value_as_int64(record->value),
                                 opentelemetry_utility::get_attributes(record->attributes));
        }
      } else if (opentelemetry::nostd::holds_alternative<
                     opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
        auto real_observer = opentelemetry::nostd::get<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
        if (real_observer) {
          real_observer->Observe(get_opentelemetry_utility_metrics_record_value_as_double(record->value),
                                 opentelemetry_utility::get_attributes(record->attributes));
        }
      }

      record.reset();
    }

    FWLOGDEBUG("[Telemetry]: Export metric meter instrument {}(@{}) with {} record(s) to version {}", metrics_item->key,
               callback, export_record_count, metrics_item->export_version.load(std::memory_order_acquire));
  };

  instrument->AddCallback(observer.origin_callback, reinterpret_cast<void*>(&observer));
  return true;
}

}  // namespace

SERVER_FRAME_API void opentelemetry_utility::setup() {
  // Skip if already initialized
  if (true == get_global_metrics_set().second.initialized.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  // Reset handles
  rpc::telemetry::global_service::add_on_ready([]() {
    std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set = get_global_metrics_set();
    if (data_set.second.closing.load(std::memory_order_acquire)) {
      return;
    }

    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};

    for (auto& observable : data_set.second.int64_observable_by_pointer) {
      if (!observable.second) {
        continue;
      }

      internal_add_global_metrics_observable_int64(*observable.second);
    }

    for (auto& observable : data_set.second.double_observable_by_pointer) {
      if (!observable.second) {
        continue;
      }

      internal_add_global_metrics_observable_double(*observable.second);
    }
  });
}

SERVER_FRAME_API int opentelemetry_utility::tick() {
  int ret = 0;

  std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> callback_data_set =
      get_global_metrics_set();
  size_t collecting_version = callback_data_set.second.collecting_version.load(std::memory_order_acquire);
  if (collecting_version == callback_data_set.second.collected_version.load(std::memory_order_acquire)) {
    return ret;
  }

  if (callback_data_set.second.closing.load(std::memory_order_acquire)) {
    return ret;
  }
  if (!callback_data_set.second.initialized.load(std::memory_order_acquire)) {
    return ret;
  }

  int32_t max_record_per_loop = logic_config::me()->get_logic().telemetry().executor().max_metric_record_per_loop();
  std::chrono::system_clock::duration max_tick_time_per_loop = std::chrono::system_clock::duration::zero();

  atapp::app* app_inst = atapp::app::get_last_instance();
  if (nullptr != app_inst) {
    max_tick_time_per_loop = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds(app_inst->get_origin_configure().timer().tick_interval().seconds()) +
        std::chrono::nanoseconds(app_inst->get_origin_configure().timer().tick_interval().nanos()));
  }

  if (max_record_per_loop <= 0) {
    max_record_per_loop = 1024;
  }

  if (max_tick_time_per_loop < std::chrono::milliseconds{4}) {
    max_tick_time_per_loop =
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds{4});
  }
  atfw::util::time::time_utility::update();
  auto start_time = std::chrono::system_clock::now();

  std::lock_guard<std::recursive_mutex> lock_guard{callback_data_set.first};
  bool all_collected = true;
  size_t exported_metric_count = 0;
  size_t exporting_metric_count = 0;
  for (auto& observable : callback_data_set.second.int64_observable_by_pointer) {
    if (!observable.second) {
      ++exported_metric_count;
      continue;
    }

    if (!observable.second->callback) {
      ++exported_metric_count;
      continue;
    }

    // 有数据未导出，不需要再导出一次
    size_t export_version = observable.second->export_version.load(std::memory_order_acquire);
    if (export_version == collecting_version ||
        export_version != observable.second->collect_version.load(std::memory_order_acquire)) {
      ++exported_metric_count;
      continue;
    }

    atfw::util::time::time_utility::update();
    auto end_time = std::chrono::system_clock::now();
    if (ret >= max_record_per_loop || end_time - start_time >= max_tick_time_per_loop) {
      all_collected = false;
      break;
    }

    size_t old_record_count = observable.second->stat_push_record_counter_sum.load(std::memory_order_acquire);
    observable.second->callback(*observable.second);
    size_t new_record_count = observable.second->stat_push_record_counter_sum.load(std::memory_order_acquire);
    if (new_record_count > old_record_count) {
      ret += static_cast<int32_t>(new_record_count - old_record_count);
    }

    ++exporting_metric_count;
    ++exported_metric_count;
    FWLOGDEBUG("[Telemetry]: Collect metric meter instrument {}(@{}) with {} record(s) from version {} to {}",
               observable.second->key, observable.first, new_record_count - old_record_count, export_version,
               collecting_version);

    observable.second->collect_version.store(collecting_version, std::memory_order_release);
  }

  for (auto& observable : callback_data_set.second.double_observable_by_pointer) {
    if (!observable.second) {
      ++exported_metric_count;
      continue;
    }

    if (!observable.second->callback) {
      ++exported_metric_count;
      continue;
    }

    // 有数据未导出，不需要再导出一次
    size_t export_version = observable.second->export_version.load(std::memory_order_acquire);
    if (export_version == collecting_version ||
        export_version != observable.second->collect_version.load(std::memory_order_acquire)) {
      ++exported_metric_count;
      continue;
    }

    atfw::util::time::time_utility::update();
    auto end_time = std::chrono::system_clock::now();
    if (ret >= max_record_per_loop || end_time - start_time >= max_tick_time_per_loop) {
      all_collected = false;
      ++exported_metric_count;
      break;
    }

    size_t old_record_count = observable.second->stat_push_record_counter_sum.load(std::memory_order_acquire);
    observable.second->callback(*observable.second);
    size_t new_record_count = observable.second->stat_push_record_counter_sum.load(std::memory_order_acquire);
    if (new_record_count > old_record_count) {
      ret += static_cast<int32_t>(new_record_count - old_record_count);
    }

    ++exporting_metric_count;
    ++exported_metric_count;
    FWLOGDEBUG("[Telemetry]: Collect metric meter instrument {}(@{}) with {} record(s) from version {} to {}",
               observable.second->key, observable.first, new_record_count - old_record_count, export_version,
               collecting_version);
    observable.second->collect_version.store(collecting_version, std::memory_order_release);
  }

  if (all_collected) {
    callback_data_set.second.collected_version.store(collecting_version, std::memory_order_release);
  }

  if (ret > 0 || all_collected) {
    FWLOGINFO(
        "[Telemetry]: Export {} record(s) in {}/{}/{} metric meter instrument(s), all collected: {}, cost {}us", ret,
        exporting_metric_count, exported_metric_count,
        callback_data_set.second.int64_observable_by_pointer.size() +
            callback_data_set.second.double_observable_by_pointer.size(),
        all_collected,
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start_time).count());
  }

  return ret;
}

SERVER_FRAME_API void opentelemetry_utility::stop() {
  // Skip if already closed
  if (false == get_global_metrics_set().second.closing.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set = get_global_metrics_set();
  std::shared_ptr<::rpc::telemetry::group_type> telemetry_lifetime =
      rpc::telemetry::global_service::get_default_group();

  std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};

  for (auto& observable : data_set.second.int64_observable_by_pointer) {
    if (!observable.second) {
      continue;
    }

    if (nullptr == observable.second->origin_callback) {
      continue;
    }

    meter_instrument_key metrics_key{observable.second->meter_instrument_name,
                                     observable.second->meter_instrument_description,
                                     observable.second->meter_instrument_unit};
    auto instrument = rpc::telemetry::global_service::get_metrics_observable(observable.second->meter_name, metrics_key,
                                                                             telemetry_lifetime);
    if (!instrument) {
      continue;
    }

    instrument->RemoveCallback(observable.second->origin_callback, reinterpret_cast<void*>(observable.second.get()));
    observable.second->origin_callback = nullptr;
  }

  for (auto& observable : data_set.second.double_observable_by_pointer) {
    if (!observable.second) {
      continue;
    }

    if (nullptr == observable.second->origin_callback) {
      continue;
    }

    meter_instrument_key metrics_key{observable.second->meter_instrument_name,
                                     observable.second->meter_instrument_description,
                                     observable.second->meter_instrument_unit};
    auto instrument = rpc::telemetry::global_service::get_metrics_observable(observable.second->meter_name, metrics_key,
                                                                             telemetry_lifetime);
    if (!instrument) {
      continue;
    }

    instrument->RemoveCallback(observable.second->origin_callback, reinterpret_cast<void*>(observable.second.get()));
    observable.second->origin_callback = nullptr;
  }
}

SERVER_FRAME_API atfw::util::memory::strong_rc_ptr<opentelemetry_utility::metrics_attributes_with_lifetime>
opentelemetry_utility::create_attributes_with_lifetime() {
  return atfw::util::memory::make_strong_rc<metrics_attributes_with_lifetime>();
}

SERVER_FRAME_API const std::unordered_map<std::string, opentelemetry::common::AttributeValue>&
opentelemetry_utility::get_attributes(const metrics_attributes_with_lifetime& source) {
  return source.attributes;
}

SERVER_FRAME_API void opentelemetry_utility::populate_attributes(
    metrics_attributes_with_lifetime& target, const opentelemetry::common::KeyValueIterable& attributes) {
  target.attributes.reserve(attributes.size() + target.attributes.size());

  opentelemetry_utility_attribute_converter converter{target};
  attributes.ForEachKeyValue(
      [&](opentelemetry::nostd::string_view key, opentelemetry::common::AttributeValue value) noexcept {
        target.attributes[static_cast<std::string>(key)] = opentelemetry::nostd::visit(converter, value);
        return true;
      });
}

SERVER_FRAME_API void opentelemetry_utility::populate_attributes(metrics_attributes_with_lifetime& target,
                                                                 attribute_span_type attributes) {
  target.attributes.reserve(attributes.size() + target.attributes.size());

  opentelemetry_utility_attribute_converter converter{target};
  for (auto& kv : attributes) {
    target.attributes[static_cast<std::string>(kv.first)] = opentelemetry::nostd::visit(converter, kv.second);
  }
}

SERVER_FRAME_API void opentelemetry_utility::populate_attributes(
    metrics_attributes_with_lifetime& target,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attributes) {
  target.attributes.reserve(attributes.size() + target.attributes.size());

  opentelemetry_utility_attribute_converter converter{target};
  for (auto& kv : attributes) {
    target.attributes[static_cast<std::string>(kv.first)] = opentelemetry::nostd::visit(converter, kv.second);
  }
}

SERVER_FRAME_API void opentelemetry_utility::protobuf_to_otel_attributes(const google::protobuf::Message& message,
                                                                         attributes_map_type& output,
                                                                         gsl::string_view key_prefix) {
  opentelemetry_utility_protobuf_to_otel_attributes_message(message.GetReflection(), message, output, key_prefix);
}

SERVER_FRAME_API opentelemetry::common::AttributeValue opentelemetry_utility::convert_attribute_value_wihtout_array(
    const opentelemetry::sdk::common::OwnedAttributeValue& value) {
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
    const auto& data = opentelemetry::nostd::get<std::vector<int32_t>>(value);
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::span<const int32_t>{data.data(), data.size()}};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<uint32_t>>(value)) {
    const auto& data = opentelemetry::nostd::get<std::vector<uint32_t>>(value);
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::span<const uint32_t>{data.data(), data.size()}};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<int64_t>>(value)) {
    const auto& data = opentelemetry::nostd::get<std::vector<int64_t>>(value);
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::span<const int64_t>{data.data(), data.size()}};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<uint64_t>>(value)) {
    const auto& data = opentelemetry::nostd::get<std::vector<uint64_t>>(value);
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::span<const uint64_t>{data.data(), data.size()}};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<uint8_t>>(value)) {
    const auto& data = opentelemetry::nostd::get<std::vector<uint8_t>>(value);
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::span<const uint8_t>{data.data(), data.size()}};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<double>>(value)) {
    const auto& data = opentelemetry::nostd::get<std::vector<double>>(value);
    return opentelemetry::common::AttributeValue{opentelemetry::nostd::span<const double>{data.data(), data.size()}};
  } else if (opentelemetry::nostd::holds_alternative<std::vector<std::string>>(value)) {
    // 暂无低开销解决方案，目前公共属性中没有数组类型，故而不处理所有的数组类型也是没有问题的
    // 参见 https://github.com/open-telemetry/opentelemetry-cpp/pull/1154 里的讨论
    return opentelemetry::common::AttributeValue{};
  }

  return opentelemetry::common::AttributeValue{};
}

SERVER_FRAME_API std::string opentelemetry_utility::convert_attribute_value_to_string(
    const opentelemetry::common::AttributeValue& value) {
  opentelemetry_utility_attribute_value_to_string_converter converter;
  return opentelemetry::nostd::visit(converter, value);
}

SERVER_FRAME_API std::string opentelemetry_utility::convert_attribute_value_to_string(
    const opentelemetry::sdk::common::OwnedAttributeValue& value) {
  opentelemetry_utility_attribute_owned_value_to_string_converter converter;
  return opentelemetry::nostd::visit(converter, value);
}

SERVER_FRAME_API bool opentelemetry_utility::add_global_metics_observable_int64(
    metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
    std::function<void(metrics_observer&)> fn) {
  if (!fn) {
    return false;
  }

  // opentelemetry only use metrics name as key of metric storage
  std::string key = atfw::util::string::format("{}:{}", gsl::string_view{meter_name.data(), meter_name.size()},
                                               gsl::string_view{metrics_key.name.data(), metrics_key.name.size()});

  std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set = get_global_metrics_set();
  {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    auto iter = data_set.second.int64_observable_by_key.find(key);
    if (data_set.second.int64_observable_by_key.end() != iter && iter->second) {
      iter->second->callback = fn;
      return true;
    }
  }

  auto handle = atfw::memory::stl::make_shared<opentelemetry_utility::metrics_observer>();
  if (!handle) {
    return false;
  }
  handle->key = key;
  handle->type = type;
  handle->meter_name = static_cast<std::string>(meter_name);
  handle->meter_instrument_name = static_cast<std::string>(metrics_key.name);
  handle->meter_instrument_description = static_cast<std::string>(metrics_key.description);
  handle->meter_instrument_unit = static_cast<std::string>(metrics_key.unit);
  handle->collect_interval = protobuf_to_chrono_duration<>(
      logic_config::me()->get_logic().telemetry().opentelemetry().metrics().reader().export_interval());
  if (handle->collect_interval < std::chrono::seconds{2}) {
    handle->collect_interval = std::chrono::seconds{15};
  } else {
    handle->collect_interval -= std::chrono::seconds{1};
  }
  handle->callback = std::move(fn);
  handle->origin_callback = nullptr;
  if (false == data_set.second.initialized.load(std::memory_order_acquire) ||
      internal_add_global_metrics_observable_int64(*handle)) {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    data_set.second.int64_observable_by_key[key] = handle;
    data_set.second.int64_observable_by_pointer[reinterpret_cast<void*>(handle.get())] = handle;
    ++data_set.second.collecting_version;
    return true;
  } else {
    return false;
  }
}

SERVER_FRAME_API bool opentelemetry_utility::add_global_metics_observable_double(
    metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
    std::function<void(metrics_observer&)> fn) {
  if (!fn) {
    return false;
  }

  // opentelemetry only use metrics name as key of metric storage
  std::string key = atfw::util::string::format("{}:{}", gsl::string_view{meter_name.data(), meter_name.size()},
                                               gsl::string_view{metrics_key.name.data(), metrics_key.name.size()});
  std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set = get_global_metrics_set();
  {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    auto iter = data_set.second.double_observable_by_key.find(key);
    if (data_set.second.double_observable_by_key.end() != iter && iter->second) {
      iter->second->callback = fn;
      return true;
    }
  }

  auto handle = atfw::memory::stl::make_shared<opentelemetry_utility::metrics_observer>();
  if (!handle) {
    return false;
  }
  handle->key = key;
  handle->type = type;
  handle->meter_name = static_cast<std::string>(meter_name);
  handle->meter_instrument_name = static_cast<std::string>(metrics_key.name);
  handle->meter_instrument_description = static_cast<std::string>(metrics_key.description);
  handle->meter_instrument_unit = static_cast<std::string>(metrics_key.unit);
  handle->collect_interval = protobuf_to_chrono_duration<>(
      logic_config::me()->get_logic().telemetry().opentelemetry().metrics().reader().export_interval());
  if (handle->collect_interval < std::chrono::seconds{2}) {
    handle->collect_interval = std::chrono::seconds{15};
  } else {
    handle->collect_interval -= std::chrono::seconds{1};
  }
  handle->callback = std::move(fn);
  handle->origin_callback = nullptr;
  if (false == data_set.second.initialized.load(std::memory_order_acquire) ||
      internal_add_global_metrics_observable_double(*handle)) {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    data_set.second.double_observable_by_key[key] = handle;
    data_set.second.double_observable_by_pointer[reinterpret_cast<void*>(handle.get())] = handle;
    ++data_set.second.collecting_version;
    return true;
  } else {
    return false;
  }
}

SERVER_FRAME_API void opentelemetry_utility::global_metics_observe_record(
    metrics_observer& observer, opentelemetry::nostd::variant<int64_t, double> value,
    const opentelemetry::common::KeyValueIterable& attributes) {
  atfw::util::memory::strong_rc_ptr<opentelemetry_utility::metrics_record> record =
      atfw::util::memory::make_strong_rc<opentelemetry_utility::metrics_record>();
  if (!record) {
    return;
  }

  record->value = value;
  populate_attributes(record->attributes, attributes);

  observer.records.emplace(std::move(record));

  ++observer.stat_push_record_counter_sum;
}

SERVER_FRAME_API void opentelemetry_utility::global_metics_observe_record(
    metrics_observer& observer, opentelemetry::nostd::variant<int64_t, double> value, attribute_span_type attributes) {
  atfw::util::memory::strong_rc_ptr<opentelemetry_utility::metrics_record> record =
      atfw::util::memory::make_strong_rc<opentelemetry_utility::metrics_record>();
  if (!record) {
    return;
  }

  record->value = value;
  populate_attributes(record->attributes, attributes);

  observer.records.emplace(std::move(record));

  ++observer.stat_push_record_counter_sum;
}

SERVER_FRAME_API void opentelemetry_utility::send_notification_event(rpc::context& ctx,
                                                                     notification_domain event_domain,
                                                                     gsl::string_view event_name,
                                                                     gsl::string_view message,
                                                                     attribute_span_type attrbites) {
  if (event_name.empty()) {
    return;
  }

  auto try_group =
      rpc::telemetry::global_service::get_group(rpc::telemetry::semantic_conventions::kGroupNameNotification);
  if (!try_group) {
    try_group = rpc::telemetry::global_service::get_default_group();
  }
  auto logger = rpc::telemetry::global_service::get_logger(try_group, "notification");
  if (!logger) {
    FWLOGWARNING("Can not get logger and ignore notification domain: {}, message: {}", event_name, message);
    return;
  }

  attribute_pair_type standard_attributes[6] = {
      attribute_pair_type{rpc::telemetry::semantic_conventions::kEventDomain,
                          get_notification_event_log_domain(event_domain)},
      attribute_pair_type{opentelemetry::trace::SemanticConventions::kEventName,
                          opentelemetry::common::AttributeValue{
                              opentelemetry::nostd::string_view{event_name.data(), event_name.size()}}}};
  size_t attribute_pair_size = 2;
  if (!ctx.get_task_context().task_name.empty()) {
    standard_attributes[attribute_pair_size++] =
        attribute_pair_type{rpc::telemetry::semantic_conventions::kRpcSystemValueAtRpcTask,
                            opentelemetry::nostd::string_view{ctx.get_task_context().task_name.data(),
                                                              ctx.get_task_context().task_name.size()}};
  }
  if (0 != ctx.get_task_context().reference_object_type_id) {
    standard_attributes[attribute_pair_size++] = attribute_pair_type{
        rpc::telemetry::semantic_conventions::kRpcRouterObjectTypeID, ctx.get_task_context().reference_object_type_id};
  }
  if (0 != ctx.get_task_context().reference_object_zone_id) {
    standard_attributes[attribute_pair_size++] = attribute_pair_type{
        rpc::telemetry::semantic_conventions::kRpcRouterObjectZoneID, ctx.get_task_context().reference_object_zone_id};
  }
  if (0 != ctx.get_task_context().reference_object_instance_id) {
    standard_attributes[attribute_pair_size++] =
        attribute_pair_type{rpc::telemetry::semantic_conventions::kRpcRouterObjectInstanceID,
                            ctx.get_task_context().reference_object_instance_id};
  }
  opentelemetry::nostd::string_view body{message.data(), message.size()};
  if (0 != (static_cast<int32_t>(event_domain) & static_cast<int32_t>(notification_domain::kStackTraceBitFlag))) {
    char* buffer = reinterpret_cast<char*>(tls_buffers_get_buffer(tls_buffers_type_t::EN_TBT_DEFAULT));
    size_t buffer_size = tls_buffers_get_length(tls_buffers_type_t::EN_TBT_DEFAULT);

    if (message.size() + 16 < buffer_size) {
      atfw::util::log::stacktrace_options options;
      options.skip_start_frames = 1;
      options.skip_end_frames = 1;
      options.max_frames = 0;

      size_t written =
          atfw::util::log::stacktrace_write(buffer + message.size() + 13, buffer_size - 13 - message.size(), &options);
      if (written > 0) {
        memcpy(buffer, message.data(), message.size());
        memcpy(buffer + message.size(), "\nStacktrace:\n", 13);

        written += message.size() + 13;
        if (written < buffer_size) {
          buffer[written] = 0;
          body = opentelemetry::nostd::string_view{buffer, written};
        } else {
          buffer[buffer_size - 1] = 0;
          body = opentelemetry::nostd::string_view{buffer, buffer_size - 1};
        }
      }
    }
  }

  auto& trace_span = ctx.get_trace_span();
  if (trace_span) {
    logger->EmitLogRecord(get_notification_log_level(event_domain), trace_span->GetContext(), attrbites,
                          attribute_span_type(&standard_attributes[0], attribute_pair_size),
                          atfw::util::time::time_utility::sys_now(), body);
  } else {
    logger->EmitLogRecord(get_notification_log_level(event_domain), attrbites,
                          attribute_span_type(&standard_attributes[0], attribute_pair_size),
                          atfw::util::time::time_utility::sys_now(), body);
  }
}

SERVER_FRAME_API void opentelemetry_utility::send_notification_event(
    rpc::context& ctx, notification_domain event_domain, gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_notification_event(ctx, event_domain, event_name, message,
                          attribute_span_type{attrbites.begin(), attrbites.end()});
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(rpc::context& ctx, gsl::string_view event_name,
                                                                       gsl::string_view message,
                                                                       attribute_span_type attrbites) {
  send_log_to_default_group(ctx, event_name, opentelemetry::logs::Severity::kInfo, event_name, message, attrbites);
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(
    rpc::context& ctx, gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_log_to_default_group(ctx, event_name, opentelemetry::logs::Severity::kInfo, event_name, message,
                            attribute_span_type{attrbites.begin(), attrbites.end()});
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(rpc::context& ctx,
                                                                       opentelemetry::logs::Severity severity,
                                                                       gsl::string_view event_name,
                                                                       gsl::string_view message,
                                                                       attribute_span_type attrbites) {
  send_log_to_default_group(ctx, event_name, severity, event_name, message, attrbites);
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(
    rpc::context& ctx, opentelemetry::logs::Severity severity, gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_log_to_default_group(ctx, event_name, severity, event_name, message,
                            attribute_span_type{attrbites.begin(), attrbites.end()});
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(rpc::context& ctx, gsl::string_view logger_name,
                                                                       opentelemetry::logs::Severity severity,
                                                                       gsl::string_view event_name,
                                                                       gsl::string_view message,
                                                                       attribute_span_type attrbites) {
  if (event_name.empty()) {
    return;
  }

  auto logger = rpc::telemetry::global_service::get_logger(
      opentelemetry::nostd::string_view{logger_name.data(), logger_name.size()});
  if (!logger) {
    FWLOGWARNING("Can not get logger {} and ignore otel-logs domain: {}, message: {}", logger_name, event_name,
                 message);
    return;
  }

  attribute_pair_type standard_attributes[6] = {attribute_pair_type{
      opentelemetry::trace::SemanticConventions::kEventName,
      opentelemetry::common::AttributeValue{opentelemetry::nostd::string_view{event_name.data(), event_name.size()}}}};

  size_t attribute_pair_size = 2;
  if (!ctx.get_task_context().task_name.empty()) {
    standard_attributes[attribute_pair_size++] =
        attribute_pair_type{rpc::telemetry::semantic_conventions::kRpcSystemValueAtRpcTask,
                            opentelemetry::nostd::string_view{ctx.get_task_context().task_name.data(),
                                                              ctx.get_task_context().task_name.size()}};
  }
  if (0 != ctx.get_task_context().reference_object_type_id) {
    standard_attributes[attribute_pair_size++] = attribute_pair_type{
        rpc::telemetry::semantic_conventions::kRpcRouterObjectTypeID, ctx.get_task_context().reference_object_type_id};
  }
  if (0 != ctx.get_task_context().reference_object_zone_id) {
    standard_attributes[attribute_pair_size++] = attribute_pair_type{
        rpc::telemetry::semantic_conventions::kRpcRouterObjectZoneID, ctx.get_task_context().reference_object_type_id};
  }
  if (0 != ctx.get_task_context().reference_object_instance_id) {
    standard_attributes[attribute_pair_size++] =
        attribute_pair_type{rpc::telemetry::semantic_conventions::kRpcRouterObjectInstanceID,
                            ctx.get_task_context().reference_object_type_id};
  }

  auto& trace_span = ctx.get_trace_span();
  if (trace_span) {
    logger->EmitLogRecord(severity, trace_span->GetContext(), attrbites,
                          attribute_span_type(&standard_attributes[0], attribute_pair_size),
                          atfw::util::time::time_utility::sys_now(),
                          opentelemetry::nostd::string_view{message.data(), message.size()});
  } else {
    logger->EmitLogRecord(severity, attrbites, attribute_span_type(&standard_attributes[0], attribute_pair_size),
                          atfw::util::time::time_utility::sys_now(),
                          opentelemetry::nostd::string_view{message.data(), message.size()});
  }
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(
    rpc::context& ctx, gsl::string_view logger_name, opentelemetry::logs::Severity severity,
    gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_log_to_default_group(ctx, logger_name, severity, event_name, message,
                            attribute_span_type{attrbites.begin(), attrbites.end()});
}

}  // namespace telemetry
}  // namespace rpc

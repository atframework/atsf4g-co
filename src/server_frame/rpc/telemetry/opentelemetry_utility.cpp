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
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <memory/object_allocator.h>

#include <functional>
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

namespace {

static opentelemetry::nostd::string_view get_notification_event_log_domain(notification_domain domain) {
  switch (domain) {
    case notification_domain::kCritical: {
      return kkNotificationCritical;
    }
    case notification_domain::kError: {
      return kkNotificationError;
    }
    case notification_domain::kWarning: {
      return kkNotificationWarning;
    }
    default: {
      return kNotificationNotice;
    }
  }
}

static opentelemetry::logs::Severity get_notification_log_level(notification_domain domain) {
  switch (domain) {
    case notification_domain::kCritical: {
      return opentelemetry::logs::Severity::kFatal;
    }
    case notification_domain::kError: {
      return opentelemetry::logs::Severity::kError;
    }
    case notification_domain::kWarning: {
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
  for (auto value : values) {
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
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::int32>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetInt32(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<int64_t>(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::int64>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetInt64(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<uint32_t>(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::uint32>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetUInt32(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<uint64_t>(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<google::protobuf::uint64>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetUInt64(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<std::string>(message, fds));
      } else {
        std::string empty;
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] =
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
              util::log::format("{}{}[{}].", key_prefix, fds->name(), i));
        }
      } else {
        if (reflection->HasField(message, fds)) {
          auto& sub_message = reflection->GetMessage(message, fds);
          opentelemetry_utility_protobuf_to_otel_attributes_message(
              sub_message.GetReflection(), sub_message, output, util::log::format("{}{}.", key_prefix, fds->name()));
        }
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<double>(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<double>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetDouble(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<double>(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<float>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetFloat(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<bool>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetBool(message, fds);
      }
      break;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
      if (fds->is_repeated()) {
        opentelemetry_utility_protobuf_to_otel_attributes_assign_vector<int32_t>(
            util::log::format("{}{}", key_prefix, fds->name()), output,
            reflection->GetRepeatedFieldRef<int32_t>(message, fds));
      } else {
        output.attributes[util::log::format("{}{}", key_prefix, fds->name())] = reflection->GetEnumValue(message, fds);
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
  for (auto fds : fds_set) {
    opentelemetry_utility_protobuf_to_otel_attributes_field(reflection, message, fds, output, key_prefix);
  }
}

struct UTIL_SYMBOL_LOCAL opentelemetry_utility_global_metrics_item {
  std::string key;
  std::function<void(opentelemetry::metrics::ObserverResult&)> fn;
};

struct UTIL_SYMBOL_LOCAL opentelemetry_utility_global_metrics_set {
  std::unordered_map<std::string, std::shared_ptr<opentelemetry_utility_global_metrics_item>> int64_observable_by_key;
  std::unordered_map<void*, std::shared_ptr<opentelemetry_utility_global_metrics_item>> int64_observable_by_pointer;

  std::unordered_map<std::string, std::shared_ptr<opentelemetry_utility_global_metrics_item>> double_observable_by_key;
  std::unordered_map<void*, std::shared_ptr<opentelemetry_utility_global_metrics_item>> double_observable_by_pointer;
};

static std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> get_global_metrics_set() {
  static std::shared_ptr<::rpc::telemetry::group_type> default_group;
  static std::recursive_mutex lock;
  static opentelemetry_utility_global_metrics_set ret;

  // Cleanup after reload
  if (default_group != rpc::telemetry::global_service::get_default_group()) {
    default_group = rpc::telemetry::global_service::get_default_group();
    ret.int64_observable_by_key.clear();
    ret.int64_observable_by_pointer.clear();
    ret.double_observable_by_key.clear();
    ret.double_observable_by_pointer.clear();
  }
  return {lock, ret};
}

}  // namespace

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
  if (opentelemetry::nostd::holds_alternative<bool>(value)) {
    return opentelemetry::nostd::get<bool>(value) ? "true" : "false";
  } else if (opentelemetry::nostd::holds_alternative<int32_t>(value)) {
    return util::log::format("{}", opentelemetry::nostd::get<int32_t>(value));
  } else if (opentelemetry::nostd::holds_alternative<int64_t>(value)) {
    return util::log::format("{}", opentelemetry::nostd::get<int64_t>(value));
  } else if (opentelemetry::nostd::holds_alternative<uint32_t>(value)) {
    return util::log::format("{}", opentelemetry::nostd::get<uint32_t>(value));
  } else if (opentelemetry::nostd::holds_alternative<uint64_t>(value)) {
    return util::log::format("{}", opentelemetry::nostd::get<uint64_t>(value));
  } else if (opentelemetry::nostd::holds_alternative<double>(value)) {
    return util::log::format("{}", opentelemetry::nostd::get<double>(value));
  } else if (opentelemetry::nostd::holds_alternative<const char*>(value)) {
    return opentelemetry::nostd::get<const char*>(value);
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::string_view>(value)) {
    return static_cast<std::string>(opentelemetry::nostd::get<opentelemetry::nostd::string_view>(value));
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::span<const bool>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item : opentelemetry::nostd::get<opentelemetry::nostd::span<const bool>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss << item;
    }
    ss << "]";
    return ss.str();
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::span<const int32_t>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item : opentelemetry::nostd::get<opentelemetry::nostd::span<const int32_t>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss << item;
    }
    ss << "]";
    return ss.str();
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::span<const int64_t>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item : opentelemetry::nostd::get<opentelemetry::nostd::span<const int64_t>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss << item;
    }
    ss << "]";
    return ss.str();
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::span<const uint32_t>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item : opentelemetry::nostd::get<opentelemetry::nostd::span<const uint32_t>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss << item;
    }
    ss << "]";
    return ss.str();
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::span<const uint64_t>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item : opentelemetry::nostd::get<opentelemetry::nostd::span<const uint64_t>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss << item;
    }
    ss << "]";
    return ss.str();
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::span<const double>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item : opentelemetry::nostd::get<opentelemetry::nostd::span<const double>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss << item;
    }
    ss << "]";
    return ss.str();
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::span<const uint8_t>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item : opentelemetry::nostd::get<opentelemetry::nostd::span<const uint8_t>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss << static_cast<uint32_t>(item);
    }
    ss << "]";
    return ss.str();
  } else if (opentelemetry::nostd::holds_alternative<
                 opentelemetry::nostd::span<const opentelemetry::nostd::string_view>>(value)) {
    std::stringstream ss;
    ss << "[";
    bool is_first = true;
    for (auto& item :
         opentelemetry::nostd::get<opentelemetry::nostd::span<const opentelemetry::nostd::string_view>>(value)) {
      if (!is_first) {
        ss << ", ";
      }
      is_first = false;
      ss.write(item.data(), static_cast<std::streamsize>(item.size()));
    }
    ss << "]";
    return ss.str();
  }

  return "";
}

SERVER_FRAME_API bool opentelemetry_utility::add_global_metics_observable_int64(
    metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
    std::function<void(opentelemetry::metrics::ObserverResult&)> fn) {
  if (!fn) {
    return false;
  }

  // opentelemetry only use metrics name as key of metric storage
  std::string key = util::log::format("{}:{}", gsl::string_view{meter_name.data(), meter_name.size()},
                                      gsl::string_view{metrics_key.name.data(), metrics_key.name.size()});

  std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set = get_global_metrics_set();
  {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    if (data_set.second.int64_observable_by_key.end() != data_set.second.int64_observable_by_key.find(key)) {
      return false;
    }
  }

  std::shared_ptr<::rpc::telemetry::group_type> telemetry_lifetime =
      rpc::telemetry::global_service::get_default_group();
  auto instrument = rpc::telemetry::global_service::get_metrics_observable(meter_name, metrics_key, telemetry_lifetime);
  if (instrument) {
    return false;
  }

  if (type == metrics_observable_type::kGauge) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_gauge_int64(meter_name, metrics_key,
                                                                                        telemetry_lifetime);
  } else if (type == metrics_observable_type::kCounter) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_counter_int64(meter_name, metrics_key,
                                                                                          telemetry_lifetime);
  } else {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_up_down_counter_int64(
        meter_name, metrics_key, telemetry_lifetime);
  }

  if (!instrument) {
    return false;
  }

  auto handle = atfw::memory::stl::make_shared<opentelemetry_utility_global_metrics_item>();
  if (!handle) {
    return false;
  }
  handle->key = key;
  handle->fn = std::move(fn);
  {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    data_set.second.int64_observable_by_key[key] = handle;
    data_set.second.int64_observable_by_pointer[reinterpret_cast<void*>(handle.get())] = handle;
  }

  instrument->AddCallback(
      [](opentelemetry::metrics::ObserverResult result, void* callback) {
        std::shared_ptr<opentelemetry_utility_global_metrics_item> metrics_item;
        {
          std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> callback_data_set =
              get_global_metrics_set();
          std::lock_guard<std::recursive_mutex> lock_guard{callback_data_set.first};
          auto iter = callback_data_set.second.int64_observable_by_pointer.find(callback);
          if (iter != callback_data_set.second.int64_observable_by_pointer.end()) {
            metrics_item = iter->second;
          }
        }
        if (!metrics_item) {
          return;
        }
        if (metrics_item->fn) {
          metrics_item->fn(result);
        }
      },
      reinterpret_cast<void*>(handle.get()));

  return true;
}

SERVER_FRAME_API bool opentelemetry_utility::add_global_metics_observable_double(
    metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
    std::function<void(opentelemetry::metrics::ObserverResult&)> fn) {
  if (!fn) {
    return false;
  }

  // opentelemetry only use metrics name as key of metric storage
  std::string key = util::log::format("{}:{}", gsl::string_view{meter_name.data(), meter_name.size()},
                                      gsl::string_view{metrics_key.name.data(), metrics_key.name.size()});

  std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set = get_global_metrics_set();
  {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    if (data_set.second.double_observable_by_key.end() != data_set.second.double_observable_by_key.find(key)) {
      return false;
    }
  }

  std::shared_ptr<::rpc::telemetry::group_type> telemetry_lifetime =
      rpc::telemetry::global_service::get_default_group();
  auto instrument = rpc::telemetry::global_service::get_metrics_observable(meter_name, metrics_key, telemetry_lifetime);
  if (instrument) {
    return false;
  }

  if (type == metrics_observable_type::kGauge) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_gauge_double(meter_name, metrics_key,
                                                                                         telemetry_lifetime);
  } else if (type == metrics_observable_type::kCounter) {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_counter_double(meter_name, metrics_key,
                                                                                           telemetry_lifetime);
  } else {
    instrument = rpc::telemetry::global_service::mutable_metrics_observable_up_down_counter_double(
        meter_name, metrics_key, telemetry_lifetime);
  }

  if (!instrument) {
    return false;
  }

  auto handle = atfw::memory::stl::make_shared<opentelemetry_utility_global_metrics_item>();
  if (!handle) {
    return false;
  }
  handle->key = key;
  handle->fn = std::move(fn);
  {
    std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
    data_set.second.double_observable_by_key[key] = handle;
    data_set.second.double_observable_by_pointer[reinterpret_cast<void*>(handle.get())] = handle;
  }

  instrument->AddCallback(
      [](opentelemetry::metrics::ObserverResult result, void* callback) {
        std::shared_ptr<opentelemetry_utility_global_metrics_item> metrics_item;
        {
          std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> callback_data_set =
              get_global_metrics_set();
          std::lock_guard<std::recursive_mutex> lock_guard{callback_data_set.first};
          auto iter = callback_data_set.second.double_observable_by_pointer.find(callback);
          if (iter != callback_data_set.second.double_observable_by_pointer.end()) {
            metrics_item = iter->second;
          }
        }
        if (!metrics_item) {
          return;
        }
        if (metrics_item->fn) {
          metrics_item->fn(result);
        }
      },
      reinterpret_cast<void*>(handle.get()));

  return true;
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
        rpc::telemetry::semantic_conventions::kRpcRouterObjectZoneID, ctx.get_task_context().reference_object_type_id};
  }
  if (0 != ctx.get_task_context().reference_object_instance_id) {
    standard_attributes[attribute_pair_size++] =
        attribute_pair_type{rpc::telemetry::semantic_conventions::kRpcRouterObjectInstanceID,
                            ctx.get_task_context().reference_object_type_id};
  }

  auto& trace_span = ctx.get_trace_span();
  if (trace_span) {
    logger->EmitLogRecord(get_notification_log_level(event_domain), trace_span->GetContext(), attrbites,
                          attribute_span_type(&standard_attributes[0], attribute_pair_size),
                          util::time::time_utility::sys_now(),
                          opentelemetry::nostd::string_view{message.data(), message.size()});
  } else {
    logger->EmitLogRecord(get_notification_log_level(event_domain), attrbites,
                          attribute_span_type(&standard_attributes[0], attribute_pair_size),
                          util::time::time_utility::sys_now(),
                          opentelemetry::nostd::string_view{message.data(), message.size()});
  }
}

SERVER_FRAME_API void opentelemetry_utility::send_notification_event(
    rpc::context& ctx, notification_domain event_domain, gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_notification_event(ctx, event_domain, event_name, message,
                          attribute_span_type{attrbites.begin(), attrbites.end()});
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(rpc::context& ctx, gsl::string_view event_domain,
                                                                       gsl::string_view event_name,
                                                                       gsl::string_view message,
                                                                       attribute_span_type attrbites) {
  send_log_to_default_group(ctx, event_domain, opentelemetry::logs::Severity::kInfo, event_domain, event_name, message,
                            attrbites);
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(
    rpc::context& ctx, gsl::string_view event_domain, gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_log_to_default_group(ctx, event_domain, opentelemetry::logs::Severity::kInfo, event_domain, event_name, message,
                            attribute_span_type{attrbites.begin(), attrbites.end()});
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(
    rpc::context& ctx, opentelemetry::logs::Severity severity, gsl::string_view event_domain,
    gsl::string_view event_name, gsl::string_view message, attribute_span_type attrbites) {
  send_log_to_default_group(ctx, event_domain, severity, event_domain, event_name, message, attrbites);
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(
    rpc::context& ctx, opentelemetry::logs::Severity severity, gsl::string_view event_domain,
    gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_log_to_default_group(ctx, event_domain, severity, event_domain, event_name, message,
                            attribute_span_type{attrbites.begin(), attrbites.end()});
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(rpc::context& ctx, gsl::string_view logger_name,
                                                                       opentelemetry::logs::Severity severity,
                                                                       gsl::string_view event_domain,
                                                                       gsl::string_view event_name,
                                                                       gsl::string_view message,
                                                                       attribute_span_type attrbites) {
  if (event_name.empty()) {
    return;
  }

  auto logger = rpc::telemetry::global_service::get_logger(
      opentelemetry::nostd::string_view{logger_name.data(), logger_name.size()});
  if (!logger) {
    return;
  }

  attribute_pair_type standard_attributes[6] = {
      attribute_pair_type{rpc::telemetry::semantic_conventions::kEventDomain,
                          opentelemetry::nostd::string_view{event_domain.data(), event_domain.size()}},
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
        rpc::telemetry::semantic_conventions::kRpcRouterObjectZoneID, ctx.get_task_context().reference_object_type_id};
  }
  if (0 != ctx.get_task_context().reference_object_instance_id) {
    standard_attributes[attribute_pair_size++] =
        attribute_pair_type{rpc::telemetry::semantic_conventions::kRpcRouterObjectInstanceID,
                            ctx.get_task_context().reference_object_type_id};
  }

  auto& trace_span = ctx.get_trace_span();
  if (trace_span) {
    logger->EmitLogRecord(
        severity, opentelemetry::nostd::string_view{event_domain.data(), event_domain.size()}, trace_span->GetContext(),
        attrbites, attribute_span_type(&standard_attributes[0], attribute_pair_size),
        util::time::time_utility::sys_now(), opentelemetry::nostd::string_view{message.data(), message.size()});
  } else {
    logger->EmitLogRecord(severity, opentelemetry::nostd::string_view{event_domain.data(), event_domain.size()},
                          attrbites, attribute_span_type(&standard_attributes[0], attribute_pair_size),
                          util::time::time_utility::sys_now(),
                          opentelemetry::nostd::string_view{message.data(), message.size()});
  }
}

SERVER_FRAME_API void opentelemetry_utility::send_log_to_default_group(
    rpc::context& ctx, gsl::string_view logger_name, opentelemetry::logs::Severity severity,
    gsl::string_view event_domain, gsl::string_view event_name, gsl::string_view message,
    std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
        attrbites) {
  send_log_to_default_group(ctx, logger_name, severity, event_domain, event_name, message,
                            attribute_span_type{attrbites.begin(), attrbites.end()});
}

}  // namespace telemetry
}  // namespace rpc

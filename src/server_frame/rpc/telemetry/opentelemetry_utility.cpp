// Copyright 2022 atframework
// Created by owent on 2022-03-03.
//

#include "rpc/telemetry/opentelemetry_utility.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/reflection.h>

#include <config/compiler/protobuf_suffix.h>

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "rpc/telemetry/rpc_global_service.h"

// Patch for Windows SDK
#if defined(GetMessage)
#  undef GetMessage
#endif

namespace rpc {

namespace telemetry {
namespace {
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

template <class CallbackType>
struct UTIL_SYMBOL_LOCAL opentelemetry_utility_global_metrics_item {
  std::string key;
  CallbackType fn;
};

struct UTIL_SYMBOL_LOCAL opentelemetry_utility_global_metrics_set {
  std::unordered_map<std::string, std::shared_ptr<opentelemetry_utility_global_metrics_item<std::function<int64_t()>>>>
      int64_observable_by_key;
  std::unordered_map<void*, std::shared_ptr<opentelemetry_utility_global_metrics_item<std::function<int64_t()>>>>
      int64_observable_by_pointer;

  std::unordered_map<std::string, std::shared_ptr<opentelemetry_utility_global_metrics_item<std::function<double()>>>>
      double_observable_by_key;
  std::unordered_map<void*, std::shared_ptr<opentelemetry_utility_global_metrics_item<std::function<double()>>>>
      double_observable_by_pointer;
};

static std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> get_global_metrics_set() {
  static std::recursive_mutex lock;
  static opentelemetry_utility_global_metrics_set ret;
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

SERVER_FRAME_API bool opentelemetry_utility::add_global_metics_observable_int64(
    metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
    std::function<int64_t()> fn) {
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

  auto handle = std::make_shared<opentelemetry_utility_global_metrics_item<std::function<int64_t()>>>();
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
        std::shared_ptr<opentelemetry_utility_global_metrics_item<std::function<int64_t()>>> metrics_item;
        {
          std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set =
              get_global_metrics_set();
          std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
          auto iter = data_set.second.int64_observable_by_pointer.find(callback);
          if (iter != data_set.second.int64_observable_by_pointer.end()) {
            metrics_item = iter->second;
          }
        }
        if (!metrics_item) {
          return;
        }
        int64_t value = 0;
        if (metrics_item->fn) {
          value = metrics_item->fn();
        }

        std::shared_ptr<::rpc::telemetry::group_type> __lifetime;
        if (opentelemetry::nostd::holds_alternative<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
          auto observer = opentelemetry::nostd::get<
              opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
          if (observer) {
            observer->Observe(static_cast<int64_t>(value),
                              rpc::telemetry::global_service::get_metrics_labels(__lifetime));
          }
        } else if (opentelemetry::nostd::holds_alternative<
                       opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
          auto observer = opentelemetry::nostd::get<
              opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
          if (observer) {
            observer->Observe(static_cast<double>(value),
                              rpc::telemetry::global_service::get_metrics_labels(__lifetime));
          }
        }
      },
      reinterpret_cast<void*>(handle.get()));

  return true;
}

SERVER_FRAME_API bool opentelemetry_utility::add_global_metics_observable_double(
    metrics_observable_type type, opentelemetry::nostd::string_view meter_name, meter_instrument_key metrics_key,
    std::function<double()> fn) {
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

  auto handle = std::make_shared<opentelemetry_utility_global_metrics_item<std::function<double()>>>();
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
        std::shared_ptr<opentelemetry_utility_global_metrics_item<std::function<double()>>> metrics_item;
        {
          std::pair<std::recursive_mutex&, opentelemetry_utility_global_metrics_set&> data_set =
              get_global_metrics_set();
          std::lock_guard<std::recursive_mutex> lock_guard{data_set.first};
          auto iter = data_set.second.double_observable_by_pointer.find(callback);
          if (iter != data_set.second.double_observable_by_pointer.end()) {
            metrics_item = iter->second;
          }
        }
        if (!metrics_item) {
          return;
        }
        double value = 0;
        if (metrics_item->fn) {
          value = metrics_item->fn();
        }

        std::shared_ptr<::rpc::telemetry::group_type> __lifetime;
        if (opentelemetry::nostd::holds_alternative<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
          auto observer = opentelemetry::nostd::get<
              opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
          if (observer) {
            observer->Observe(static_cast<int64_t>(value),
                              rpc::telemetry::global_service::get_metrics_labels(__lifetime));
          }
        } else if (opentelemetry::nostd::holds_alternative<
                       opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
          auto observer = opentelemetry::nostd::get<
              opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
          if (observer) {
            observer->Observe(static_cast<double>(value),
                              rpc::telemetry::global_service::get_metrics_labels(__lifetime));
          }
        }
      },
      reinterpret_cast<void*>(handle.get()));

  return true;
}

}  // namespace telemetry
}  // namespace rpc

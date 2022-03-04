// Copyright 2022 atframework
// Created by owent on 2022/03/03.
//

#include "rpc/telemetry/opentelemetry_utility.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/reflection.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <vector>

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
}  // namespace

void opentelemetry_utility::protobuf_to_otel_attributes(const google::protobuf::Message& message,
                                                        attributes_map_type& output, gsl::string_view key_prefix) {
  opentelemetry_utility_protobuf_to_otel_attributes_message(message.GetReflection(), message, output, key_prefix);
}

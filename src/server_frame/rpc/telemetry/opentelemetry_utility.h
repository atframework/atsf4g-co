// Copyright 2022 atframework
// Created by owent on 2022-03-03.
//

#pragma once

#include <config/compile_optimize.h>
#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/message.h>

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/sdk/common/attribute_utils.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class opentelemetry_utility {
 public:
  struct UTIL_SYMBOL_VISIBLE attributes_map_type {
    using type = std::unordered_map<std::string, opentelemetry::common::AttributeValue>;
    using value_type = std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue>;
    using string_view_vec_type = std::vector<opentelemetry::nostd::string_view>;

    type attributes;
    value_type owned_attributes;

    std::list<string_view_vec_type> string_view_storages;
    std::list<std::unique_ptr<bool[]>> bool_view_storages;
  };

 public:
  SERVER_FRAME_API void protobuf_to_otel_attributes(const google::protobuf::Message& message, attributes_map_type& output,
                                   gsl::string_view key_prefix = "");
};

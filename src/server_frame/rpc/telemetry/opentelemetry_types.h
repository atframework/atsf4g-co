// Copyright 2023 atframework
// Created by owent on 2023-09-14.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <opentelemetry/common/key_value_iterable.h>
#include <opentelemetry/nostd/function_ref.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/nostd/utility.h>

namespace rpc {

namespace telemetry {
struct UTIL_SYMBOL_VISIBLE meter_instrument_key {
  opentelemetry::nostd::string_view name;
  opentelemetry::nostd::string_view description;
  opentelemetry::nostd::string_view unit;

  inline meter_instrument_key(opentelemetry::nostd::string_view input_name = "",
                              opentelemetry::nostd::string_view input_description = "",
                              opentelemetry::nostd::string_view input_unit = "")
      : name(input_name), description(input_description), unit(input_unit) {}
};

template <class T>
class UTIL_SYMBOL_VISIBLE multiple_key_value_iterable_view final : public opentelemetry::common::KeyValueIterable {
 public:
  explicit multiple_key_value_iterable_view(opentelemetry::nostd::span<const T> containers) noexcept
      : containers_{containers} {}

  // KeyValueIterable
  bool ForEachKeyValue(
      opentelemetry::nostd::function_ref<bool(opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue)>
          callback) const noexcept override {
    for (auto& container : containers_) {
      auto element_iter = std::begin(container);
      auto element_last = std::end(container);
      for (; element_iter != element_last; ++element_iter) {
        if (!callback(element_iter->first, element_iter->second)) {
          return false;
        }
      }
    }
    return true;
  }

  size_t size() const noexcept override {
    size_t ret = 0;
    for (auto& container : containers_) {
      ret += opentelemetry::nostd::size(container);
    }
    return ret;
  }

 private:
  opentelemetry::nostd::span<const T> containers_;
};
}  // namespace telemetry
}  // namespace rpc

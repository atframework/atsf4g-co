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
struct ATFW_UTIL_SYMBOL_VISIBLE meter_instrument_key {
  opentelemetry::nostd::string_view name;
  opentelemetry::nostd::string_view description;
  opentelemetry::nostd::string_view unit;

  inline meter_instrument_key(opentelemetry::nostd::string_view input_name = "",
                              opentelemetry::nostd::string_view input_description = "",
                              opentelemetry::nostd::string_view input_unit = "")
      : name(input_name), description(input_description), unit(input_unit) {}
};

template <class T>
class ATFW_UTIL_SYMBOL_VISIBLE multiple_key_value_iterable_view final : public opentelemetry::common::KeyValueIterable {
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

enum class trace_parent_mode : uint8_t {
  kParent = 0,
  kLink = 1,
};

enum trace_dynamic_policy : uint8_t {
  kUnset = 0,
  kDrop,
  kRecording,
};

struct ATFW_UTIL_SYMBOL_VISIBLE trace_inherit_options {
  trace_parent_mode mode;
  bool inherit_allocator;
  bool inherit_parent_span;

  ATFW_UTIL_FORCEINLINE trace_inherit_options() noexcept
      : mode(trace_parent_mode::kParent), inherit_allocator(true), inherit_parent_span(true) {}
  ATFW_UTIL_FORCEINLINE explicit trace_inherit_options(trace_parent_mode m) noexcept
      : mode(m), inherit_allocator(true), inherit_parent_span(true) {}
  ATFW_UTIL_FORCEINLINE explicit trace_inherit_options(trace_parent_mode m, bool inherit_alloc) noexcept
      : mode(m), inherit_allocator(inherit_alloc), inherit_parent_span(true) {}
  ATFW_UTIL_FORCEINLINE explicit trace_inherit_options(trace_parent_mode m, bool inherit_alloc,
                                                       bool inherit_parent_trace_span) noexcept
      : mode(m), inherit_allocator(inherit_alloc), inherit_parent_span(inherit_parent_trace_span) {}
};

struct ATFW_UTIL_SYMBOL_VISIBLE trace_create_options {};

}  // namespace telemetry
}  // namespace rpc

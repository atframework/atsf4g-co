// Copyright 2024 atframework
// Created by owent

#include "logic/hpa/logic_hpa_data_type.h"

#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <log/log_wrapper.h>

#include <common/string_oprs.h>

#include <memory/object_allocator.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#if !((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
constexpr const int64_t logic_hpa_observable_value::kInt64NaN;
constexpr const double logic_hpa_observable_value::kDoubleNaN;
#endif

namespace {
static int64_t pull_value_to_int64(const logic_hpa_pull_value& input) {
  if (absl::holds_alternative<int64_t>(input.value)) {
    return absl::get<int64_t>(input.value);
  } else if (absl::holds_alternative<double>(input.value)) {
    return static_cast<int64_t>(absl::get<double>(input.value) + std::numeric_limits<float>::epsilon());
  } else {
    return 0;
  }
}

static double pull_value_to_double(const logic_hpa_pull_value& input) {
  if (absl::holds_alternative<int64_t>(input.value)) {
    return static_cast<double>(absl::get<int64_t>(input.value));
  } else if (absl::holds_alternative<double>(input.value)) {
    return absl::get<double>(input.value);
  } else {
    return 0.0;
  }
}

}  // namespace

SERVER_FRAME_API logic_hpa_pull_instant_record::logic_hpa_pull_instant_record() {}

SERVER_FRAME_API logic_hpa_pull_instant_record::~logic_hpa_pull_instant_record() {}

SERVER_FRAME_API void logic_hpa_pull_instant_record::set_name(gsl::string_view name) noexcept {
  name_ = std::string(name);
}

SERVER_FRAME_API void logic_hpa_pull_instant_record::set_value(const logic_hpa_pull_value& value) noexcept {
  data_ = value;
}

SERVER_FRAME_API gsl::string_view logic_hpa_pull_instant_record::get_name() const noexcept { return name_; }

SERVER_FRAME_API std::chrono::system_clock::time_point logic_hpa_pull_instant_record::get_time_point() const noexcept {
  return data_.timepoint;
}

SERVER_FRAME_API int64_t logic_hpa_pull_instant_record::get_value_as_int64() const noexcept {
  return pull_value_to_int64(data_);
}

SERVER_FRAME_API double logic_hpa_pull_instant_record::get_value_as_double() const noexcept {
  return pull_value_to_double(data_);
}

SERVER_FRAME_API logic_hpa_pull_range_record::logic_hpa_pull_range_record() {}

SERVER_FRAME_API logic_hpa_pull_range_record::~logic_hpa_pull_range_record() {}

SERVER_FRAME_API void logic_hpa_pull_range_record::set_name(gsl::string_view name) noexcept {
  name_ = std::string(name);
}

SERVER_FRAME_API void logic_hpa_pull_range_record::add_value(const logic_hpa_pull_value& value) noexcept {
  data_.push_back(value);
}

SERVER_FRAME_API gsl::string_view logic_hpa_pull_range_record::get_name() const noexcept { return name_; }

SERVER_FRAME_API std::size_t logic_hpa_pull_range_record::get_value_size() const noexcept { return data_.size(); }

SERVER_FRAME_API std::chrono::system_clock::time_point logic_hpa_pull_range_record::get_time_point(
    std::size_t index) const noexcept {
  if (index >= data_.size()) {
    return std::chrono::system_clock::from_time_t(0);
  }

  return data_[index].timepoint;
}

SERVER_FRAME_API int64_t logic_hpa_pull_range_record::get_value_as_int64(std::size_t index) const noexcept {
  if (index >= data_.size()) {
    return 0;
  }

  return pull_value_to_int64(data_[index]);
}

SERVER_FRAME_API double logic_hpa_pull_range_record::get_value_as_double(std::size_t index) const noexcept {
  if (index >= data_.size()) {
    return 0.0;
  }

  return pull_value_to_double(data_[index]);
}

std::unique_ptr<logic_hpa_pull_instant_record> logic_hpa_pull_range_record::reduce(
    logic_hpa_range_reduce_type reduce_type) const noexcept {
  logic_hpa_pull_value value;
  if (data_.empty()) {
    return make_instant_record(value);
  }
  value = data_[0];

  // Range数据转Instant数据的规则
  switch (reduce_type) {
    case logic_hpa_range_reduce_type::kFirst: {
      for (size_t i = 1; i < data_.size(); ++i) {
        if (data_[i].timepoint < value.timepoint) {
          value = data_[i];
        }
      }
      break;
    }
    case logic_hpa_range_reduce_type::kSum: {
      bool previous_int64 = absl::holds_alternative<int64_t>(value.value);
      for (size_t i = 1; i < data_.size(); ++i) {
        if (previous_int64 && absl::holds_alternative<int64_t>(data_[i].value)) {
          value.value = pull_value_to_int64(value) + pull_value_to_int64(data_[i]);
        } else {
          previous_int64 = false;
          value.value = pull_value_to_double(value) + pull_value_to_double(data_[i]);
        }
      }
      break;
    }
    case logic_hpa_range_reduce_type::kAvg: {
      bool previous_int64 = absl::holds_alternative<int64_t>(value.value);
      for (size_t i = 1; i < data_.size(); ++i) {
        if (previous_int64 && absl::holds_alternative<int64_t>(data_[i].value)) {
          value.value = pull_value_to_int64(value) + pull_value_to_int64(data_[i]);
        } else {
          previous_int64 = false;
          value.value = pull_value_to_double(value) + pull_value_to_double(data_[i]);
        }
      }
      if (previous_int64 && (pull_value_to_int64(value) % static_cast<int64_t>(data_.size())) == 0) {
        value.value = pull_value_to_int64(value) / static_cast<int64_t>(data_.size());
      } else {
        value.value = pull_value_to_double(value) / static_cast<double>(data_.size());
      }
      break;
    }
    case logic_hpa_range_reduce_type::kMin: {
      bool previous_int64 = absl::holds_alternative<int64_t>(value.value);
      for (size_t i = 1; i < data_.size(); ++i) {
        if (previous_int64 && absl::holds_alternative<int64_t>(data_[i].value)) {
          if (pull_value_to_int64(value) > pull_value_to_int64(data_[i])) {
            value = data_[i];
          }
        } else {
          previous_int64 = false;
          if (pull_value_to_double(value) > pull_value_to_double(data_[i])) {
            value = data_[i];
          }
        }
      }
      break;
    }
    case logic_hpa_range_reduce_type::kMax: {
      bool previous_int64 = absl::holds_alternative<int64_t>(value.value);
      for (size_t i = 1; i < data_.size(); ++i) {
        if (previous_int64 && absl::holds_alternative<int64_t>(data_[i].value)) {
          if (pull_value_to_int64(value) < pull_value_to_int64(data_[i])) {
            value = data_[i];
          }
        } else {
          previous_int64 = false;
          if (pull_value_to_double(value) < pull_value_to_double(data_[i])) {
            value = data_[i];
          }
        }
      }
      break;
    }
    default: {  // logic_hpa_range_reduce_type::kLast
      for (size_t i = 1; i < data_.size(); ++i) {
        if (data_[i].timepoint > value.timepoint) {
          value = data_[i];
        }
      }
      break;
    }
  }

  return make_instant_record(value);
}

SERVER_FRAME_API logic_hpa_pull_result::logic_hpa_pull_result() {}

SERVER_FRAME_API logic_hpa_pull_result::~logic_hpa_pull_result() {}

SERVER_FRAME_API void logic_hpa_pull_result::add_instant_record(
    std::unique_ptr<logic_hpa_pull_instant_record>&& record) noexcept {
  instant_record_.emplace_back(std::move(record));
}

SERVER_FRAME_API void logic_hpa_pull_result::add_range_record(
    std::unique_ptr<logic_hpa_pull_range_record>&& record) noexcept {
  range_record_.emplace_back(std::move(record));
}

SERVER_FRAME_API gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>>
logic_hpa_pull_result::get_instant_records() const noexcept {
  return instant_record_;
}

SERVER_FRAME_API gsl::span<const std::unique_ptr<logic_hpa_pull_range_record>>
logic_hpa_pull_result::get_range_records() const noexcept {
  return range_record_;
}

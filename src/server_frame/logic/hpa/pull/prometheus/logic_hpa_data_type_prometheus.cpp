// Copyright 2024 atframework
// Created by owent

#include "logic/hpa/pull/prometheus/logic_hpa_data_type_prometheus.h"

#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <log/log_wrapper.h>

#include <common/string_oprs.h>

#include <memory/object_allocator.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace {
static logic_hpa_pull_value rapidjson_value_to_pull_value(rapidjson::Value& input) {
  logic_hpa_pull_value ret;
  if (!input.IsArray()) {
    ret.timepoint = std::chrono::system_clock::from_time_t(0);
    return ret;
  }
  if (input.Size() < 2) {
    ret.timepoint = std::chrono::system_clock::from_time_t(0);
    return ret;
  }

  // 数据类型转换，标准化适配Json数据类型到指标数据类型
  if (input[0].IsString()) {
    std::stringstream ss;
    ss.write(input[0].GetString(), static_cast<std::streamsize>(input[0].GetStringLength()));
    double timepoint = 0;
    ss >> timepoint;
    ret.timepoint = std::chrono::system_clock::from_time_t(static_cast<time_t>(std::floor(timepoint)));
    ret.timepoint += std::chrono::microseconds{static_cast<time_t>((timepoint - std::floor(timepoint)) * 1000000)};
  } else if (input[0].IsInt64()) {
    ret.timepoint = std::chrono::system_clock::from_time_t(input[0].GetInt64());
  } else if (input[0].IsDouble()) {
    double timepoint = input[0].GetDouble();
    ret.timepoint = std::chrono::system_clock::from_time_t(static_cast<time_t>(std::floor(timepoint)));
    ret.timepoint += std::chrono::microseconds{static_cast<time_t>((timepoint - std::floor(timepoint)) * 1000000)};
  } else {
    ret.timepoint = std::chrono::system_clock::from_time_t(0);
  }

  if (input[1].IsString()) {
    gsl::string_view value{input[1].GetString(), static_cast<size_t>(input[1].GetStringLength())};

    if (value.end() == std::find(value.begin(), value.end(), '.')) {
      ret.value = util::string::to_int<int64_t>(value);
    } else {
      std::stringstream ss;
      ss.write(input[1].GetString(), static_cast<std::streamsize>(input[1].GetStringLength()));
      double real_value = 0;
      ss >> real_value;
      ret.value = real_value;
    }

  } else if (input[1].IsInt64()) {
    ret.value = input[1].GetInt64();
  } else if (input[1].IsDouble()) {
    ret.value = input[1].GetDouble();
  }

  return ret;
}
}  // namespace

struct logic_hpa_pull_internal_prometheus_result_data {
  rapidjson::Document document;
};

struct logic_hpa_pull_internal_prometheus_record_data {
  rapidjson::Value data;
  util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_result_data> result;
};

SERVER_FRAME_API logic_hpa_pull_instant_record_prometheus::logic_hpa_pull_instant_record_prometheus(
    util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data)
    : internal_data_{internal_data} {
  if (!internal_data) {
    return;
  }

  if (!internal_data->data.IsObject()) {
    return;
  }

  // 解析Prometheus里的特殊标签
  // @see https://prometheus.io/docs/prometheus/latest/querying/api/
  do {
    auto metric_iter = internal_data->data.FindMember("metric");
    if (metric_iter == internal_data->data.MemberEnd()) {
      break;
    }

    if (!metric_iter->value.IsObject()) {
      break;
    }

    auto name_iter = metric_iter->value.FindMember("__name__");
    if (name_iter == metric_iter->value.MemberEnd()) {
      break;
    }
    if (!name_iter->value.IsString()) {
      break;
    }

    set_name(gsl::string_view{name_iter->value.GetString(), name_iter->value.GetStringLength()});
  } while (false);

  // 提取数据
  do {
    auto value_iter = internal_data->data.FindMember("value");
    if (value_iter == internal_data->data.MemberEnd()) {
      break;
    }
    set_value(rapidjson_value_to_pull_value(value_iter->value));
  } while (false);
}

SERVER_FRAME_API logic_hpa_pull_instant_record_prometheus::logic_hpa_pull_instant_record_prometheus(
    util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data,
    logic_hpa_pull_value value)
    : internal_data_{internal_data} {
  set_value(value);
  if (!internal_data) {
    return;
  }

  if (!internal_data->data.IsObject()) {
    return;
  }

  // 解析Prometheus里的特殊标签
  // @see https://prometheus.io/docs/prometheus/latest/querying/api/
  do {
    auto metric_iter = internal_data->data.FindMember("metric");
    if (metric_iter == internal_data->data.MemberEnd()) {
      break;
    }

    if (!metric_iter->value.IsObject()) {
      break;
    }

    auto name_iter = metric_iter->value.FindMember("__name__");
    if (name_iter == metric_iter->value.MemberEnd()) {
      break;
    }
    if (!name_iter->value.IsString()) {
      break;
    }

    set_name(gsl::string_view{name_iter->value.GetString(), name_iter->value.GetStringLength()});
  } while (false);
}

SERVER_FRAME_API logic_hpa_pull_instant_record_prometheus::~logic_hpa_pull_instant_record_prometheus() {}

SERVER_FRAME_API gsl::string_view logic_hpa_pull_instant_record_prometheus::get_label(
    gsl::string_view key) const noexcept {
  if (!internal_data_) {
    return {};
  }

  if (!internal_data_->data.IsObject()) {
    return {};
  }

  // Prometheus的标签在 metric 字段里
  auto metric_iter = internal_data_->data.FindMember("metric");
  if (metric_iter == internal_data_->data.MemberEnd()) {
    return {};
  }

  if (!metric_iter->value.IsObject()) {
    return {};
  }

  // 提取string_view，不不会发生复制
  rapidjson::Value jkey;
  jkey.SetString(key.data(), static_cast<rapidjson::SizeType>(key.size()));
  auto value_iter = metric_iter->value.FindMember(jkey);
  if (value_iter == metric_iter->value.MemberEnd()) {
    return {};
  }

  if (!value_iter->value.IsString()) {
    return {};
  }

  return {value_iter->value.GetString(), static_cast<size_t>(value_iter->value.GetStringLength())};
}

SERVER_FRAME_API logic_hpa_pull_range_record_prometheus::logic_hpa_pull_range_record_prometheus(
    util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data)
    : internal_data_{internal_data} {
  if (!internal_data) {
    return;
  }

  if (!internal_data->data.IsObject()) {
    return;
  }

  // 解析Prometheus里的特殊标签
  // @see https://prometheus.io/docs/prometheus/latest/querying/api/
  do {
    auto metric_iter = internal_data->data.FindMember("metric");
    if (metric_iter == internal_data->data.MemberEnd()) {
      break;
    }

    if (!metric_iter->value.IsObject()) {
      break;
    }

    auto name_iter = metric_iter->value.FindMember("__name__");
    if (name_iter == metric_iter->value.MemberEnd()) {
      break;
    }
    if (!name_iter->value.IsString()) {
      break;
    }

    set_name(gsl::string_view{name_iter->value.GetString(), name_iter->value.GetStringLength()});
  } while (false);

  // 提取数据
  do {
    auto values_iter = internal_data->data.FindMember("values");
    if (values_iter == internal_data->data.MemberEnd()) {
      break;
    }
    if (!values_iter->value.IsArray()) {
      break;
    }

    for (auto value_iter = values_iter->value.Begin(); value_iter != values_iter->value.End(); ++value_iter) {
      add_value(rapidjson_value_to_pull_value(*value_iter));
    }
  } while (false);
}

SERVER_FRAME_API logic_hpa_pull_range_record_prometheus::~logic_hpa_pull_range_record_prometheus() {}

SERVER_FRAME_API std::unique_ptr<logic_hpa_pull_instant_record>
logic_hpa_pull_range_record_prometheus::make_instant_record(const logic_hpa_pull_value& value) const noexcept {
  return gsl::make_unique<logic_hpa_pull_instant_record_prometheus>(internal_data_, value);
}

SERVER_FRAME_API gsl::string_view logic_hpa_pull_range_record_prometheus::get_label(
    gsl::string_view key) const noexcept {
  if (!internal_data_) {
    return {};
  }

  if (!internal_data_->data.IsObject()) {
    return {};
  }

  // Prometheus的标签在 metric 字段里
  auto metric_iter = internal_data_->data.FindMember("metric");
  if (metric_iter == internal_data_->data.MemberEnd()) {
    return {};
  }

  if (!metric_iter->value.IsObject()) {
    return {};
  }

  // 提取string_view，不不会发生复制
  rapidjson::Value jkey;
  jkey.SetString(key.data(), static_cast<rapidjson::SizeType>(key.size()));
  auto value_iter = metric_iter->value.FindMember(jkey);
  if (value_iter == metric_iter->value.MemberEnd()) {
    return {};
  }

  if (!value_iter->value.IsString()) {
    return {};
  }

  return {value_iter->value.GetString(), static_cast<size_t>(value_iter->value.GetStringLength())};
}

SERVER_FRAME_API logic_hpa_pull_result_prometheus::logic_hpa_pull_result_prometheus()
    : internal_data_{atfw::memory::stl::make_strong_rc<logic_hpa_pull_internal_prometheus_result_data>()} {}

SERVER_FRAME_API logic_hpa_pull_result_prometheus::~logic_hpa_pull_result_prometheus() {}

SERVER_FRAME_API bool logic_hpa_pull_result_prometheus::parse(gsl::string_view input) noexcept {
  if (!internal_data_ || input.empty()) {
    return false;
  }

#if defined(ATFRAMEWORK_UTILS_ENABLE_EXCEPTION) && ATFRAMEWORK_UTILS_ENABLE_EXCEPTION
  try {
#endif
    internal_data_->document.Parse(input.data(), input.size());
#if defined(ATFRAMEWORK_UTILS_ENABLE_EXCEPTION) && ATFRAMEWORK_UTILS_ENABLE_EXCEPTION
  } catch (...) {
    FWLOGERROR("[HPA]: Parse json failed.\n{}", input);

    internal_data_->document.SetObject();
    return false;
  }
#endif
  if (!internal_data_->document.IsObject()) {
    internal_data_->document.SetObject();
    return false;
  }

  // Parse results
  auto data_iter = internal_data_->document.FindMember("data");
  if (data_iter == internal_data_->document.MemberEnd()) {
    return true;
  }
  if (data_iter->value.IsNull()) {
    return true;
  }
  if (data_iter->value.IsString()) {
    FWLOGDEBUG("[HPA]: Got unknown pull result data: {}",
               gsl::string_view{data_iter->value.GetString(), static_cast<size_t>(data_iter->value.GetStringLength())});
    return true;
  }
  if (!data_iter->value.IsObject()) {
    return false;
  }

  // 同时适配多种不同类型的数据
  // @see https://prometheus.io/docs/prometheus/latest/querying/api/
  auto result_iter = data_iter->value.FindMember("result");
  if (result_iter == data_iter->value.MemberEnd()) {
    return true;
  }
  if (result_iter->value.IsNull()) {
    return true;
  }

  if (result_iter->value.IsObject()) {
    auto internal_record_data = atfw::memory::stl::make_strong_rc<logic_hpa_pull_internal_prometheus_record_data>();
    internal_record_data->data = result_iter->value;
    internal_record_data->result = internal_data_;
    if (internal_record_data->data.MemberEnd() != internal_record_data->data.FindMember("value")) {
      add_instant_record(gsl::make_unique<logic_hpa_pull_instant_record_prometheus>(internal_record_data));
    } else if (internal_record_data->data.MemberEnd() != internal_record_data->data.FindMember("values")) {
      add_range_record(gsl::make_unique<logic_hpa_pull_range_record_prometheus>(internal_record_data));
    }
    return true;
  }

  if (result_iter->value.IsArray()) {
    for (auto item_iter = result_iter->value.Begin(); item_iter != result_iter->value.End(); ++item_iter) {
      if (!(*item_iter).IsObject()) {
        continue;
      }
      auto internal_record_data = atfw::memory::stl::make_strong_rc<logic_hpa_pull_internal_prometheus_record_data>();
      internal_record_data->data = *item_iter;
      internal_record_data->result = internal_data_;

      if (internal_record_data->data.MemberEnd() != internal_record_data->data.FindMember("value")) {
        add_instant_record(gsl::make_unique<logic_hpa_pull_instant_record_prometheus>(internal_record_data));
      } else if (internal_record_data->data.MemberEnd() != internal_record_data->data.FindMember("values")) {
        add_range_record(gsl::make_unique<logic_hpa_pull_range_record_prometheus>(internal_record_data));
      }
    }
  } else {
    return false;
  }
  return true;
}

SERVER_FRAME_API gsl::string_view logic_hpa_pull_result_prometheus::get_status() const noexcept {
  if (!internal_data_) {
    return {};
  }

  auto iter = internal_data_->document.FindMember("status");
  if (iter == internal_data_->document.MemberEnd()) {
    return {};
  }

  if (!iter->value.IsString()) {
    return {};
  }

  return {iter->value.GetString(), static_cast<size_t>(iter->value.GetStringLength())};
}

SERVER_FRAME_API bool logic_hpa_pull_result_prometheus::is_error() const noexcept {
  if (!internal_data_) {
    return false;
  }

  auto iter = internal_data_->document.FindMember("error");
  if (iter == internal_data_->document.MemberEnd()) {
    return false;
  }

  return get_status() == "error";
}

SERVER_FRAME_API gsl::string_view logic_hpa_pull_result_prometheus::get_error_type() const noexcept {
  if (!internal_data_) {
    return {};
  }

  auto iter = internal_data_->document.FindMember("errorType");
  if (iter == internal_data_->document.MemberEnd()) {
    return {};
  }

  if (!iter->value.IsString()) {
    return {};
  }

  return {iter->value.GetString(), static_cast<size_t>(iter->value.GetStringLength())};
}

SERVER_FRAME_API gsl::string_view logic_hpa_pull_result_prometheus::get_error_message() const noexcept {
  if (!internal_data_) {
    return {};
  }

  auto iter = internal_data_->document.FindMember("error");
  if (iter == internal_data_->document.MemberEnd()) {
    return {};
  }

  if (!iter->value.IsString()) {
    return {};
  }

  return {iter->value.GetString(), static_cast<size_t>(iter->value.GetStringLength())};
}

SERVER_FRAME_API std::vector<gsl::string_view> logic_hpa_pull_result_prometheus::get_warning_messages() const noexcept {
  std::vector<gsl::string_view> ret;
  if (!internal_data_) {
    return ret;
  }

  auto iter = internal_data_->document.FindMember("warnings");
  if (iter == internal_data_->document.MemberEnd()) {
    return ret;
  }

  if (iter->value.IsString()) {
    ret.push_back({iter->value.GetString(), static_cast<size_t>(iter->value.GetStringLength())});
    return ret;
  }

  if (!iter->value.IsArray()) {
    return {};
  }

  for (auto item_iter = iter->value.Begin(); item_iter != iter->value.End(); ++item_iter) {
    if (!(*item_iter).IsString()) {
      continue;
    }

    ret.push_back({(*item_iter).GetString(), static_cast<size_t>((*item_iter).GetStringLength())});
  }

  return ret;
}

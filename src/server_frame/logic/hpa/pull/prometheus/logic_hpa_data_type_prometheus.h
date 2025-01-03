// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

#include "logic/hpa/logic_hpa_data_type.h"

struct logic_hpa_pull_internal_prometheus_record_data;
struct logic_hpa_pull_internal_prometheus_result_data;

class UTIL_SYMBOL_VISIBLE logic_hpa_pull_instant_record_prometheus : public logic_hpa_pull_instant_record {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_pull_instant_record_prometheus);
  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_pull_instant_record_prometheus);

 public:
  SERVER_FRAME_API logic_hpa_pull_instant_record_prometheus(
      atfw::util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data);

  SERVER_FRAME_API logic_hpa_pull_instant_record_prometheus(
      atfw::util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data,
      logic_hpa_pull_value value);

  SERVER_FRAME_API ~logic_hpa_pull_instant_record_prometheus();

  SERVER_FRAME_API gsl::string_view get_label(gsl::string_view key) const noexcept override;

 private:
  atfw::util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data_;
};

class UTIL_SYMBOL_VISIBLE logic_hpa_pull_range_record_prometheus : public logic_hpa_pull_range_record {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_pull_range_record_prometheus);

  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_pull_range_record_prometheus);

 protected:
  SERVER_FRAME_API std::unique_ptr<logic_hpa_pull_instant_record> make_instant_record(
      const logic_hpa_pull_value& value) const noexcept override;

 public:
  SERVER_FRAME_API logic_hpa_pull_range_record_prometheus(
      atfw::util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data);

  SERVER_FRAME_API ~logic_hpa_pull_range_record_prometheus();

  SERVER_FRAME_API gsl::string_view get_label(gsl::string_view key) const noexcept override;

 private:
  atfw::util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_record_data> internal_data_;
};

// Message format, @see https://prometheus.io/docs/prometheus/latest/querying/api/
// {
//   "status": "success" | "error",
//   "data": <data>,
//
//   // Only set if status is "error". The data field may still hold
//   // additional data.
//   "errorType": "<string>",
//   "error": "<string>",
//
//   // Only if there were warnings while executing the request.
//   // There will still be data in the data field.
//   "warnings": ["<string>"]
// }

class UTIL_SYMBOL_VISIBLE logic_hpa_pull_result_prometheus : public logic_hpa_pull_result {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_pull_result_prometheus);

  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_pull_result_prometheus);

 public:
  SERVER_FRAME_API logic_hpa_pull_result_prometheus();

  SERVER_FRAME_API ~logic_hpa_pull_result_prometheus();

  SERVER_FRAME_API bool parse(gsl::string_view input) noexcept override;

  SERVER_FRAME_API gsl::string_view get_status() const noexcept override;

  SERVER_FRAME_API bool is_error() const noexcept override;

  SERVER_FRAME_API gsl::string_view get_error_type() const noexcept override;

  SERVER_FRAME_API gsl::string_view get_error_message() const noexcept override;

  SERVER_FRAME_API std::vector<gsl::string_view> get_warning_messages() const noexcept override;

  SERVER_FRAME_API gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record_prometheus>> get_instant_records()
      const noexcept;

  SERVER_FRAME_API gsl::span<const std::unique_ptr<logic_hpa_pull_range_record_prometheus>> get_range_records()
      const noexcept;

 private:
  atfw::util::memory::strong_rc_ptr<logic_hpa_pull_internal_prometheus_result_data> internal_data_;
};

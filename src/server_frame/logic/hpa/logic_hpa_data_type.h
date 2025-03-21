// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/server_frame_build_feature.h>

#include <absl/types/variant.h>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <gsl/select-gsl.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#ifdef min
#  undef min
#endif

enum class logic_hpa_range_reduce_type : int32_t {
  kLast = 0,
  kFirst = 1,
  kSum = 2,
  kAvg = 3,
  kMin = 4,
  kMax = 5,
};

enum class logic_hpa_ssl_version : int32_t {
  kNone = 0,
  kSsl3,    // default for curl version < 7.34.0
  kTlsV10,  // TLSv1.0
  kTlsV11,  // TLSv1.1
  kTlsV12,  // TLSv1.2, default for curl version >= 7.34.0
  kTlsV13,  // TLSv1.3
};

enum class logic_hpa_event_active_type : int8_t {
  // Not active, and will be ignored
  kUnactive = 0,
  // Watch only, do not active but trigger callback when actived by other handle
  kWatchOnly = 1,
  // Active this event
  kActive = 2,
};

template <class T, bool HasQuietNaN>
struct logic_hpa_observable_value_NaN;
template <class T>
struct logic_hpa_observable_value_NaN<T, true> {
  static inline constexpr T NaN() noexcept { return std::numeric_limits<T>::quiet_NaN(); }
};
template <class T>
struct logic_hpa_observable_value_NaN<T, false> {
  static inline constexpr T NaN() noexcept { return std::numeric_limits<T>::signaling_NaN(); }
};

struct logic_hpa_observable_value {
  static constexpr const int64_t kInt64NaN = std::numeric_limits<int64_t>::min();
  static constexpr const double kDoubleNaN =
      logic_hpa_observable_value_NaN<double, std::numeric_limits<double>::has_quiet_NaN>::NaN();

  ATFW_UTIL_FORCEINLINE static bool is_nan(int64_t v) noexcept { return v == kInt64NaN; }

  ATFW_UTIL_FORCEINLINE static bool is_nan(double v) noexcept { return ::std::isnan(v); }
};

struct ATFW_UTIL_SYMBOL_VISIBLE logic_hpa_pull_value {
  std::chrono::system_clock::time_point timepoint = std::chrono::system_clock::from_time_t(0);
  absl::variant<int64_t, double> value;

  inline logic_hpa_pull_value() noexcept {}
};

enum class logic_hpa_pull_metrics_name_mode : uint8_t {
  kWithNameAndUnit = 0,
  kWithNameOnly = 1,
};

class ATFW_UTIL_SYMBOL_VISIBLE logic_hpa_pull_instant_record {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_pull_instant_record);
  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_pull_instant_record);

 protected:
  SERVER_FRAME_API logic_hpa_pull_instant_record();

  SERVER_FRAME_API void set_name(gsl::string_view name) noexcept;

  SERVER_FRAME_API void set_value(const logic_hpa_pull_value& value) noexcept;

 public:
  SERVER_FRAME_API virtual ~logic_hpa_pull_instant_record();

  SERVER_FRAME_API gsl::string_view get_name() const noexcept;

  SERVER_FRAME_API std::chrono::system_clock::time_point get_time_point() const noexcept;

  SERVER_FRAME_API int64_t get_value_as_int64() const noexcept;

  SERVER_FRAME_API double get_value_as_double() const noexcept;

  virtual gsl::string_view get_label(gsl::string_view key) const noexcept = 0;

 private:
  std::string name_;
  logic_hpa_pull_value data_;
};

class ATFW_UTIL_SYMBOL_VISIBLE logic_hpa_pull_range_record {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_pull_range_record);

  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_pull_range_record);

 protected:
  SERVER_FRAME_API logic_hpa_pull_range_record();

  SERVER_FRAME_API void set_name(gsl::string_view name) noexcept;

  SERVER_FRAME_API void add_value(const logic_hpa_pull_value& value) noexcept;

  virtual std::unique_ptr<logic_hpa_pull_instant_record> make_instant_record(
      const logic_hpa_pull_value& value) const noexcept = 0;

 public:
  SERVER_FRAME_API virtual ~logic_hpa_pull_range_record();

  SERVER_FRAME_API gsl::string_view get_name() const noexcept;

  SERVER_FRAME_API std::size_t get_value_size() const noexcept;

  SERVER_FRAME_API std::chrono::system_clock::time_point get_time_point(std::size_t index) const noexcept;

  SERVER_FRAME_API int64_t get_value_as_int64(std::size_t index) const noexcept;

  SERVER_FRAME_API double get_value_as_double(std::size_t index) const noexcept;

  std::unique_ptr<logic_hpa_pull_instant_record> reduce(logic_hpa_range_reduce_type reduce_type) const noexcept;

  virtual gsl::string_view get_label(gsl::string_view key) const noexcept = 0;

 private:
  std::string name_;
  std::vector<logic_hpa_pull_value> data_;
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

class ATFW_UTIL_SYMBOL_VISIBLE logic_hpa_pull_result {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_pull_result);

  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_pull_result);

 protected:
  SERVER_FRAME_API logic_hpa_pull_result();

  SERVER_FRAME_API void add_instant_record(std::unique_ptr<logic_hpa_pull_instant_record>&& record) noexcept;

  SERVER_FRAME_API void add_range_record(std::unique_ptr<logic_hpa_pull_range_record>&& record) noexcept;

 public:
  SERVER_FRAME_API virtual ~logic_hpa_pull_result();

  virtual bool parse(gsl::string_view input) noexcept = 0;

  virtual gsl::string_view get_status() const noexcept = 0;

  virtual bool is_error() const noexcept = 0;

  virtual gsl::string_view get_error_type() const noexcept = 0;

  virtual gsl::string_view get_error_message() const noexcept = 0;

  virtual std::vector<gsl::string_view> get_warning_messages() const noexcept = 0;

  ATFW_UTIL_FORCEINLINE bool has_range_record() const noexcept { return !range_record_.empty(); }

  ATFW_UTIL_FORCEINLINE bool has_instant_record() const noexcept { return !instant_record_.empty(); }

  SERVER_FRAME_API gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> get_instant_records() const noexcept;

  SERVER_FRAME_API gsl::span<const std::unique_ptr<logic_hpa_pull_range_record>> get_range_records() const noexcept;

 private:
  std::vector<std::unique_ptr<logic_hpa_pull_instant_record>> instant_record_;
  std::vector<std::unique_ptr<logic_hpa_pull_range_record>> range_record_;
};

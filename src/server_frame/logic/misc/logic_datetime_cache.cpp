// Copyright 2026 atframework

#include "logic/misc/logic_datetime_cache.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.const.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/compile_optimize.h>
#include <config/excel_config_const_index.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <utility>

namespace {

// 2026-01-05 00:00+08:00(Mon)
static constexpr const time_t kTimestampStartPoint = 1767542400;
struct ATFW_UTIL_SYMBOL_LOCAL next_timepoint_cache_t {
  time_t current_day_start;
  time_t next_day_start;
  time_t current_week_start;
  time_t next_week_start;
  time_t current_month_start;
  time_t next_month_start;
  time_t max_timestamp;
};

static next_timepoint_cache_t& get_logic_datetime_next_cache() {
  static next_timepoint_cache_t cache{};
  return cache;
}

static std::pair<time_t, time_t> refresh_day_cache() {
  time_t now = atfw::util::time::time_utility::get_now();
  if ATFW_UTIL_UNLIKELY_CONDITION (now < get_logic_datetime_next_cache().current_day_start ||
                                   now >= get_logic_datetime_next_cache().next_day_start) {
    get_logic_datetime_next_cache().current_day_start = atfw::util::time::time_utility::get_day_start_time(now);
    get_logic_datetime_next_cache().next_day_start =
        get_logic_datetime_next_cache().current_day_start + atfw::util::time::time_utility::DAY_SECONDS;
  }

  return {get_logic_datetime_next_cache().current_day_start, get_logic_datetime_next_cache().next_day_start};
}

static std::pair<time_t, time_t> refresh_week_cache() {
  time_t now = atfw::util::time::time_utility::get_now();
  if ATFW_UTIL_UNLIKELY_CONDITION (now < get_logic_datetime_next_cache().current_week_start ||
                                   now >= get_logic_datetime_next_cache().next_week_start) {
    // 周一为一周的第一天
    get_logic_datetime_next_cache().current_week_start = atfw::util::time::time_utility::get_week_start_time(now, 1);
    get_logic_datetime_next_cache().next_week_start =
        get_logic_datetime_next_cache().current_week_start + atfw::util::time::time_utility::WEEK_SECONDS;
  }

  return {get_logic_datetime_next_cache().current_week_start, get_logic_datetime_next_cache().next_week_start};
}

static std::pair<time_t, time_t> refresh_month_cache() {
  time_t now = atfw::util::time::time_utility::get_now();
  if ATFW_UTIL_UNLIKELY_CONDITION (now < get_logic_datetime_next_cache().current_month_start ||
                                   now >= get_logic_datetime_next_cache().next_month_start) {
    get_logic_datetime_next_cache().current_month_start = atfw::util::time::time_utility::get_month_start_time(now);
    get_logic_datetime_next_cache().next_month_start = atfw::util::time::time_utility::get_month_start_time(
        get_logic_datetime_next_cache().current_month_start + (atfw::util::time::time_utility::DAY_SECONDS * 32));
  }

  return {get_logic_datetime_next_cache().current_month_start, get_logic_datetime_next_cache().next_month_start};
}

}  // namespace

SERVER_FRAME_API std::chrono::system_clock::time_point logic_datetime_cache_get_today_start_timepoint(time_t offset) {
  auto res = refresh_day_cache();
  return std::chrono::system_clock::from_time_t(res.first + offset);
}

SERVER_FRAME_API std::chrono::system_clock::time_point logic_datetime_cache_get_next_day_start_timepoint(
    time_t offset) {
  time_t now = atfw::util::time::time_utility::get_now();
  auto res = refresh_day_cache();
  if (res.first + offset > now) {
    return std::chrono::system_clock::from_time_t(res.first + offset);
  }

  return std::chrono::system_clock::from_time_t(res.second + offset);
}

SERVER_FRAME_API std::chrono::system_clock::time_point logic_datetime_cache_get_next_week_start_timepoint(
    time_t offset) {
  time_t now = atfw::util::time::time_utility::get_now();
  auto res = refresh_week_cache();
  if (res.first + offset > now) {
    return std::chrono::system_clock::from_time_t(res.first + offset);
  }

  return std::chrono::system_clock::from_time_t(res.second + offset);
}

SERVER_FRAME_API std::chrono::system_clock::time_point logic_datetime_cache_get_next_month_start_timepoint(
    time_t offset) {
  time_t now = atfw::util::time::time_utility::get_now();
  auto res = refresh_month_cache();
  if (res.first + offset > now) {
    return std::chrono::system_clock::from_time_t(res.first + offset);
  }
  return std::chrono::system_clock::from_time_t(res.second + offset);
}

SERVER_FRAME_API std::chrono::system_clock::time_point logic_datetime_cache_get_max_timepoint() {
  if ATFW_UTIL_UNLIKELY_CONDITION (get_logic_datetime_next_cache().max_timestamp <= kTimestampStartPoint) {
    get_logic_datetime_next_cache().max_timestamp = logic_config::me()->get_const_settings().max_timestamp();

    // fallback
    if (get_logic_datetime_next_cache().max_timestamp <= kTimestampStartPoint) {
      get_logic_datetime_next_cache().max_timestamp =
          std::chrono::system_clock::to_time_t(std::chrono::system_clock::time_point::max()) - 1;
    }
  }

  return std::chrono::system_clock::from_time_t(get_logic_datetime_next_cache().max_timestamp);
}

SERVER_FRAME_API std::chrono::system_clock::duration logic_datetime_cache_get_default_daily_refresh_offset() {
  return protobuf_to_chrono_duration<std::chrono::system_clock::duration>(
      excel::get_const_config().daily_reset_time_offset());
}

SERVER_FRAME_API time_t logic_datetime_cache_get_day_id(time_t now, time_t offset) {
  time_t relative_to = kTimestampStartPoint;

  now = atfw::util::time::time_utility::get_day_start_time(now - offset);
  relative_to = atfw::util::time::time_utility::get_day_start_time(relative_to - offset);

  return (now - relative_to) / atfw::util::time::time_utility::DAY_SECONDS;
}

SERVER_FRAME_API std::chrono::system_clock::time_point logic_datetime_cache_get_day_start_timepoint(time_t now,
                                                                                                    time_t offset) {
  now = atfw::util::time::time_utility::get_day_start_time(now - offset);
  return std::chrono::system_clock::from_time_t(now + offset);
}

SERVER_FRAME_API time_t logic_datetime_cache_get_week_id(time_t now, time_t offset) {
  time_t relative_to = kTimestampStartPoint;

  now = atfw::util::time::time_utility::get_week_start_time(now - offset, 1);
  relative_to = atfw::util::time::time_utility::get_week_start_time(relative_to - offset, 1);

  return (now - relative_to) / atfw::util::time::time_utility::WEEK_SECONDS;
}

SERVER_FRAME_API std::chrono::system_clock::time_point logic_datetime_cache_get_week_start_timepoint(time_t now,
                                                                                                     time_t offset) {
  now = atfw::util::time::time_utility::get_week_start_time(now - offset, 1);
  return std::chrono::system_clock::from_time_t(now + offset);
}

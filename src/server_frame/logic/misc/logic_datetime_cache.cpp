#include "logic/misc/logic_datetime_cache.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.const.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/compile_optimize.h>
#include <config/excel_config_const_index.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

namespace detail {
struct next_timepoint_cache_t {
  time_t current_day_start;
  time_t next_day_start;
  time_t current_week_start;
  time_t next_week_start;
  time_t current_month_start;
  time_t next_month_start;
  time_t max_timestamp;
};

static next_timepoint_cache_t g_logic_datetime_next_cache = {0, 0, 0, 0, 0, 0, 0};

static std::pair<time_t, time_t> refresh_day_cache() {
  time_t now = util::time::time_utility::get_now();
  UTIL_UNLIKELY_IF (now < detail::g_logic_datetime_next_cache.current_day_start ||
                    now >= detail::g_logic_datetime_next_cache.next_day_start) {
    detail::g_logic_datetime_next_cache.current_day_start = util::time::time_utility::get_day_start_time(now);
    detail::g_logic_datetime_next_cache.next_day_start =
        detail::g_logic_datetime_next_cache.current_day_start + util::time::time_utility::DAY_SECONDS;
  }

  return std::pair<time_t, time_t>(detail::g_logic_datetime_next_cache.current_day_start,
                                   detail::g_logic_datetime_next_cache.next_day_start);
}

static std::pair<time_t, time_t> refresh_week_cache() {
  time_t now = util::time::time_utility::get_now();
  UTIL_UNLIKELY_IF (now < detail::g_logic_datetime_next_cache.current_week_start ||
                    now >= detail::g_logic_datetime_next_cache.next_week_start) {
    // 周一为一周的第一天
    detail::g_logic_datetime_next_cache.current_week_start = util::time::time_utility::get_week_start_time(now, 1);
    detail::g_logic_datetime_next_cache.next_week_start =
        detail::g_logic_datetime_next_cache.current_week_start + util::time::time_utility::WEEK_SECONDS;
  }

  return std::pair<time_t, time_t>(detail::g_logic_datetime_next_cache.current_week_start,
                                   detail::g_logic_datetime_next_cache.next_week_start);
}

static std::pair<time_t, time_t> refresh_month_cache() {
  time_t now = util::time::time_utility::get_now();
  UTIL_UNLIKELY_IF (now < detail::g_logic_datetime_next_cache.current_month_start ||
                    now >= detail::g_logic_datetime_next_cache.next_month_start) {
    detail::g_logic_datetime_next_cache.current_month_start = util::time::time_utility::get_month_start_time(now);
    detail::g_logic_datetime_next_cache.next_month_start = util::time::time_utility::get_month_start_time(
        detail::g_logic_datetime_next_cache.current_month_start + util::time::time_utility::DAY_SECONDS * 32);
  }

  return std::pair<time_t, time_t>(detail::g_logic_datetime_next_cache.current_month_start,
                                   detail::g_logic_datetime_next_cache.next_month_start);
}

}  // namespace detail

time_t logic_datetime_cache_get_today_start_timepoint(time_t offset) {
  auto res = detail::refresh_day_cache();
  return res.first + offset;
}

time_t logic_datetime_cache_get_next_day_start_timepoint(time_t offset) {
  time_t now = util::time::time_utility::get_now();
  auto res = detail::refresh_day_cache();
  if (res.first + offset > now) {
    return res.first + offset;
  }
  return res.second + offset;
}

time_t logic_datetime_cache_get_next_week_start_timepoint(time_t offset) {
  time_t now = util::time::time_utility::get_now();
  auto res = detail::refresh_week_cache();
  if (res.first + offset > now) {
    return res.first + offset;
  }
  return res.second + offset;
}

time_t logic_datetime_cache_get_next_month_start_timepoint(time_t offset) {
  time_t now = util::time::time_utility::get_now();
  auto res = detail::refresh_month_cache();
  if (res.first + offset > now) {
    return res.first + offset;
  }
  return res.second + offset;
}

time_t logic_datetime_cache_get_max_timepoint() {
  UTIL_UNLIKELY_IF (detail::g_logic_datetime_next_cache.max_timestamp < INT32_MAX - 1) {
    detail::g_logic_datetime_next_cache.max_timestamp = logic_config::me()->get_const_settings().max_timestamp();

    // fallback
    if (detail::g_logic_datetime_next_cache.max_timestamp <= 0) {
      detail::g_logic_datetime_next_cache.max_timestamp = INT32_MAX - 1;
    }
  }

  return detail::g_logic_datetime_next_cache.max_timestamp;
}

time_t logic_datetime_cache_get_default_daily_refresh_offset() {
  return excel::get_const_config().daily_reset_time_offset().seconds();
}

time_t logic_datetime_cache_get_day_id(time_t now, time_t offset) {
  time_t relative_to = 1577808000;  // 2020-01-01 00:00:00+08:00

  now = util::time::time_utility::get_day_start_time(now - offset);
  relative_to = util::time::time_utility::get_day_start_time(relative_to - offset);

  return (now - relative_to) / util::time::time_utility::DAY_SECONDS;
}

time_t logic_datetime_cache_get_day_start_timepoint(time_t now, time_t offset) {
  now = util::time::time_utility::get_day_start_time(now - offset);
  return now + offset;
}

time_t logic_datetime_cache_get_week_id(time_t now, time_t offset) {
  time_t relative_to = 1577808000;  // 2020-01-01 00:00:00+08:00

  now = util::time::time_utility::get_week_start_time(now - offset, 1);
  relative_to = util::time::time_utility::get_week_start_time(relative_to - offset, 1);

  return (now - relative_to) / util::time::time_utility::WEEK_SECONDS;
}

time_t logic_datetime_cache_get_week_start_timepoint(time_t now, time_t offset) {
  now = util::time::time_utility::get_week_start_time(now - offset, 1);
  return now + offset;
}

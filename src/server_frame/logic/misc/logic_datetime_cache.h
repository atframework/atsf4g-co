#pragma once

#include <ctime>

SERVER_FRAME_API time_t logic_datetime_cache_get_today_start_timepoint(time_t offset = 0);

SERVER_FRAME_API time_t logic_datetime_cache_get_next_day_start_timepoint(time_t offset = 0);

SERVER_FRAME_API time_t logic_datetime_cache_get_next_week_start_timepoint(time_t offset = 0);

SERVER_FRAME_API time_t logic_datetime_cache_get_next_month_start_timepoint(time_t offset = 0);

SERVER_FRAME_API time_t logic_datetime_cache_get_max_timepoint();

SERVER_FRAME_API time_t logic_datetime_cache_get_default_daily_refresh_offset();

/**
 * @brief (统一天ID算法)给定时间戳判定天ID
 * @param now 基于这个时间戳计算
 * @param offset 一天内的偏移值,比如5点结算这里填5*3600
 * @return 天ID，对于相同的offset，按now参数单调递增
 */
SERVER_FRAME_API time_t logic_datetime_cache_get_day_id(time_t now, time_t offset = 0);

/**
 * @brief (统一时间算法)给定时间戳判定一天的起始时间戳
 * @param now 基于这个时间戳计算
 * @param offset 一天内的偏移值,比如5点结算这里填5*3600
 * @return 天的起始时间戳，对于相同的offset
 */
SERVER_FRAME_API time_t logic_datetime_cache_get_day_start_timepoint(time_t now, time_t offset = 0);

/**
 * @brief (统一周ID算法)给定时间戳判定周ID
 * @param now 基于这个时间戳计算
 * @param offset 一周内的偏移值,比如周一5点结算这里填5*3600
 * @return 周ID，对于相同的offset，按now参数单调递增
 */
SERVER_FRAME_API time_t logic_datetime_cache_get_week_id(time_t now, time_t offset = 0);

/**
 * @brief (统一时间算法)给定时间戳判定一周的起始时间戳
 * @param now 基于这个时间戳计算
 * @param offset 一周内的偏移值,比如周一5点结算这里填5*3600
 * @return 周的起始时间戳，对于相同的offset
 */
SERVER_FRAME_API time_t logic_datetime_cache_get_week_start_timepoint(time_t now, time_t offset = 0);

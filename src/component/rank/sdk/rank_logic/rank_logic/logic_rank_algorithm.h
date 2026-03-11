#pragma once

#include <config/compile_optimize.h>
#include <config/compiler_features.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>
#include <protocol/common/com.struct.rank.common.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel/config_easy_api.h>

#include <stdint.h>
#include <cstddef>
#include <ctime>
#include <memory>

#include "config/server_frame_build_feature.h"

namespace PROJECT_NAMESPACE_ID {
namespace config {
class ExcelRankRule;
class ExcelRankPeriodRewardPool;

}  // namespace config
}  // namespace PROJECT_NAMESPACE_ID

using logic_rank_rule_cfg_t = PROJECT_NAMESPACE_ID::config::ExcelRankRule;
using logic_rank_reward_cfg_t = PROJECT_NAMESPACE_ID::config::ExcelRankPeriodRewardPool;

RANK_LOGIC_SDK_API bool logic_rank_is_global_rank(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept;

RANK_LOGIC_SDK_API uint32_t
logic_rank_get_global_rank_shared_zone_id(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept;

struct UTIL_SYMBOL_VISIBLE logic_rank_open_period_t {
  time_t begin_time;           // 包含 >=
  time_t end_time;             // 不包含 <
  time_t writable_begin_time;  // 包含 >=
  time_t writable_end_time;    // 不包含 <

  inline logic_rank_open_period_t() : begin_time(0), end_time(0), writable_begin_time(0), writable_end_time(0) {}
  inline logic_rank_open_period_t(time_t begin, time_t end, time_t writable_begin, time_t writable_end)
      : begin_time(begin), end_time(end), writable_begin_time(writable_begin), writable_end_time(writable_end) {}
};  // 有效时间为 begin_time <= T < end_time


RANK_LOGIC_SDK_API bool logic_rank_has_rank_daily_reward(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept;

RANK_LOGIC_SDK_API bool logic_rank_has_rank_custom_reward(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept;

bool logic_rank_has_rank_reward(int32_t reward_pool_id, PROJECT_NAMESPACE_ID::EnRankPeriodRewardType reward_type);

RANK_LOGIC_SDK_API time_t logic_rank_get_current_settlement_daily_id(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg,
                                                               time_t now);

RANK_LOGIC_SDK_API time_t logic_rank_get_current_settlement_daily_start_time(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg, time_t now);

RANK_LOGIC_SDK_API int64_t logic_rank_get_current_settlement_custom_season_id(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg, time_t now);

/**
 * @brief 获取排行榜的最终结束时间
 *
 * @param incfg 排行榜配置
 * @return first为结束时间，second为保护时间
 */
RANK_LOGIC_SDK_API std::pair<time_t, time_t> logic_rank_get_final_settlement_custom_expired_time(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg);

/**
 * @brief 传入排行榜配置和指定时间，返回该排行榜指定时间所在的每日开放的开始和结束时间
 * @param cfg 排行榜配置
 * @param now 指定参考时间
 * @note 如果传入的now未开放，则返回下一个周期的开始和结束时间
 * @note 如果cfg为空，返回 { begin_time = 0, end_time = 0 }
 * @note 如果没有每日锁板，返回now所在天的开始和结束时间
 * @return 指定时间所在的每日开放的开始和结束时间
 */
RANK_LOGIC_SDK_API logic_rank_open_period_t logic_rank_get_rank_daily_valid_period(const logic_rank_rule_cfg_t& cfg,
                                                                             time_t now);

/**
 * @brief 传入排行榜配置和指定时间，返回该排行榜指定时间所在的自定义周期的开放的开始和结束时间
 * @param cfg 排行榜配置
 * @param now 指定参考时间
 * @note 如果传入的now未开放，则返回下一个周期的开始和结束时间
 * @note 如果不存在自定义周期的开放周期或者cfg为空，返回 { begin_time = 0, end_time = 0 }
 * @return 指定时间所在的自定义周期的开放的开始和结束时间
 */
RANK_LOGIC_SDK_API logic_rank_open_period_t logic_rank_get_rank_custom_valid_period(const logic_rank_rule_cfg_t& cfg,
                                                                              time_t now);

RANK_LOGIC_SDK_API time_t logic_rank_get_rank_next_valid_time(const logic_rank_rule_cfg_t& cfg, time_t now);

RANK_LOGIC_SDK_API bool logic_rank_is_rank_valid_now(const logic_rank_rule_cfg_t& cfg, time_t now);
RANK_LOGIC_SDK_API bool logic_rank_is_rank_writable_now(const logic_rank_rule_cfg_t& cfg, time_t now);

RANK_LOGIC_SDK_API time_t logic_rank_get_record_expire_time(const logic_rank_rule_cfg_t& cfg, time_t now);

/**
 * @breif ELO分值算法
 * @see https://zhuanlan.zhihu.com/p/57480433
 * @see https://en.wikipedia.org/wiki/Elo_rating_system
 * @param ra ELO分值算法Ra参数
 * @param rb ELO分值算法Rb参数
 * @param k  k系数
 * @param rating_point rating_point值，无效类型会取默认值: 400
 * @note 我们采用定制化算法:(latex)
          $$
          E_a=\frac k {1+10^\frac {R_a-R_b} {Rating}} \\
          A胜利: R_a'= R_a + E_a \\
          A失败: R_a'= R_a - E_a
          $$
 */
RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_ea(uint32_t ra, uint32_t rb, uint32_t k = 16, uint32_t rating_point = 400);

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_winner_score(uint32_t winner_score, uint32_t loser_socre, uint32_t k = 16,
                                                      uint32_t rating_point = 400);

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_winner_score(const PROJECT_NAMESPACE_ID::DRankRuleContent& cfg,
                                                      uint32_t winner_score, uint32_t winner_rank_no,
                                                      uint32_t loser_socre, uint32_t loser_rank_no);

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_loser_score(uint32_t winner_score, uint32_t loser_socre, uint32_t k = 16,
                                                     uint32_t rating_point = 400);

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_loser_score(const PROJECT_NAMESPACE_ID::DRankRuleContent& cfg,
                                                     uint32_t winner_score, uint32_t winner_rank_no,
                                                     uint32_t loser_socre, uint32_t loser_rank_no);

RANK_LOGIC_SDK_API bool logic_rank_is_self_rank(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept;

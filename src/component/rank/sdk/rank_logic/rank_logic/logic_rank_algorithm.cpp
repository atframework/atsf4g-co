#include "logic_rank_algorithm.h"

#include <gsl/select-gsl.h>
#include <lock/lock_holder.h>
#include <lock/spin_lock.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.const.config.pb.h>
#include <protocol/config/com.struct.rank.config.pb.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.rank.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel/config_easy_api.h>
#include <config/excel_config_const_index.h>
#include <config/excel_config_rank_index.h>
#include <logic/misc/logic_datetime_cache.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>

#include <cmath>
#include <limits>

#ifdef max
#  undef max
#endif

#ifdef min
#  undef min
#endif

static constexpr const uint64_t kEloAlgorithmPrecisionAmplify = 1000000;
static constexpr const uint32_t kEloDefaultRatingPoint = 400;

RANK_LOGIC_SDK_API bool logic_rank_is_global_rank(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept {
  return cfg.content().shared_zone_id() > 0;
}

RANK_LOGIC_SDK_API uint32_t
logic_rank_get_global_rank_shared_zone_id(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept {
  if (cfg.content().shared_zone_id() > 0) {
    return cfg.content().shared_zone_id() + logic_config::me()->get_server_cfg().rank().shared_zone_id_offset();
  }

  return 0;
}

RANK_LOGIC_SDK_API bool logic_rank_has_rank_daily_reward(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept {
  return logic_rank_has_rank_reward(cfg.content().daily_settlement().rank_reward_pool_id(),
                                    cfg.content().daily_settlement().reward_type());
}

RANK_LOGIC_SDK_API bool logic_rank_has_rank_custom_reward(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept {
  return logic_rank_has_rank_reward(cfg.content().custom_settlement().rank_reward_pool_id(),
                                    cfg.content().custom_settlement().reward_type());
}

bool logic_rank_has_rank_reward(int32_t reward_pool_id, PROJECT_NAMESPACE_ID::EnRankPeriodRewardType reward_type) {
  return reward_pool_id != 0 && reward_type != PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_NONE;
}

RANK_LOGIC_SDK_API time_t logic_rank_get_current_settlement_daily_id(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg,
                                                               time_t now) {
  return logic_datetime_cache_get_day_id(now, incfg.content().settlement_time_offset().seconds());
}

RANK_LOGIC_SDK_API time_t logic_rank_get_current_settlement_daily_start_time(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg, time_t now) {
  return logic_datetime_cache_get_day_start_timepoint(now, incfg.content().settlement_time_offset().seconds());
}

RANK_LOGIC_SDK_API int64_t logic_rank_get_current_settlement_custom_season_id(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg, time_t now) {
  int64_t ret = logic_rank_get_current_settlement_daily_id(incfg, now);

  int64_t interval_days = incfg.content().custom_settlement().interval_days();
  if (interval_days > 0) {
    int64_t base_day_id =
        logic_rank_get_current_settlement_daily_id(incfg, incfg.content().valid_time().begin_time().seconds());
    if (base_day_id < 0) {
      base_day_id = 0;
    }

    int64_t offset_days = (ret - base_day_id) % interval_days;
    int64_t writable_days = interval_days - incfg.content().custom_settlement().lock_last_days();
    ret -= offset_days;

    // 自定义周期中，如果过了可写时间点就可以提前结算了
    if (writable_days > 0 && offset_days >= writable_days) {
      ret += interval_days;
    }

    ret /= interval_days;
  }

  return ret;
}

RANK_LOGIC_SDK_API std::pair<time_t, time_t> logic_rank_get_final_settlement_custom_expired_time(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& incfg) {
  std::pair<time_t, time_t> ret{0, 0};

  if (incfg.content().valid_time().end_time().seconds() > 0) {
    time_t end_time = incfg.content().valid_time().end_time().seconds();
    time_t expired_time =
        end_time + incfg.content().custom_settlement().interval_days() * util::time::time_utility::DAY_SECONDS;
    if (ret.first == 0 || ret.first > end_time) {
      ret.first = end_time;
    }

    if (ret.second == 0 || ret.second > expired_time) {
      ret.second = expired_time;
    }
  }

  return ret;
}

RANK_LOGIC_SDK_API logic_rank_open_period_t logic_rank_get_rank_daily_valid_period(const logic_rank_rule_cfg_t& cfg,
                                                                             time_t now) {
  logic_rank_open_period_t ret;

  // 至少要从配置的开始时间开始
  if (now < cfg.content().valid_time().begin_time().seconds()) {
    now = cfg.content().valid_time().begin_time().seconds();
  }

  time_t day_start_time = util::time::time_utility::get_day_start_time(now);
  ret.begin_time = day_start_time;
  ret.end_time = day_start_time + util::time::time_utility::DAY_SECONDS;
  ret.writable_begin_time = ret.begin_time;
  ret.writable_end_time = ret.end_time;

  do {
    if (cfg.content().daily_settlement().rank_reward_pool_id() <= 0) {
      break;
    }

    // 相同则是不限制
    if (cfg.content().lock_type() != PROJECT_NAMESPACE_ID::EN_RANK_LOCK_TYPE_DAILY ||
        cfg.content().lock_time().begin_time().seconds() == cfg.content().lock_time().end_time().seconds()) {
      break;
    }

    // 每日榜开放时间段
    if (cfg.content().lock_time().begin_time().seconds() < cfg.content().lock_time().end_time().seconds()) {
      // 非跨天
      time_t lock_begin =
          ret.begin_time + cfg.content().lock_time().begin_time().seconds() % util::time::time_utility::DAY_SECONDS;
      time_t lock_end =
          ret.begin_time + cfg.content().lock_time().end_time().seconds() % util::time::time_utility::DAY_SECONDS;
      if (now < lock_begin) {
        ret.writable_begin_time = ret.begin_time;
        ret.writable_end_time = lock_begin;
      } else {
        ret.writable_begin_time = lock_end;
        ret.writable_end_time = ret.end_time;
      }
    } else {
      // 跨天
      ret.writable_begin_time =
          ret.begin_time + cfg.content().lock_time().end_time().seconds() % util::time::time_utility::DAY_SECONDS;
      ret.writable_end_time =
          ret.begin_time + cfg.content().lock_time().begin_time().seconds() % util::time::time_utility::DAY_SECONDS;
    }
  } while (false);

  // 不能超过结束时间
  if (cfg.content().valid_time().end_time().seconds() > 0 &&
      ret.end_time > cfg.content().valid_time().end_time().seconds()) {
    ret.end_time = cfg.content().valid_time().end_time().seconds();

    if (ret.writable_end_time > ret.end_time) {
      ret.writable_end_time = ret.end_time;
    }
  }

  return ret;
}

RANK_LOGIC_SDK_API logic_rank_open_period_t logic_rank_get_rank_custom_valid_period(const logic_rank_rule_cfg_t& cfg,
                                                                              time_t now) {
  logic_rank_open_period_t ret;
  if (cfg.content().custom_settlement().interval_days() > 0) {
    // 至少要从配置的开始时间开始
    if (now < cfg.content().valid_time().begin_time().seconds()) {
      now = cfg.content().valid_time().begin_time().seconds();
    }

    time_t period_range = cfg.content().custom_settlement().interval_days() * util::time::time_utility::DAY_SECONDS;

    time_t period_no = (now - cfg.content().valid_time().begin_time().seconds()) / period_range;
    time_t period_start_time = cfg.content().valid_time().begin_time().seconds() + period_no * period_range;
    ret.begin_time = period_start_time;
    ret.end_time = period_start_time + period_range;
  }

  if (ret.begin_time == 0 && ret.end_time == 0) {
    return ret;
  }
  ret.writable_begin_time = ret.begin_time;
  ret.writable_end_time = ret.end_time;

  do {
    if (cfg.content().custom_settlement().rank_reward_pool_id() <= 0) {
      break;
    }

    // 相同则是不限制
    if (cfg.content().lock_type() != PROJECT_NAMESPACE_ID::EN_RANK_LOCK_TYPE_CUSTOM ||
        cfg.content().lock_time().begin_time().seconds() == cfg.content().lock_time().end_time().seconds()) {
      break;
    }

    // 自定义周期榜开放时间段
    if (cfg.content().lock_time().begin_time().seconds() < cfg.content().lock_time().end_time().seconds()) {
      // 非跨周期
      time_t lock_begin = ret.begin_time + cfg.content().lock_time().begin_time().seconds();
      time_t lock_end = ret.begin_time + cfg.content().lock_time().end_time().seconds();
      if (now < lock_begin) {
        ret.writable_begin_time = ret.begin_time;
        ret.writable_end_time = lock_begin;
      } else {
        ret.writable_begin_time = lock_end;
        ret.writable_end_time = ret.end_time;
      }
    } else {
      // 跨周期
      ret.writable_begin_time = ret.begin_time + cfg.content().lock_time().end_time().seconds();
      ret.writable_end_time = ret.begin_time + cfg.content().lock_time().begin_time().seconds();
    }
  } while (false);

  // 判定可写时间区间
  if (cfg.content().custom_settlement().lock_first_days() > 0 && ret.begin_time > 0) {
    time_t writable_begin_time =
        ret.begin_time + cfg.content().custom_settlement().lock_first_days() * util::time::time_utility::DAY_SECONDS;
    if (writable_begin_time > ret.writable_begin_time) {
      ret.writable_begin_time = writable_begin_time;
    }
  }

  if (cfg.content().custom_settlement().lock_last_days() > 0 && ret.end_time > 0) {
    time_t writable_end_time =
        ret.end_time - cfg.content().custom_settlement().lock_last_days() * util::time::time_utility::DAY_SECONDS;
    if (ret.writable_end_time > writable_end_time) {
      ret.writable_end_time = writable_end_time;
    }
  }

  // 不能早于开始时间
  if (cfg.content().valid_time().begin_time().seconds() > 0 &&
      ret.begin_time < cfg.content().valid_time().begin_time().seconds()) {
    ret.begin_time = cfg.content().valid_time().begin_time().seconds();

    if (ret.writable_begin_time < ret.begin_time) {
      ret.writable_begin_time = ret.begin_time;
    }
  }

  // 不能超过结束时间
  if (cfg.content().valid_time().end_time().seconds() > 0 &&
      ret.end_time > cfg.content().valid_time().end_time().seconds()) {
    ret.end_time = cfg.content().valid_time().end_time().seconds();

    if (ret.writable_end_time > ret.end_time) {
      ret.writable_end_time = ret.end_time;
    }
  }

  return ret;
}

RANK_LOGIC_SDK_API time_t logic_rank_get_rank_next_valid_time(const logic_rank_rule_cfg_t& cfg, time_t now) {
  // 相同则是不限制
  if (cfg.content().lock_time().begin_time().seconds() == cfg.content().lock_time().end_time().seconds() ||
      cfg.content().lock_type() == PROJECT_NAMESPACE_ID::EN_RANK_LOCK_TYPE_NONE) {
    return now;
  }

  time_t ret = 0;
  if (cfg.content().daily_settlement().rank_reward_pool_id() > 0) {
    logic_rank_open_period_t valid_period = logic_rank_get_rank_daily_valid_period(cfg, now);
    if (now >= valid_period.begin_time && (0 == valid_period.end_time || now < valid_period.end_time)) {
      ret = logic_rank_get_rank_daily_valid_period(cfg, valid_period.end_time).begin_time;
    } else {
      ret = valid_period.begin_time;
    }
  } else if (cfg.content().custom_settlement().rank_reward_pool_id() > 0) {
    logic_rank_open_period_t valid_period = logic_rank_get_rank_custom_valid_period(cfg, now);
    if (now >= valid_period.begin_time && (0 == valid_period.end_time || now < valid_period.end_time)) {
      ret = logic_rank_get_rank_custom_valid_period(cfg, valid_period.end_time).begin_time;
    } else {
      ret = valid_period.begin_time;
    }
  }

  if (ret == 0) {
    ret = now;
  }

  return ret;
}

RANK_LOGIC_SDK_API bool logic_rank_is_rank_valid_now(const logic_rank_rule_cfg_t& cfg, time_t now) {
  // 相同则是不限制
  if (cfg.content().lock_time().begin_time().seconds() == cfg.content().lock_time().end_time().seconds() ||
      cfg.content().lock_type() == PROJECT_NAMESPACE_ID::EN_RANK_LOCK_TYPE_NONE) {
    return true;
  }

  if (cfg.content().daily_settlement().rank_reward_pool_id() > 0) {
    logic_rank_open_period_t valid_period = logic_rank_get_rank_daily_valid_period(cfg, now);
    return now >= valid_period.begin_time && (0 == valid_period.end_time || now < valid_period.end_time);
  } else if (cfg.content().custom_settlement().rank_reward_pool_id() > 0) {
    logic_rank_open_period_t valid_period = logic_rank_get_rank_custom_valid_period(cfg, now);
    return now >= valid_period.begin_time && (0 == valid_period.end_time || now < valid_period.end_time);
  }

  return true;
}

RANK_LOGIC_SDK_API bool logic_rank_is_rank_writable_now(const logic_rank_rule_cfg_t& cfg, time_t now) {
  // 相同则是不限制
  if (cfg.content().lock_time().begin_time().seconds() == cfg.content().lock_time().end_time().seconds() ||
      cfg.content().lock_type() == PROJECT_NAMESPACE_ID::EN_RANK_LOCK_TYPE_NONE) {
    return true;
  }

  if (cfg.content().daily_settlement().rank_reward_pool_id() > 0) {
    logic_rank_open_period_t valid_period = logic_rank_get_rank_daily_valid_period(cfg, now);
    return now >= valid_period.writable_begin_time &&
           (0 == valid_period.writable_end_time || now < valid_period.writable_end_time);
  } else if (cfg.content().custom_settlement().rank_reward_pool_id() > 0) {
    logic_rank_open_period_t valid_period = logic_rank_get_rank_custom_valid_period(cfg, now);
    return now >= valid_period.writable_begin_time &&
           (0 == valid_period.writable_end_time || now < valid_period.writable_end_time);
  }

  return true;
}

RANK_LOGIC_SDK_API time_t logic_rank_get_record_expire_time(const logic_rank_rule_cfg_t& cfg, time_t now) {
  // 不限制超时
  if (cfg.content().daily_settlement().rank_reward_pool_id() <= 0 &&
      cfg.content().custom_settlement().rank_reward_pool_id() <= 0) {
    return 0;
  }

  // 如果没有周期性结算，则提交记录是永久生效的
  if (cfg.content().lock_time().begin_time().seconds() == cfg.content().lock_time().end_time().seconds() ||
      cfg.content().lock_type() == PROJECT_NAMESPACE_ID::EN_RANK_LOCK_TYPE_NONE) {
    return 0;
  }

  //   在极端条件下，如果分数提交时赛季已经结束，则提交到下一个周期中
  //   如果下一个周期未上线，分数丢弃
  logic_rank_open_period_t valid_period;
  if (cfg.content().custom_settlement().rank_reward_pool_id() > 0) {
    // 有自定义结算规则则最大周期是按自定义天数
    valid_period = logic_rank_get_rank_custom_valid_period(cfg, now);
  } else if (cfg.content().daily_settlement().rank_reward_pool_id() > 0) {
    // 有日结算规则则最大周期是天
    valid_period = logic_rank_get_rank_daily_valid_period(cfg, now);
  }

  time_t expire_time = valid_period.writable_end_time;
  if (valid_period.writable_end_time <= 0) {
    expire_time = valid_period.end_time;
  }

  time_t end_time = cfg.content().valid_time().end_time().seconds();
  if (end_time > 0 && (0 == expire_time || expire_time > end_time)) {
    expire_time = end_time;
  }

  // 延长一个保存周期
  if (valid_period.end_time != 0 && 0 != valid_period.begin_time && valid_period.end_time > valid_period.begin_time) {
    expire_time += valid_period.end_time - valid_period.begin_time;
  } else {
    expire_time += util::time::time_utility::DAY_SECONDS;
  }

  return expire_time;
}

namespace detials {
static std::unordered_map<uint32_t, std::vector<uint64_t>> g_elo_rating_point_cache;
static util::lock::spin_lock g_elo_rating_point_cache_lock;

static uint64_t cache_power_10(uint32_t denominator, uint32_t numerator) {
  if (numerator >= denominator) {
    numerator %= denominator;
  }

  if (denominator > 1024) {
    uint64_t ret = static_cast<uint64_t>(std::pow(10.0, static_cast<double>(numerator) / denominator) *
                                         kEloAlgorithmPrecisionAmplify);
    if (ret <= 0) {
      ret = 1;
    }
    return ret;
  }

  std::vector<uint64_t>* select_set;
  util::lock::lock_holder<util::lock::spin_lock> holder(g_elo_rating_point_cache_lock);

  auto iter = g_elo_rating_point_cache.find(denominator);
  if (iter == g_elo_rating_point_cache.end()) {
    select_set = &g_elo_rating_point_cache[denominator];
    select_set->resize(denominator, 0);
  } else {
    select_set = &iter->second;
  }

  uint64_t ret = (*select_set)[numerator];
  if (0 == ret) {
    ret = static_cast<uint64_t>(std::pow(10.0, static_cast<double>(numerator) / denominator) *
                                kEloAlgorithmPrecisionAmplify);
    if (ret <= 0) {
      ret = 1;
    }
    (*select_set)[numerator] = ret;
  }

  return ret;
}

static uint64_t binary_power(uint64_t base, uint32_t p) {
  uint64_t ret = 1;
  while (p) {
    if (p & 1) {
      ret = ret * base;
    }
    base = base * base;
    p >>= 1;
  }

  return ret;
}

static uint64_t get_elo_rating_point(uint32_t base, uint32_t offset) {
  if (base <= 1) {
    return binary_power(10, offset) * kEloAlgorithmPrecisionAmplify;
  }

  uint32_t mod = offset % base;
  if (0 == mod) {
    return binary_power(10, offset / base) * kEloAlgorithmPrecisionAmplify;
  }

  return binary_power(10, offset / base) * cache_power_10(base, mod);
}
}  // namespace detials

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_ea(uint32_t ra, uint32_t rb, uint32_t k, uint32_t rating_point) {
  if (rating_point <= 0) {
    rating_point = 400;
  }

  if (k <= 0) {
    k = 16;
  }

  if (ra >= rb) {
    uint64_t denominator = detials::get_elo_rating_point(rating_point, ra - rb);
    if (denominator == 0) {
      return 1;
    } else {
      return static_cast<uint32_t>(k * kEloAlgorithmPrecisionAmplify / (kEloAlgorithmPrecisionAmplify + denominator));
    }
  } else {
    uint64_t denominator = detials::get_elo_rating_point(rating_point, rb - ra);
    if (denominator == 0) {
      return k;
    } else {
      return static_cast<uint32_t>(k * kEloAlgorithmPrecisionAmplify /
                                   (kEloAlgorithmPrecisionAmplify +
                                    (kEloAlgorithmPrecisionAmplify * kEloAlgorithmPrecisionAmplify) / denominator));
    }
  }
}

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_winner_score(uint32_t winner_score, uint32_t loser_socre, uint32_t k,
                                                      uint32_t rating_point) {
  return logic_rank_elo_get_ea(winner_score, loser_socre, k, rating_point);
}

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_winner_score(const PROJECT_NAMESPACE_ID::DRankRuleContent& cfg,
                                                      uint32_t winner_score, uint32_t winner_rank_no,
                                                      uint32_t loser_socre,
                                                      ATFW_EXPLICIT_UNUSED_ATTR uint32_t loser_rank_no) {
  uint32_t elo_k = 0;
  uint32_t elo_rating_point = kEloDefaultRatingPoint;

  if (0 != cfg.custom_settlement().rank_reward_pool_id() &&
      PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_NONE != cfg.custom_settlement().reward_type()) {
    auto rank_settle_cfg =
        excel::get_current_rank_settle_rewards(cfg.custom_settlement().rank_reward_pool_id(),
                                               cfg.custom_settlement().reward_type(), winner_rank_no, winner_score);

    if (rank_settle_cfg) {
      elo_k = static_cast<uint32_t>(rank_settle_cfg->content().elo_k() > 0 ? rank_settle_cfg->content().elo_k() : 0);
      elo_rating_point = rank_settle_cfg->content().elo_rating_point() > 0
                             ? static_cast<uint32_t>(rank_settle_cfg->content().elo_rating_point())
                             : static_cast<uint32_t>(kEloDefaultRatingPoint);
    }
  } else if (0 != cfg.daily_settlement().rank_reward_pool_id() &&
             PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_NONE != cfg.daily_settlement().reward_type()) {
    auto rank_settle_cfg =
        excel::get_current_rank_settle_rewards(cfg.daily_settlement().rank_reward_pool_id(),
                                               cfg.daily_settlement().reward_type(), winner_rank_no, winner_score);

    if (rank_settle_cfg) {
      elo_k = static_cast<uint32_t>(rank_settle_cfg->content().elo_k() > 0 ? rank_settle_cfg->content().elo_k() : 0);
      elo_rating_point = rank_settle_cfg->content().elo_rating_point() > 0
                             ? static_cast<uint32_t>(rank_settle_cfg->content().elo_rating_point())
                             : static_cast<uint32_t>(kEloDefaultRatingPoint);
    }
  }

  return logic_rank_elo_get_winner_score(winner_score, loser_socre, elo_k, elo_rating_point);
}

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_loser_score(uint32_t winner_score, uint32_t loser_socre, uint32_t k,
                                                     uint32_t rating_point) {
  uint32_t s = logic_rank_elo_get_ea(loser_socre, winner_score, k, rating_point);
  if (s > k) {
    return 0;
  }
  return k - s;
}

RANK_LOGIC_SDK_API uint32_t logic_rank_elo_get_loser_score(const PROJECT_NAMESPACE_ID::DRankRuleContent& cfg,
                                                     uint32_t winner_score,
                                                     ATFW_EXPLICIT_UNUSED_ATTR uint32_t winner_rank_no,
                                                     uint32_t loser_socre, uint32_t loser_rank_no) {
  uint32_t elo_k = 0;
  uint32_t elo_rating_point = kEloDefaultRatingPoint;

  if (0 != cfg.custom_settlement().rank_reward_pool_id() &&
      PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_NONE != cfg.custom_settlement().reward_type()) {
    auto rank_settle_cfg =
        excel::get_current_rank_settle_rewards(cfg.custom_settlement().rank_reward_pool_id(),
                                               cfg.custom_settlement().reward_type(), loser_rank_no, loser_socre);

    if (rank_settle_cfg) {
      elo_k = static_cast<uint32_t>(rank_settle_cfg->content().elo_k() > 0 ? rank_settle_cfg->content().elo_k() : 0);
      elo_rating_point = rank_settle_cfg->content().elo_rating_point() > 0
                             ? static_cast<uint32_t>(rank_settle_cfg->content().elo_rating_point())
                             : static_cast<uint32_t>(kEloDefaultRatingPoint);
    }
  } else if (0 != cfg.daily_settlement().rank_reward_pool_id() &&
             PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_NONE != cfg.daily_settlement().reward_type()) {
    auto rank_settle_cfg = excel::get_current_rank_settle_rewards(
        cfg.daily_settlement().rank_reward_pool_id(), cfg.daily_settlement().reward_type(), loser_rank_no, loser_socre);

    if (rank_settle_cfg) {
      elo_k = static_cast<uint32_t>(rank_settle_cfg->content().elo_k() > 0 ? rank_settle_cfg->content().elo_k() : 0);
      elo_rating_point = rank_settle_cfg->content().elo_rating_point() > 0
                             ? static_cast<uint32_t>(rank_settle_cfg->content().elo_rating_point())
                             : static_cast<uint32_t>(kEloDefaultRatingPoint);
    }
  }

  return logic_rank_elo_get_loser_score(winner_score, loser_socre, elo_k, elo_rating_point);
}

RANK_LOGIC_SDK_API bool logic_rank_is_self_rank(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg) noexcept {
  return cfg.rank_impl_type() == PROJECT_NAMESPACE_ID::EN_RANK_IMPL_TYPE_SELF ||
         cfg.rank_impl_type() == PROJECT_NAMESPACE_ID::EN_RANK_IMPL_TYPE_DEFAULT;
}
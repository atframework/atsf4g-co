// Copyright 2024 atframework
// Created by owent

#pragma once

#include <opentelemetry/nostd/shared_ptr.h>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>
#include <memory/rc_ptr.h>
#include <nostd/nullability.h>
#include <time/time_utility.h>

#include <config/server_frame_build_feature.h>

#include <rpc/telemetry/rpc_global_service.h>

#include <gsl/select-gsl.h>

#include <memory>

#include "logic/hpa/logic_hpa_data_type.h"

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_hpa_cfg;
class logic_hpa_policy;
}  // namespace config
PROJECT_NAMESPACE_END

class logic_hpa_policy;

/**
 * @brief 指标拉取器接口类
 *
 */
class ATFW_UTIL_SYMBOL_VISIBLE logic_hpa_puller {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_puller);
  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_puller);

 protected:
  SERVER_FRAME_API logic_hpa_puller(logic_hpa_policy& owner);

 public:
  SERVER_FRAME_API virtual ~logic_hpa_puller();

  virtual int tick(util::time::time_utility::raw_time_t now) = 0;

  virtual void stop() = 0;

  virtual bool do_pull() = 0;

  virtual bool is_pulling() const noexcept = 0;

  virtual bool is_stopped() const noexcept = 0;

  virtual bool can_pulling_available() const noexcept = 0;

  ATFW_UTIL_FORCEINLINE logic_hpa_policy& get_owner() noexcept { return *owner_; }
  ATFW_UTIL_FORCEINLINE const logic_hpa_policy& get_owner() const noexcept { return *owner_; }

 private:
  util::nostd::nonnull<logic_hpa_policy*> owner_;
};

class logic_hpa_puller_factory {
 public:
  /**
   * @brief 工厂接口，创建一个新的拉取器实例
   *
   * @param policy 策略
   * @param telemetry_group 指标组
   * @param hpa_cfg HPA配置
   * @param policy_cfg 策略配置
   * @return 拉取器实例
   */
  SERVER_FRAME_API static util::memory::strong_rc_ptr<logic_hpa_puller> make_new_instance(
      logic_hpa_policy& policy, std::shared_ptr<rpc::telemetry::group_type> telemetry_group,
      const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
      const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
};

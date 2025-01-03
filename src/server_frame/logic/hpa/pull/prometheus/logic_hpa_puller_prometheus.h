// Copyright 2024 atframework
// Created by owent

#pragma once

#include <opentelemetry/nostd/shared_ptr.h>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <network/http_request.h>

#include <config/server_frame_build_feature.h>

#include <rpc/telemetry/rpc_global_service.h>

#include <gsl/select-gsl.h>

#include <memory>

#include "logic/hpa/logic_hpa_puller.h"

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_hpa_cfg;
class logic_hpa_policy;
}  // namespace config
PROJECT_NAMESPACE_END

class logic_hpa_policy;

class logic_hpa_puller_prometheus : public logic_hpa_puller {
  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_puller_prometheus);
  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_puller_prometheus);

 public:
  SERVER_FRAME_API logic_hpa_puller_prometheus(logic_hpa_policy& policy,
                                               std::shared_ptr<rpc::telemetry::group_type>& telemetry_group,
                                               const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
                                               const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);

  SERVER_FRAME_API ~logic_hpa_puller_prometheus();

  SERVER_FRAME_API int tick(util::time::time_utility::raw_time_t now) override;

  SERVER_FRAME_API void stop() override;

  SERVER_FRAME_API bool do_pull() override;

  SERVER_FRAME_API bool is_pulling() const noexcept override;

  SERVER_FRAME_API bool is_stopped() const noexcept override;

  SERVER_FRAME_API bool can_pulling_available() const noexcept override;

 private:
  void make_query(std::shared_ptr<rpc::telemetry::group_type>& telemetry_group);

 private:
  bool stoping_;
  atfw::util::network::http_request::ptr_t pull_request_;
  std::string pull_url_;
  std::vector<std::string> pull_http_headers_;
};

class logic_hpa_pull_factory {
  SERVER_FRAME_API static std::unique_ptr<logic_hpa_puller_prometheus> make_puller(
      logic_hpa_policy& policy, const std::shared_ptr<rpc::telemetry::group_type>& telemetry_group,
      const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
      const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg);
};

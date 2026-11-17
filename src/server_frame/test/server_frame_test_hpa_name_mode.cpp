// Copyright 2026 atframework

#include <atframework/testing/runtime.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "frame/test_macros.h"
#include "logic/hpa/logic_hpa_controller.h"
#include "logic/hpa/logic_hpa_policy.h"
#include "logic/logic_server_setup.h"
#include "rpc/telemetry/rpc_global_service.h"

CASE_TEST(server_frame_unit_test, hpa_prometheus_name_only_uses_raw_metric_name) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::hpa};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  logic_hpa_controller *controller = logic_server_last_common_module() == nullptr
                                         ? nullptr
                                         : logic_server_last_common_module()->get_hpa_controller().get();
  auto telemetry_group = rpc::telemetry::global_service::get_group("hpa");
  CASE_EXPECT_TRUE(controller != nullptr);
  CASE_EXPECT_TRUE(!!telemetry_group);
  if (nullptr == controller || !telemetry_group) {
    test.stop();
    return;
  }

  PROJECT_NAMESPACE_ID::config::logic_hpa_cfg hpa_cfg;
  hpa_cfg.mutable_metrics()->set_enable(true);
  hpa_cfg.mutable_metrics()->set_pull_metrics_name_mode(
      PROJECT_NAMESPACE_ID::config::EN_LOGIC_HPA_PULL_METRICS_NAME_WITH_NAME_ONLY);
  PROJECT_NAMESPACE_ID::config::logic_hpa_policy policy_cfg;
  policy_cfg.set_metrics_name("rpc_unit_test_custom");
  policy_cfg.set_metrics_unit("count");
  auto policy =
      std::make_shared<logic_hpa_policy>(*controller, telemetry_group, hpa_cfg, policy_cfg,
                                         std::unordered_map<std::string, opentelemetry::common::AttributeValue>{},
                                         std::unordered_map<std::string, std::string>{});
  CASE_EXPECT_TRUE(!!policy);
  if (policy) {
    CASE_EXPECT_EQ("rpc_unit_test_custom", policy->get_query());
  }
  policy.reset();

  hpa_cfg.mutable_metrics()->set_pull_metrics_name_mode(
      PROJECT_NAMESPACE_ID::config::EN_LOGIC_HPA_PULL_METRICS_NAME_WITH_NAME_AND_UNIT);
  policy = std::make_shared<logic_hpa_policy>(*controller, telemetry_group, hpa_cfg, policy_cfg,
                                              std::unordered_map<std::string, opentelemetry::common::AttributeValue>{},
                                              std::unordered_map<std::string, std::string>{});
  CASE_EXPECT_TRUE(!!policy);
  if (policy) {
    CASE_EXPECT_EQ("rpc_unit_test_custom_count", policy->get_query());
  }
  policy.reset();

  CASE_EXPECT_EQ(0, test.stop());
}

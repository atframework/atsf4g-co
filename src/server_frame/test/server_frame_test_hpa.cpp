// Copyright 2026 atframework

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <atframework/testing/mock_hpa.h>
#include <atframework/testing/runtime.h>

#include "frame/test_macros.h"
#include "logic/hpa/logic_hpa_controller.h"
#include "logic/hpa/logic_hpa_data_type.h"
#include "logic/hpa/logic_hpa_policy.h"
#include "logic/logic_server_setup.h"

namespace {
bool pump_until(atframework::testing::runtime &test, const std::function<bool()> &pred,
                std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    test.pump_once();
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  return pred();
}
}  // namespace

// server_frame component: with the hpa feature the runtime installs the default prometheus pull hook
// (IMPLEMENTATION_PLAN.md 8.10). Pulls are recorded without creating any http_request; metrics without a
// configured answer are fed an error result so the policy stays not-ready; a configured success answer flows
// through the real parse -> trigger_event_on_pull_result -> do_ready chain.
CASE_TEST(server_frame_unit_test, hpa_prometheus_pull_hook_chain) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::hpa};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  CASE_EXPECT_TRUE(test.hpa().is_bound());

  // Capture the custom policy. The callback fires immediately when the policy already exists.
  logic_hpa_controller *controller = logic_server_last_common_module() == nullptr
                                         ? nullptr
                                         : logic_server_last_common_module()->get_hpa_controller().get();
  CASE_EXPECT_TRUE(controller != nullptr);
  if (nullptr == controller) {
    test.stop();
    return;
  }
  std::shared_ptr<logic_hpa_policy> policy;
  controller->set_on_setup_custom_policy(
      "rpc_unit_test_custom",
      [&policy](logic_hpa_controller &, std::shared_ptr<logic_hpa_policy> input) { policy = input; });
  CASE_EXPECT_TRUE(!!policy);
  if (!policy) {
    controller->remove_on_setup_custom_policy("rpc_unit_test_custom");
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(!policy->is_ready());

  // A policy only pulls when it has at least one active instant/range callback (is_pulling_available). The
  // callbacks are also the observation points of the real callback chain.
  int instant_callback_count = 0;
  int64_t last_instant_value = 0;
  auto instant_handle = policy->add_event_on_pull_instant(
      [&instant_callback_count, &last_instant_value](
          logic_hpa_policy &, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> records) {
        ++instant_callback_count;
        if (!records.empty() && records.front()) {
          last_instant_value = records.front()->get_value_as_int64();
        }
      });
  int error_callback_count = 0;
  policy->set_event_on_pull_error(
      [&error_callback_count](logic_hpa_policy &, gsl::string_view, gsl::string_view) { ++error_callback_count; });

  // Unconfigured answer: the pull is recorded, an error result is fed, and the policy must NOT become ready.
  CASE_EXPECT_TRUE(pump_until(test, [&test] { return !test.hpa().pull_history().empty(); },
                              std::chrono::milliseconds{10000}));
  const auto &history = test.hpa().pull_history();
  CASE_EXPECT_EQ("rpc_unit_test_custom", history.front().metrics_name);
  CASE_EXPECT_TRUE(!history.front().url.empty());
  CASE_EXPECT_TRUE(!history.front().query.empty());
  // Give the policy a few more pull cycles; an unanswered pull must feed the error path and never do_ready.
  CASE_EXPECT_TRUE(pump_until(test, [&error_callback_count] { return error_callback_count > 0; },
                              std::chrono::milliseconds{10000}));
  CASE_EXPECT_EQ(0, instant_callback_count);
  CASE_EXPECT_TRUE(!policy->is_ready());

  // Configured success answer: the real parse + trigger chain drives the instant callback and do_ready.
  test.hpa().set_answer(
      "rpc_unit_test_custom",
      "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":{\"__name__\":"
      "\"rpc_unit_test_custom\"},\"value\":[1735689600,\"42\"]}]}}");
  CASE_EXPECT_TRUE(pump_until(test, [&instant_callback_count] { return instant_callback_count > 0; },
                              std::chrono::milliseconds{10000}));
  CASE_EXPECT_EQ(42, static_cast<int>(last_instant_value));
  CASE_EXPECT_TRUE(pump_until(test, [&policy] { return policy->is_ready(); }, std::chrono::milliseconds{10000}));

  policy->remove_event_on_pull_instant(instant_handle);
  policy->clear_event_on_pull_error();

  controller->remove_on_setup_custom_policy("rpc_unit_test_custom");
  CASE_EXPECT_EQ(0, test.stop());
}

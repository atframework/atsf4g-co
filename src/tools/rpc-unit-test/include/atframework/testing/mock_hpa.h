// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <atframework/testing/runtime.h>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
#  include "logic/hpa/mock/logic_hpa_mock_prometheus.h"
#endif

namespace atframework {
namespace testing {

class mock_hpa;

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
using hpa_pull_query = logic_hpa::mock::prometheus_pull_query;
#else
// Hooks-off placeholder so the facade signature stays stable (never recorded without hooks).
struct ATFW_UTIL_SYMBOL_VISIBLE hpa_pull_query {
  std::string url;
  std::string metrics_name;
  std::string query;
  uint64_t sequence = 0;
};
#endif

// HPA prometheus pull mock (IMPLEMENTATION_PLAN.md 8.10). The runtime installs the default pull hook when the
// hpa feature is enabled: pulls are recorded and never create a real http_request. Metrics without a
// configured answer are fed an error result (do_ready is not triggered, which is the signal to configure an
// answer); configured answers are fed through the real logic_hpa_policy::trigger_event_on_pull_result chain.
class RPC_UNIT_TEST_API mock_hpa {
 public:
  mock_hpa();
  ~mock_hpa();

  mock_hpa(const mock_hpa &) = delete;
  mock_hpa &operator=(const mock_hpa &) = delete;

  void bind();
  void unbind() noexcept;
  bool is_bound() const noexcept;

  // Configure the canned Prometheus HTTP API response body for one policy metrics name.
  void set_answer(gsl::string_view metrics_name, std::string json_body);
  void remove_answer(gsl::string_view metrics_name);
  void clear_answers();

  const std::vector<hpa_pull_query> &pull_history() const noexcept;
  void clear_pull_history() noexcept;

 private:
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  bool on_answer(const logic_hpa::mock::prometheus_pull_query &query, std::string &out_json);
  logic_hpa::mock::prometheus_pull_hook_handle hook_handle_;
#endif
  bool bound_ = false;
  std::unordered_map<std::string, std::string> answers_;
  std::vector<hpa_pull_query> empty_history_;
};

}  // namespace testing
}  // namespace atframework

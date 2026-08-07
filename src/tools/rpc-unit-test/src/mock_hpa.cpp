// Copyright 2026 atframework

#include <atframework/testing/mock_hpa.h>

#include <string>
#include <utility>
#include <vector>

namespace atframework {
namespace testing {

mock_hpa::mock_hpa() = default;
mock_hpa::~mock_hpa() { unbind(); }

void mock_hpa::bind() {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (bound_) {
    return;
  }
  bound_ = true;
  logic_hpa::mock::clear_prometheus_pull_history();
  hook_handle_ = logic_hpa::mock::install_prometheus_pull_hook(
      [this](const logic_hpa::mock::prometheus_pull_query &query, std::string &out_json) {
        return on_answer(query, out_json);
      });
#endif
}

void mock_hpa::unbind() noexcept {
  if (!bound_) {
    return;
  }
  bound_ = false;
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  hook_handle_.reset();
  logic_hpa::mock::clear_prometheus_pull_history();
#endif
  answers_.clear();
}

bool mock_hpa::is_bound() const noexcept { return bound_; }

void mock_hpa::set_answer(gsl::string_view metrics_name, std::string json_body) {
  answers_[std::string{metrics_name.data(), metrics_name.size()}] = std::move(json_body);
}

void mock_hpa::remove_answer(gsl::string_view metrics_name) {
  answers_.erase(std::string{metrics_name.data(), metrics_name.size()});
}

void mock_hpa::clear_answers() { answers_.clear(); }

const std::vector<hpa_pull_query> &mock_hpa::pull_history() const noexcept {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  return logic_hpa::mock::get_prometheus_pull_history();
#else
  return empty_history_;
#endif
}

void mock_hpa::clear_pull_history() noexcept {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  logic_hpa::mock::clear_prometheus_pull_history();
#endif
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
bool mock_hpa::on_answer(const logic_hpa::mock::prometheus_pull_query &query, std::string &out_json) {
  auto iter = answers_.find(query.metrics_name);
  if (iter == answers_.end()) {
    return false;
  }
  out_json = iter->second;
  return true;
}
#endif

}  // namespace testing
}  // namespace atframework

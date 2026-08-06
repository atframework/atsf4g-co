// Copyright 2026 atframework

#include "logic/hpa/mock/logic_hpa_mock_prometheus.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

#  include <log/log_wrapper.h>

#  include "logic/hpa/logic_hpa_policy.h"
#  include "logic/hpa/pull/prometheus/logic_hpa_data_type_prometheus.h"
#  include "logic/hpa/pull/prometheus/logic_hpa_puller_prometheus.h"

namespace logic_hpa {
namespace mock {
namespace {

struct prometheus_pull_hook_state {
  prometheus_pull_answer_fn answer;
};

prometheus_pull_answer_fn& get_installed_pull_hook() {
  static prometheus_pull_answer_fn fn;
  return fn;
}

std::shared_ptr<prometheus_pull_hook_state>& get_installed_pull_hook_state() {
  static std::shared_ptr<prometheus_pull_hook_state> state;
  return state;
}

std::vector<prometheus_pull_query>& get_pull_history_storage() {
  static std::vector<prometheus_pull_query> history;
  return history;
}

uint64_t& get_pull_query_sequence() {
  static uint64_t sequence = 1;
  return sequence;
}

std::string sanitize_error_message(gsl::string_view message) {
  std::string ret;
  ret.reserve(message.size());
  for (char c : message) {
    ret.push_back('"' == c ? '\'' : c);
  }
  return ret;
}

}  // namespace

namespace details {
void uninstall_prometheus_pull_hook_if_active(const void* state) noexcept {
  if (nullptr != state && get_installed_pull_hook_state().get() == state) {
    get_installed_pull_hook_state().reset();
    get_installed_pull_hook() = nullptr;
  }
}
}  // namespace details

prometheus_pull_hook_handle install_prometheus_pull_hook(prometheus_pull_answer_fn fn) {
  auto state = std::make_shared<prometheus_pull_hook_state>();
  state->answer = std::move(fn);
  get_installed_pull_hook() = state->answer;
  get_installed_pull_hook_state() = state;
  return prometheus_pull_hook_handle{state};
}

const std::vector<prometheus_pull_query>& get_prometheus_pull_history() noexcept { return get_pull_history_storage(); }

void clear_prometheus_pull_history() noexcept { get_pull_history_storage().clear(); }

void feed_prometheus_pull_error(logic_hpa_policy& policy, gsl::string_view error_message) noexcept {
  logic_hpa_pull_result_prometheus result;
  std::string error_json = "{\"status\":\"error\",\"errorType\":\"unit_test\",\"error\":\"" +
                           sanitize_error_message(error_message) + "\"}";
  if (result.parse(error_json)) {
    policy.trigger_event_on_pull_result(result);
  }
}

namespace details {
bool pull_with_installed_hook_for_unit_test(logic_hpa_puller_prometheus& puller) {
  if (!get_installed_pull_hook()) {
    return false;
  }

  prometheus_pull_query query;
  query.url = puller.get_pull_url_for_unit_test();
  query.metrics_name = puller.get_owner().get_metrics_name();
  query.query = puller.get_owner().get_query();
  query.sequence = get_pull_query_sequence()++;
  get_pull_history_storage().push_back(query);

  std::string answer_json;
  bool answered = false;
  if (get_installed_pull_hook()) {
    answered = get_installed_pull_hook()(query, answer_json);
  }

  if (answered && !answer_json.empty()) {
    logic_hpa_pull_result_prometheus result;
    if (result.parse(answer_json)) {
      puller.get_owner().trigger_event_on_pull_result(result);
    } else {
      FWLOGWARNING("[HPA] unit test mock pull answer of policy {} is not a valid prometheus response",
                   query.metrics_name);
      feed_prometheus_pull_error(puller.get_owner(), "mock answer is not a valid prometheus response");
    }
  } else {
    FWLOGWARNING("[HPA] unit test mock pull of policy {} has no configured answer, feeding an error result",
                 query.metrics_name);
    feed_prometheus_pull_error(puller.get_owner(), "no mock answer configured for this metrics");
  }
  return true;
}
}  // namespace details

}  // namespace mock
}  // namespace logic_hpa

#endif

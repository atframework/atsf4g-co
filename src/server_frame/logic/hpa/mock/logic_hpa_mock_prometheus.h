// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

#  include <gsl/select-gsl.h>

#  include <cstdint>
#  include <functional>
#  include <memory>
#  include <string>
#  include <vector>

class logic_hpa_policy;
class logic_hpa_puller_prometheus;

// Unit-test mock seam for the HPA prometheus puller (see IMPLEMENTATION_PLAN.md 8.10). The registry is a
// single process-wide hook owned by the server_frame DLL. When installed, logic_hpa_puller_prometheus::do_pull
// records the query and never creates an http_request; the installed answer function decides which JSON body
// is fed back through the real logic_hpa_policy::trigger_event_on_pull_result callback chain.
namespace logic_hpa {
namespace mock {

// One recorded pull query (url, policy metrics name and the rendered prometheus query).
struct ATFW_UTIL_SYMBOL_VISIBLE prometheus_pull_query {
  std::string url;
  std::string metrics_name;
  std::string query;
  uint64_t sequence = 0;
};

// Answer for one pull: return true and fill out_json with a Prometheus HTTP API response body to feed a
// result; return false (or leave out_json empty) to feed an error result as the default no-answer diagnostic.
using prometheus_pull_answer_fn = std::function<bool(const prometheus_pull_query& query, std::string& out_json)>;

// RAII handle of the installed pull hook. The hook is uninstalled when the last handle is released (only if
// it is still the active one).
class SERVER_FRAME_API prometheus_pull_hook_handle {
 public:
  prometheus_pull_hook_handle() = default;

  bool empty() const noexcept;
  explicit operator bool() const noexcept;
  void reset();

 private:
  explicit prometheus_pull_hook_handle(std::shared_ptr<void> state);
  std::shared_ptr<void> state_;

  friend SERVER_FRAME_API prometheus_pull_hook_handle install_prometheus_pull_hook(prometheus_pull_answer_fn fn);
};

SERVER_FRAME_API prometheus_pull_hook_handle install_prometheus_pull_hook(prometheus_pull_answer_fn fn);

SERVER_FRAME_API const std::vector<prometheus_pull_query>& get_prometheus_pull_history() noexcept;
SERVER_FRAME_API void clear_prometheus_pull_history() noexcept;

// Feed an error result through the real policy callback chain (diagnostic path for unanswered pulls).
SERVER_FRAME_API void feed_prometheus_pull_error(logic_hpa_policy& policy, gsl::string_view error_message) noexcept;

namespace details {
// Internal entry invoked by logic_hpa_puller_prometheus::do_pull. Returns false when no hook is installed
// (caller falls through to the production HTTP path).
SERVER_FRAME_API bool pull_with_installed_hook_for_unit_test(logic_hpa_puller_prometheus& puller);
}  // namespace details

}  // namespace mock
}  // namespace logic_hpa

#endif

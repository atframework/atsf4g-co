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

// Unit-test mock seam for the HPA prometheus puller (see the HPA section of doc/docs/development/rpc-unit-test.md).
// The registry is a
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

namespace details {
// Uninstall the process-wide pull hook only when `state` is still the active hook state.
SERVER_FRAME_API void uninstall_prometheus_pull_hook_if_active(const void* state) noexcept;
}  // namespace details

// RAII handle of the installed pull hook. reset() (or letting reset() be called by the owning engine
// at teardown) uninstalls the hook only if it is still the active one. Follows the
// rpc::unit_test::mock_rule_handle pattern: visibility macro + fully inline methods, so the class has
// no DLL-exported ABI surface and no C4251 member-interface warning.
class ATFW_UTIL_SYMBOL_VISIBLE prometheus_pull_hook_handle {
 public:
  ATFW_UTIL_FORCEINLINE prometheus_pull_hook_handle() = default;
  ATFW_UTIL_FORCEINLINE explicit prometheus_pull_hook_handle(std::shared_ptr<void> state) noexcept
      : state_(std::move(state)) {}

  ATFW_UTIL_FORCEINLINE prometheus_pull_hook_handle(const prometheus_pull_hook_handle&) = default;
  ATFW_UTIL_FORCEINLINE prometheus_pull_hook_handle(prometheus_pull_hook_handle&&) = default;
  ATFW_UTIL_FORCEINLINE prometheus_pull_hook_handle& operator=(const prometheus_pull_hook_handle&) = default;
  ATFW_UTIL_FORCEINLINE prometheus_pull_hook_handle& operator=(prometheus_pull_hook_handle&&) = default;

  ATFW_UTIL_FORCEINLINE bool empty() const noexcept { return !state_; }
  ATFW_UTIL_FORCEINLINE explicit operator bool() const noexcept { return !!state_; }

  ATFW_UTIL_FORCEINLINE void reset() noexcept {
    if (!state_) {
      return;
    }
    const void* state = state_.get();
    state_.reset();
    details::uninstall_prometheus_pull_hook_if_active(state);
  }

 private:
  std::shared_ptr<void> state_;
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

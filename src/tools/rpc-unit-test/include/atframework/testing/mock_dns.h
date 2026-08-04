// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <atframework/testing/runtime.h>

#include "rpc/dns/lookup.h"

LIBATAPP_MACRO_NAMESPACE_BEGIN
class app;
LIBATAPP_MACRO_NAMESPACE_END

namespace atsf4g {
namespace testing {

class mock_dns;

// One recorded DNS lookup request.
struct ATFW_UTIL_SYMBOL_VISIBLE dns_request_record {
  std::string domain;
  uint64_t sequence = 0;
  uint64_t task_id = 0;
  bool matched_rule = false;
};

// Behavior and matching options of one DNS rule. Rules match by exact domain.
struct ATFW_UTIL_SYMBOL_VISIBLE dns_rule_options {
  // How many times this rule can fire (0 = unlimited).
  uint32_t times = 0;
  // Extra pump generations to delay the response after the rule fired.
  uint32_t delay_generations = 0;
  // Never complete the lookup (simulates a lost response / lookup timeout).
  bool no_response = false;
};

namespace detail {
struct dns_rule_state {
  std::string domain;
  rpc::dns::details::callback_data_type records;
  dns_rule_options options;
  uint32_t fired = 0;
  bool removed = false;
};
}  // namespace detail

// RAII handle of one registered DNS rule. The rule is removed when the last handle is released.
class RPC_UNIT_TEST_API dns_rule_handle {
 public:
  dns_rule_handle() = default;
  ~dns_rule_handle();

  dns_rule_handle(const dns_rule_handle &) = delete;
  dns_rule_handle &operator=(const dns_rule_handle &) = delete;
  dns_rule_handle(dns_rule_handle &&) noexcept;
  dns_rule_handle &operator=(dns_rule_handle &&) noexcept;

  explicit operator bool() const noexcept { return !!rule_; }

  void reset();

 private:
  friend class mock_dns;
  explicit dns_rule_handle(std::shared_ptr<detail::dns_rule_state> rule);

  std::shared_ptr<detail::dns_rule_state> rule_;
};

// Mock DNS engine. send_dns_lookup is consumed by the dispatcher unit-test hook, the response is
// completed asynchronously inside the runtime pump through the real rpc::custom_resume path, exactly
// like ss_msg_dispatcher::dns_lookup_callback does. Unmatched lookups fail fast with an empty record
// set, mirroring real resolver failure semantics (dns_lookup_callback ignores the libuv status and
// delivers whatever records exist).
class RPC_UNIT_TEST_API mock_dns {
 public:
  mock_dns();
  ~mock_dns();

  mock_dns(const mock_dns &) = delete;
  mock_dns &operator=(const mock_dns &) = delete;

  bool is_active() const noexcept;

  // Register a successful resolution with an explicit record set for an exact domain.
  dns_rule_handle mock(gsl::string_view domain, rpc::dns::details::callback_data_type records,
                       const dns_rule_options &options = dns_rule_options{});
  // Shortcuts for single-record resolutions.
  dns_rule_handle mock_a(gsl::string_view domain, gsl::string_view ipv4,
                         const dns_rule_options &options = dns_rule_options{});
  dns_rule_handle mock_aaaa(gsl::string_view domain, gsl::string_view ipv6,
                            const dns_rule_options &options = dns_rule_options{});
  // Register a resolution failure (empty record set) for an exact domain.
  dns_rule_handle mock_error(gsl::string_view domain, const dns_rule_options &options = dns_rule_options{});

  // History
  size_t calls(gsl::string_view domain) const;
  size_t call_count() const noexcept { return calls_.size(); }
  const dns_request_record *call_at(size_t index) const;
  // Lookups whose response arrived after the waiting task had gone away.
  size_t late_response_count() const noexcept { return late_responses_; }
  // Unmatched lookups completed with the default empty-record policy.
  size_t unmatched_count() const noexcept { return unmatched_; }

  std::string get_diagnostic() const { return diagnostic_; }

 private:
  friend class runtime;
  void bind();
  void unbind();
  void deliver_pending();

  bool on_lookup(gsl::string_view domain, uint64_t sequence, uint64_t task_id);
  void queue_response(dns_request_record &request, rpc::dns::details::callback_data_type records,
                      uint32_t delay_generations);

  struct pending_response {
    dns_request_record request;
    rpc::dns::details::callback_data_type records;
    uint64_t deliver_at_generation = 0;
  };

  bool bound_ = false;
  uint64_t current_generation_ = 0;
  size_t late_responses_ = 0;
  size_t unmatched_ = 0;
  std::string diagnostic_;
  std::vector<std::shared_ptr<detail::dns_rule_state>> rules_;
  std::deque<dns_request_record> calls_;
  std::deque<pending_response> pending_;
};

}  // namespace testing
}  // namespace atsf4g

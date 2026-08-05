// Copyright 2026 atframework

#include <atframework/testing/mock_dns.h>

#include <log/log_wrapper.h>

#include "dispatcher/ss_msg_dispatcher.h"
#include "dispatcher/task_manager.h"
#include "rpc/rpc_utils.h"

namespace atframework {
namespace testing {

dns_rule_handle::~dns_rule_handle() { reset(); }

dns_rule_handle::dns_rule_handle(dns_rule_handle &&other) noexcept : rule_(std::move(other.rule_)) {}

dns_rule_handle &dns_rule_handle::operator=(dns_rule_handle &&other) noexcept {
  if (this != &other) {
    reset();
    rule_ = std::move(other.rule_);
  }
  return *this;
}

void dns_rule_handle::reset() {
  if (rule_) {
    rule_->removed = true;
    rule_.reset();
  }
}

dns_rule_handle::dns_rule_handle(std::shared_ptr<detail::dns_rule_state> rule) : rule_(std::move(rule)) {}

mock_dns::mock_dns() = default;

mock_dns::~mock_dns() { unbind(); }

bool mock_dns::is_active() const noexcept { return bound_; }

void mock_dns::bind() {
  if (bound_) {
    return;
  }
  bound_ = true;

  ss_msg_dispatcher::me()->set_dns_lookup_hook_for_unit_test(
      [this](const ss_msg_dispatcher::dns_lookup_hook_request &request) -> bool {
        return on_lookup(request.domain, request.sequence, request.task_id);
      });
}

void mock_dns::unbind() {
  if (!bound_) {
    return;
  }
  bound_ = false;

  if (!ss_msg_dispatcher::is_instance_destroyed()) {
    ss_msg_dispatcher::me()->set_dns_lookup_hook_for_unit_test(nullptr);
  }

  rules_.clear();
  calls_.clear();
  pending_.clear();
}

dns_rule_handle mock_dns::mock(gsl::string_view domain, rpc::dns::details::callback_data_type records,
                               const dns_rule_options &options) {
  if (!bound_ || domain.empty()) {
    diagnostic_ = "mock_dns: mock registration requires an active engine and a non-empty domain";
    return dns_rule_handle{};
  }

  auto rule = std::make_shared<detail::dns_rule_state>();
  if (!rule) {
    diagnostic_ = "mock_dns: allocate rule failed";
    return dns_rule_handle{};
  }
  rule->domain = static_cast<std::string>(domain);
  rule->records = std::move(records);
  rule->options = options;
  rules_.push_back(rule);
  return dns_rule_handle{std::move(rule)};
}

dns_rule_handle mock_dns::mock_a(gsl::string_view domain, gsl::string_view ipv4, const dns_rule_options &options) {
  rpc::dns::details::callback_data_type records;
  records.push_back(rpc::dns::address_record{rpc::dns::address_type::kA, static_cast<std::string>(ipv4)});
  return mock(domain, std::move(records), options);
}

dns_rule_handle mock_dns::mock_aaaa(gsl::string_view domain, gsl::string_view ipv6, const dns_rule_options &options) {
  rpc::dns::details::callback_data_type records;
  records.push_back(rpc::dns::address_record{rpc::dns::address_type::kAAAA, static_cast<std::string>(ipv6)});
  return mock(domain, std::move(records), options);
}

dns_rule_handle mock_dns::mock_error(gsl::string_view domain, const dns_rule_options &options) {
  return mock(domain, rpc::dns::details::callback_data_type{}, options);
}

size_t mock_dns::calls(gsl::string_view domain) const {
  size_t ret = 0;
  for (const auto &call : calls_) {
    if (call.domain == domain) {
      ++ret;
    }
  }
  return ret;
}

const dns_request_record *mock_dns::call_at(size_t index) const {
  if (index >= calls_.size()) {
    return nullptr;
  }
  return &calls_[index];
}

bool mock_dns::on_lookup(gsl::string_view domain, uint64_t sequence, uint64_t task_id) {
  if (!bound_) {
    return false;
  }

  dns_request_record request;
  request.domain = static_cast<std::string>(domain);
  request.sequence = sequence;
  request.task_id = task_id;

  // First matching rule in insertion order wins; exhausted (times reached) and removed rules are
  // skipped so FIFO scripts compose.
  std::shared_ptr<detail::dns_rule_state> matched;
  for (auto &rule : rules_) {
    if (rule->removed || rule->domain != request.domain) {
      continue;
    }
    if (0 != rule->options.times && rule->fired >= rule->options.times) {
      continue;
    }
    matched = rule;
    break;
  }

  if (matched) {
    request.matched_rule = true;
    ++matched->fired;
    calls_.push_back(request);
    if (matched->options.no_response) {
      return true;
    }
    queue_response(calls_.back(), matched->records, matched->options.delay_generations);
    return true;
  }

  // Default unmatched policy: fail fast with an empty record set, mirroring real resolver failure
  // semantics, instead of letting the caller wait for the full lookup timeout.
  ++unmatched_;
  calls_.push_back(request);
  queue_response(calls_.back(), rpc::dns::details::callback_data_type{}, 0);
  return true;
}

void mock_dns::queue_response(dns_request_record &request, rpc::dns::details::callback_data_type records,
                              uint32_t delay_generations) {
  pending_response pending;
  pending.request = request;
  pending.records = std::move(records);
  pending.deliver_at_generation = current_generation_ + delay_generations;
  pending_.push_back(std::move(pending));
}

void mock_dns::deliver_pending() {
  ++current_generation_;

  while (!pending_.empty() && pending_.front().deliver_at_generation <= current_generation_) {
    pending_response pending = std::move(pending_.front());
    pending_.pop_front();

    if (task_manager::is_instance_destroyed()) {
      ++late_responses_;
      continue;
    }

    auto task_inst = task_manager::me()->get_task(pending.request.task_id);
    if (task_type_trait::empty(task_inst)) {
      // The waiting task was killed (e.g. by a test-level task timeout): discard and count it.
      ++late_responses_;
      continue;
    }

    dispatcher_resume_data_type callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
    callback_data.message.message_type =
        reinterpret_cast<uintptr_t>(ss_msg_dispatcher::me()->get_dns_lookup_rpc_type());
    callback_data.message.msg_addr = reinterpret_cast<void *>(&pending.records);
    callback_data.sequence = pending.request.sequence;
    rpc::custom_resume(task_inst, callback_data);
  }
}

}  // namespace testing
}  // namespace atframework

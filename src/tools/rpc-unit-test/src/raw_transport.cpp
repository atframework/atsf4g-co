// Copyright 2026 atframework

#include <atframework/testing/raw_transport.h>

#include <atframe/atapp.h>
#include <atframe/modules/service_discovery_module.h>

#include <algorithm>

namespace atframework {
namespace testing {

transport_rule_handle::transport_rule_handle(std::shared_ptr<detail::transport_rule_state> rule)
    : rule_(std::move(rule)) {}

transport_rule_handle::~transport_rule_handle() { reset(); }

transport_rule_handle::transport_rule_handle(transport_rule_handle &&other) noexcept : rule_(std::move(other.rule_)) {}

transport_rule_handle &transport_rule_handle::operator=(transport_rule_handle &&other) noexcept {
  reset();
  rule_ = std::move(other.rule_);
  return *this;
}

void transport_rule_handle::reset() {
  if (rule_) {
    rule_->active = false;
    rule_.reset();
  }
}

raw_transport::raw_transport() = default;

raw_transport::~raw_transport() { unbind(); }

bool raw_transport::is_active() const noexcept { return nullptr != owner_; }

transport_rule_handle raw_transport::add_rule(uint64_t node_id, int32_t type,
                                              const transport_send_behavior &behavior) {
  auto rule = std::make_shared<detail::transport_rule_state>();
  rule->node_id = node_id;
  rule->type = type;
  rule->behavior = behavior;
  rules_.push_back(rule);
  return transport_rule_handle{std::move(rule)};
}

void raw_transport::set_default_behavior(const transport_send_behavior &behavior) { default_behavior_ = behavior; }

size_t raw_transport::outbound_count() const noexcept { return outbound_history_.size(); }

size_t raw_transport::outbound_count_to(uint64_t node_id) const noexcept {
  size_t ret = 0;
  for (const auto &record : outbound_history_) {
    if (record.target_node_id == node_id) {
      ++ret;
    }
  }
  return ret;
}

size_t raw_transport::outbound_count_to(gsl::string_view node_name) const {
  size_t ret = 0;
  for (const auto &record : outbound_history_) {
    if (record.target_node_name.size() == node_name.size() &&
        0 == record.target_node_name.compare(0, std::string::npos, node_name.data(), node_name.size())) {
      ++ret;
    }
  }
  return ret;
}

const outbound_message *raw_transport::outbound_at(size_t index) const noexcept {
  if (index >= outbound_history_.size()) {
    return nullptr;
  }
  return &outbound_history_[index];
}

void raw_transport::clear_history() { outbound_history_.clear(); }

uint64_t raw_transport::connected_at(size_t index) const noexcept {
  if (index >= connected_history_.size()) {
    return 0;
  }
  return connected_history_[index];
}

int raw_transport::inject_inbound(const atfw::atapp::etcd_discovery_node::ptr_t &sender_node, int32_t type,
                                  gsl::span<const unsigned char> payload, uint64_t sequence) {
  pending_event event;
  if (!sender_node) {
    diagnostic_ = "inject_inbound requires a valid sender node";
    return -1;
  }
  if (!is_active()) {
    diagnostic_ = "raw_transport is not active";
    return -1;
  }

  event.deliver_at_generation = current_generation_ + 1;
  event.is_ack = false;
  event.node = sender_node;
  event.type = type;
  event.sequence = sequence;
  event.payload.assign(payload.begin(), payload.end());
  pending_events_.push_back(std::move(event));
  return 0;
}

int raw_transport::inject_inbound(const atfw::atapp::etcd_discovery_node::ptr_t &sender_node, int32_t type,
                                  gsl::span<const unsigned char> payload, uint64_t sequence,
                                  const atfw::atapp::protocol::atapp_metadata &metadata) {
  pending_event event;
  if (!sender_node) {
    diagnostic_ = "inject_inbound requires a valid sender node";
    return -1;
  }
  if (!is_active()) {
    diagnostic_ = "raw_transport is not active";
    return -1;
  }

  event.deliver_at_generation = current_generation_ + 1;
  event.is_ack = false;
  event.node = sender_node;
  event.type = type;
  event.sequence = sequence;
  event.payload.assign(payload.begin(), payload.end());
  event.metadata = std::make_unique<atfw::atapp::protocol::atapp_metadata>();
  if (event.metadata) {
    event.metadata->CopyFrom(metadata);
  }
  pending_events_.push_back(std::move(event));
  return 0;
}

size_t raw_transport::collect_outbound(outbound_cursor &cursor, std::vector<const outbound_message *> &output) {
  size_t ret = 0;
  while (cursor.next_index < outbound_history_.size()) {
    const outbound_message &record = outbound_history_[cursor.next_index];
    // One-generation barrier: records captured in this pump call are only consumable from the next one.
    if (record.pump_generation + 1 >= current_generation_) {
      break;
    }
    output.push_back(&record);
    ++cursor.next_index;
    ++ret;
  }
  return ret;
}

void raw_transport::bind(atfw::atapp::app *owner) {
  owner_ = owner;
  current_generation_ = 0;
  diagnostic_.clear();
}

void raw_transport::unbind() {
  owner_ = nullptr;
  for (auto &rule : rules_) {
    if (rule) {
      rule->active = false;
    }
  }
  rules_.clear();
  default_behavior_ = transport_send_behavior{};
  outbound_history_.clear();
  connected_history_.clear();
  pending_events_.clear();
}

void raw_transport::deliver_pending() {
  if (!is_active()) {
    pending_events_.clear();
    return;
  }

  while (!pending_events_.empty()) {
    const pending_event &front = pending_events_.front();
    if (front.deliver_at_generation >= current_generation_) {
      break;
    }

    pending_event event = std::move(pending_events_.front());
    pending_events_.pop_front();

    atfw::atapp::app::message_t msg;
    msg.type = event.type;
    msg.message_sequence = event.sequence;
    msg.data = gsl::span<const unsigned char>{event.payload.data(), event.payload.size()};
    msg.metadata = event.metadata.get();

    atfw::atapp::app::message_sender_t sender;
    sender.direct_source_id = event.node->get_discovery_info().id();
    sender.id = event.node->get_discovery_info().id();
    sender.name = event.node->get_discovery_info().name();
    sender.remote = owner_->get_endpoint(sender.id);

    if (event.is_ack) {
      owner_->trigger_event_on_forward_response(sender, msg, event.ack_error_code);
    } else {
      owner_->trigger_event_on_forward_request(sender, msg);
    }
  }
}

int32_t raw_transport::capture_outbound(uint64_t node_id, gsl::string_view node_name, int32_t type, uint64_t sequence,
                                        gsl::span<const unsigned char> data,
                                        const atfw::atapp::protocol::atapp_metadata *metadata) {
  const transport_send_behavior *behavior = &default_behavior_;
  for (auto &rule : rules_) {
    if (!rule || !rule->active) {
      continue;
    }
    if (rule->node_id != 0 && rule->node_id != node_id) {
      continue;
    }
    if (rule->type >= 0 && rule->type != type) {
      continue;
    }
    behavior = &rule->behavior;
    break;
  }

  outbound_message record;
  record.target_node_id = node_id;
  record.target_node_name.assign(node_name.data(), node_name.size());
  record.type = type;
  record.sequence = sequence;
  record.payload.assign(data.begin(), data.end());
  if (nullptr != metadata) {
    record.has_metadata = true;
    record.metadata.CopyFrom(*metadata);
  }
  record.pump_generation = current_generation_;
  record.immediate_error = behavior->immediate_error;
  outbound_history_.push_back(std::move(record));

  if (behavior->deliver_ack && 0 == behavior->immediate_error) {
    pending_event event;
    event.deliver_at_generation = current_generation_ + 1;
    event.is_ack = true;
    if (owner_ && 0 != node_id && owner_->get_service_discovery_module()) {
      event.node = owner_->get_service_discovery_module()->get_global_discovery().get_node_by_id(node_id);
    }
    event.type = type;
    event.sequence = sequence;
    event.ack_error_code = behavior->ack_error_code;
    if (event.node) {
      pending_events_.push_back(std::move(event));
    }
  }

  return behavior->immediate_error;
}

void raw_transport::capture_connect(uint64_t node_id) {
  if (0 != node_id) {
    connected_history_.push_back(node_id);
  }
}

void raw_transport::capture_disconnect(uint64_t) {}

}  // namespace testing
}  // namespace atframework

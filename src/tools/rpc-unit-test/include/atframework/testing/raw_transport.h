// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframe/atapp_conf.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp_config.h>
#include <atframe/etcdcli/etcd_discovery.h>

#include <atframework/testing/runtime.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

LIBATAPP_MACRO_NAMESPACE_BEGIN
class app;
LIBATAPP_MACRO_NAMESPACE_END

namespace atframework {
namespace testing {

class raw_transport;
class mock_connector;

// One captured outbound message. Payload and metadata are owned by the record.
struct ATFW_UTIL_SYMBOL_VISIBLE outbound_message {
  uint64_t target_node_id = 0;
  std::string target_node_name;
  int32_t type = 0;
  uint64_t sequence = 0;
  std::vector<unsigned char> payload;
  bool has_metadata = false;
  atfw::atapp::protocol::atapp_metadata metadata;
  uint64_t pump_generation = 0;
  // True when the send was answered synchronously with a transport error.
  int32_t immediate_error = 0;
};

// Behavior applied to outbound messages matched by a transport rule.
struct ATFW_UTIL_SYMBOL_VISIBLE transport_send_behavior {
  // Returned synchronously from the send call (0 = success).
  int32_t immediate_error = 0;
  // When true, an ack is delivered through trigger_event_on_forward_response in a later pump generation.
  bool deliver_ack = false;
  // Error code carried by the ack (0 = delivered successfully).
  int32_t ack_error_code = 0;
};

namespace detail {
struct transport_rule_state {
  uint64_t node_id = 0;  // 0 matches any target
  int32_t type = -1;     // -1 matches any message type
  transport_send_behavior behavior;
  bool active = true;
};
}  // namespace detail

// RAII handle of one transport rule. The rule is disabled when the handle is destroyed.
class RPC_UNIT_TEST_API transport_rule_handle {
 public:
  transport_rule_handle() = default;
  ~transport_rule_handle();

  transport_rule_handle(const transport_rule_handle &) = delete;
  transport_rule_handle &operator=(const transport_rule_handle &) = delete;
  transport_rule_handle(transport_rule_handle &&other) noexcept;
  transport_rule_handle &operator=(transport_rule_handle &&other) noexcept;

  void reset();

 private:
  friend class raw_transport;
  explicit transport_rule_handle(std::shared_ptr<detail::transport_rule_state> rule);

  std::shared_ptr<detail::transport_rule_state> rule_;
};

// Raw atapp transport capture/injection engine. It sits below the SS rule engine: the mock connector
// only captures outbound bytes here, and typed engines consume them through outbound_cursor.
class RPC_UNIT_TEST_API raw_transport {
 public:
  raw_transport();
  ~raw_transport();

  raw_transport(const raw_transport &) = delete;
  raw_transport &operator=(const raw_transport &) = delete;

  bool is_active() const noexcept;

  // Rule management. The first matching rule (in insertion order) wins; inactive rules are skipped.
  transport_rule_handle add_rule(uint64_t node_id, int32_t type, const transport_send_behavior &behavior);
  void set_default_behavior(const transport_send_behavior &behavior);
  const transport_send_behavior &get_default_behavior() const noexcept { return default_behavior_; }

  // Outbound history
  size_t outbound_count() const noexcept;
  size_t outbound_count_to(uint64_t node_id) const noexcept;
  size_t outbound_count_to(gsl::string_view node_name) const;
  const outbound_message *outbound_at(size_t index) const noexcept;
  void clear_history();

  // Connected endpoint history (node ids, in connect order).
  size_t connected_count() const noexcept { return connected_history_.size(); }
  uint64_t connected_at(size_t index) const noexcept;

  // Inject one inbound message as if it was sent from sender_node. Delivery crosses at least one pump
  // generation boundary; the message is then pushed through app::trigger_event_on_forward_request.
  // Returns 0 on success, negative error code on failure.
  int inject_inbound(const atfw::atapp::etcd_discovery_node::ptr_t &sender_node, int32_t type,
                     gsl::span<const unsigned char> payload, uint64_t sequence = 0);
  int inject_inbound(const atfw::atapp::etcd_discovery_node::ptr_t &sender_node, int32_t type,
                     gsl::span<const unsigned char> payload, uint64_t sequence,
                     const atfw::atapp::protocol::atapp_metadata &metadata);

  // Consumption cursor for typed engines (SS, ...). collect_outbound returns records not consumed yet
  // that already crossed the one-generation barrier. Pointers stay valid until clear_history()/unbind().
  struct ATFW_UTIL_SYMBOL_VISIBLE outbound_cursor {
    size_t next_index = 0;
  };
  size_t collect_outbound(outbound_cursor &cursor, std::vector<const outbound_message *> &output);

  size_t pending_event_count() const noexcept { return pending_events_.size(); }

  // Diagnostics of the last rejected operation (inactive engine, invalid parameters, ...).
  const std::string &get_diagnostic() const noexcept { return diagnostic_; }

 private:
  friend class mock_connector;
  friend class mock_ss;
  friend class runtime;

  void bind(atfw::atapp::app *owner);
  void unbind();

  // Runtime pump integration: set the current generation and deliver all events that crossed the barrier.
  void set_current_generation(uint64_t generation) noexcept { current_generation_ = generation; }
  uint64_t get_current_generation() const noexcept { return current_generation_; }
  void deliver_pending();

  // Called by mock_connector in the send call stack. Returns the immediate error code (0 = accepted).
  int32_t capture_outbound(uint64_t node_id, gsl::string_view node_name, int32_t type, uint64_t sequence,
                           gsl::span<const unsigned char> data, const atfw::atapp::protocol::atapp_metadata *metadata);
  void capture_connect(uint64_t node_id);
  void capture_disconnect(uint64_t node_id);

  struct pending_event {
    uint64_t deliver_at_generation = 0;
    bool is_ack = false;
    atfw::atapp::etcd_discovery_node::ptr_t node;
    int32_t type = 0;
    uint64_t sequence = 0;
    std::vector<unsigned char> payload;
    std::unique_ptr<atfw::atapp::protocol::atapp_metadata> metadata;
    int32_t ack_error_code = 0;
  };

  atfw::atapp::app *owner_ = nullptr;
  uint64_t current_generation_ = 0;
  transport_send_behavior default_behavior_;
  std::vector<std::shared_ptr<detail::transport_rule_state>> rules_;
  std::deque<outbound_message> outbound_history_;
  std::vector<uint64_t> connected_history_;
  std::deque<pending_event> pending_events_;
  std::string diagnostic_;
};

}  // namespace testing
}  // namespace atframework

// Copyright 2026 atframework

#include <atframe/atapp.h>

#include <config/extern_service_types.h>

#include <chrono>
#include <cstring>
#include <string>

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>

#include "frame/test_macros.h"

namespace {
static gsl::span<const unsigned char> make_payload(const char *text) {
  return gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(text), std::strlen(text)};
}

static atframework::testing::mock_node make_remote_node(uint64_t id, const char *name) {
  atframework::testing::mock_node node;
  node.set_id(id).set_name(name).set_type_id(4097).set_type_name("rpc-unit-test-remote").set_zone_id(1);
  return node;
}
}  // namespace

CASE_TEST(rpc_unit_test, transport_send_by_id_name_discovery) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x120001, "unit-test-remote-a"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  atfw::atapp::app *owner = test.get_app();
  CASE_EXPECT_TRUE(nullptr != owner);

  uint64_t sequence = 1001;
  CASE_EXPECT_EQ(0, owner->send_message(0x120001, 223, make_payload("by-id"), &sequence));
  ++sequence;
  CASE_EXPECT_EQ(0, owner->send_message(std::string{"unit-test-remote-a"}, 223, make_payload("by-name"), &sequence));
  ++sequence;
  CASE_EXPECT_EQ(0, owner->send_message(remote, 223, make_payload("by-discovery"), &sequence));

  CASE_EXPECT_EQ(3, static_cast<int>(test.transport().outbound_count()));
  CASE_EXPECT_EQ(3, static_cast<int>(test.transport().outbound_count_to(0x120001)));
  CASE_EXPECT_EQ(3, static_cast<int>(test.transport().outbound_count_to("unit-test-remote-a")));

  const atframework::testing::outbound_message *first = test.transport().outbound_at(0);
  CASE_EXPECT_TRUE(nullptr != first);
  if (nullptr != first) {
    CASE_EXPECT_EQ(0x120001, static_cast<int64_t>(first->target_node_id));
    CASE_EXPECT_EQ(223, first->type);
    CASE_EXPECT_EQ(1001, static_cast<int>(first->sequence));
    std::string payload{reinterpret_cast<const char *>(first->payload.data()), first->payload.size()};
    CASE_EXPECT_EQ("by-id", payload);
  }

  const atframework::testing::outbound_message *third = test.transport().outbound_at(2);
  CASE_EXPECT_TRUE(nullptr != third);
  if (nullptr != third) {
    std::string payload{reinterpret_cast<const char *>(third->payload.data()), third->payload.size()};
    CASE_EXPECT_EQ("by-discovery", payload);
  }

  // The connector must have been connected exactly once for the single endpoint.
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().connected_count()));
  CASE_EXPECT_EQ(0x120001, static_cast<int64_t>(test.transport().connected_at(0)));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, transport_send_with_metadata) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x120011, "unit-test-remote-meta"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  atfw::atapp::protocol::atapp_metadata metadata;
  (*metadata.mutable_labels())["route-key"] = "route-value";

  uint64_t sequence = 2001;
  CASE_EXPECT_EQ(0, test.get_app()->send_message(0x120011, 224, make_payload("with-meta"), &sequence, &metadata));

  const atframework::testing::outbound_message *record = test.transport().outbound_at(0);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_TRUE(record->has_metadata);
    auto iter = record->metadata.labels().find("route-key");
    CASE_EXPECT_TRUE(iter != record->metadata.labels().end());
    if (iter != record->metadata.labels().end()) {
      CASE_EXPECT_EQ("route-value", iter->second);
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, transport_send_selection_modes) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote_a = test.discovery().add_node(make_remote_node(0x120021, "unit-test-remote-hash-a"));
  auto remote_b = test.discovery().add_node(make_remote_node(0x120022, "unit-test-remote-hash-b"));
  CASE_EXPECT_TRUE(!!remote_a);
  CASE_EXPECT_TRUE(!!remote_b);
  if (!remote_a || !remote_b) {
    test.stop();
    return;
  }

  atfw::atapp::app *owner = test.get_app();
  uint64_t sequence = 3001;

  CASE_EXPECT_EQ(0, owner->send_message_by_consistent_hash(static_cast<uint64_t>(1234567), 225,
                                                           make_payload("by-hash"), &sequence));
  ++sequence;
  CASE_EXPECT_EQ(0, owner->send_message_by_random(225, make_payload("by-random"), &sequence));
  ++sequence;
  CASE_EXPECT_EQ(0, owner->send_message_by_round_robin(225, make_payload("by-round-robin"), &sequence));

  CASE_EXPECT_EQ(3, static_cast<int>(test.transport().outbound_count()));
  for (size_t i = 0; i < 3; ++i) {
    const atframework::testing::outbound_message *record = test.transport().outbound_at(i);
    CASE_EXPECT_TRUE(nullptr != record);
    if (nullptr != record) {
      bool is_known_target = record->target_node_id == 0x120021 || record->target_node_id == 0x120022;
      CASE_EXPECT_TRUE(is_known_target);
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, transport_immediate_error_rule) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x120031, "unit-test-remote-error"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  atframework::testing::transport_send_behavior behavior;
  behavior.immediate_error = -12345;
  auto rule = test.transport().add_rule(0x120031, -1, behavior);

  uint64_t sequence = 4001;
  CASE_EXPECT_EQ(-12345, test.get_app()->send_message(0x120031, 226, make_payload("will-fail"), &sequence));

  // The failed send is still recorded with its immediate error.
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().outbound_count()));
  const atframework::testing::outbound_message *record = test.transport().outbound_at(0);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_EQ(-12345, record->immediate_error);
  }

  // After the rule handle is destroyed, sends succeed again.
  rule.reset();
  ++sequence;
  CASE_EXPECT_EQ(0, test.get_app()->send_message(0x120031, 226, make_payload("succeed-again"), &sequence));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, transport_ack_delivery_barrier) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x120041, "unit-test-remote-ack"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  atframework::testing::transport_send_behavior behavior;
  behavior.deliver_ack = true;
  behavior.ack_error_code = 0;
  auto rule = test.transport().add_rule(0x120041, -1, behavior);

  uint64_t sequence = 5001;
  CASE_EXPECT_EQ(0, test.get_app()->send_message(0x120041, 227, make_payload("ack-me"), &sequence));
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().pending_event_count()));

  // One-generation barrier: the ack is not delivered in the first pump call after the send.
  test.pump_once();
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().pending_event_count()));

  test.pump_once();
  CASE_EXPECT_EQ(0, static_cast<int>(test.transport().pending_event_count()));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, transport_inject_inbound) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x120051, "unit-test-remote-inbound"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  // Inject an in-server message with an invalid body. The real SS dispatcher will reject it during
  // unpack; the point of this case is the atapp forward path, sender identity and pump barrier.
  CASE_EXPECT_EQ(0, test.transport().inject_inbound(
                         remote, static_cast<int32_t>(::atfw::component::message_type::kInServerMessage),
                         make_payload("invalid-ss-body"), 6001));
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().pending_event_count()));

  test.pump_once();
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().pending_event_count()));

  CASE_EXPECT_GE(test.pump_once(), 0);
  CASE_EXPECT_EQ(0, static_cast<int>(test.transport().pending_event_count()));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, transport_consecutive_fixture) {
  for (int round = 0; round < 2; ++round) {
    atframework::testing::runtime test;
    atframework::testing::runtime_options options;
    options.features = {atframework::testing::feature::ss};

    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      CASE_MSG_INFO() << "runtime start failed at round " << round << ": " << test.get_diagnostic() << '\n';
      return;
    }

    auto remote = test.discovery().add_node(make_remote_node(0x120061, "unit-test-remote-repeat"));
    CASE_EXPECT_TRUE(!!remote);
    if (!remote) {
      test.stop();
      return;
    }

    uint64_t sequence = 7001;
    CASE_EXPECT_EQ(0, test.get_app()->send_message(0x120061, 228, make_payload("repeat"), &sequence));
    CASE_EXPECT_EQ(1, static_cast<int>(test.transport().outbound_count()));

    CASE_EXPECT_EQ(0, test.stop());
  }
}

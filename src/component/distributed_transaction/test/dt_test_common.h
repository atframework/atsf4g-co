// Copyright 2026 atframework
//
// Shared helpers for the distributed transaction unit tests: dtcoordsvr discovery injection,
// storage builders, the client vtable event recorder and a pump-based wait helper.

#ifndef SRC_COMPONENT_DISTRIBUTED_TRANSACTION_TEST_DT_TEST_COMMON_H_
#define SRC_COMPONENT_DISTRIBUTED_TRANSACTION_TEST_DT_TEST_COMMON_H_

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/runtime.h>

#include <gsl/select-gsl.h>

#include <time/time_utility.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "config/extern_service_types.h"
#include "frame/test_macros.h"
#include "logic/logic_server_setup.h"

namespace dt_test {

// Scoped business-clock offset; runtime/task hard timeouts keep using sys_now().
class global_now_offset_guard {
 public:
  global_now_offset_guard() : previous_(atfw::util::time::time_utility::get_global_now_offset()) {}
  explicit global_now_offset_guard(std::chrono::system_clock::duration advance_by)
      : previous_(atfw::util::time::time_utility::get_global_now_offset()) {
    atfw::util::time::time_utility::set_global_now_offset(previous_ + advance_by);
  }
  ~global_now_offset_guard() { atfw::util::time::time_utility::set_global_now_offset(previous_); }
  global_now_offset_guard(const global_now_offset_guard&) = delete;
  global_now_offset_guard& operator=(const global_now_offset_guard&) = delete;

 private:
  std::chrono::system_clock::duration previous_;
};

// The HPA-patched scaling_ready selector requires the hpa_scaling_ready=1 metadata label (see the
// create-contract case and logic_hpa_controller).
inline atfw::testing::mock_node make_coordinator_node(uint64_t node_id, uint32_t zone_id = 1) {
  atfw::testing::mock_node node;
  node.set_id(node_id)
      .set_name("unit-test-dtcoordsvr-" + std::to_string(node_id))
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtCoordSvr))
      .set_type_name("dtcoordsvr")
      .set_zone_id(zone_id)
      .add_label("hpa_scaling_ready", "1");
  return node;
}

// Inject dtcoordsvr discovery nodes and replay them into the common module discovery index. mock
// injection writes the global discovery set directly; the index only sees new nodes on reload.
inline bool inject_coordinators(atfw::testing::runtime& test, const std::vector<uint64_t>& node_ids,
                                uint32_t zone_id = 1) {
  bool all_added = true;
  for (uint64_t node_id : node_ids) {
    all_added = all_added && !!test.discovery().add_node(make_coordinator_node(node_id, zone_id));
  }
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
  return all_added;
}

// Build a PREPARED transaction storage with the given participators (status PREPARED so that
// create_transaction on the coordinator side accepts it).
inline void make_prepared_storage(atfw::distributed_system::transaction_blob_storage& out, gsl::string_view uuid,
                                  const std::vector<std::string>& participator_keys, bool memory_only = false,
                                  std::chrono::system_clock::duration timeout = std::chrono::seconds{30}) {
  out.Clear();
  out.mutable_metadata()->set_transaction_uuid(uuid.data(), uuid.size());
  out.mutable_metadata()->set_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  out.mutable_metadata()->set_memory_only(memory_only);
  // Use the framework cached clock: transaction_manager computes TTL windows against
  // time_utility::now(), and mixing clocks skews the sub-second remainder across the ceil().
  auto now = atfw::util::time::time_utility::now();
  auto* prepare_timepoint = out.mutable_metadata()->mutable_prepare_timepoint();
  prepare_timepoint->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
  prepare_timepoint->set_nanos(static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() % 1000000000));
  auto* expire_timepoint = out.mutable_metadata()->mutable_expire_timepoint();
  auto expire = now + timeout;
  expire_timepoint->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(expire.time_since_epoch()).count());
  expire_timepoint->set_nanos(static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(expire.time_since_epoch()).count() % 1000000000));
  out.mutable_configure()->set_resolve_max_times(3);
  out.mutable_configure()->set_lock_retry_max_times(3);
  out.mutable_configure()->mutable_resolve_retry_interval()->set_seconds(0);
  out.mutable_configure()->mutable_resolve_retry_interval()->set_nanos(10000000);  // 10ms
  out.mutable_configure()->mutable_lock_wait_interval_min()->set_seconds(0);
  out.mutable_configure()->mutable_lock_wait_interval_min()->set_nanos(1000000);  // 1ms
  out.mutable_configure()->mutable_lock_wait_interval_max()->set_seconds(0);
  out.mutable_configure()->mutable_lock_wait_interval_max()->set_nanos(4000000);  // 4ms

  for (const auto& key : participator_keys) {
    auto& participator = (*out.mutable_participators())[key];
    participator.set_participator_key(key);
    participator.set_participator_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  }
}

// Pump the mock event loop until pred() turns true or the deadline expires. Tasks started by
// tick()/auto actions make progress through pump_once() (one full pump generation per call).
inline bool wait_for(atfw::testing::runtime& test, const std::function<bool()>& pred,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds{3000}) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    test.pump_once();
    std::this_thread::yield();
  }
  return true;
}

// Order-tolerant event comparison: protobuf map iteration order is not stable across instances,
// so multi-participant event lists are compared as multisets plus explicit phase-order checks.
inline bool expect_event_multiset(const std::vector<std::string>& actual, std::vector<std::string> expected) {
  if (!CASE_EXPECT_EQ(expected.size(), actual.size())) {
    std::string joined;
    for (const auto& event : actual) {
      joined += event;
      joined += "; ";
    }
    CASE_MSG_INFO() << "actual events: " << joined << '\n';
    return false;
  }
  std::vector<std::string> sorted_actual = actual;
  std::sort(sorted_actual.begin(), sorted_actual.end());
  std::sort(expected.begin(), expected.end());
  bool all_equal = true;
  for (size_t i = 0; i < sorted_actual.size(); ++i) {
    all_equal = CASE_EXPECT_EQ(expected[i], sorted_actual[i]) && all_equal;
  }
  return all_equal;
}

// True when every event starting with earlier_prefix happens before any event starting with
// later_prefix (phase ordering is deterministic even when the intra-phase order is not).
inline bool expect_all_before(const std::vector<std::string>& events, gsl::string_view earlier_prefix,
                              gsl::string_view later_prefix) {
  size_t last_earlier = 0;
  size_t first_later = events.size();
  for (size_t i = 0; i < events.size(); ++i) {
    if (events[i].compare(0, earlier_prefix.size(), earlier_prefix.data(), earlier_prefix.size()) == 0) {
      last_earlier = i;
    }
    if (events[i].compare(0, later_prefix.size(), later_prefix.data(), later_prefix.size()) == 0) {
      first_later = std::min(first_later, i);
    }
  }
  return CASE_EXPECT_TRUE(last_earlier < first_later);
}

// Compares an ordered event list element by element (CASE_EXPECT_EQ cannot print std::vector).
inline bool expect_event_list(const std::vector<std::string>& actual, const std::vector<std::string>& expected) {
  bool size_equal = CASE_EXPECT_EQ(expected.size(), actual.size());
  if (!size_equal) {
    std::string joined;
    for (const auto& event : actual) {
      joined += event;
      joined += "; ";
    }
    CASE_MSG_INFO() << "actual events: " << joined << '\n';
    return false;
  }
  bool all_equal = true;
  for (size_t i = 0; i < actual.size(); ++i) {
    all_equal = CASE_EXPECT_EQ(expected[i], actual[i]) && all_equal;
  }
  return all_equal;
}

}  // namespace dt_test

#endif  // SRC_COMPONENT_DISTRIBUTED_TRANSACTION_TEST_DT_TEST_COMMON_H_

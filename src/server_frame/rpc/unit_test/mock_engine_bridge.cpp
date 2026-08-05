// Copyright 2026 atframework

#include <rpc/unit_test/mock_engine_bridge.h>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

namespace rpc {
namespace unit_test {
namespace {
mock_engine_bridge_t &mutable_bridge_storage() {
  static mock_engine_bridge_t bridge;
  return bridge;
}
}  // namespace

void merge_mock_engine_bridge_for_unit_test(mock_engine_bridge_t slots) {
  mock_engine_bridge_t &bridge = mutable_bridge_storage();
  if (slots.register_ss_rule) {
    bridge.register_ss_rule = std::move(slots.register_ss_rule);
  }
  if (slots.db_set_error_rule) {
    bridge.db_set_error_rule = std::move(slots.db_set_error_rule);
  }
  if (slots.db_force_not_found_rule) {
    bridge.db_force_not_found_rule = std::move(slots.db_force_not_found_rule);
  }
  if (slots.db_set_raw_kv) {
    bridge.db_set_raw_kv = std::move(slots.db_set_raw_kv);
  }
  if (slots.db_get_raw_kv) {
    bridge.db_get_raw_kv = std::move(slots.db_get_raw_kv);
  }
  if (slots.db_append_raw_kl) {
    bridge.db_append_raw_kl = std::move(slots.db_append_raw_kl);
  }
  if (slots.db_get_raw_kl) {
    bridge.db_get_raw_kl = std::move(slots.db_get_raw_kl);
  }
  if (slots.db_set_raw_ttl) {
    bridge.db_set_raw_ttl = std::move(slots.db_set_raw_ttl);
  }
}

void clear_ss_mock_engine_bridge_slot() { mutable_bridge_storage().register_ss_rule = nullptr; }

void clear_db_mock_engine_bridge_slots() {
  mock_engine_bridge_t &bridge = mutable_bridge_storage();
  bridge.db_set_error_rule = nullptr;
  bridge.db_force_not_found_rule = nullptr;
  bridge.db_set_raw_kv = nullptr;
  bridge.db_get_raw_kv = nullptr;
  bridge.db_append_raw_kl = nullptr;
  bridge.db_get_raw_kl = nullptr;
  bridge.db_set_raw_ttl = nullptr;
}

const mock_engine_bridge_t &get_mock_engine_bridge_for_unit_test() noexcept { return mutable_bridge_storage(); }

}  // namespace unit_test
}  // namespace rpc

#endif

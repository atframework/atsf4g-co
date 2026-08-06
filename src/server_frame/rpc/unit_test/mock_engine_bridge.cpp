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
  if (slots.db_register_typed_handler) {
    bridge.db_register_typed_handler = std::move(slots.db_register_typed_handler);
  }
}

void clear_ss_mock_engine_bridge_slot() { mutable_bridge_storage().register_ss_rule = nullptr; }

void clear_db_mock_engine_bridge_slots() {
  mutable_bridge_storage().db_register_typed_handler = nullptr;
}

const mock_engine_bridge_t &get_mock_engine_bridge_for_unit_test() noexcept { return mutable_bridge_storage(); }

}  // namespace unit_test
}  // namespace rpc

#endif

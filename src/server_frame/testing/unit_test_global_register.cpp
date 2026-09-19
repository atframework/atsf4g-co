// Copyright 2026 atframework

#include "testing/unit_test_global_register.h"

#include <atomic>
#include <list>

namespace {
struct ATFW_UTIL_SYMBOL_LOCAL server_frame_unit_test_setup_action_data {
  std::atomic<bool> already_run{false};
  std::list<server_frame_unit_test_setup_action_function> actions;
};

static server_frame_unit_test_setup_action_data& get_server_frame_unit_test_setup_action_data() {
  static server_frame_unit_test_setup_action_data data;
  return data;
}
}  // namespace

SERVER_FRAME_API void server_frame_unit_test_register_setup_action(server_frame_unit_test_setup_action_function fn) {
  if (fn == nullptr) {
    return;
  }

  get_server_frame_unit_test_setup_action_data().actions.push_back(fn);
  if (get_server_frame_unit_test_setup_action_data().already_run.load()) {
    fn();
  }
}

SERVER_FRAME_API void server_frame_unit_test_run_setup_action() {
  if (get_server_frame_unit_test_setup_action_data().already_run.exchange(true)) {
    return;
  }

  for (auto& action : get_server_frame_unit_test_setup_action_data().actions) {
    action();
  }
}

SERVER_FRAME_API size_t server_frame_unit_test_get_setup_action_count() {
  return get_server_frame_unit_test_setup_action_data().actions.size();
}

SERVER_FRAME_API bool server_frame_unit_test_is_setup_action_already_run() {
  return get_server_frame_unit_test_setup_action_data().already_run.load();
}

// Copyright 2026 atframework

#include "testing/unit_test_case_cleanup.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

#  include <algorithm>
#  include <map>
#  include <mutex>
#  include <string>
#  include <utility>
#  include <vector>

namespace {

struct cleanup_registry_entry {
  int level;
  unit_test_case_cleanup_fn fn;
};

std::mutex& get_cleanup_registry_mutex() {
  static std::mutex ret;
  return ret;
}

// 注册表为函数级 static：组件 registrar 在静态初始化期注册时按需构造，规避跨编译单元初始化顺序问题。
std::map<std::string, cleanup_registry_entry>& get_cleanup_registry() {
  static std::map<std::string, cleanup_registry_entry> ret;
  return ret;
}

}  // namespace

SERVER_FRAME_API void server_frame_unit_test_register_case_cleanup(const char* name, int level,
                                                                   unit_test_case_cleanup_fn fn) {
  if (nullptr == name || !fn) {
    return;
  }

  std::lock_guard<std::mutex> lock_guard{get_cleanup_registry_mutex()};
  get_cleanup_registry()[name] = cleanup_registry_entry{level, std::move(fn)};
}

SERVER_FRAME_API bool server_frame_unit_test_unregister_case_cleanup(const char* name) {
  if (nullptr == name) {
    return false;
  }

  std::lock_guard<std::mutex> lock_guard{get_cleanup_registry_mutex()};
  return get_cleanup_registry().erase(name) > 0;
}

SERVER_FRAME_API void server_frame_unit_test_run_case_cleanups() {
  // 先在锁内拷贝快照，锁外执行回调：清理函数自身不得再改注册表，这里仅防御重入死锁。
  std::vector<std::pair<std::string, cleanup_registry_entry>> snapshots;
  {
    std::lock_guard<std::mutex> lock_guard{get_cleanup_registry_mutex()};
    snapshots.assign(get_cleanup_registry().begin(), get_cleanup_registry().end());
  }

  std::sort(snapshots.begin(), snapshots.end(), [](const auto& l, const auto& r) {
    if (l.second.level != r.second.level) {
      return l.second.level < r.second.level;
    }
    return l.first < r.first;
  });

  for (auto& snapshot : snapshots) {
    snapshot.second.fn();
  }
}

#endif

// Copyright 2026 atframework

#include <atframework/testing/runtime.h>

#include <vector>

#include "frame/test_macros.h"
#include "testing/unit_test_case_cleanup.h"
#include "testing/unit_test_global_register.h"

namespace {

// 探针清理只追加执行序号，用 level 本身作为序号可以直接校验升序。
struct case_cleanup_probe {
  static std::vector<int> fired_levels;
  static bool is_registered;

  static void record(int level) { fired_levels.push_back(level); }

  static void reset() { fired_levels.clear(); }
};
std::vector<int> case_cleanup_probe::fired_levels;
bool case_cleanup_probe::is_registered = false;

void register_case_cleanup_probes() {
  server_frame_unit_test_register_case_cleanup(
      "server_frame.test_probe_service_cache", kUnitTestCaseCleanupLevelServiceCache,
      [] { case_cleanup_probe::record(kUnitTestCaseCleanupLevelServiceCache); });
  server_frame_unit_test_register_case_cleanup("server_frame.test_probe_business", kUnitTestCaseCleanupLevelBusiness,
                                               [] { case_cleanup_probe::record(kUnitTestCaseCleanupLevelBusiness); });
}

void unregister_case_cleanup_probes() {
  server_frame_unit_test_unregister_case_cleanup("server_frame.test_probe_business");
  server_frame_unit_test_unregister_case_cleanup("server_frame.test_probe_service_cache");
}

ATFW_SERVER_FRAME_TESTING_SETUP(dtmq_client_subscriber_case_cleanup_registered) {
  case_cleanup_probe::is_registered = true;
}

}  // namespace

// 契约：注册的跨用例清理在每个 runtime 边界执行——start() 的防御性清理与 stop() 的常规清理各一次；
// 同一轮内按 level 升序(业务层先于服务缓存层)；unregister 后不再执行。
CASE_TEST(server_frame_unit_test_case_cleanup, fires_on_runtime_boundaries_in_level_order) {
  case_cleanup_probe::reset();
  register_case_cleanup_probes();

  {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;

    // start 边界：防御性清理上一用例可能残留的状态
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
      unregister_case_cleanup_probes();
      return;
    }
    CASE_EXPECT_EQ(2, static_cast<int>(case_cleanup_probe::fired_levels.size()));
    if (2 == case_cleanup_probe::fired_levels.size()) {
      CASE_EXPECT_EQ(kUnitTestCaseCleanupLevelBusiness, case_cleanup_probe::fired_levels[0]);
      CASE_EXPECT_EQ(kUnitTestCaseCleanupLevelServiceCache, case_cleanup_probe::fired_levels[1]);
    }

    // stop 边界：app 已销毁后的常规清理
    CASE_EXPECT_EQ(0, test.stop());
    CASE_EXPECT_EQ(4, static_cast<int>(case_cleanup_probe::fired_levels.size()));
  }

  {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;

    // 第二个 runtime 生命周期重复同样契约：每个用例边界都清理
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
      unregister_case_cleanup_probes();
      return;
    }
    CASE_EXPECT_EQ(6, static_cast<int>(case_cleanup_probe::fired_levels.size()));
    CASE_EXPECT_EQ(0, test.stop());
    CASE_EXPECT_EQ(8, static_cast<int>(case_cleanup_probe::fired_levels.size()));
    if (8 == case_cleanup_probe::fired_levels.size()) {
      // 四轮边界(start/stop × 两个 runtime)，每轮两条记录按 level 升序
      for (size_t round = 0; round < 4; ++round) {
        CASE_EXPECT_EQ(kUnitTestCaseCleanupLevelBusiness, case_cleanup_probe::fired_levels[round * 2]);
        CASE_EXPECT_EQ(kUnitTestCaseCleanupLevelServiceCache, case_cleanup_probe::fired_levels[(round * 2) + 1]);
      }
    }
  }

  // 临时探针必须反注册，避免影响同进程内后续用例
  CASE_EXPECT_TRUE(server_frame_unit_test_unregister_case_cleanup("server_frame.test_probe_business"));
  CASE_EXPECT_TRUE(server_frame_unit_test_unregister_case_cleanup("server_frame.test_probe_service_cache"));
  CASE_EXPECT_FALSE(server_frame_unit_test_unregister_case_cleanup("server_frame.test_probe_business"));

  case_cleanup_probe::reset();
  server_frame_unit_test_run_case_cleanups();
  CASE_EXPECT_TRUE(case_cleanup_probe::fired_levels.empty());
}

CASE_TEST(server_frame_unit_test_case_cleanup, global_register) {
  CASE_EXPECT_TRUE(case_cleanup_probe::is_registered);
  CASE_EXPECT_TRUE(server_frame_unit_test_is_setup_action_already_run());
  CASE_EXPECT_GT(server_frame_unit_test_get_setup_action_count(), 0);
}

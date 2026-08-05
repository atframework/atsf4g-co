// Copyright 2026 atframework

#include <atframework/testing/runtime.h>

#include <chrono>

#include "frame/test_macros.h"
#include "rpc/rpc_utils.h"

CASE_TEST(rpc_unit_test, runtime_start_stop) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::dns,
                      atframework::testing::feature::db};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_empty_task) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  auto task = test.run_task("empty_task", std::chrono::seconds{2},
                            [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_FALSE(result.task_timed_out);
  CASE_EXPECT_FALSE(result.runtime_poisoned);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_task_business_result_code) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto task = test.run_task("business_error", std::chrono::seconds{2},
                            [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(42); });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(42, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_task_timeout) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto task = test.run_task(
      "task_timeout", std::chrono::milliseconds{300}, [](rpc::context &ctx) -> rpc::result_code_type {
        // Wait on a custom type/sequence that is never resumed; the task-level timeout must fire.
        static int never_resumed_rpc_type;
        auto await_options = dispatcher_make_default<dispatcher_await_options>();
        await_options.sequence = 1;
        await_options.timeout = std::chrono::seconds{30};
        auto res = RPC_AWAIT_CODE_RESULT(rpc::custom_wait(ctx, &never_resumed_rpc_type, await_options));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_TRUE(result.task_timed_out);
  CASE_EXPECT_FALSE(result.runtime_poisoned);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_consecutive_fixture) {
  // A second runtime must start cleanly after the previous one stopped: no leaked app, task or
  // process-level state may block a consecutive fixture.
  for (int i = 0; i < 2; ++i) {
    atframework::testing::runtime test;
    atframework::testing::runtime_options options;
    options.features = {atframework::testing::feature::ss};

    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      CASE_MSG_INFO() << "fixture " << i << " start failed: " << test.get_diagnostic() << '\n';
      return;
    }

    auto task = test.run_task("consecutive", std::chrono::seconds{2},
                              [i](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(i); });
    if (!task.empty()) {
      auto result = test.wait(task, std::chrono::seconds{5});
      CASE_EXPECT_TRUE(result.task_exited);
      CASE_EXPECT_EQ(i, result.result_code);
    }

    CASE_EXPECT_EQ(0, test.stop());
  }
}

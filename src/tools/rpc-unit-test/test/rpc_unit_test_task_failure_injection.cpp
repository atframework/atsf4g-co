// Copyright 2026 atframework
//
// Selftest for the unit-test-only interception hooks:
//   - task_manager::mock_create_task: intercept create_task_with_timeout by task
//     type demangled name (the mock hook caches the demangled name per action type and may modify
//     task timeout through the reference).
//   - rpc::unit_test::mock_async_invoke: intercept async_invoke by task name (same
//     timeout-reference contract).
// Hook contract: return 0 (or positive) = do not intercept; negative = fail with that error code.
// RAII handles uninstall on destruction and the runtime reset clears leftovers, so no hook leaks
// across cases. Production builds fully strip both seams; these cases only run when
// PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS is on.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/runtime.h>

#include <chrono>
#include <string>

#include "dispatcher/task_manager.h"
#include "frame/test_macros.h"
#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_utils.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

CASE_TEST(rpc_unit_test, task_manager_create_task_interception_hook) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  // No mock hooks by default: the fast path reports nothing.
  CASE_EXPECT_FALSE(task_manager::mock_create_task_active());

  // run_task goes through rpc::async_invoke -> create_task<task_action_async_invoke>.
  auto baseline = test.run_task("hook_baseline", std::chrono::seconds{2},
                                [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
  CASE_EXPECT_FALSE(baseline.empty());

  // Callback hook: fail only the async-invoke action type; other types pass through untouched.
  {
    auto hook = task_manager::mock_create_task(
        [](gsl::string_view demangled_task_type_name, std::chrono::system_clock::duration &) -> int {
          if (demangled_task_type_name.find("task_action_async_invoke") != gsl::string_view::npos) {
            return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
          }
          return 0;
        });
    CASE_EXPECT_TRUE(static_cast<bool>(hook));
    CASE_EXPECT_TRUE(task_manager::mock_create_task_active());

    auto blocked = test.run_task("hook_blocked", std::chrono::seconds{2},
                                 [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
    CASE_EXPECT_TRUE(blocked.empty());
    if (blocked.empty()) {
      CASE_MSG_INFO() << "run_task diagnostic: " << blocked.get_diagnostic() << '\n';
    }
  }

  // RAII release uninstalls the hook; creation works again.
  CASE_EXPECT_FALSE(task_manager::mock_create_task_active());
  auto recovered = test.run_task("hook_recovered", std::chrono::seconds{2},
                                 [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
  CASE_EXPECT_FALSE(recovered.empty());
  if (!recovered.empty()) {
    auto result = test.wait(recovered, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // A leaked hook is force-cleared by the reset path (the runtime calls the same clear on rebuild,
  // so the next case always starts clean).
  {
    auto leaked = task_manager::mock_create_task(
        [](gsl::string_view, std::chrono::system_clock::duration &) -> int {
          return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        });
    CASE_EXPECT_TRUE(static_cast<bool>(leaked));
    CASE_EXPECT_TRUE(task_manager::mock_create_task_active());
    task_manager::mock_create_task_clear();
    CASE_EXPECT_FALSE(task_manager::mock_create_task_active());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, task_manager_create_task_hook_can_modify_timeout) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  // The hook does not intercept, but shrinks the task timeout below the body's wait so the task
  // dies with EN_SYS_TIMEOUT — proving the timeout reference is honored without interception.
  auto hook = task_manager::mock_create_task(
      [](gsl::string_view demangled_task_type_name, std::chrono::system_clock::duration &timeout) -> int {
        if (demangled_task_type_name.find("task_action_async_invoke") != gsl::string_view::npos) {
          timeout = std::chrono::milliseconds{150};
        }
        return 0;
      });
  CASE_EXPECT_TRUE(static_cast<bool>(hook));

  auto task = test.run_task("hook_timeout_shrink", std::chrono::seconds{30},
                            [](rpc::context &ctx) -> rpc::result_code_type {
                              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::seconds{5})));
                            });
  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, result.result_code);

  // hook cleanup
  hook.reset();
  CASE_EXPECT_FALSE(task_manager::mock_create_task_active());

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, async_invoke_interception_hook_by_name) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  // Hooks must be installed before the calls that should observe them.
  auto hook = rpc::unit_test::mock_async_invoke(
      [](gsl::string_view name, std::chrono::system_clock::duration &) -> int {
        if (name.find("selftest-blocked") != gsl::string_view::npos) {
          return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
        }
        return 0;
      });
  CASE_EXPECT_TRUE(static_cast<bool>(hook));

  int blocked_error = 0;
  int allowed_created = 0;
  int recovered_error = 0;
  auto task = test.run_task("hook_driver", std::chrono::seconds{4},
                            [&blocked_error, &allowed_created](rpc::context &ctx) -> rpc::result_code_type {
    auto blocked = rpc::async_invoke(ctx, "selftest-blocked-task",
                                     [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
    if (blocked.is_error()) {
      blocked_error = *blocked.get_error();
    }

    auto allowed = rpc::async_invoke(ctx, "selftest-allowed-task",
                                     [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
    if (!allowed.is_error()) {
      allowed_created = 1;
    }
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Only the name the callback recognized failed, with the code it returned.
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, blocked_error);
  CASE_EXPECT_EQ(1, allowed_created);

  // RAII release: the previously blocked name works again.
  hook.reset();
  auto recovered_task = test.run_task("hook_recovered", std::chrono::seconds{4},
                                      [&recovered_error](rpc::context &ctx) -> rpc::result_code_type {
    auto recovered = rpc::async_invoke(ctx, "selftest-blocked-task",
                                       [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
    if (recovered.is_error()) {
      recovered_error = *recovered.get_error();
    }
    RPC_RETURN_CODE(0);
  });
  auto recovered_result = test.wait(recovered_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(recovered_result.task_exited);
  CASE_EXPECT_EQ(0, recovered_result.result_code);
  CASE_EXPECT_EQ(0, recovered_error);

  CASE_EXPECT_EQ(0, test.stop());
}

#endif

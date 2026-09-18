// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

#  include <functional>

// 跨用例清理的执行层级：数值小的先执行，同层级按注册名排序，顺序确定。
// 业务管理器先清，基础设施层最后清，保证清理业务对象时触发的底层回调仍能访问到底层状态。
constexpr const int kUnitTestCaseCleanupLevelBusiness = 0;          // 业务管理器
constexpr const int kUnitTestCaseCleanupLevelServiceCache = 100;    // 服务本地缓存
constexpr const int kUnitTestCaseCleanupLevelInfrastructure = 200;  // 基础设施层

using unit_test_case_cleanup_fn = std::function<void()>;

/**
 * @brief Register a cleanup callback that runs at every unit-test case boundary.
 * @note Only available when PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS is on. The rpc-unit-test runtime
 *       invokes all registered callbacks each time a fixture stops (and defensively when one starts), so
 *       process-lifetime singletons and file-level statics do not leak state into the next case in the same
 *       test executable. Re-registering an existing name replaces its level and callback. The callback must
 *       be idempotent, must not throw, and must only touch state that is quiescent at a case boundary (no
 *       running task, app already destroyed). Registrations live for the whole process; components register
 *       from a file-scope static object inside their own translation unit so linkers cannot drop it.
 */
SERVER_FRAME_API void server_frame_unit_test_register_case_cleanup(const char* name, int level,
                                                                   unit_test_case_cleanup_fn fn);

/**
 * @brief Remove a previously registered cleanup callback. Returns whether an entry with this name existed.
 * @note Only needed by temporary registrations such as test probes; component registrars never unregister.
 */
SERVER_FRAME_API bool server_frame_unit_test_unregister_case_cleanup(const char* name);

/**
 * @brief Run all registered case cleanups ordered by (level, name).
 * @note Called by the rpc-unit-test runtime at fixture teardown boundaries. Safe to call repeatedly and
 *       when nothing was ever registered.
 */
SERVER_FRAME_API void server_frame_unit_test_run_case_cleanups();

#endif

// Copyright 2026 atframework

#include "testing/unit_test_reset.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

#  include <dispatcher/cs_msg_dispatcher.h>
#  include <dispatcher/ss_msg_dispatcher.h>
#  include <dispatcher/task_manager.h>
#  include <rpc/rpc_async_invoke.h>

SERVER_FRAME_API void server_frame_unit_test_reset_dispatcher_registrations() {
  ss_msg_dispatcher::me()->reset_registrations_for_unit_test();
  cs_msg_dispatcher::me()->reset_registrations_for_unit_test();
  // 拦截钩子兜底清理：runtime 重建时移除上一用例可能泄漏的钩子
  task_manager::mock_create_task_clear();  // 注册表为文件级 static，不经单例，避免 teardown 期悬空
  rpc::unit_test::mock_async_invoke_clear();
}

#endif

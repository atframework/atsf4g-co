#include "task_action_connect_external.pseudo.h"

CASE_TEST(task_action_connect_external, run_task_action_connect_external_registers_first_connection) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ConnectExternalReq request;
  atorbit::dsc::session::session_router session_router;

  request.set_session(9527);

  auto result = atorbit::dsc::service::logic::action::run_task_action_connect_external(
      runtime, rpc_context, request, session_router);

  CASE_EXPECT_EQ(0, result);
}

CASE_TEST(task_action_connect_external, run_task_action_connect_external_rejects_duplicate_live_connection) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ConnectExternalReq request;
  atorbit::dsc::session::session_router session_router;

  request.set_session(9527);
  atorbit::dsc::service::logic::action::run_task_action_connect_external(runtime, rpc_context, request, session_router);

  auto result = atorbit::dsc::service::logic::action::run_task_action_connect_external(
      runtime, rpc_context, request, session_router);

  CASE_EXPECT_EQ(15, result);
}
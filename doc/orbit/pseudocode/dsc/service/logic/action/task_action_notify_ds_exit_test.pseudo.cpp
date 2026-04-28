#include "task_action_notify_ds_exit.pseudo.h"

CASE_TEST(task_action_notify_ds_exit, run_task_action_notify_ds_exit_releases_ds_owner_and_updates_agent_load) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::NotifyDSExitReq request;
  atorbit::dsc::registry::disconnect_cleanup disconnect_cleanup;
  atorbit::dsc::session::session_router session_router;
  atorbit::dsc::registry::agent_registry agent_registry;
  atorbit::dsc::registry::agent_registration_t registration;
  atorbit::dsc::session::ds_composite_key_t ds_key;

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  agent_registry.upsert_registered_agent(registration);
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 42;
  session_router.bind_ds_owner(9527, ds_key);

  request.set_request(7001, 42, 2, -9, "crash", 3.0, 2048.0, 5.0, 12288.0, 1);

  auto result = atorbit::dsc::service::logic::action::run_task_action_notify_ds_exit(
      runtime, rpc_context, request, disconnect_cleanup, session_router, agent_registry);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_FALSE(session_router.validate_ds_owner(9527, ds_key));
  CASE_EXPECT_EQ(3.0, agent_registry.get_cpu_used(7001));
}
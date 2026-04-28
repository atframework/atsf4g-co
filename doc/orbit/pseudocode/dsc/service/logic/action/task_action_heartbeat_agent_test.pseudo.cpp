#include "task_action_heartbeat_agent.pseudo.h"

CASE_TEST(task_action_heartbeat_agent, run_task_action_heartbeat_agent_refreshes_heartbeat_timestamp) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::HeartbeatAgentReq request;
  atorbit::dsc::registry::agent_registry agent_registry;
  atorbit::dsc::registry::agent_registration_t registration;

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  agent_registry.upsert_registered_agent(registration);
  request.set_request(7001, 12345, 3.0, 2048.0, 2);

  auto result = atorbit::dsc::service::logic::action::run_task_action_heartbeat_agent(
      runtime, rpc_context, request, agent_registry);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(12345, agent_registry.get_last_heartbeat_ms(7001));
}
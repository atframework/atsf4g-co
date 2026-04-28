#include "task_action_register_agent.pseudo.h"

CASE_TEST(task_action_register_agent, run_task_action_register_agent_inserts_registry_record) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::RegisterAgentReq request;
  atorbit::dsc::registry::agent_registry agent_registry;

  request.set_request(7001, "region-cn-east", 10.0, 16384.0, 2.0, 1024.0, 1, 1);

  auto result = atorbit::dsc::service::logic::action::run_task_action_register_agent(
      runtime, rpc_context, request, agent_registry);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_TRUE(agent_registry.has_agent(7001));
}
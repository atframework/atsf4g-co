#include "task_action_report_agent_load.pseudo.h"

CASE_TEST(task_action_report_agent_load, run_task_action_report_agent_load_updates_load_snapshot) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ReportAgentLoadReq request;
  atorbit::dsc::registry::agent_registry agent_registry;
  atorbit::dsc::registry::agent_registration_t registration;

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  agent_registry.upsert_registered_agent(registration);
  request.set_request(7001, 4.0, 3072.0, 6.0, 13312.0, 3);

  auto result = atorbit::dsc::service::logic::action::run_task_action_report_agent_load(
      runtime, rpc_context, request, agent_registry);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(4.0, agent_registry.get_cpu_used(7001));
}
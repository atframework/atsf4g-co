#include "scheduler_service.pseudo.h"

CASE_TEST(scheduler_service, select_agent_for_launch_picks_most_available_eligible_agent) {
  atorbit::dsc::registry::agent_registry agent_registry;
  atorbit::dsc::registry::agent_registration_t registration_a;
  atorbit::dsc::registry::agent_registration_t registration_b;
  atorbit::dsc::registry::load_snapshot_t load_a;
  atorbit::dsc::registry::load_snapshot_t load_b;
  atorbit::dsc::scheduler::scheduler_service scheduler(&agent_registry);
  atorbit::dsc::scheduler::launch_request_t request;

  registration_a.agent_id = 7001;
  registration_a.region = "region-cn-east";
  registration_b.agent_id = 7002;
  registration_b.region = "region-cn-east";
  agent_registry.upsert_registered_agent(registration_a);
  agent_registry.upsert_registered_agent(registration_b);

  load_a.cpu_available = 4.0;
  load_a.memory_available_mb = 4096.0;
  load_b.cpu_available = 8.0;
  load_b.memory_available_mb = 8192.0;
  agent_registry.update_agent_load(7001, load_a);
  agent_registry.update_agent_load(7002, load_b);

  request.owner_unique_id = 9527;
  request.target_region = "region-cn-east";
  request.expected_cpu = 1.0;
  request.expected_memory_mb = 1024.0;

  auto selected = scheduler.select_agent_for_launch(request);

  CASE_EXPECT_TRUE(selected.found);
  CASE_EXPECT_EQ(7002, selected.agent_id);
}

CASE_TEST(scheduler_service, select_agent_for_launch_rejects_region_mismatch) {
  atorbit::dsc::registry::agent_registry agent_registry;
  atorbit::dsc::registry::agent_registration_t registration;
  atorbit::dsc::registry::load_snapshot_t load;
  atorbit::dsc::scheduler::scheduler_service scheduler(&agent_registry);
  atorbit::dsc::scheduler::launch_request_t request;

  registration.agent_id = 7001;
  registration.region = "region-cn-west";
  agent_registry.upsert_registered_agent(registration);
  load.cpu_available = 8.0;
  load.memory_available_mb = 8192.0;
  agent_registry.update_agent_load(7001, load);

  request.owner_unique_id = 9527;
  request.target_region = "region-cn-east";
  request.expected_cpu = 1.0;
  request.expected_memory_mb = 1024.0;

  auto selected = scheduler.select_agent_for_launch(request);

  CASE_EXPECT_FALSE(selected.found);
}
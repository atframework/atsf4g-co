#include "agent_registry.pseudo.h"

CASE_TEST(agent_registry, upsert_registered_agent_inserts_agent_record) {
  atorbit::dsc::registry::agent_registry registry;
  atorbit::dsc::registry::agent_registration_t registration;

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  registration.load.cpu_capacity = 10.0;
  registration.load.memory_capacity_mb = 16384.0;

  auto result = registry.upsert_registered_agent(registration);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_TRUE(registry.has_agent(7001));
  CASE_EXPECT_EQ(1, registry.get_agent_count());
}

CASE_TEST(agent_registry, refresh_agent_heartbeat_updates_last_heartbeat_timestamp) {
  atorbit::dsc::registry::agent_registry registry;
  atorbit::dsc::registry::agent_registration_t registration;
  atorbit::dsc::registry::load_snapshot_t load;

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  registry.upsert_registered_agent(registration);

  load.cpu_used = 3.0;
  load.memory_used_mb = 2048.0;
  load.running_ds_count = 2;

  auto result = registry.refresh_agent_heartbeat(7001, 12345, load);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(12345, registry.get_last_heartbeat_ms(7001));
}

CASE_TEST(agent_registry, update_agent_load_overwrites_latest_load_snapshot) {
  atorbit::dsc::registry::agent_registry registry;
  atorbit::dsc::registry::agent_registration_t registration;
  atorbit::dsc::registry::load_snapshot_t load;

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  registry.upsert_registered_agent(registration);

  load.cpu_used = 4.0;
  load.memory_used_mb = 3072.0;
  load.cpu_available = 6.0;
  load.memory_available_mb = 13312.0;
  load.running_ds_count = 3;

  auto result = registry.update_agent_load(7001, load);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(4.0, registry.get_cpu_used(7001));
}
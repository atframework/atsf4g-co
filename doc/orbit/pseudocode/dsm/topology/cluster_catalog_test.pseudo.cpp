#include "cluster_catalog.pseudo.h"

static int test_cluster_catalog_upsert_controller_inserts_controller_record() {
  atorbit::dsm::topology::cluster_catalog catalog;
  atorbit::dsm::topology::controller_registration_t registration;

  registration.controller_id = 5001;
  registration.region = "region-cn-east";
  registration.controller_addr = "dsc://region-cn-east/controller-a";

  auto result = catalog.upsert_controller(registration);

  if (result != 0) {
    return -1;
  }

  if (!catalog.has_controller(5001)) {
    return -2;
  }

  if (catalog.get_controller_count() != 1) {
    return -3;
  }

  if (catalog.get_controller_region(5001) != registration.region) {
    return -4;
  }

  return 0;
}

static int test_cluster_catalog_upsert_controller_updates_existing_controller_record() {
  atorbit::dsm::topology::cluster_catalog catalog;
  atorbit::dsm::topology::controller_registration_t registration;

  registration.controller_id = 5001;
  registration.region = "region-cn-east";
  registration.controller_addr = "dsc://region-cn-east/controller-a";
  catalog.upsert_controller(registration);

  registration.controller_addr = "dsc://region-cn-east/controller-b";
  auto result = catalog.upsert_controller(registration);
  atorbit::dsm::topology::controller_record_t output_records[4] = {};
  auto output_count = catalog.list_controllers(output_records, 4);

  if (result != 0) {
    return -1;
  }

  if (catalog.get_controller_count() != 1) {
    return -2;
  }

  if (output_count != 1) {
    return -3;
  }

  if (output_records[0].controller_addr != registration.controller_addr) {
    return -4;
  }

  return 0;
}

static int test_cluster_catalog_update_controller_snapshot_refreshes_aggregate_state() {
  atorbit::dsm::topology::cluster_catalog catalog;
  atorbit::dsm::topology::controller_registration_t registration;
  atorbit::dsm::topology::controller_snapshot_t snapshot;

  registration.controller_id = 5001;
  registration.region = "region-cn-east";
  registration.controller_addr = "dsc://region-cn-east/controller-a";
  catalog.upsert_controller(registration);

  snapshot.controller_id = 5001;
  snapshot.agent_count = 3;
  snapshot.ds_count = 18;
  snapshot.session_count = 42;
  snapshot.inflight_count = 5;
  snapshot.draining = true;

  auto result = catalog.update_controller_snapshot(snapshot);

  if (result != 0) {
    return -1;
  }

  if (catalog.get_controller_agent_count(5001) != 3) {
    return -2;
  }

  if (catalog.get_controller_ds_count(5001) != 18) {
    return -3;
  }

  if (catalog.get_controller_session_count(5001) != 42) {
    return -4;
  }

  if (catalog.get_controller_inflight_count(5001) != 5) {
    return -5;
  }

  if (!catalog.is_controller_draining(5001)) {
    return -6;
  }

  return 0;
}

static int test_cluster_catalog_build_inventory_summary_aggregates_region_scope() {
  atorbit::dsm::topology::cluster_catalog catalog;
  atorbit::dsm::topology::controller_registration_t east_controller_a;
  atorbit::dsm::topology::controller_registration_t east_controller_b;
  atorbit::dsm::topology::controller_registration_t west_controller;
  atorbit::dsm::topology::controller_snapshot_t east_snapshot_a;
  atorbit::dsm::topology::controller_snapshot_t east_snapshot_b;
  atorbit::dsm::topology::controller_snapshot_t west_snapshot;
  atorbit::dsm::topology::inventory_query_t query;
  atorbit::dsm::topology::inventory_summary_t summary;

  east_controller_a.controller_id = 5001;
  east_controller_a.region = "region-cn-east";
  east_controller_a.controller_addr = "dsc://region-cn-east/controller-a";
  catalog.upsert_controller(east_controller_a);

  east_controller_b.controller_id = 5002;
  east_controller_b.region = "region-cn-east";
  east_controller_b.controller_addr = "dsc://region-cn-east/controller-b";
  catalog.upsert_controller(east_controller_b);

  west_controller.controller_id = 6001;
  west_controller.region = "region-us-west";
  west_controller.controller_addr = "dsc://region-us-west/controller-a";
  catalog.upsert_controller(west_controller);

  east_snapshot_a.controller_id = 5001;
  east_snapshot_a.agent_count = 3;
  east_snapshot_a.ds_count = 18;
  east_snapshot_a.session_count = 42;
  east_snapshot_a.inflight_count = 5;
  east_snapshot_a.draining = true;
  catalog.update_controller_snapshot(east_snapshot_a);

  east_snapshot_b.controller_id = 5002;
  east_snapshot_b.agent_count = 2;
  east_snapshot_b.ds_count = 9;
  east_snapshot_b.session_count = 16;
  east_snapshot_b.inflight_count = 3;
  east_snapshot_b.draining = false;
  catalog.update_controller_snapshot(east_snapshot_b);

  west_snapshot.controller_id = 6001;
  west_snapshot.agent_count = 4;
  west_snapshot.ds_count = 20;
  west_snapshot.session_count = 28;
  west_snapshot.inflight_count = 7;
  west_snapshot.draining = false;
  catalog.update_controller_snapshot(west_snapshot);

  query.has_region_filter = true;
  query.region = "region-cn-east";

  auto result = catalog.build_inventory_summary(query, summary);

  if (result != 0) {
    return -1;
  }

  if (summary.controller_count != 2) {
    return -2;
  }

  if (summary.agent_count != 5) {
    return -3;
  }

  if (summary.ds_count != 27) {
    return -4;
  }

  if (summary.session_count != 58) {
    return -5;
  }

  if (summary.inflight_count != 8) {
    return -6;
  }

  if (summary.draining_controller_count != 1) {
    return -7;
  }

  return 0;
}

static int test_cluster_catalog_list_controllers_by_query_returns_filtered_detail_records() {
  atorbit::dsm::topology::cluster_catalog catalog;
  atorbit::dsm::topology::controller_registration_t east_controller_a;
  atorbit::dsm::topology::controller_registration_t east_controller_b;
  atorbit::dsm::topology::controller_registration_t west_controller;
  atorbit::dsm::topology::inventory_query_t query;
  atorbit::dsm::topology::controller_record_t output_records[4] = {};

  east_controller_a.controller_id = 5001;
  east_controller_a.region = "region-cn-east";
  east_controller_a.controller_addr = "dsc://region-cn-east/controller-a";
  catalog.upsert_controller(east_controller_a);

  east_controller_b.controller_id = 5002;
  east_controller_b.region = "region-cn-east";
  east_controller_b.controller_addr = "dsc://region-cn-east/controller-b";
  catalog.upsert_controller(east_controller_b);

  west_controller.controller_id = 6001;
  west_controller.region = "region-us-west";
  west_controller.controller_addr = "dsc://region-us-west/controller-a";
  catalog.upsert_controller(west_controller);

  query.has_region_filter = true;
  query.region = "region-cn-east";

  auto output_count = catalog.list_controllers_by_query(query, output_records, 4);

  if (output_count != 2) {
    return -1;
  }

  if (output_records[0].controller_id != 5001) {
    return -2;
  }

  if (output_records[1].controller_id != 5002) {
    return -3;
  }

  return 0;
}

static int test_cluster_catalog_replace_controller_ds_inventory_and_list_ds_by_query() {
  atorbit::dsm::topology::cluster_catalog catalog;
  atorbit::dsm::topology::controller_registration_t controller;
  atorbit::dsm::topology::ds_snapshot_record_t ds_records[2] = {};
  atorbit::dsm::topology::inventory_query_t query;
  atorbit::dsm::topology::ds_record_t output_records[4] = {};

  controller.controller_id = 5001;
  controller.region = "region-cn-east";
  controller.controller_addr = "dsc://region-cn-east/controller-a";
  catalog.upsert_controller(controller);

  ds_records[0].dsa_id = 7001;
  ds_records[0].ds_id = 8001;
  ds_records[0].owner_unique_id = 9527;
  ds_records[0].running_state = 1;
  ds_records[0].exit_reason = 0;
  ds_records[0].last_active_millis = 1001;
  ds_records[1].dsa_id = 7002;
  ds_records[1].ds_id = 8002;
  ds_records[1].owner_unique_id = 9528;
  ds_records[1].running_state = 2;
  ds_records[1].exit_reason = 5;
  ds_records[1].last_active_millis = 1002;

  auto result = catalog.replace_controller_ds_inventory(5001, ds_records, 2);
  query.has_controller_filter = true;
  query.controller_id = 5001;
  auto output_count = catalog.list_ds_by_query(query, output_records, 4);

  if (result != 0) {
    return -1;
  }

  if (output_count != 2) {
    return -2;
  }

  if (output_records[0].dsa_id != 7001) {
    return -3;
  }

  if (output_records[1].ds_id != 8002) {
    return -4;
  }

  if (output_records[1].exit_reason != 5) {
    return -5;
  }

  return 0;
}
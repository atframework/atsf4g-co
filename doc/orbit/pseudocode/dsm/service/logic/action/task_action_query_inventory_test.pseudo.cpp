#include "task_action_query_inventory.pseudo.h"

static int test_task_action_query_inventory_returns_region_summary() {
  atorbit::dsm::service::app::runtime_handle_t runtime = 0;
  atorbit::dsm::service::app::rpc_context_t rpc_context;
  atorbit::dsm::service::app::QueryInventoryReq request;
  atorbit::dsm::topology::cluster_catalog cluster_catalog;

  atorbit::dsm::topology::controller_registration_t east_controller_a;
  east_controller_a.controller_id = 5001;
  east_controller_a.region = "region-cn-east";
  east_controller_a.controller_addr = "dsc://region-cn-east/controller-a";
  cluster_catalog.upsert_controller(east_controller_a);

  atorbit::dsm::topology::controller_registration_t east_controller_b;
  east_controller_b.controller_id = 5002;
  east_controller_b.region = "region-cn-east";
  east_controller_b.controller_addr = "dsc://region-cn-east/controller-b";
  cluster_catalog.upsert_controller(east_controller_b);

  atorbit::dsm::topology::controller_registration_t west_controller;
  west_controller.controller_id = 6001;
  west_controller.region = "region-us-west";
  west_controller.controller_addr = "dsc://region-us-west/controller-a";
  cluster_catalog.upsert_controller(west_controller);

  atorbit::dsm::topology::controller_snapshot_t east_snapshot_a;
  east_snapshot_a.controller_id = 5001;
  east_snapshot_a.agent_count = 3;
  east_snapshot_a.ds_count = 18;
  east_snapshot_a.session_count = 42;
  east_snapshot_a.inflight_count = 5;
  east_snapshot_a.draining = true;
  cluster_catalog.update_controller_snapshot(east_snapshot_a);
  atorbit::dsm::topology::ds_snapshot_record_t east_ds_records_a[2] = {};
  east_ds_records_a[0].dsa_id = 7001;
  east_ds_records_a[0].ds_id = 8001;
  east_ds_records_a[0].owner_unique_id = 9527;
  east_ds_records_a[0].running_state = 1;
  east_ds_records_a[0].exit_reason = 0;
  east_ds_records_a[0].last_active_millis = 1001;
  east_ds_records_a[1].dsa_id = 7001;
  east_ds_records_a[1].ds_id = 8002;
  east_ds_records_a[1].owner_unique_id = 9528;
  east_ds_records_a[1].running_state = 1;
  east_ds_records_a[1].exit_reason = 0;
  east_ds_records_a[1].last_active_millis = 1002;
  cluster_catalog.replace_controller_ds_inventory(5001, east_ds_records_a, 2);

  atorbit::dsm::topology::controller_snapshot_t east_snapshot_b;
  east_snapshot_b.controller_id = 5002;
  east_snapshot_b.agent_count = 2;
  east_snapshot_b.ds_count = 9;
  east_snapshot_b.session_count = 16;
  east_snapshot_b.inflight_count = 3;
  east_snapshot_b.draining = false;
  cluster_catalog.update_controller_snapshot(east_snapshot_b);
  atorbit::dsm::topology::ds_snapshot_record_t east_ds_records_b[1] = {};
  east_ds_records_b[0].dsa_id = 7002;
  east_ds_records_b[0].ds_id = 8101;
  east_ds_records_b[0].owner_unique_id = 9537;
  east_ds_records_b[0].running_state = 1;
  east_ds_records_b[0].exit_reason = 0;
  east_ds_records_b[0].last_active_millis = 1011;
  cluster_catalog.replace_controller_ds_inventory(5002, east_ds_records_b, 1);

  atorbit::dsm::topology::controller_snapshot_t west_snapshot;
  west_snapshot.controller_id = 6001;
  west_snapshot.agent_count = 4;
  west_snapshot.ds_count = 20;
  west_snapshot.session_count = 28;
  west_snapshot.inflight_count = 7;
  west_snapshot.draining = false;
  cluster_catalog.update_controller_snapshot(west_snapshot);
  atorbit::dsm::topology::ds_snapshot_record_t west_ds_records[1] = {};
  west_ds_records[0].dsa_id = 7101;
  west_ds_records[0].ds_id = 9001;
  west_ds_records[0].owner_unique_id = 9627;
  west_ds_records[0].running_state = 2;
  west_ds_records[0].exit_reason = 11;
  west_ds_records[0].last_active_millis = 2001;
  cluster_catalog.replace_controller_ds_inventory(6001, west_ds_records, 1);

  request.set_request(0, "region-cn-east");

  auto result = atorbit::dsm::service::logic::action::run_task_action_query_inventory(
      runtime, rpc_context, request, cluster_catalog);

  if (result != 0) {
    return -1;
  }

  if (!rpc_context.query_inventory_response_written) {
    return -2;
  }

  if (rpc_context.queried_region != request.region()) {
    return -3;
  }

  if (rpc_context.queried_controller_count != 2) {
    return -4;
  }

  if (rpc_context.queried_agent_count != 5) {
    return -5;
  }

  if (rpc_context.queried_ds_count != 27) {
    return -6;
  }

  if (rpc_context.queried_session_count != 58) {
    return -7;
  }

  if (rpc_context.queried_inflight_count != 8) {
    return -8;
  }

  if (rpc_context.queried_draining_controller_count != 1) {
    return -9;
  }

  if (rpc_context.queried_detail_count != 2) {
    return -10;
  }

  if (rpc_context.queried_detail_truncated) {
    return -11;
  }

  if (rpc_context.queried_controller_id[0] != 5001) {
    return -12;
  }

  if (rpc_context.queried_controller_region[0] != request.region()) {
    return -13;
  }

  if (rpc_context.queried_controller_addr[0] != east_controller_a.controller_addr) {
    return -14;
  }

  if (rpc_context.queried_controller_agent_count[0] != 3) {
    return -15;
  }

  if (rpc_context.queried_controller_ds_count[0] != 18) {
    return -16;
  }

  if (rpc_context.queried_controller_session_count[0] != 42) {
    return -17;
  }

  if (rpc_context.queried_controller_inflight_count[0] != 5) {
    return -18;
  }

  if (!rpc_context.queried_controller_draining[0]) {
    return -19;
  }

  if (rpc_context.queried_controller_id[1] != 5002) {
    return -20;
  }

  if (rpc_context.queried_controller_addr[1] != east_controller_b.controller_addr) {
    return -21;
  }

  if (rpc_context.queried_controller_draining[1]) {
    return -22;
  }

  if (rpc_context.queried_ds_detail_count != 3) {
    return -23;
  }

  if (rpc_context.queried_ds_detail_truncated) {
    return -24;
  }

  if (rpc_context.queried_ds_controller_id[0] != 5001) {
    return -25;
  }

  if (rpc_context.queried_ds_region[0] != request.region()) {
    return -26;
  }

  if (rpc_context.queried_ds_dsa_id[0] != 7001) {
    return -27;
  }

  if (rpc_context.queried_ds_id[0] != 8001) {
    return -28;
  }

  if (rpc_context.queried_ds_owner_unique_id[0] != 9527) {
    return -29;
  }

  if (rpc_context.queried_ds_running_state[0] != 1) {
    return -30;
  }

  if (rpc_context.queried_ds_last_active_millis[2] != 1011) {
    return -31;
  }

  return 0;
}

static int test_task_action_query_inventory_rejects_unknown_controller_filter() {
  atorbit::dsm::service::app::runtime_handle_t runtime = 0;
  atorbit::dsm::service::app::rpc_context_t rpc_context;
  atorbit::dsm::service::app::QueryInventoryReq request;
  atorbit::dsm::topology::cluster_catalog cluster_catalog;

  request.set_request(5001, nullptr);

  auto result = atorbit::dsm::service::logic::action::run_task_action_query_inventory(
      runtime, rpc_context, request, cluster_catalog);

  if (result != -3) {
    return -1;
  }

  return 0;
}

static int test_task_action_query_inventory_returns_single_controller_detail() {
  atorbit::dsm::service::app::runtime_handle_t runtime = 0;
  atorbit::dsm::service::app::rpc_context_t rpc_context;
  atorbit::dsm::service::app::QueryInventoryReq request;
  atorbit::dsm::topology::cluster_catalog cluster_catalog;

  atorbit::dsm::topology::controller_registration_t controller;
  controller.controller_id = 5001;
  controller.region = "region-cn-east";
  controller.controller_addr = "dsc://region-cn-east/controller-a";
  cluster_catalog.upsert_controller(controller);

  atorbit::dsm::topology::controller_snapshot_t snapshot;
  snapshot.controller_id = 5001;
  snapshot.agent_count = 3;
  snapshot.ds_count = 18;
  snapshot.session_count = 42;
  snapshot.inflight_count = 5;
  snapshot.draining = true;
  cluster_catalog.update_controller_snapshot(snapshot);
  atorbit::dsm::topology::ds_snapshot_record_t ds_records[2] = {};
  ds_records[0].dsa_id = 7001;
  ds_records[0].ds_id = 8001;
  ds_records[0].owner_unique_id = 9527;
  ds_records[0].running_state = 1;
  ds_records[0].exit_reason = 0;
  ds_records[0].last_active_millis = 1001;
  ds_records[1].dsa_id = 7001;
  ds_records[1].ds_id = 8002;
  ds_records[1].owner_unique_id = 9528;
  ds_records[1].running_state = 2;
  ds_records[1].exit_reason = 5;
  ds_records[1].last_active_millis = 1002;
  cluster_catalog.replace_controller_ds_inventory(5001, ds_records, 2);

  request.set_request(5001, "region-cn-east");

  auto result = atorbit::dsm::service::logic::action::run_task_action_query_inventory(
      runtime, rpc_context, request, cluster_catalog);

  if (result != 0) {
    return -1;
  }

  if (rpc_context.queried_controller_count != 1) {
    return -2;
  }

  if (rpc_context.queried_detail_count != 1) {
    return -3;
  }

  if (rpc_context.queried_controller_id[0] != 5001) {
    return -4;
  }

  if (rpc_context.queried_controller_ds_count[0] != 18) {
    return -5;
  }

  if (!rpc_context.queried_controller_draining[0]) {
    return -6;
  }

  if (rpc_context.queried_ds_detail_count != 2) {
    return -7;
  }

  if (rpc_context.queried_ds_id[1] != 8002) {
    return -8;
  }

  if (rpc_context.queried_ds_exit_reason[1] != 5) {
    return -9;
  }

  return 0;
}
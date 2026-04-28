#include "task_action_report_controller_snapshot.pseudo.h"

CASE_TEST(task_action_report_controller_snapshot, run_task_action_report_controller_snapshot_updates_catalog_state) {
  atorbit::dsm::service::app::runtime_handle_t runtime = 0;
  atorbit::dsm::service::app::rpc_context_t rpc_context;
  atorbit::dsm::service::app::RegisterControllerReq register_request;
  atorbit::dsm::service::app::ReportControllerSnapshotReq snapshot_request;
  atorbit::dsm::topology::cluster_catalog cluster_catalog;

  register_request.set_request(5001, "region-cn-east", "dsc://region-cn-east/controller-a");
  atorbit::dsm::topology::controller_registration_t registration;
  registration.controller_id = register_request.controller_id();
  registration.region = register_request.region();
  registration.controller_addr = register_request.controller_addr();
  cluster_catalog.upsert_controller(registration);

  snapshot_request.set_request(5001, 3, 18, 42, 5, true);
  snapshot_request.append_ds_detail(7001, 8001, 9527, 1, 0, 1001);
  snapshot_request.append_ds_detail(7001, 8002, 9528, 1, 0, 1002);

  auto result = atorbit::dsm::service::logic::action::run_task_action_report_controller_snapshot(
      runtime, rpc_context, snapshot_request, cluster_catalog);
  atorbit::dsm::topology::inventory_query_t query;
  atorbit::dsm::topology::ds_record_t ds_records[4] = {};

  query.has_controller_filter = true;
  query.controller_id = 5001;
  auto ds_count = cluster_catalog.list_ds_by_query(query, ds_records, 4);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(3, cluster_catalog.get_controller_agent_count(5001));
  CASE_EXPECT_EQ(18, cluster_catalog.get_controller_ds_count(5001));
  CASE_EXPECT_EQ(42, cluster_catalog.get_controller_session_count(5001));
  CASE_EXPECT_EQ(5, cluster_catalog.get_controller_inflight_count(5001));
  CASE_EXPECT_TRUE(cluster_catalog.is_controller_draining(5001));
  CASE_EXPECT_TRUE(rpc_context.report_controller_snapshot_response_written);
  CASE_EXPECT_EQ(5001, rpc_context.reported_controller_id);
  CASE_EXPECT_EQ(18, rpc_context.reported_ds_count);
  CASE_EXPECT_TRUE(rpc_context.reported_draining);
  CASE_EXPECT_EQ(2, ds_count);
  CASE_EXPECT_EQ(7001, ds_records[0].dsa_id);
  CASE_EXPECT_EQ(8001, ds_records[0].ds_id);
  CASE_EXPECT_EQ(9527, ds_records[0].owner_unique_id);
}

CASE_TEST(task_action_report_controller_snapshot, run_task_action_report_controller_snapshot_rejects_unknown_controller) {
  atorbit::dsm::service::app::runtime_handle_t runtime = 0;
  atorbit::dsm::service::app::rpc_context_t rpc_context;
  atorbit::dsm::service::app::ReportControllerSnapshotReq snapshot_request;
  atorbit::dsm::topology::cluster_catalog cluster_catalog;

  snapshot_request.set_request(5001, 3, 18, 42, 5, false);

  auto result = atorbit::dsm::service::logic::action::run_task_action_report_controller_snapshot(
      runtime, rpc_context, snapshot_request, cluster_catalog);

  CASE_EXPECT_EQ(-3, result);
}
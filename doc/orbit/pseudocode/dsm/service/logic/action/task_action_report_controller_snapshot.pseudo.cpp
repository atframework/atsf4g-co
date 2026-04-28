#include "task_action_report_controller_snapshot.pseudo.h"

namespace atorbit {
namespace dsm {
namespace service {
namespace logic {
namespace action {

namespace {

static app::rpc_result_t write_report_controller_snapshot_response(
    app::rpc_context_t& rpc_context,
    const topology::controller_snapshot_t& snapshot) {
  rpc_context.report_controller_snapshot_response_written = true;
  rpc_context.reported_controller_id = snapshot.controller_id;
  rpc_context.reported_ds_count = snapshot.ds_count;
  rpc_context.reported_draining = snapshot.draining;
  return 0;
}

}  // namespace

app::rpc_result_t run_task_action_report_controller_snapshot(app::runtime_handle_t& runtime,
                                                             app::rpc_context_t& rpc_context,
                                                             const app::ReportControllerSnapshotReq& request,
                                                             topology::cluster_catalog& cluster_catalog) {
  (void)runtime;
  if (!request.has_snapshot() || 0 == request.controller_id()) {
    app::write_invalid_request_log(rpc_context);
    return app::build_rpc_error(app::ERROR_CODE_INVALID_ARGUMENT);
  }

  topology::controller_snapshot_t snapshot;
  snapshot.controller_id = request.controller_id();
  snapshot.agent_count = request.agent_count();
  snapshot.ds_count = request.ds_count();
  snapshot.session_count = request.session_count();
  snapshot.inflight_count = request.inflight_count();
  snapshot.draining = request.draining();

  topology::ds_snapshot_record_t ds_records[app::REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT] = {};
  for (unsigned long long index = 0; index < request.ds_detail_count(); ++index) {
    ds_records[index].dsa_id = request.ds_detail_dsa_id(index);
    ds_records[index].ds_id = request.ds_detail_ds_id(index);
    ds_records[index].owner_unique_id = request.ds_detail_owner_unique_id(index);
    ds_records[index].running_state = request.ds_detail_running_state(index);
    ds_records[index].exit_reason = request.ds_detail_exit_reason(index);
    ds_records[index].last_active_millis = request.ds_detail_last_active_millis(index);
  }

  auto catalog_result = cluster_catalog.update_controller_snapshot(snapshot);
  if (catalog_result != 0) {
    return app::build_rpc_error(catalog_result);
  }

  auto ds_inventory_result = cluster_catalog.replace_controller_ds_inventory(
      snapshot.controller_id, ds_records, request.ds_detail_count());
  if (ds_inventory_result != 0) {
    return app::build_rpc_error(ds_inventory_result);
  }

  return write_report_controller_snapshot_response(rpc_context, snapshot);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsm
}  // namespace atorbit
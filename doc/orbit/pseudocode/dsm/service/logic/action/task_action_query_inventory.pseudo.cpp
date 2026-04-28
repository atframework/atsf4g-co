#include "task_action_query_inventory.pseudo.h"

namespace atorbit {
namespace dsm {
namespace service {
namespace logic {
namespace action {

namespace {

static app::rpc_result_t write_query_inventory_response(app::rpc_context_t& rpc_context,
                                                        const topology::inventory_summary_t& summary) {
  rpc_context.query_inventory_response_written = true;
  rpc_context.queried_region = summary.region;
  rpc_context.queried_controller_count = summary.controller_count;
  rpc_context.queried_agent_count = summary.agent_count;
  rpc_context.queried_ds_count = summary.ds_count;
  rpc_context.queried_session_count = summary.session_count;
  rpc_context.queried_inflight_count = summary.inflight_count;
  rpc_context.queried_draining_controller_count = summary.draining_controller_count;
  return 0;
}

static void write_query_inventory_details(app::rpc_context_t& rpc_context,
                                          const topology::controller_record_t detail_records[],
                                          unsigned long long detail_count,
                                          unsigned long long total_controller_count) {
  rpc_context.queried_detail_count = detail_count;
  rpc_context.queried_detail_truncated = total_controller_count > detail_count;

  for (unsigned long long index = 0; index < detail_count; ++index) {
    rpc_context.queried_controller_id[index] = detail_records[index].controller_id;
    rpc_context.queried_controller_region[index] = detail_records[index].region;
    rpc_context.queried_controller_addr[index] = detail_records[index].controller_addr;
    rpc_context.queried_controller_agent_count[index] = detail_records[index].current_agent_count;
    rpc_context.queried_controller_ds_count[index] = detail_records[index].current_ds_count;
    rpc_context.queried_controller_session_count[index] = detail_records[index].current_session_count;
    rpc_context.queried_controller_inflight_count[index] = detail_records[index].current_inflight_count;
    rpc_context.queried_controller_draining[index] = detail_records[index].draining;
  }
}

static void write_query_inventory_ds_details(app::rpc_context_t& rpc_context,
                                             const topology::ds_record_t detail_records[],
                                             unsigned long long detail_count,
                                             unsigned long long total_ds_count) {
  rpc_context.queried_ds_detail_count = detail_count;
  rpc_context.queried_ds_detail_truncated = total_ds_count > detail_count;

  for (unsigned long long index = 0; index < detail_count; ++index) {
    rpc_context.queried_ds_controller_id[index] = detail_records[index].controller_id;
    rpc_context.queried_ds_region[index] = detail_records[index].region;
    rpc_context.queried_ds_dsa_id[index] = detail_records[index].dsa_id;
    rpc_context.queried_ds_id[index] = detail_records[index].ds_id;
    rpc_context.queried_ds_owner_unique_id[index] = detail_records[index].owner_unique_id;
    rpc_context.queried_ds_running_state[index] = detail_records[index].running_state;
    rpc_context.queried_ds_exit_reason[index] = detail_records[index].exit_reason;
    rpc_context.queried_ds_last_active_millis[index] = detail_records[index].last_active_millis;
  }
}

}  // namespace

app::rpc_result_t run_task_action_query_inventory(app::runtime_handle_t& runtime,
                                                  app::rpc_context_t& rpc_context,
                                                  const app::QueryInventoryReq& request,
                                                  const topology::cluster_catalog& cluster_catalog) {
  (void)runtime;

  topology::inventory_query_t query;
  query.has_region_filter = request.has_region_filter();
  query.region = request.region();
  query.has_controller_filter = request.has_controller_filter();
  query.controller_id = request.controller_id();

  topology::inventory_summary_t summary;
  auto catalog_result = cluster_catalog.build_inventory_summary(query, summary);
  if (catalog_result != 0) {
    return app::build_rpc_error(catalog_result);
  }

  topology::controller_record_t detail_records[app::QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  auto detail_count = cluster_catalog.list_controllers_by_query(
      query, detail_records, app::QUERY_INVENTORY_CONTROLLER_LIMIT);
  topology::ds_record_t ds_detail_records[app::QUERY_INVENTORY_DS_LIMIT] = {};
  auto total_ds_detail_count = cluster_catalog.count_ds_by_query(query);
  auto ds_detail_count = cluster_catalog.list_ds_by_query(query, ds_detail_records, app::QUERY_INVENTORY_DS_LIMIT);

  auto response_result = write_query_inventory_response(rpc_context, summary);
  write_query_inventory_details(rpc_context, detail_records, detail_count, summary.controller_count);
  write_query_inventory_ds_details(rpc_context, ds_detail_records, ds_detail_count, total_ds_detail_count);
  return response_result;
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsm
}  // namespace atorbit
#include "handle_ss_rpc_managerservice.pseudo.h"

namespace atorbit {
namespace dsm {
namespace service {
namespace app {

int handle_ss_rpc_managerservice::register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime) {
  manager_facade_ = create_managerservice_facade(runtime);

  bind_rpc_handler(dispatcher, "ManagerService.RegisterController");
  bind_rpc_handler(dispatcher, "ManagerService.ReportControllerSnapshot");
  bind_rpc_handler(dispatcher, "ManagerService.QueryInventory");
  bind_rpc_handler(dispatcher, "ManagerService.StopDedicatedServer");
  bind_rpc_handler(dispatcher, "ManagerService.DrainController");
  bind_rpc_handler(dispatcher, "ManagerService.DrainRegion");
  bind_rpc_handler(dispatcher, "ManagerService.ApplyRoutingPlan");
  return 0;
}

rpc_result_t handle_ss_rpc_managerservice::handle_register_controller(rpc_context_t& rpc_context,
                                                                      const RegisterControllerReq& request) {
  if (!request.has_controller() || 0 == request.controller_id() || nullptr == request.region()) {
    return reject_invalid_request(rpc_context);
  }

  return manager_facade_.register_controller(rpc_context, request);
}

rpc_result_t handle_ss_rpc_managerservice::handle_report_controller_snapshot(
    rpc_context_t& rpc_context,
    const ReportControllerSnapshotReq& request) {
  if (!request.has_snapshot() || 0 == request.controller_id()) {
    return reject_invalid_request(rpc_context);
  }

  return manager_facade_.report_controller_snapshot(rpc_context, request);
}

rpc_result_t handle_ss_rpc_managerservice::handle_query_inventory(rpc_context_t& rpc_context,
                                                                  const QueryInventoryReq& request) {
  return manager_facade_.query_inventory(rpc_context, request);
}

rpc_result_t handle_ss_rpc_managerservice::handle_stop_dedicated_server(rpc_context_t& rpc_context,
                                                                         const StopDedicatedServerReq& request) {
  return manager_facade_.stop_dedicated_server(rpc_context, request);
}

rpc_result_t handle_ss_rpc_managerservice::handle_drain_controller(rpc_context_t& rpc_context,
                                                                    const DrainControllerReq& request) {
  return manager_facade_.drain_controller(rpc_context, request);
}

rpc_result_t handle_ss_rpc_managerservice::handle_drain_region(rpc_context_t& rpc_context,
                                                                const DrainRegionReq& request) {
  return manager_facade_.drain_region(rpc_context, request);
}

rpc_result_t handle_ss_rpc_managerservice::handle_apply_routing_plan(rpc_context_t& rpc_context,
                                                                      const ApplyRoutingPlanReq& request) {
  return manager_facade_.apply_routing_plan(rpc_context, request);
}

rpc_result_t handle_ss_rpc_managerservice::reject_invalid_request(rpc_context_t& rpc_context) {
  write_invalid_request_log(rpc_context);
  return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
}

}  // namespace app
}  // namespace service
}  // namespace dsm
}  // namespace atorbit
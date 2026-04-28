#include "managerservice.pseudo.h"

namespace atorbit {
namespace dsm {
namespace service {
namespace rpc {

managerservice_facade::managerservice_facade(app::runtime_handle_t& runtime)
    : runtime_(&runtime), task_router_(create_task_router(runtime)) {}

app::rpc_result_t managerservice_facade::register_controller(app::rpc_context_t& rpc_context,
                                                             const app::RegisterControllerReq& request) {
  auto task = task_router_.build("task_action_register_controller", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

app::rpc_result_t managerservice_facade::report_controller_snapshot(app::rpc_context_t& rpc_context,
                                                                    const app::ReportControllerSnapshotReq& request) {
  auto task = task_router_.build("task_action_report_controller_snapshot", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

app::rpc_result_t managerservice_facade::query_inventory(app::rpc_context_t& rpc_context,
                                                         const app::QueryInventoryReq& request) {
  auto task = task_router_.build("task_action_query_inventory", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

app::rpc_result_t managerservice_facade::stop_dedicated_server(app::rpc_context_t& rpc_context,
                                                                const app::StopDedicatedServerReq& request) {
  auto task = task_router_.build("task_action_stop_dedicated_server", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

app::rpc_result_t managerservice_facade::drain_controller(app::rpc_context_t& rpc_context,
                                                           const app::DrainControllerReq& request) {
  auto task = task_router_.build("task_action_drain_controller", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

app::rpc_result_t managerservice_facade::drain_region(app::rpc_context_t& rpc_context,
                                                       const app::DrainRegionReq& request) {
  auto task = task_router_.build("task_action_drain_region", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

app::rpc_result_t managerservice_facade::apply_routing_plan(app::rpc_context_t& rpc_context,
                                                             const app::ApplyRoutingPlanReq& request) {
  auto task = task_router_.build("task_action_apply_routing_plan", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

}  // namespace rpc
}  // namespace service
}  // namespace dsm
}  // namespace atorbit
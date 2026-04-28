#include "controllerservice.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace rpc {

controllerservice_facade::controllerservice_facade(runtime_handle_t& runtime)
    : runtime_(&runtime), task_router_(create_task_router(runtime)) {}

rpc_result_t controllerservice_facade::register_agent(rpc_context_t& rpc_context, const RegisterAgentReq& request) {
  auto task = task_router_.build("task_action_register_agent", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t controllerservice_facade::heartbeat_agent(rpc_context_t& rpc_context, const HeartbeatAgentReq& request) {
  auto task = task_router_.build("task_action_heartbeat_agent", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t controllerservice_facade::report_agent_load(rpc_context_t& rpc_context, const ReportAgentLoadReq& request) {
  auto task = task_router_.build("task_action_report_agent_load", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t controllerservice_facade::notify_ds_started(rpc_context_t& rpc_context, const NotifyDSStartedReq& request) {
  auto task = task_router_.build("task_action_notify_ds_started", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t controllerservice_facade::notify_ds_exit(rpc_context_t& rpc_context, const NotifyDSExitReq& request) {
  auto task = task_router_.build("task_action_notify_ds_exit", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t controllerservice_facade::forward_from_ds(rpc_context_t& rpc_context, const ForwardFromDSReq& request) {
  auto task = task_router_.build("task_action_handle_upstream_message", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t controllerservice_facade::ack_downstream(rpc_context_t& rpc_context, const AckDownstreamReq& request) {
  auto task = task_router_.build("task_action_ack_downstream", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t controllerservice_facade::reconcile_agent_state(rpc_context_t& rpc_context, const ReconcileAgentStateReq& request) {
  auto task = task_router_.build("task_action_reconcile_agent_state", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

}  // namespace rpc
}  // namespace service
}  // namespace dsc
}  // namespace atorbit

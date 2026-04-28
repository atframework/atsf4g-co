#include "handle_ss_rpc_controllerservice.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace app {

int handle_ss_rpc_controllerservice::register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime) {
  controller_facade_ = create_controllerservice_facade(runtime);

  bind_rpc_handler(dispatcher, "ControllerService.RegisterAgent");
  bind_rpc_handler(dispatcher, "ControllerService.HeartbeatAgent");
  bind_rpc_handler(dispatcher, "ControllerService.ReportAgentLoad");
  bind_rpc_handler(dispatcher, "ControllerService.NotifyDSStarted");
  bind_rpc_handler(dispatcher, "ControllerService.NotifyDSExit");
  bind_rpc_handler(dispatcher, "ControllerService.ForwardFromDS");
  bind_rpc_handler(dispatcher, "ControllerService.AckDownstream");
  bind_rpc_handler(dispatcher, "ControllerService.ReconcileAgentState");
  return 0;
}

rpc_result_t handle_ss_rpc_controllerservice::handle_register_agent(rpc_context_t& rpc_context, const RegisterAgentReq& request) {
  if (!request.has_agent()) {
    return reject_invalid_request(rpc_context);
  }

  return controller_facade_.register_agent(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::handle_heartbeat_agent(rpc_context_t& rpc_context, const HeartbeatAgentReq& request) {
  if (!request.has_agent()) {
    return reject_invalid_request(rpc_context);
  }

  return controller_facade_.heartbeat_agent(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::handle_report_agent_load(rpc_context_t& rpc_context, const ReportAgentLoadReq& request) {
  if (!request.has_agent()) {
    return reject_invalid_request(rpc_context);
  }

  return controller_facade_.report_agent_load(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::handle_notify_ds_started(rpc_context_t& rpc_context, const NotifyDSStartedReq& request) {
  return controller_facade_.notify_ds_started(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::handle_notify_ds_exit(rpc_context_t& rpc_context, const NotifyDSExitReq& request) {
  if (!request.has_ds()) {
    return reject_invalid_request(rpc_context);
  }

  return controller_facade_.notify_ds_exit(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::handle_forward_from_ds(rpc_context_t& rpc_context, const ForwardFromDSReq& request) {
  if (!request.has_ds() || 0 == request.ack_seq() || nullptr == request.payload()) {
    return reject_invalid_request(rpc_context);
  }

  return controller_facade_.forward_from_ds(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::handle_ack_downstream(rpc_context_t& rpc_context, const AckDownstreamReq& request) {
  return controller_facade_.ack_downstream(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::handle_reconcile_agent_state(rpc_context_t& rpc_context, const ReconcileAgentStateReq& request) {
  return controller_facade_.reconcile_agent_state(rpc_context, request);
}

rpc_result_t handle_ss_rpc_controllerservice::reject_invalid_request(rpc_context_t& rpc_context) {
  // 记录参数错误，并返回统一失败码
  write_invalid_request_log(rpc_context);
  return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
}

}  // namespace app
}  // namespace service
}  // namespace dsc
}  // namespace atorbit

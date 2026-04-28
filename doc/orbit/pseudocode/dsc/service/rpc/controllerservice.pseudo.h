#pragma once

// Phase 1
// 目标: 固定 ControllerService facade 与未来 task_action 的桥接关系。
// 未来真实落点: src/dsc/service/rpc/controller/controllerservice.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace rpc {

class controllerservice_facade {
public:
  explicit controllerservice_facade(runtime_handle_t& runtime);

  rpc_result_t register_agent(rpc_context_t& rpc_context, const RegisterAgentReq& request);
  rpc_result_t heartbeat_agent(rpc_context_t& rpc_context, const HeartbeatAgentReq& request);
  rpc_result_t report_agent_load(rpc_context_t& rpc_context, const ReportAgentLoadReq& request);
  rpc_result_t notify_ds_started(rpc_context_t& rpc_context, const NotifyDSStartedReq& request);
  rpc_result_t notify_ds_exit(rpc_context_t& rpc_context, const NotifyDSExitReq& request);
  rpc_result_t forward_from_ds(rpc_context_t& rpc_context, const ForwardFromDSReq& request);
  rpc_result_t ack_downstream(rpc_context_t& rpc_context, const AckDownstreamReq& request);
  rpc_result_t reconcile_agent_state(rpc_context_t& rpc_context, const ReconcileAgentStateReq& request);

private:
  runtime_handle_t* runtime_ = nullptr;
  task_router_t task_router_;
};

}  // namespace rpc
}  // namespace service
}  // namespace dsc
}  // namespace atorbit

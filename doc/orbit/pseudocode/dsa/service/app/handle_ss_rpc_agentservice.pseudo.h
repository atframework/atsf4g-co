#pragma once

// Phase 1
// 目标: 固定 AgentService 在 DSA 侧的入站注册和分发入口。
// 未来真实落点: src/dsa/service/app/handle_ss_rpc_agentservice.cpp

namespace atorbit {
namespace dsa {
namespace service {
namespace app {

class handle_ss_rpc_agentservice {
public:
  int register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime);

  rpc_result_t handle_start_ds(rpc_context_t& rpc_context, const StartDSReq& request);
  rpc_result_t handle_stop_ds(rpc_context_t& rpc_context, const StopDSReq& request);
  rpc_result_t handle_query_ds_state(rpc_context_t& rpc_context, const QueryDSStateReq& request);
  rpc_result_t handle_drain_agent(rpc_context_t& rpc_context, const DrainAgentReq& request);
  rpc_result_t handle_forward_to_ds(rpc_context_t& rpc_context, const ForwardToDSReq& request);
  rpc_result_t handle_ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request);

private:
  rpc_result_t reject_invalid_request(rpc_context_t& rpc_context);

private:
  agentservice_facade_t agent_facade_;
};

}  // namespace app
}  // namespace service
}  // namespace dsa
}  // namespace atorbit

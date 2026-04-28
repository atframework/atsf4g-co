#include "handle_ss_rpc_agentservice.pseudo.h"

namespace atorbit {
namespace dsa {
namespace service {
namespace app {

int handle_ss_rpc_agentservice::register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime) {
  agent_facade_ = create_agentservice_facade(runtime);

  dispatcher.bind("AgentService.StartDS", this, &handle_ss_rpc_agentservice::handle_start_ds);
  dispatcher.bind("AgentService.StopDS", this, &handle_ss_rpc_agentservice::handle_stop_ds);
  dispatcher.bind("AgentService.QueryDSState", this, &handle_ss_rpc_agentservice::handle_query_ds_state);
  dispatcher.bind("AgentService.DrainAgent", this, &handle_ss_rpc_agentservice::handle_drain_agent);
  dispatcher.bind("AgentService.ForwardToDS", this, &handle_ss_rpc_agentservice::handle_forward_to_ds);
  dispatcher.bind("AgentService.AckUpstream", this, &handle_ss_rpc_agentservice::handle_ack_upstream);
  return 0;
}

rpc_result_t handle_ss_rpc_agentservice::handle_start_ds(rpc_context_t& rpc_context, const StartDSReq& request) {
  if (!request.has_meta()) {
    return reject_invalid_request(rpc_context);
  }

  return agent_facade_.start_ds(rpc_context, request);
}

rpc_result_t handle_ss_rpc_agentservice::handle_stop_ds(rpc_context_t& rpc_context, const StopDSReq& request) {
  if (!request.has_ds()) {
    return reject_invalid_request(rpc_context);
  }

  return agent_facade_.stop_ds(rpc_context, request);
}

rpc_result_t handle_ss_rpc_agentservice::handle_query_ds_state(rpc_context_t& rpc_context, const QueryDSStateReq& request) {
  return agent_facade_.query_ds_state(rpc_context, request);
}

rpc_result_t handle_ss_rpc_agentservice::handle_drain_agent(rpc_context_t& rpc_context, const DrainAgentReq& request) {
  return agent_facade_.drain_agent(rpc_context, request);
}

rpc_result_t handle_ss_rpc_agentservice::handle_forward_to_ds(rpc_context_t& rpc_context, const ForwardToDSReq& request) {
  return agent_facade_.forward_to_ds(rpc_context, request);
}

rpc_result_t handle_ss_rpc_agentservice::handle_ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request) {
  return agent_facade_.ack_upstream(rpc_context, request);
}

rpc_result_t handle_ss_rpc_agentservice::reject_invalid_request(rpc_context_t& rpc_context) {
  // 记录参数错误，并返回统一失败码
  write_invalid_request_log(rpc_context);
  return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
}

}  // namespace app
}  // namespace service
}  // namespace dsa
}  // namespace atorbit

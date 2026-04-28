#include "agentservice.pseudo.h"

namespace atorbit {
namespace dsa {
namespace service {
namespace rpc {

agentservice_facade::agentservice_facade(runtime_handle_t& runtime)
		: runtime_(&runtime), task_router_(create_task_router(runtime)) {}

rpc_result_t agentservice_facade::start_ds(rpc_context_t& rpc_context, const StartDSReq& request) {
	auto task = task_router_.build("task_action_start_ds", rpc_context, request);
	submit_task(task);
	return build_pending_response(rpc_context);
}

rpc_result_t agentservice_facade::stop_ds(rpc_context_t& rpc_context, const StopDSReq& request) {
	auto task = task_router_.build("task_action_stop_ds", rpc_context, request);
	submit_task(task);
	return build_pending_response(rpc_context);
}

rpc_result_t agentservice_facade::query_ds_state(rpc_context_t& rpc_context, const QueryDSStateReq& request) {
	auto task = task_router_.build("task_action_query_ds_state", rpc_context, request);
	submit_task(task);
	return build_pending_response(rpc_context);
}

rpc_result_t agentservice_facade::drain_agent(rpc_context_t& rpc_context, const DrainAgentReq& request) {
	auto task = task_router_.build("task_action_query_ds_state", rpc_context, request);
	submit_task(task);
	return build_accept_response(rpc_context);
}

rpc_result_t agentservice_facade::forward_to_ds(rpc_context_t& rpc_context, const ForwardToDSReq& request) {
	auto task = task_router_.build("task_action_forward_to_ds", rpc_context, request);
	submit_task(task);
	return build_accept_response(rpc_context);
}

rpc_result_t agentservice_facade::ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request) {
	auto task = task_router_.build("task_action_ack_upstream", rpc_context, request);
	submit_task(task);
	return build_accept_response(rpc_context);
}

}  // namespace rpc
}  // namespace service
}  // namespace dsa
}  // namespace atorbit

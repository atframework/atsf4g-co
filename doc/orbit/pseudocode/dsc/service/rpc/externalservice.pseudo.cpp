#include "externalservice.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace rpc {

externalservice_facade::externalservice_facade(runtime_handle_t& runtime)
    : runtime_(&runtime), task_router_(create_task_router(runtime)) {}

rpc_result_t externalservice_facade::connect_external(rpc_context_t& rpc_context, const ConnectExternalReq& request) {
  auto task = task_router_.build("task_action_connect_external", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t externalservice_facade::reconnect_external(rpc_context_t& rpc_context, const ReconnectExternalReq& request) {
  auto task = task_router_.build("task_action_reconnect_external", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t externalservice_facade::launch_dedicated_server(rpc_context_t& rpc_context, const LaunchDedicatedServerReq& request) {
  auto task = task_router_.build("task_action_launch_dedicated_server", rpc_context, request);
  submit_task(task);
  return build_pending_response(rpc_context);
}

rpc_result_t externalservice_facade::send_to_ds(rpc_context_t& rpc_context, const SendToDSReq& request) {
  auto task = task_router_.build("task_action_send_to_ds", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t externalservice_facade::ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request) {
  auto task = task_router_.build("task_action_ack_upstream", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

rpc_result_t externalservice_facade::remove_session(rpc_context_t& rpc_context, const RemoveSessionReq& request) {
  auto task = task_router_.build("task_action_remove_session", rpc_context, request);
  submit_task(task);
  return build_accept_response(rpc_context);
}

}  // namespace rpc
}  // namespace service
}  // namespace dsc
}  // namespace atorbit

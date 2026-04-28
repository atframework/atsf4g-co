#include "handle_cs_rpc_externalservice.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace app {

int handle_cs_rpc_externalservice::register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime) {
  external_facade_ = create_externalservice_facade(runtime);

  bind_rpc_handler(dispatcher, "ExternalService.ConnectExternal");
  bind_rpc_handler(dispatcher, "ExternalService.ReconnectExternal");
  bind_rpc_handler(dispatcher, "ExternalService.LaunchDedicatedServer");
  bind_rpc_handler(dispatcher, "ExternalService.SendToDS");
  bind_rpc_handler(dispatcher, "ExternalService.AckUpstream");
  bind_rpc_handler(dispatcher, "ExternalService.RemoveSession");
  return 0;
}

rpc_result_t handle_cs_rpc_externalservice::handle_connect_external(rpc_context_t& rpc_context, const ConnectExternalReq& request) {
  if (!request.has_session()) {
    return reject_invalid_request(rpc_context);
  }

  return external_facade_.connect_external(rpc_context, request);
}

rpc_result_t handle_cs_rpc_externalservice::handle_reconnect_external(rpc_context_t& rpc_context, const ReconnectExternalReq& request) {
  if (!request.has_session()) {
    return reject_invalid_request(rpc_context);
  }

  return external_facade_.reconnect_external(rpc_context, request);
}

rpc_result_t handle_cs_rpc_externalservice::handle_launch_dedicated_server(rpc_context_t& rpc_context, const LaunchDedicatedServerReq& request) {
  if (!request.has_session() || nullptr == request.target_region() || request.expected_cpu() <= 0 ||
      request.expected_memory_mb() <= 0) {
    return reject_invalid_request(rpc_context);
  }

  return external_facade_.launch_dedicated_server(rpc_context, request);
}

rpc_result_t handle_cs_rpc_externalservice::handle_send_to_ds(rpc_context_t& rpc_context, const SendToDSReq& request) {
  if (!request.has_session() || 0 == request.dsa_id() || 0 == request.ds_id() || nullptr == request.payload()) {
    return reject_invalid_request(rpc_context);
  }

  return external_facade_.send_to_ds(rpc_context, request);
}

rpc_result_t handle_cs_rpc_externalservice::handle_ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request) {
  return external_facade_.ack_upstream(rpc_context, request);
}

rpc_result_t handle_cs_rpc_externalservice::handle_remove_session(rpc_context_t& rpc_context, const RemoveSessionReq& request) {
  return external_facade_.remove_session(rpc_context, request);
}

rpc_result_t handle_cs_rpc_externalservice::reject_invalid_request(rpc_context_t& rpc_context) {
  // 记录参数错误，并返回统一失败码
  write_invalid_request_log(rpc_context);
  return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
}

}  // namespace app
}  // namespace service
}  // namespace dsc
}  // namespace atorbit

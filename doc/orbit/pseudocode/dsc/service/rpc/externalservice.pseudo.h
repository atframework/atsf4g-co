#pragma once

// Phase 1
// 目标: 固定 ExternalService facade 与未来 task_action 的桥接关系。
// 未来真实落点: src/dsc/service/rpc/external/externalservice.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace rpc {

class externalservice_facade {
public:
  explicit externalservice_facade(runtime_handle_t& runtime);

  rpc_result_t connect_external(rpc_context_t& rpc_context, const ConnectExternalReq& request);
  rpc_result_t reconnect_external(rpc_context_t& rpc_context, const ReconnectExternalReq& request);
  rpc_result_t launch_dedicated_server(rpc_context_t& rpc_context, const LaunchDedicatedServerReq& request);
  rpc_result_t send_to_ds(rpc_context_t& rpc_context, const SendToDSReq& request);
  rpc_result_t ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request);
  rpc_result_t remove_session(rpc_context_t& rpc_context, const RemoveSessionReq& request);

private:
  runtime_handle_t* runtime_ = nullptr;
  task_router_t task_router_;
};

}  // namespace rpc
}  // namespace service
}  // namespace dsc
}  // namespace atorbit

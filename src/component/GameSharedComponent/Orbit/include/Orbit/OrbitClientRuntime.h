// Copyright 2026 atframework

#pragma once

#include <Orbit/OrbitClientSdkTypes.h>

#include <atframe/atapp.h>

#include <design_pattern/singleton.h>

#include <chrono>
#include <memory>
#include <vector>

namespace google {
namespace protobuf {
class MessageLite;
class MethodDescriptor;
}  // namespace protobuf
}  // namespace google

namespace atframework {
class SSMsg;
}

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {

class ORBIT_CLIENT_SDK_API OrbitClientRuntime final : public util::design_pattern::singleton<OrbitClientRuntime> {
 public:
  using clock_type = std::chrono::steady_clock;

 public:
  OrbitClientRuntime();
  ~OrbitClientRuntime();

  int init(int argc, char* argv[], const OrbitClientCallbacks& callbacks);
  int init(uint64_t app_id, const OrbitClientOptions& options, const OrbitClientCallbacks& callbacks);
  void tick();

  // 进程已准备成功 可以通知Agent了
  bool notify_process_ready(const std::string& custom_data = std::string{});
  // 发送消息给Server
  bool send_to_server(const std::string& payload);
  // 请求停止服务
  bool request_end(orbit::EnClientExitReason reason, int32_t exit_code, const std::string& custom_data = std::string{});

 private:
  int extract_launch_options(int argc, char* argv[], uint64_t& app_id, OrbitClientOptions& options) const;
  void build_client_launch_arguments(uint64_t app_id, std::vector<std::string>& output) const;
  void install_app_callbacks();
  void restore_app_callbacks();
  int on_atapp_forward_request(::atframework::atapp::app& app,
                               const ::atframework::atapp::app::message_sender_t& source,
                               const ::atframework::atapp::app::message_t& msg);
  int on_atapp_forward_response(::atframework::atapp::app& app,
                                const ::atframework::atapp::app::message_sender_t& source,
                                const ::atframework::atapp::app::message_t& msg, int32_t error_code);
  int on_atapp_connected(::atframework::atapp::app& app, ::atbus::endpoint& ep, int status);
  int on_atapp_disconnected(::atframework::atapp::app& app, ::atbus::endpoint& ep, int status);
  bool should_handle_atapp_message(const ::atframework::atapp::app& app,
                                   const ::atframework::atapp::app::message_sender_t& source,
                                   const ::atframework::atapp::app::message_t& msg) const;

  void set_state(OrbitClientRuntimeState next_state);
  void log(OrbitClientLogLevel level, const std::string& message) const;

  bool connect();
  bool send_heartbeat(const OrbitClientLoadSnapshot& snapshot);
  void on_received_message(const std::string& message);

 private:
  bool send_stream_message(const ::google::protobuf::MessageLite& body,
                           const ::google::protobuf::MethodDescriptor& method);
  bool pack_stream_message(std::string& output, const ::google::protobuf::MethodDescriptor& method,
                           const ::google::protobuf::MessageLite& body);
  bool unpack_message(atframework::SSMsg& output, const std::string& message) const;
  bool dispatch_received_message(const atframework::SSMsg& message);
  uint64_t allocate_sequence();

  bool rpc_send_client_heartbeat(const orbit::DTAClientHeartbeatNotify& request);
  bool rpc_send_send_to_server(const orbit::DTASendToServerNotify& request);
  bool rpc_send_client_start(const orbit::DTAClientStartReq& request);
  bool rpc_send_client_exit(const orbit::DTAClientExitReq& request);
  bool rpc_receive_forward_to_client(const orbit::ATDForwardToClientNotify& request);
  bool rpc_receive_fork_seed_client(const orbit::ATDForkSeedClientNotify& request);

 private:
  std::unique_ptr<::atframework::atapp::app> app_;
  OrbitClientCallbacks callbacks_;
  OrbitClientOptions options_;
  OrbitClientRuntimeState state_;
  bool configured_;
  bool app_callbacks_installed_;
  uint64_t agent_bus_id_;
  uint64_t sequence_allocator_;
  clock_type::time_point last_heartbeat_timepoint_;
};

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END
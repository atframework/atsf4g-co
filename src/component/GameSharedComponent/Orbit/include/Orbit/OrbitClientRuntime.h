// Copyright 2026 atframework

#pragma once

#include <Orbit/OrbitClientSdkTypes.h>
#include <tbb/concurrent_queue.h>

#define UI UI_ST
#include <atframe/atapp.h>
#undef UI

#include <design_pattern/singleton.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>

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

class OrbitClientRuntime {
 public:
  using clock_type = std::chrono::steady_clock;

#if defined(ORBIT_CLIENT_SDK_DLL) && ORBIT_CLIENT_SDK_DLL
#  if defined(ORBIT_CLIENT_SDK_NATIVE) && ORBIT_CLIENT_SDK_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(OrbitClientRuntime)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(OrbitClientRuntime)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(OrbitClientRuntime)
#endif

 private:
  ORBIT_CLIENT_SDK_API OrbitClientRuntime();

 public:
  ORBIT_CLIENT_SDK_API virtual ~OrbitClientRuntime();

  ORBIT_CLIENT_SDK_API int init(int argc, char* argv[], bool io_thread, const OrbitClientCallbacks& callbacks);
  ORBIT_CLIENT_SDK_API int init(uint64_t app_id, const OrbitClientOptions& options,
                                const OrbitClientCallbacks& callbacks);
  ORBIT_CLIENT_SDK_API void tick();
  ORBIT_CLIENT_SDK_API void reset();

  ORBIT_CLIENT_SDK_API bool enabled() const;
  ORBIT_CLIENT_SDK_API bool enabled_io_thread() const;
  ORBIT_CLIENT_SDK_API void post_to_io_thread(std::function<void()> task);
  ORBIT_CLIENT_SDK_API void post_to_caller_thread(std::function<void()> task);

  // 是否种子进程
  ORBIT_CLIENT_SDK_API bool is_seed_process() const;
  // 种子进程准备成功
  ORBIT_CLIENT_SDK_API int32_t notify_seed_process_ready();
  // 种子进程等待Fork通知
  ORBIT_CLIENT_SDK_API int32_t blocking_seed_process();

  // 进程已准备成功 可以通知Agent了
  ORBIT_CLIENT_SDK_API int32_t notify_process_ready(const std::string& client_addr,
                                                    const std::string& custom_data = std::string{});
  // 这个接口会返回额外启动参数
  ORBIT_CLIENT_SDK_API const std::vector<std::string>& get_custom_launch_arguments() const;
  // 通过Key查找额外启动参数
  ORBIT_CLIENT_SDK_API const std::string& find_custom_launch_argument(const std::string& key) const;
  // 发送消息给Server
  ORBIT_CLIENT_SDK_API int32_t
  send_to_server(const std::string& payload, OrbitClientRpcCallback<orbit::ATDSendToServerRsp> callback = nullptr,
                 const OrbitClientRequestOptions& request_options = OrbitClientRequestOptions{});
  // 请求停止服务
  ORBIT_CLIENT_SDK_API int32_t request_end(orbit::EnClientExitReason reason, int32_t exit_code,
                                           const std::string& custom_data = std::string{});

  ORBIT_CLIENT_SDK_API void log(OrbitClientLogLevel level, const char* file_name, int line_number,
                                const std::string& message) const;
  ORBIT_CLIENT_SDK_API static std::string protobuf_mini_dumper_get_readable(const ::google::protobuf::Message& msg);

  using client_request_raw_callback_t = std::function<void(int32_t, const ::atframework::SSMsg&)>;

 private:
  void io_tick();

  int32_t notify_seed_process_ready_inner();
  int32_t notify_process_ready_inner(const std::string& client_addr, const std::string& custom_data);
  int32_t request_end_inner(orbit::EnClientExitReason reason, int32_t exit_code, const std::string& custom_data);

  int extract_launch_options(int argc, char* argv[], uint64_t& app_id, OrbitClientOptions& options);
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

  bool connect();
  int32_t send_heartbeat(const OrbitClientLoadSnapshot& snapshot);
  void on_received_message(const std::string& message);

  int32_t on_received_fork_request(const orbit::ATDForkSeedClientReq& request);
  void on_received_stop_request(const orbit::ATDStopClientReq& request);
  int32_t process_fork_request();

 private:
  struct pending_client_request_t {
    std::string packed_message;
    const ::google::protobuf::MethodDescriptor* method = nullptr;
    client_request_raw_callback_t callback;
    time_t timeout_second = 4;
    time_t deadline = 0;
    int32_t retry_times_left = 0;
    bool reliable = false;
  };

  int32_t send_request_message(const ::google::protobuf::MessageLite& body,
                               const ::google::protobuf::MethodDescriptor& method,
                               client_request_raw_callback_t callback,
                               const OrbitClientRequestOptions& request_options);
  int32_t send_response_message(const ::atframework::SSMsgHead& req_head, const ::google::protobuf::MessageLite& body,
                                const ::google::protobuf::MethodDescriptor& method);
  int32_t send_stream_message(const ::google::protobuf::MessageLite& body,
                              const ::google::protobuf::MethodDescriptor& method);
  int32_t pack_request_message(std::string& output, uint64_t& task_id,
                               const ::google::protobuf::MethodDescriptor& method,
                               const ::google::protobuf::MessageLite& body);
  int32_t pack_response_message(const ::atframework::SSMsgHead& req_head, std::string& output,
                                const ::google::protobuf::MethodDescriptor& method,
                                const ::google::protobuf::MessageLite& body);
  int32_t pack_stream_message(std::string& output, const ::google::protobuf::MethodDescriptor& method,
                              const ::google::protobuf::MessageLite& body);
  int32_t send_message(const std::string& packed_message, const google::protobuf::MethodDescriptor& method,
                       bool reliable = false, uint64_t task_id = 0);

  int32_t unpack_message(atframework::SSMsg& output, const std::string& message) const;
  int32_t dispatch_request_response(const atframework::SSMsg& message);
  int32_t dispatch_received_message(const atframework::SSMsg& message);
  bool retry_pending_request(uint64_t task_id, pending_client_request_t& pending, int32_t error_code,
                             const char* reason);
  void complete_pending_request(uint64_t task_id, int32_t error_code, const atframework::SSMsg* message);
  void reschedule_pending_request_timeout(uint64_t task_id, pending_client_request_t& pending);
  void execute_pending_request_timeouts();
  uint64_t allocate_sequence();
  void finalize_shutdown();
  OrbitClientLoadSnapshot make_default_load_snapshot();

  int32_t rpc_send_client_heartbeat(const orbit::DTAClientHeartbeatNotify& request);
  int32_t rpc_send_send_to_server(const orbit::DTASendToServerReq& request,
                                  OrbitClientRpcCallback<orbit::ATDSendToServerRsp> callback,
                                  const OrbitClientRequestOptions& request_options);
  int32_t rpc_send_client_start(const orbit::DTAClientStartReq& request,
                                OrbitClientRpcCallback<orbit::ATDClientStartRsp> callback,
                                const OrbitClientRequestOptions& request_options);
  int32_t rpc_send_client_exit(const orbit::DTAClientExitReq& request,
                               OrbitClientRpcCallback<orbit::ATDClientExitRsp> callback,
                               const OrbitClientRequestOptions& request_options);
  int32_t rpc_receive_forward_to_client(const ::atframework::SSMsgHead& req_head,
                                        orbit::ATDForwardToClientReq& request);
  int32_t rpc_receive_fork_seed_client(const ::atframework::SSMsgHead& req_head, orbit::ATDForkSeedClientReq& request);
  int32_t rpc_receive_stop_client(const ::atframework::SSMsgHead& req_head, orbit::ATDStopClientReq& request);

 private:
  std::unique_ptr<::atframework::atapp::app> app_;
  bool enabled_;
  OrbitClientCallbacks callbacks_;
  OrbitClientOptions options_;
  std::atomic<OrbitClientRuntimeState> state_;
  bool configured_;
  bool app_callbacks_installed_;
  uint64_t agent_bus_id_;
  uint64_t sequence_allocator_;
  time_t last_heartbeat_timepoint_;
  std::unordered_map<uint64_t, pending_client_request_t> pending_client_request_map_;
  std::multimap<time_t, uint64_t> pending_client_request_timeout_map_;

  std::atomic<bool> io_thread_running_;
  std::thread io_thread_;

  tbb::concurrent_queue<orbit::ATDForkSeedClientReq> pending_fork_requests_;
  tbb::concurrent_queue<std::function<void()>> io_task_;
  tbb::concurrent_queue<std::function<void()>> caller_task_;

  uv_rusage_t last_self_rusage_;
  std::chrono::steady_clock::time_point last_self_usage_sample_timepoint_;
  double last_self_cpu_used_ = 0.0;
  bool has_self_usage_sample_ = false;
};

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END
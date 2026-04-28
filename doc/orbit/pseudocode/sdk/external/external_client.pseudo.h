#pragma once

#include "external_event_stream.pseudo.h"

// Phase 4
// 目标: 描述 Service 侧 SDK 的公开接口，重点固化 LaunchDS ready 语义与 DS 断链回调。
// 未来真实落点: include/atorbit/sdk/external/external_client.h

namespace atorbit {
namespace sdk {
namespace external {

using result_code_t = int;
using unique_id_t = unsigned long long;

enum class connection_state_t {
  k_disconnected = 0,
  k_connected = 1,
  k_recovering = 2,
};

enum class ds_route_state_t {
  k_empty = 0,
  k_ready = 1,
  k_disconnected = 2,
  k_exited = 3,
};

enum class ds_disconnect_reason_t {
  k_transport_lost = 1,
  k_route_lost = 2,
  k_agent_disconnected = 3,
  k_manager_draining = 4,
};

struct launch_request_t {
  const char* target_region = nullptr;
  double expected_cpu = 0;
  double expected_memory_mb = 0;
  const char* custom_args[8] = {};
  unsigned long long custom_arg_count = 0;
};

struct launch_result_t {
  result_code_t result_code = 0;
  ds_identity_t ds;
  const char* client_addr = nullptr;
};

class external_client {
public:
  result_code_t connect(const char* dsc_region, unique_id_t unique_id);
  launch_result_t launch_dedicated_server(const launch_request_t& request);
  result_code_t send_to_ds(const ds_identity_t& ds, message_buffer_t payload);
  void set_ds_message_callback(ds_message_callback_t callback);
  // 由 SDK 内部 connector / notify loop 在链路断开时调用，业务层只订阅回调。
  result_code_t handle_transport_disconnected(int disconnect_reason);
  void set_ds_disconnect_callback(ds_disconnect_callback_t callback);
  void set_ds_exit_callback(ds_exit_callback_t callback);
  result_code_t disconnect();

private:
  struct owned_ds_route_t {
    ds_identity_t ds;
    const char* client_addr = nullptr;
    int route_state = 0;
    bool occupied = false;
  };

private:
  result_code_t ensure_connected() const;
  bool validate_launch_request(const launch_request_t& request) const;
  unsigned long long allocate_request_id();
  owned_ds_route_t* find_owned_ds(const ds_identity_t& ds);
  owned_ds_route_t* bind_ready_ds(const ds_identity_t& ds, const char* client_addr);
  void mark_all_routes_disconnected(int disconnect_reason);
  void clear_all_routes();

private:
  unique_id_t unique_id_ = 0;
  const char* bound_region_ = nullptr;
  unsigned long long next_request_id_ = 1;
  connection_state_t connection_state_ = connection_state_t::k_disconnected;
  external_event_stream event_stream_;
  owned_ds_route_t owned_ds_routes_[32];
};

}  // namespace external
}  // namespace sdk
}  // namespace atorbit

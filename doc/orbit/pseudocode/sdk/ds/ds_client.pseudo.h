#pragma once

#include "ds_runtime_loop.pseudo.h"

// Phase 4
// 目标: 描述 DS 侧 SDK 的公开接口，重点固化 Init / OnMessage / SendMessage / StopSelf 闭环。
// 未来真实落点: include/atorbit/sdk/ds/ds_client.h

namespace atorbit {
namespace sdk {
namespace ds {

enum class ds_client_state_t {
  k_created = 0,
  k_running = 1,
  k_stopping = 2,
  k_stopped = 3,
};

class ds_client {
public:
  result_code_t init(const char* local_endpoint, unsigned long long ds_id);
  result_code_t send_message(message_buffer_t payload);
  result_code_t poll_message_once();
  void set_message_callback(downstream_callback_t callback);
  result_code_t send_heartbeat(const heartbeat_sample_t& sample);
  result_code_t stop_self(int exit_code, message_buffer_t user_data);

private:
  result_code_t ensure_running() const;

private:
  unsigned long long last_downstream_seq_ = 0;
  ds_client_state_t state_ = ds_client_state_t::k_created;
  downstream_callback_t downstream_callback_ = nullptr;
  ds_runtime_loop runtime_loop_;
};

}  // namespace ds
}  // namespace sdk
}  // namespace atorbit

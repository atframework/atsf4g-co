#pragma once

// Phase 4.5
// 目标: 抽离 DS SDK 的 register / heartbeat / poll / stop 运行时循环。
// 未来真实落点: include/atorbit/sdk/ds/ds_runtime_loop.h

namespace atorbit {
namespace sdk {
namespace ds {

using result_code_t = int;
using message_buffer_t = const char*;

struct downstream_message_t {
  unsigned long long seq = 0;
  message_buffer_t payload = nullptr;
  bool require_ack = true;
};

struct heartbeat_sample_t {
  double cpu_used = 0;
  double memory_used_mb = 0;
};

using downstream_callback_t = void (*)(const downstream_message_t& message);

class ds_runtime_loop {
public:
  result_code_t start(const char* local_endpoint, unsigned long long ds_id);
  result_code_t send_message(message_buffer_t payload) const;
  result_code_t poll_message_once(unsigned long long last_downstream_seq,
                                  downstream_callback_t downstream_callback,
                                  unsigned long long& next_last_downstream_seq) const;
  result_code_t send_heartbeat(const heartbeat_sample_t& sample) const;
  result_code_t stop_self(int exit_code, message_buffer_t user_data) const;
  bool is_started() const;

private:
  result_code_t send_register();
  result_code_t recv_message(unsigned long long last_downstream_seq, downstream_message_t& output_message) const;
  result_code_t ack_downstream(unsigned long long seq) const;
  result_code_t flush_outbound_before_stop() const;
  result_code_t send_stop_notice(int exit_code, message_buffer_t user_data) const;
  result_code_t wait_for_local_channel_drain() const;

private:
  const char* local_endpoint_ = nullptr;
  unsigned long long ds_id_ = 0;
  bool register_sent_ = false;
  mutable bool stopping_ = false;
  mutable bool stopped_ = false;
  mutable int last_exit_code_ = 0;
  mutable message_buffer_t last_exit_user_data_ = nullptr;
};

}  // namespace ds
}  // namespace sdk
}  // namespace atorbit
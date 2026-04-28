#pragma once

// Phase 2
// 目标: 固化 DSA 与 DS 之间的本地 channel 绑定、收包分发与上下行转发接口。
// 未来真实落点: src/dsa/agent/local_channel_service.cpp

namespace atorbit {
namespace dsa {
namespace agent {

struct local_register_message_t {
  unsigned long long ds_id = 0;
  const char* local_endpoint = nullptr;
  int process_id = 0;
};

struct local_heartbeat_message_t {
  unsigned long long ds_id = 0;
  long long timestamp_ms = 0;
  double actual_cpu = 0;
  double actual_memory_mb = 0;
};

struct local_exit_message_t {
  unsigned long long ds_id = 0;
  int exit_code = 0;
  const char* user_data = nullptr;
};

struct downstream_packet_t {
  unsigned long long ds_id = 0;
  unsigned long long sequence = 0;
  const char* payload = nullptr;
};

class local_channel_service {
public:
  int register_ds(unsigned long long ds_id, const char* local_endpoint, int process_id);
  int remove_ds(unsigned long long ds_id);
  bool has_ds(unsigned long long ds_id) const;

  int handle_register_message(const local_register_message_t& message);
  int handle_heartbeat_message(const local_heartbeat_message_t& message);
  int handle_exit_message(const local_exit_message_t& message);
  int forward_to_ds(const downstream_packet_t& packet);
  int ack_upstream(unsigned long long ds_id, unsigned long long sequence);

private:
  struct channel_record_t {
    unsigned long long ds_id = 0;
    const char* local_endpoint = nullptr;
    int process_id = 0;
    long long last_heartbeat_ms = 0;
    bool exit_notified = false;
  };

  channel_record_t* find_record(unsigned long long ds_id);
  const channel_record_t* find_record(unsigned long long ds_id) const;

private:
  channel_record_t channels_[64];
  unsigned long long channel_count_ = 0;
};

}  // namespace agent
}  // namespace dsa
}  // namespace atorbit

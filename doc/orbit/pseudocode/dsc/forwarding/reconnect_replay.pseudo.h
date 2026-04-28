#pragma once

// Phase 4.4
// 目标: 固化外部服务重连后的回放窗口计算与 replay message 枚举。
// 未来真实落点: src/dsc/forwarding/reconnect_replay.cpp

namespace atorbit {
namespace dsc {
namespace forwarding {

using result_code_t = int;
using unique_id_t = unsigned long long;

struct buffered_upstream_message_t {
  unsigned long long seq = 0;
  unsigned long long dsa_id = 0;
  unsigned long long ds_id = 0;
  const char* payload = nullptr;
  bool occupied = false;
};

class upstream_buffer_cursor {
public:
  virtual ~upstream_buffer_cursor() = default;

  virtual unsigned long long list_after(unique_id_t owner_unique_id,
                                        unsigned long long last_received_seq,
                                        buffered_upstream_message_t output_messages[],
                                        unsigned long long capacity) const = 0;
};

class reconnect_replay {
public:
  explicit reconnect_replay(const upstream_buffer_cursor* buffer_cursor);

  result_code_t collect_replay_messages(unique_id_t owner_unique_id,
                                        unsigned long long last_received_seq,
                                        buffered_upstream_message_t replay_messages[],
                                        unsigned long long capacity,
                                        unsigned long long& replay_count) const;

private:
  bool validate_request(unique_id_t owner_unique_id, unsigned long long capacity) const;
  bool is_message_replayable(const buffered_upstream_message_t& message, unsigned long long last_received_seq) const;

private:
  const upstream_buffer_cursor* buffer_cursor_ = nullptr;
};

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
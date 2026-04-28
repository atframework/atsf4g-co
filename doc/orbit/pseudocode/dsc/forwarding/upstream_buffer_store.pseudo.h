#pragma once

#include "reconnect_replay.pseudo.h"

// Phase 4.5
// 目标: 固化 DS -> External 上行离线缓冲，覆盖 owner 分区、TTL 清理、容量淘汰与 dup guard。
// 未来真实落点: src/dsc/forwarding/upstream_buffer_store.cpp

namespace atorbit {
namespace dsc {
namespace forwarding {

class upstream_buffer_store : public upstream_buffer_cursor {
public:
  struct buffered_record_t {
    unique_id_t owner_unique_id = 0;
    buffered_upstream_message_t message;
    long long write_time_ms = 0;
    long long expire_at_ms = 0;
    bool occupied = false;
  };

  upstream_buffer_store(unsigned long long max_pending_per_session, long long pending_ttl_ms);

  result_code_t buffer_offline_message(unique_id_t owner_unique_id,
                                       const buffered_upstream_message_t& message,
                                       long long now_ms);
  unsigned long long ack_up_to(unique_id_t owner_unique_id, unsigned long long acked_seq);
  unsigned long long prune_expired(long long now_ms);

  unsigned long long list_after(unique_id_t owner_unique_id,
                                unsigned long long last_received_seq,
                                buffered_upstream_message_t output_messages[],
                                unsigned long long capacity) const override;

private:
  struct session_partition_t {
    unique_id_t owner_unique_id = 0;
    buffered_record_t records_[64];
    unsigned long long record_count = 0;
    bool occupied = false;
  };

private:
  bool validate_config() const;
  bool validate_message(unique_id_t owner_unique_id, const buffered_upstream_message_t& message) const;
  session_partition_t* get_or_create_partition(unique_id_t owner_unique_id);
  session_partition_t* find_partition(unique_id_t owner_unique_id);
  const session_partition_t* find_partition(unique_id_t owner_unique_id) const;
  buffered_record_t* find_duplicate_record(session_partition_t& partition, unsigned long long seq);
  buffered_record_t* append_record(session_partition_t& partition);
  buffered_record_t* evict_oldest_record(session_partition_t& partition);
  void compact_partition(session_partition_t& partition);

private:
  unsigned long long max_pending_per_session_ = 0;
  long long pending_ttl_ms_ = 0;
  session_partition_t partitions_[32];
};

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
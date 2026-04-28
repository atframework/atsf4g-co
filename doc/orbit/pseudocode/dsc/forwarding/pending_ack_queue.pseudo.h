#pragma once

#include "../session/session_router.pseudo.h"

// Phase 4.1
// 目标: 抽离下行消息 pending ack 的登记、完成、重试扫描与超时回收。
// 未来真实落点: src/dsc/forwarding/pending_ack_queue.cpp

namespace atorbit {
namespace dsc {
namespace forwarding {

using result_code_t = int;

struct pending_ack_record_t {
  session::unique_id_t owner_unique_id = 0;
  session::ds_composite_key_t ds_key;
  unsigned long long ack_seq = 0;
  const char* payload = nullptr;
  unsigned long long retry_count = 0;
  long long next_retry_at_ms = 0;
  long long expire_at_ms = 0;
  bool occupied = false;
};

class pending_ack_queue {
public:
  result_code_t track_pending_ack(session::unique_id_t owner_unique_id,
                                  const session::ds_composite_key_t& ds_key,
                                  unsigned long long ack_seq,
                                  const char* payload,
                                  long long now_ms);
  result_code_t complete_pending_ack(const session::ds_composite_key_t& ds_key, unsigned long long ack_seq);
  unsigned long long collect_retryable_entries(long long now_ms,
                                               pending_ack_record_t output_records[],
                                               unsigned long long capacity);
  unsigned long long collect_expired_entries(long long now_ms,
                                             pending_ack_record_t output_records[],
                                             unsigned long long capacity);

  unsigned long long pending_ack_count() const;
  bool has_pending_ack(const session::ds_composite_key_t& ds_key, unsigned long long ack_seq) const;

private:
  bool validate_track_request(session::unique_id_t owner_unique_id,
                              const session::ds_composite_key_t& ds_key,
                              unsigned long long ack_seq,
                              const char* payload,
                              long long now_ms) const;
  pending_ack_record_t* allocate_entry();
  pending_ack_record_t* find_entry(const session::ds_composite_key_t& ds_key, unsigned long long ack_seq);
  const pending_ack_record_t* find_entry(const session::ds_composite_key_t& ds_key,
                                         unsigned long long ack_seq) const;

private:
  pending_ack_record_t pending_ack_records_[64];
  long long ack_timeout_ms_ = 5000;
  unsigned long long max_retry_ = 3;
};

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
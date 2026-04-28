#include "pending_ack_queue.pseudo.h"

namespace atorbit {
namespace dsc {
namespace forwarding {

namespace {

static bool same_ds_key(const session::ds_composite_key_t& left, const session::ds_composite_key_t& right) {
  return left.dsa_id == right.dsa_id && left.ds_id == right.ds_id;
}

}  // namespace

result_code_t pending_ack_queue::track_pending_ack(session::unique_id_t owner_unique_id,
                                                   const session::ds_composite_key_t& ds_key,
                                                   unsigned long long ack_seq,
                                                   const char* payload,
                                                   long long now_ms) {
  if (!validate_track_request(owner_unique_id, ds_key, ack_seq, payload, now_ms)) {
    return -1;
  }

  auto* existing = find_entry(ds_key, ack_seq);
  if (nullptr != existing) {
    return 0;
  }

  auto* entry = allocate_entry();
  if (nullptr == entry) {
    return -2;
  }

  entry->occupied = true;
  entry->owner_unique_id = owner_unique_id;
  entry->ds_key = ds_key;
  entry->ack_seq = ack_seq;
  entry->payload = payload;
  entry->retry_count = 0;
  entry->next_retry_at_ms = now_ms + ack_timeout_ms_;
  entry->expire_at_ms = now_ms + ack_timeout_ms_ * static_cast<long long>(max_retry_ + 1);
  return 0;
}

result_code_t pending_ack_queue::complete_pending_ack(const session::ds_composite_key_t& ds_key,
                                                      unsigned long long ack_seq) {
  auto* entry = find_entry(ds_key, ack_seq);
  if (nullptr == entry) {
    return -1;
  }

  *entry = {};
  return 0;
}

unsigned long long pending_ack_queue::collect_retryable_entries(long long now_ms,
                                                                pending_ack_record_t output_records[],
                                                                unsigned long long capacity) {
  if (now_ms <= 0 || nullptr == output_records || 0 == capacity) {
    return 0;
  }

  unsigned long long output_count = 0;
  for (auto& entry : pending_ack_records_) {
    if (!entry.occupied || now_ms < entry.next_retry_at_ms || entry.retry_count >= max_retry_) {
      continue;
    }

    output_records[output_count++] = entry;
    ++entry.retry_count;
    entry.next_retry_at_ms = now_ms + ack_timeout_ms_;
    if (output_count >= capacity) {
      break;
    }
  }

  return output_count;
}

unsigned long long pending_ack_queue::collect_expired_entries(long long now_ms,
                                                              pending_ack_record_t output_records[],
                                                              unsigned long long capacity) {
  if (now_ms <= 0 || nullptr == output_records || 0 == capacity) {
    return 0;
  }

  unsigned long long output_count = 0;
  for (auto& entry : pending_ack_records_) {
    if (!entry.occupied || now_ms < entry.expire_at_ms) {
      continue;
    }

    output_records[output_count++] = entry;
    entry = {};
    if (output_count >= capacity) {
      break;
    }
  }

  return output_count;
}

unsigned long long pending_ack_queue::pending_ack_count() const {
  unsigned long long count = 0;
  for (const auto& entry : pending_ack_records_) {
    if (entry.occupied) {
      ++count;
    }
  }

  return count;
}

bool pending_ack_queue::has_pending_ack(const session::ds_composite_key_t& ds_key, unsigned long long ack_seq) const {
  return nullptr != find_entry(ds_key, ack_seq);
}

bool pending_ack_queue::validate_track_request(session::unique_id_t owner_unique_id,
                                               const session::ds_composite_key_t& ds_key,
                                               unsigned long long ack_seq,
                                               const char* payload,
                                               long long now_ms) const {
  if (0 == owner_unique_id || 0 == ds_key.dsa_id || 0 == ds_key.ds_id || 0 == ack_seq || nullptr == payload ||
      now_ms <= 0) {
    return false;
  }

  return true;
}

pending_ack_record_t* pending_ack_queue::allocate_entry() {
  for (auto& entry : pending_ack_records_) {
    if (!entry.occupied) {
      return &entry;
    }
  }

  return nullptr;
}

pending_ack_record_t* pending_ack_queue::find_entry(const session::ds_composite_key_t& ds_key,
                                                    unsigned long long ack_seq) {
  for (auto& entry : pending_ack_records_) {
    if (entry.occupied && entry.ack_seq == ack_seq && same_ds_key(entry.ds_key, ds_key)) {
      return &entry;
    }
  }

  return nullptr;
}

const pending_ack_record_t* pending_ack_queue::find_entry(const session::ds_composite_key_t& ds_key,
                                                          unsigned long long ack_seq) const {
  for (const auto& entry : pending_ack_records_) {
    if (entry.occupied && entry.ack_seq == ack_seq && same_ds_key(entry.ds_key, ds_key)) {
      return &entry;
    }
  }

  return nullptr;
}

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
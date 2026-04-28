#include "upstream_buffer_store.pseudo.h"

namespace atorbit {
namespace dsc {
namespace forwarding {

namespace {

static bool is_record_expired(const upstream_buffer_store::buffered_record_t& record, long long now_ms) {
  return record.occupied && record.expire_at_ms > 0 && record.expire_at_ms <= now_ms;
}

}  // namespace

upstream_buffer_store::upstream_buffer_store(unsigned long long max_pending_per_session, long long pending_ttl_ms)
    : max_pending_per_session_(max_pending_per_session), pending_ttl_ms_(pending_ttl_ms) {}

result_code_t upstream_buffer_store::buffer_offline_message(unique_id_t owner_unique_id,
                                                            const buffered_upstream_message_t& message,
                                                            long long now_ms) {
  if (!validate_config() || !validate_message(owner_unique_id, message)) {
    return -1;
  }

  auto* partition = get_or_create_partition(owner_unique_id);
  if (nullptr == partition) {
    return -2;
  }

  if (nullptr != find_duplicate_record(*partition, message.seq)) {
    return 1;
  }

  auto* record = append_record(*partition);
  if (nullptr == record) {
    record = evict_oldest_record(*partition);
    if (nullptr == record) {
      return -3;
    }
  }

  record->owner_unique_id = owner_unique_id;
  record->message = message;
  record->message.occupied = true;
  record->write_time_ms = now_ms;
  record->expire_at_ms = now_ms + pending_ttl_ms_;
  record->occupied = true;
  compact_partition(*partition);
  return 0;
}

unsigned long long upstream_buffer_store::ack_up_to(unique_id_t owner_unique_id, unsigned long long acked_seq) {
  auto* partition = find_partition(owner_unique_id);
  if (nullptr == partition || 0 == acked_seq) {
    return 0;
  }

  unsigned long long removed_count = 0;
  for (auto& record : partition->records_) {
    if (!record.occupied || record.message.seq > acked_seq) {
      continue;
    }

    record.occupied = false;
    record.message.occupied = false;
    ++removed_count;
  }

  compact_partition(*partition);
  return removed_count;
}

unsigned long long upstream_buffer_store::prune_expired(long long now_ms) {
  unsigned long long removed_count = 0;

  for (auto& partition : partitions_) {
    if (!partition.occupied) {
      continue;
    }

    for (auto& record : partition.records_) {
      if (!is_record_expired(record, now_ms)) {
        continue;
      }

      record.occupied = false;
      record.message.occupied = false;
      ++removed_count;
    }

    compact_partition(partition);
  }

  return removed_count;
}

unsigned long long upstream_buffer_store::list_after(unique_id_t owner_unique_id,
                                                     unsigned long long last_received_seq,
                                                     buffered_upstream_message_t output_messages[],
                                                     unsigned long long capacity) const {
  if (nullptr == output_messages || 0 == capacity) {
    return 0;
  }

  const auto* partition = find_partition(owner_unique_id);
  if (nullptr == partition) {
    return 0;
  }

  unsigned long long output_count = 0;
  for (const auto& record : partition->records_) {
    if (!record.occupied || !record.message.occupied || record.message.seq <= last_received_seq) {
      continue;
    }

    output_messages[output_count++] = record.message;
    if (output_count >= capacity) {
      break;
    }
  }

  return output_count;
}

bool upstream_buffer_store::validate_config() const {
  if (0 == max_pending_per_session_ || max_pending_per_session_ > 64 || pending_ttl_ms_ <= 0) {
    return false;
  }

  return true;
}

bool upstream_buffer_store::validate_message(unique_id_t owner_unique_id, const buffered_upstream_message_t& message) const {
  if (0 == owner_unique_id || 0 == message.seq || nullptr == message.payload) {
    return false;
  }

  return true;
}

upstream_buffer_store::session_partition_t* upstream_buffer_store::get_or_create_partition(unique_id_t owner_unique_id) {
  if (auto* existing = find_partition(owner_unique_id)) {
    return existing;
  }

  for (auto& partition : partitions_) {
    if (!partition.occupied) {
      partition.occupied = true;
      partition.owner_unique_id = owner_unique_id;
      partition.record_count = 0;
      return &partition;
    }
  }

  return nullptr;
}

upstream_buffer_store::session_partition_t* upstream_buffer_store::find_partition(unique_id_t owner_unique_id) {
  for (auto& partition : partitions_) {
    if (partition.occupied && partition.owner_unique_id == owner_unique_id) {
      return &partition;
    }
  }

  return nullptr;
}

const upstream_buffer_store::session_partition_t* upstream_buffer_store::find_partition(unique_id_t owner_unique_id) const {
  for (const auto& partition : partitions_) {
    if (partition.occupied && partition.owner_unique_id == owner_unique_id) {
      return &partition;
    }
  }

  return nullptr;
}

upstream_buffer_store::buffered_record_t* upstream_buffer_store::find_duplicate_record(session_partition_t& partition,
                                                                                       unsigned long long seq) {
  for (auto& record : partition.records_) {
    if (record.occupied && record.message.seq == seq) {
      return &record;
    }
  }

  return nullptr;
}

upstream_buffer_store::buffered_record_t* upstream_buffer_store::append_record(session_partition_t& partition) {
  if (partition.record_count >= max_pending_per_session_) {
    return nullptr;
  }

  for (auto& record : partition.records_) {
    if (!record.occupied) {
      ++partition.record_count;
      return &record;
    }
  }

  return nullptr;
}

upstream_buffer_store::buffered_record_t* upstream_buffer_store::evict_oldest_record(session_partition_t& partition) {
  buffered_record_t* oldest_record = nullptr;
  for (auto& record : partition.records_) {
    if (!record.occupied) {
      continue;
    }

    if (nullptr == oldest_record || record.write_time_ms < oldest_record->write_time_ms) {
      oldest_record = &record;
    }
  }

  return oldest_record;
}

void upstream_buffer_store::compact_partition(session_partition_t& partition) {
  unsigned long long compacted_count = 0;
  buffered_record_t compacted_records[64] = {};

  for (const auto& record : partition.records_) {
    if (!record.occupied) {
      continue;
    }

    compacted_records[compacted_count++] = record;
  }

  for (unsigned long long index = 0; index < 64; ++index) {
    partition.records_[index] = {};
    if (index < compacted_count) {
      partition.records_[index] = compacted_records[index];
    }
  }

  partition.record_count = compacted_count;
  if (0 == compacted_count) {
    partition.occupied = false;
    partition.owner_unique_id = 0;
  }
}

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
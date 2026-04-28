#include "reconnect_replay.pseudo.h"

namespace atorbit {
namespace dsc {
namespace forwarding {

namespace {

static unsigned long long clamp_read_capacity(unsigned long long requested_capacity) {
  if (0 == requested_capacity) {
    return 0;
  }

  if (requested_capacity < 64) {
    return requested_capacity;
  }

  return 64;
}

}  // namespace

reconnect_replay::reconnect_replay(const upstream_buffer_cursor* buffer_cursor) : buffer_cursor_(buffer_cursor) {}

result_code_t reconnect_replay::collect_replay_messages(unique_id_t owner_unique_id,
                                                        unsigned long long last_received_seq,
                                                        buffered_upstream_message_t replay_messages[],
                                                        unsigned long long capacity,
                                                        unsigned long long& replay_count) const {
  replay_count = 0;
  if (!validate_request(owner_unique_id, capacity)) {
    return -1;
  }

  buffered_upstream_message_t candidate_messages[64] = {};
  auto candidate_count = buffer_cursor_->list_after(
      owner_unique_id, last_received_seq, candidate_messages, clamp_read_capacity(capacity));

  unsigned long long previous_seq = last_received_seq;
  for (unsigned long long index = 0; index < candidate_count; ++index) {
    const auto& candidate = candidate_messages[index];
    if (!is_message_replayable(candidate, previous_seq)) {
      continue;
    }

    replay_messages[replay_count++] = candidate;
    previous_seq = candidate.seq;
    if (replay_count >= capacity) {
      break;
    }
  }

  return 0;
}

bool reconnect_replay::validate_request(unique_id_t owner_unique_id, unsigned long long capacity) const {
  if (nullptr == buffer_cursor_ || 0 == owner_unique_id || 0 == capacity) {
    return false;
  }

  return true;
}

bool reconnect_replay::is_message_replayable(const buffered_upstream_message_t& message,
                                             unsigned long long last_received_seq) const {
  if (!message.occupied || nullptr == message.payload) {
    return false;
  }

  if (message.seq <= last_received_seq) {
    return false;
  }

  return true;
}

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
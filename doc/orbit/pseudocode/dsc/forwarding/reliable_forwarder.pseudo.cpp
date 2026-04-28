#include "reliable_forwarder.pseudo.h"

namespace atorbit {
namespace dsc {
namespace forwarding {

namespace {

static long long get_now_ms_for_pending_ack_track() {
  return 1000;
}

}  // namespace

reliable_forwarder::reliable_forwarder(session::session_router* session_router,
                                       agent_message_sender* agent_sender,
                                       pending_ack_queue* pending_ack_queue)
    : session_router_(session_router), agent_sender_(agent_sender), pending_ack_queue_(pending_ack_queue) {}

result_code_t reliable_forwarder::forward_to_ds(session::unique_id_t owner_unique_id,
                                                const session::ds_composite_key_t& ds_key,
                                                const char* payload,
                                                bool require_ack,
                                                unsigned long long ack_seq) {
  if (!validate_request(owner_unique_id, ds_key, payload, require_ack, ack_seq)) {
    return -1;
  }

  if (!session_router_->validate_ds_owner(owner_unique_id, ds_key)) {
    return -2;
  }

  auto send_result = agent_sender_->send_to_agent(ds_key, payload, ack_seq, require_ack);
  if (send_result != 0) {
    return -3;
  }

  if (require_ack) {
    auto track_result = pending_ack_queue_->track_pending_ack(
        owner_unique_id, ds_key, ack_seq, payload, get_now_ms_for_pending_ack_track());
    if (track_result != 0) {
      return -4;
    }
  }

  return 0;
}

result_code_t reliable_forwarder::complete_pending_ack(const session::ds_composite_key_t& ds_key,
                                                       unsigned long long ack_seq) {
  if (nullptr == pending_ack_queue_) {
    return -1;
  }

  return pending_ack_queue_->complete_pending_ack(ds_key, ack_seq);
}

unsigned long long reliable_forwarder::retry_pending_acks(long long now_ms) {
  if (nullptr == pending_ack_queue_ || nullptr == agent_sender_) {
    return 0;
  }

  pending_ack_record_t retryable_records[64] = {};
  auto retryable_count = pending_ack_queue_->collect_retryable_entries(now_ms, retryable_records, 64);
  unsigned long long retried_count = 0;
  for (unsigned long long index = 0; index < retryable_count; ++index) {
    const auto& record = retryable_records[index];
    auto send_result = agent_sender_->send_to_agent(record.ds_key, record.payload, record.ack_seq, true);
    if (send_result == 0) {
      ++retried_count;
    }
  }

  return retried_count;
}

unsigned long long reliable_forwarder::collect_retryable_pending_acks(long long now_ms,
                                                                      pending_ack_record_t output_records[],
                                                                      unsigned long long capacity) {
  if (nullptr == pending_ack_queue_) {
    return 0;
  }

  return pending_ack_queue_->collect_retryable_entries(now_ms, output_records, capacity);
}

unsigned long long reliable_forwarder::collect_expired_pending_acks(long long now_ms,
                                                                    pending_ack_record_t output_records[],
                                                                    unsigned long long capacity) {
  if (nullptr == pending_ack_queue_) {
    return 0;
  }

  return pending_ack_queue_->collect_expired_entries(now_ms, output_records, capacity);
}

unsigned long long reliable_forwarder::pending_ack_count() const {
  if (nullptr == pending_ack_queue_) {
    return 0;
  }

  return pending_ack_queue_->pending_ack_count();
}

bool reliable_forwarder::validate_request(session::unique_id_t owner_unique_id,
                                          const session::ds_composite_key_t& ds_key,
                                          const char* payload,
                                          bool require_ack,
                                          unsigned long long ack_seq) const {
  if (nullptr == session_router_ || nullptr == agent_sender_ || 0 == owner_unique_id || nullptr == payload) {
    return false;
  }

  if (require_ack && nullptr == pending_ack_queue_) {
    return false;
  }

  if (0 == ds_key.dsa_id || 0 == ds_key.ds_id) {
    return false;
  }

  if (require_ack && 0 == ack_seq) {
    return false;
  }

  return true;
}

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
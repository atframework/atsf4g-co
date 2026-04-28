#pragma once

#include "pending_ack_queue.pseudo.h"
#include "../session/session_router.pseudo.h"

// Phase 4.1
// 目标: 固化 ExternalService::SendToDS 的下行转发与 require_ack 挂起登记。
// 未来真实落点: src/dsc/forwarding/reliable_forwarder.cpp

namespace atorbit {
namespace dsc {
namespace forwarding {

using result_code_t = int;

class agent_message_sender {
public:
  virtual ~agent_message_sender() = default;

  virtual result_code_t send_to_agent(const session::ds_composite_key_t& ds_key,
                                      const char* payload,
                                      unsigned long long ack_seq,
                                      bool require_ack) = 0;
};

class reliable_forwarder {
public:
  reliable_forwarder(session::session_router* session_router,
                     agent_message_sender* agent_sender,
                     pending_ack_queue* pending_ack_queue);

  result_code_t forward_to_ds(session::unique_id_t owner_unique_id,
                              const session::ds_composite_key_t& ds_key,
                              const char* payload,
                              bool require_ack,
                              unsigned long long ack_seq);
  result_code_t complete_pending_ack(const session::ds_composite_key_t& ds_key, unsigned long long ack_seq);
  unsigned long long retry_pending_acks(long long now_ms);
  unsigned long long collect_retryable_pending_acks(long long now_ms,
                                                    pending_ack_record_t output_records[],
                                                    unsigned long long capacity);
  unsigned long long collect_expired_pending_acks(long long now_ms,
                                                  pending_ack_record_t output_records[],
                                                  unsigned long long capacity);
  unsigned long long pending_ack_count() const;

private:
  bool validate_request(session::unique_id_t owner_unique_id,
                        const session::ds_composite_key_t& ds_key,
                        const char* payload,
                        bool require_ack,
                        unsigned long long ack_seq) const;

private:
  session::session_router* session_router_ = nullptr;
  agent_message_sender* agent_sender_ = nullptr;
  pending_ack_queue* pending_ack_queue_ = nullptr;
};

}  // namespace forwarding
}  // namespace dsc
}  // namespace atorbit
#include "reliable_forwarder.pseudo.h"

namespace {

class fake_agent_message_sender : public atorbit::dsc::forwarding::agent_message_sender {
public:
  atorbit::dsc::session::ds_composite_key_t last_ds_key;
  const char* last_payload = nullptr;
  unsigned long long last_ack_seq = 0;
  bool last_require_ack = false;
  int call_count = 0;

  atorbit::dsc::forwarding::result_code_t send_to_agent(const atorbit::dsc::session::ds_composite_key_t& ds_key,
                                                        const char* payload,
                                                        unsigned long long ack_seq,
                                                        bool require_ack) override {
    last_ds_key = ds_key;
    last_payload = payload;
    last_ack_seq = ack_seq;
    last_require_ack = require_ack;
    ++call_count;
    return 0;
  }
};

}  // namespace

CASE_TEST(reliable_forwarder, forward_to_ds_tracks_pending_ack_when_require_ack_is_true) {
  atorbit::dsc::session::session_router router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::reliable_forwarder forwarder(&router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);

  auto result = forwarder.forward_to_ds(9527, ds_key, "hello", true, 101);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, sender.call_count);
  CASE_EXPECT_EQ(1, forwarder.pending_ack_count());
}

CASE_TEST(reliable_forwarder, forward_to_ds_rejects_owner_mismatch_before_send) {
  atorbit::dsc::session::session_router router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::reliable_forwarder forwarder(&router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.connect(9528, 1002, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);

  auto result = forwarder.forward_to_ds(9528, ds_key, "hello", true, 101);

  CASE_EXPECT_EQ(-2, result);
  CASE_EXPECT_EQ(0, sender.call_count);
}

CASE_TEST(reliable_forwarder, complete_pending_ack_delegates_to_pending_ack_queue) {
  atorbit::dsc::session::session_router router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::reliable_forwarder forwarder(&router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);
  forwarder.forward_to_ds(9527, ds_key, "hello", true, 101);

  auto result = forwarder.complete_pending_ack(ds_key, 101);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(0, forwarder.pending_ack_count());
}

CASE_TEST(reliable_forwarder, collect_retryable_pending_acks_returns_due_entry) {
  atorbit::dsc::session::session_router router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::pending_ack_record_t output_records[4] = {};
  atorbit::dsc::forwarding::reliable_forwarder forwarder(&router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);
  forwarder.forward_to_ds(9527, ds_key, "hello", true, 101);

  auto retryable_count = forwarder.collect_retryable_pending_acks(6000, output_records, 4);

  CASE_EXPECT_EQ(1, retryable_count);
  CASE_EXPECT_EQ(101, output_records[0].ack_seq);
}

CASE_TEST(reliable_forwarder, collect_expired_pending_acks_evicts_timeout_entry) {
  atorbit::dsc::session::session_router router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::pending_ack_record_t output_records[4] = {};
  atorbit::dsc::forwarding::reliable_forwarder forwarder(&router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);
  forwarder.forward_to_ds(9527, ds_key, "hello", true, 101);

  auto expired_count = forwarder.collect_expired_pending_acks(21000, output_records, 4);

  CASE_EXPECT_EQ(1, expired_count);
  CASE_EXPECT_EQ(101, output_records[0].ack_seq);
  CASE_EXPECT_EQ(0, forwarder.pending_ack_count());
}

CASE_TEST(reliable_forwarder, retry_pending_acks_resends_due_messages) {
  atorbit::dsc::session::session_router router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::reliable_forwarder forwarder(&router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);
  forwarder.forward_to_ds(9527, ds_key, "hello", true, 101);

  auto retried_count = forwarder.retry_pending_acks(6000);

  CASE_EXPECT_EQ(1, retried_count);
  CASE_EXPECT_EQ(2, sender.call_count);
  CASE_EXPECT_EQ("hello", sender.last_payload);
}
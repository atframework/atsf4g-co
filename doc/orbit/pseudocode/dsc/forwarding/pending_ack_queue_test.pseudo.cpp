#include "pending_ack_queue.pseudo.h"

CASE_TEST(pending_ack_queue, track_pending_ack_adds_entry_and_complete_pending_ack_removes_it) {
  atorbit::dsc::forwarding::pending_ack_queue queue;
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  auto track_result = queue.track_pending_ack(9527, ds_key, 101, "hello", 1000);
  auto complete_result = queue.complete_pending_ack(ds_key, 101);

  CASE_EXPECT_EQ(0, track_result);
  CASE_EXPECT_EQ(0, complete_result);
  CASE_EXPECT_EQ(0, queue.pending_ack_count());
}

CASE_TEST(pending_ack_queue, collect_retryable_entries_returns_due_pending_ack_without_removing_it) {
  atorbit::dsc::forwarding::pending_ack_queue queue;
  atorbit::dsc::forwarding::pending_ack_record_t output_records[4];
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  queue.track_pending_ack(9527, ds_key, 101, "hello", 1000);
  auto retryable_count = queue.collect_retryable_entries(6000, output_records, 4);

  CASE_EXPECT_EQ(1, retryable_count);
  CASE_EXPECT_EQ(101, output_records[0].ack_seq);
  CASE_EXPECT_EQ("hello", output_records[0].payload);
  CASE_EXPECT_TRUE(queue.has_pending_ack(ds_key, 101));
}

CASE_TEST(pending_ack_queue, collect_expired_entries_evicts_entries_after_total_timeout) {
  atorbit::dsc::forwarding::pending_ack_queue queue;
  atorbit::dsc::forwarding::pending_ack_record_t output_records[4];
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  queue.track_pending_ack(9527, ds_key, 101, "hello", 1000);
  auto expired_count = queue.collect_expired_entries(21000, output_records, 4);

  CASE_EXPECT_EQ(1, expired_count);
  CASE_EXPECT_EQ(101, output_records[0].ack_seq);
  CASE_EXPECT_EQ(0, queue.pending_ack_count());
}

CASE_TEST(pending_ack_queue, track_pending_ack_returns_error_when_queue_is_full) {
  atorbit::dsc::forwarding::pending_ack_queue queue;
  atorbit::dsc::session::ds_composite_key_t ds_key;
  int last_result = 0;

  for (unsigned long long index = 0; index < 64; ++index) {
    ds_key.dsa_id = 7001 + index;
    ds_key.ds_id = 8001 + index;
    last_result = queue.track_pending_ack(9527, ds_key, 100 + index, "hello", 1000 + static_cast<long long>(index));
  }

  ds_key.dsa_id = 9001;
  ds_key.ds_id = 9002;
  auto overflow_result = queue.track_pending_ack(9527, ds_key, 999, "overflow", 2000);

  CASE_EXPECT_EQ(0, last_result);
  CASE_EXPECT_EQ(-2, overflow_result);
}
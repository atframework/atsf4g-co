#include "upstream_buffer_store.pseudo.h"

namespace {

static atorbit::dsc::forwarding::buffered_upstream_message_t make_buffered_message(unsigned long long seq,
                                                                                   const char* payload) {
  atorbit::dsc::forwarding::buffered_upstream_message_t message;
  message.seq = seq;
  message.dsa_id = 7001;
  message.ds_id = 8001;
  message.payload = payload;
  message.occupied = true;
  return message;
}

}  // namespace

CASE_TEST(upstream_buffer_store, buffer_offline_message_partitions_by_owner_and_lists_only_matching_owner) {
  atorbit::dsc::forwarding::upstream_buffer_store store(4, 30000);
  atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[8] = {};

  auto result_a = store.buffer_offline_message(9527, make_buffered_message(101, "owner-a-101"), 1000);
  auto result_b = store.buffer_offline_message(9528, make_buffered_message(201, "owner-b-201"), 1000);
  auto count = store.list_after(9527, 100, output_messages, 8);

  CASE_EXPECT_EQ(0, result_a);
  CASE_EXPECT_EQ(0, result_b);
  CASE_EXPECT_EQ(1, count);
  CASE_EXPECT_EQ(101, output_messages[0].seq);
}

CASE_TEST(upstream_buffer_store, buffer_offline_message_evicts_oldest_record_when_partition_is_full) {
  atorbit::dsc::forwarding::upstream_buffer_store store(2, 30000);
  atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[8] = {};

  store.buffer_offline_message(9527, make_buffered_message(101, "first"), 1000);
  store.buffer_offline_message(9527, make_buffered_message(102, "second"), 1001);
  auto result = store.buffer_offline_message(9527, make_buffered_message(103, "third"), 1002);
  auto count = store.list_after(9527, 0, output_messages, 8);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(2, count);
  CASE_EXPECT_EQ(102, output_messages[0].seq);
  CASE_EXPECT_EQ(103, output_messages[1].seq);
}

CASE_TEST(upstream_buffer_store, prune_expired_removes_outdated_messages) {
  atorbit::dsc::forwarding::upstream_buffer_store store(4, 100);
  atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[8] = {};

  store.buffer_offline_message(9527, make_buffered_message(101, "expired"), 1000);
  auto removed_count = store.prune_expired(1101);
  auto count = store.list_after(9527, 0, output_messages, 8);

  CASE_EXPECT_EQ(1, removed_count);
  CASE_EXPECT_EQ(0, count);
}

CASE_TEST(upstream_buffer_store, buffer_offline_message_rejects_duplicate_seq_for_same_owner) {
  atorbit::dsc::forwarding::upstream_buffer_store store(4, 30000);

  auto first_result = store.buffer_offline_message(9527, make_buffered_message(101, "first"), 1000);
  auto duplicate_result = store.buffer_offline_message(9527, make_buffered_message(101, "duplicate"), 1001);

  CASE_EXPECT_EQ(0, first_result);
  CASE_EXPECT_EQ(1, duplicate_result);
}

CASE_TEST(upstream_buffer_store, ack_up_to_clears_messages_that_have_been_confirmed) {
  atorbit::dsc::forwarding::upstream_buffer_store store(4, 30000);
  atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[8] = {};

  store.buffer_offline_message(9527, make_buffered_message(101, "first"), 1000);
  store.buffer_offline_message(9527, make_buffered_message(102, "second"), 1001);
  auto removed_count = store.ack_up_to(9527, 101);
  auto count = store.list_after(9527, 0, output_messages, 8);

  CASE_EXPECT_EQ(1, removed_count);
  CASE_EXPECT_EQ(1, count);
  CASE_EXPECT_EQ(102, output_messages[0].seq);
}

CASE_TEST(upstream_buffer_store, reconnect_replay_reads_messages_from_upstream_buffer_store) {
  atorbit::dsc::forwarding::upstream_buffer_store store(4, 30000);
  atorbit::dsc::forwarding::reconnect_replay replay(&store);
  atorbit::dsc::forwarding::buffered_upstream_message_t replay_messages[8] = {};
  unsigned long long replay_count = 0;

  store.buffer_offline_message(9527, make_buffered_message(101, "first"), 1000);
  store.buffer_offline_message(9527, make_buffered_message(102, "second"), 1001);

  auto result = replay.collect_replay_messages(9527, 101, replay_messages, 8, replay_count);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, replay_count);
  CASE_EXPECT_EQ(102, replay_messages[0].seq);
}
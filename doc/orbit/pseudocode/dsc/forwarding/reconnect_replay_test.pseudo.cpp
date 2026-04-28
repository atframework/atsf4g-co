#include "reconnect_replay.pseudo.h"

namespace {

class fake_upstream_buffer_cursor : public atorbit::dsc::forwarding::upstream_buffer_cursor {
public:
  unsigned long long list_after(atorbit::dsc::forwarding::unique_id_t owner_unique_id,
                                unsigned long long last_received_seq,
                                atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[],
                                unsigned long long capacity) const override {
    (void)owner_unique_id;
    (void)last_received_seq;

    if (capacity < 3) {
      return 0;
    }

    output_messages[0] = {100, 7001, 8001, "already-acked", true};
    output_messages[1] = {101, 7001, 8001, "replay-101", true};
    output_messages[2] = {102, 7001, 8001, "replay-102", true};
    return 3;
  }
};

}  // namespace

CASE_TEST(reconnect_replay, collect_replay_messages_skips_last_received_seq_and_replays_newer_messages) {
  fake_upstream_buffer_cursor cursor;
  atorbit::dsc::forwarding::reconnect_replay replay(&cursor);
  atorbit::dsc::forwarding::buffered_upstream_message_t messages[8] = {};
  unsigned long long replay_count = 0;

  auto result = replay.collect_replay_messages(9527, 100, messages, 8, replay_count);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(2, replay_count);
  CASE_EXPECT_EQ(101, messages[0].seq);
  CASE_EXPECT_EQ(102, messages[1].seq);
}

CASE_TEST(reconnect_replay, collect_replay_messages_rejects_empty_owner_uid) {
  fake_upstream_buffer_cursor cursor;
  atorbit::dsc::forwarding::reconnect_replay replay(&cursor);
  atorbit::dsc::forwarding::buffered_upstream_message_t messages[8] = {};
  unsigned long long replay_count = 0;

  auto result = replay.collect_replay_messages(0, 100, messages, 8, replay_count);

  CASE_EXPECT_EQ(-1, result);
  CASE_EXPECT_EQ(0, replay_count);
}
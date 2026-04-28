#include "ds_runtime_loop.pseudo.h"

namespace {

static unsigned long long g_runtime_loop_message_callback_count = 0;
static atorbit::sdk::ds::downstream_message_t g_runtime_loop_last_message;

static void reset_ds_runtime_loop_test_state() {
  g_runtime_loop_message_callback_count = 0;
  g_runtime_loop_last_message = {};
}

static void on_runtime_loop_downstream_message(const atorbit::sdk::ds::downstream_message_t& message) {
  ++g_runtime_loop_message_callback_count;
  g_runtime_loop_last_message = message;
}

}  // namespace

CASE_TEST(ds_runtime_loop, start_then_send_message_succeeds) {
  atorbit::sdk::ds::ds_runtime_loop runtime_loop;

  auto start_result = runtime_loop.start("local://dsa_ds_channel", 20001);
  auto send_result = runtime_loop.send_message("hello-from-ds");

  CASE_EXPECT_EQ(0, start_result);
  CASE_EXPECT_EQ(0, send_result);
}

CASE_TEST(ds_runtime_loop, poll_message_once_dispatches_registered_callback) {
  atorbit::sdk::ds::ds_runtime_loop runtime_loop;
  unsigned long long next_last_downstream_seq = 0;

  reset_ds_runtime_loop_test_state();
  runtime_loop.start("local://dsa_ds_channel", 20001);

  auto poll_result = runtime_loop.poll_message_once(0, on_runtime_loop_downstream_message, next_last_downstream_seq);

  CASE_EXPECT_EQ(0, poll_result);
  CASE_EXPECT_EQ(1, g_runtime_loop_message_callback_count);
  CASE_EXPECT_EQ(1, next_last_downstream_seq);
}

CASE_TEST(ds_runtime_loop, stop_self_returns_success_after_start) {
  atorbit::sdk::ds::ds_runtime_loop runtime_loop;
  atorbit::sdk::ds::heartbeat_sample_t sample;
  unsigned long long next_last_downstream_seq = 0;

  sample.cpu_used = 1.0;
  sample.memory_used_mb = 128.0;

  runtime_loop.start("local://dsa_ds_channel", 20001);
  auto stop_result = runtime_loop.stop_self(0, "normal_shutdown");
  auto send_result = runtime_loop.send_message("should_fail_after_stop");
  auto heartbeat_result = runtime_loop.send_heartbeat(sample);
  auto poll_result = runtime_loop.poll_message_once(0, nullptr, next_last_downstream_seq);

  CASE_EXPECT_EQ(0, stop_result);
  CASE_EXPECT_EQ(-1, send_result);
  CASE_EXPECT_EQ(-1, heartbeat_result);
  CASE_EXPECT_EQ(-1, poll_result);
}
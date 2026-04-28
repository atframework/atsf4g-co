#include "ds_client.pseudo.h"

namespace {

static unsigned long long g_message_callback_count = 0;
static atorbit::sdk::ds::downstream_message_t g_last_downstream_message;

static void reset_ds_client_test_state() {
  g_message_callback_count = 0;
  g_last_downstream_message.seq = 0;
  g_last_downstream_message.payload = nullptr;
  g_last_downstream_message.require_ack = true;
}

static void on_test_downstream_message(const atorbit::sdk::ds::downstream_message_t& message) {
  ++g_message_callback_count;
  g_last_downstream_message = message;
}

}  // namespace

CASE_TEST(ds_client, init_then_send_message_succeeds) {
  atorbit::sdk::ds::ds_client client;

  auto init_result = client.init("local://dsa_ds_channel", 20001);
  auto send_result = client.send_message("hello-from-ds");

  CASE_EXPECT_EQ(0, init_result);
  CASE_EXPECT_EQ(0, send_result);
}

CASE_TEST(ds_client, poll_message_once_dispatches_registered_callback) {
  atorbit::sdk::ds::ds_client client;

  reset_ds_client_test_state();
  client.init("local://dsa_ds_channel", 20001);
  client.set_message_callback(on_test_downstream_message);

  auto poll_result = client.poll_message_once();

  CASE_EXPECT_EQ(0, poll_result);
  CASE_EXPECT_EQ(1, g_message_callback_count);
  CASE_EXPECT_TRUE(g_last_downstream_message.seq > 0);
}

CASE_TEST(ds_client, stop_self_transitions_to_stopped_and_rejects_new_send) {
  atorbit::sdk::ds::ds_client client;

  client.init("local://dsa_ds_channel", 20001);
  auto stop_result = client.stop_self(0, "normal_shutdown");
  auto send_result = client.send_message("should_fail_after_stop");

  CASE_EXPECT_EQ(0, stop_result);
  CASE_EXPECT_EQ(-1, send_result);
}
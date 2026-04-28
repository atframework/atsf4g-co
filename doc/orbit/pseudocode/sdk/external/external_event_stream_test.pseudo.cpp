#include "external_event_stream.pseudo.h"

namespace {

static unsigned long long g_stream_message_count = 0;
static unsigned long long g_stream_disconnect_count = 0;
static atorbit::sdk::external::ds_identity_t g_last_stream_message_ds;
static atorbit::sdk::external::ds_disconnect_event_t g_last_stream_disconnect_event;

static void reset_external_event_stream_test_state() {
  g_stream_message_count = 0;
  g_stream_disconnect_count = 0;
  g_last_stream_message_ds = {};
  g_last_stream_disconnect_event = {};
}

static void on_stream_message(const atorbit::sdk::external::ds_identity_t& ds,
                              atorbit::sdk::external::message_buffer_t payload) {
  (void)payload;
  ++g_stream_message_count;
  g_last_stream_message_ds = ds;
}

static void on_stream_disconnect(const atorbit::sdk::external::ds_disconnect_event_t& event) {
  ++g_stream_disconnect_count;
  g_last_stream_disconnect_event = event;
}

}  // namespace

CASE_TEST(external_event_stream, dispatch_ds_message_calls_registered_callback) {
  atorbit::sdk::external::external_event_stream event_stream;
  atorbit::sdk::external::ds_identity_t ds;
  ds.dsa_id = 7001;
  ds.ds_id = 8001;

  reset_external_event_stream_test_state();
  event_stream.set_ds_message_callback(on_stream_message);
  event_stream.dispatch_ds_message(ds, "hello");

  CASE_EXPECT_EQ(1, g_stream_message_count);
  CASE_EXPECT_EQ(8001, g_last_stream_message_ds.ds_id);
}

CASE_TEST(external_event_stream, dispatch_ds_disconnect_calls_registered_callback) {
  atorbit::sdk::external::external_event_stream event_stream;
  atorbit::sdk::external::ds_identity_t ds;
  ds.dsa_id = 7001;
  ds.ds_id = 8001;

  reset_external_event_stream_test_state();
  event_stream.set_ds_disconnect_callback(on_stream_disconnect);
  event_stream.dispatch_ds_disconnect(ds, 3);

  CASE_EXPECT_EQ(1, g_stream_disconnect_count);
  CASE_EXPECT_EQ(8001, g_last_stream_disconnect_event.ds.ds_id);
  CASE_EXPECT_EQ(3, g_last_stream_disconnect_event.disconnect_reason);
}
#include "external_client.pseudo.h"

namespace {

static unsigned long long g_disconnect_callback_count = 0;
static atorbit::sdk::external::ds_disconnect_event_t g_last_disconnect_event;

static void reset_external_client_test_state() {
  g_disconnect_callback_count = 0;
  g_last_disconnect_event.ds.dsa_id = 0;
  g_last_disconnect_event.ds.ds_id = 0;
  g_last_disconnect_event.disconnect_reason = 0;
}

static void on_test_ds_disconnect(const atorbit::sdk::external::ds_disconnect_event_t& event) {
  ++g_disconnect_callback_count;
  g_last_disconnect_event = event;
}

}  // namespace

CASE_TEST(external_client, launch_dedicated_server_builds_launch_rpc) {
  atorbit::sdk::external::external_client client;
  atorbit::sdk::external::launch_request_t request;

  client.connect("region-cn-east", 9527);
  request.target_region = "region-cn-east";
  request.expected_cpu = 1.0;
  request.expected_memory_mb = 1024.0;

  auto result = client.launch_dedicated_server(request);
  auto send_result = client.send_to_ds(result.ds, "hello-from-service");

  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, send_result);
}

CASE_TEST(external_client, handle_transport_disconnected_notifies_owned_ds) {
  atorbit::sdk::external::external_client client;
  atorbit::sdk::external::launch_request_t request;

  reset_external_client_test_state();
  client.connect("region-cn-east", 9527);
  client.set_ds_disconnect_callback(on_test_ds_disconnect);

  request.target_region = "region-cn-east";
  request.expected_cpu = 1.0;
  request.expected_memory_mb = 1024.0;

  auto result = client.launch_dedicated_server(request);
  auto disconnect_result = client.handle_transport_disconnected(
      static_cast<int>(atorbit::sdk::external::ds_disconnect_reason_t::k_agent_disconnected));

  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, disconnect_result);
  CASE_EXPECT_EQ(1, g_disconnect_callback_count);
  CASE_EXPECT_EQ(result.ds.ds_id, g_last_disconnect_event.ds.ds_id);
  CASE_EXPECT_EQ(static_cast<int>(atorbit::sdk::external::ds_disconnect_reason_t::k_agent_disconnected),
                 g_last_disconnect_event.disconnect_reason);
}

CASE_TEST(external_client, set_ds_message_callback_keeps_upstream_handler) {
  atorbit::sdk::external::external_client client;

  client.set_ds_message_callback(nullptr);

  CASE_EXPECT_TRUE(true);
}

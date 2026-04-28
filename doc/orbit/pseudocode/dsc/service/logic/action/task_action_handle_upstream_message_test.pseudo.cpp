#include "task_action_handle_upstream_message.pseudo.h"

namespace {

class fake_external_upstream_sender : public atorbit::dsc::service::logic::action::external_upstream_sender {
public:
  unsigned long long last_owner_unique_id = 0;
  atorbit::dsc::forwarding::buffered_upstream_message_t last_message;
  int call_count = 0;
  int next_result = 0;

  atorbit::dsc::forwarding::result_code_t send_upstream_message(
      atorbit::dsc::session::unique_id_t owner_unique_id,
      const atorbit::dsc::forwarding::buffered_upstream_message_t& message) override {
    last_owner_unique_id = owner_unique_id;
    last_message = message;
    ++call_count;
    return next_result;
  }
};

}  // namespace

CASE_TEST(task_action_handle_upstream_message, run_task_action_handle_upstream_message_sends_directly_when_owner_is_online) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ForwardFromDSReq request;
  atorbit::dsc::session::session_router session_router;
  fake_external_upstream_sender sender;
  atorbit::dsc::forwarding::upstream_buffer_store upstream_buffer_store(4, 30000);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[8] = {};

  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  request.set_request(7001, 8001, 101, "up-101");

  auto result = atorbit::dsc::service::logic::action::run_task_action_handle_upstream_message(
      runtime, rpc_context, request, session_router, sender, upstream_buffer_store);
  auto buffered_count = upstream_buffer_store.list_after(9527, 0, output_messages, 8);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, sender.call_count);
  CASE_EXPECT_EQ(9527, sender.last_owner_unique_id);
  CASE_EXPECT_EQ(0, buffered_count);
}

CASE_TEST(task_action_handle_upstream_message, run_task_action_handle_upstream_message_buffers_message_when_owner_is_offline) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ForwardFromDSReq request;
  atorbit::dsc::session::session_router session_router;
  fake_external_upstream_sender sender;
  atorbit::dsc::forwarding::upstream_buffer_store upstream_buffer_store(4, 30000);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[8] = {};

  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  session_router.mark_disconnected(9527, 1001);
  request.set_request(7001, 8001, 101, "up-101");

  auto result = atorbit::dsc::service::logic::action::run_task_action_handle_upstream_message(
      runtime, rpc_context, request, session_router, sender, upstream_buffer_store);
  auto buffered_count = upstream_buffer_store.list_after(9527, 0, output_messages, 8);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(0, sender.call_count);
  CASE_EXPECT_EQ(1, buffered_count);
  CASE_EXPECT_EQ(101, output_messages[0].seq);
}

CASE_TEST(task_action_handle_upstream_message, run_task_action_handle_upstream_message_buffers_when_direct_send_fails) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ForwardFromDSReq request;
  atorbit::dsc::session::session_router session_router;
  fake_external_upstream_sender sender;
  atorbit::dsc::forwarding::upstream_buffer_store upstream_buffer_store(4, 30000);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[8] = {};

  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  sender.next_result = -1;
  request.set_request(7001, 8001, 101, "up-101");

  auto result = atorbit::dsc::service::logic::action::run_task_action_handle_upstream_message(
      runtime, rpc_context, request, session_router, sender, upstream_buffer_store);
  auto buffered_count = upstream_buffer_store.list_after(9527, 0, output_messages, 8);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, sender.call_count);
  CASE_EXPECT_EQ(1, buffered_count);
}

CASE_TEST(task_action_handle_upstream_message, run_task_action_handle_upstream_message_returns_error_when_buffer_partition_is_exhausted) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ForwardFromDSReq request;
  atorbit::dsc::session::session_router session_router;
  fake_external_upstream_sender sender;
  atorbit::dsc::forwarding::upstream_buffer_store upstream_buffer_store(1, 30000);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  atorbit::dsc::forwarding::buffered_upstream_message_t occupied_message;

  for (unsigned long long owner_index = 1; owner_index <= 32; ++owner_index) {
    occupied_message.seq = 100 + owner_index;
    occupied_message.dsa_id = 7000 + owner_index;
    occupied_message.ds_id = 8000 + owner_index;
    occupied_message.payload = "occupied";
    occupied_message.occupied = true;
    upstream_buffer_store.buffer_offline_message(9000 + owner_index, occupied_message, 1000 + owner_index);
  }

  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  sender.next_result = -1;
  request.set_request(7001, 8001, 101, "up-101");

  auto result = atorbit::dsc::service::logic::action::run_task_action_handle_upstream_message(
      runtime, rpc_context, request, session_router, sender, upstream_buffer_store);

  CASE_EXPECT_EQ(17, result);
  CASE_EXPECT_EQ(1, sender.call_count);
}
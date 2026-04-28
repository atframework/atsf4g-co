#include "task_action_reconnect_external.pseudo.h"

namespace {

class fake_upstream_buffer_cursor : public atorbit::dsc::forwarding::upstream_buffer_cursor {
public:
  const char* replay_payload_text() const {
    return "replay-101";
  }

  unsigned long long list_after(atorbit::dsc::forwarding::unique_id_t owner_unique_id,
                                unsigned long long last_received_seq,
                                atorbit::dsc::forwarding::buffered_upstream_message_t output_messages[],
                                unsigned long long capacity) const override {
    (void)owner_unique_id;
    (void)last_received_seq;

    if (0 == capacity) {
      return 0;
    }

    output_messages[0] = {101, 7001, 8001, replay_payload_text(), true};
    return 1;
  }
};

}  // namespace

CASE_TEST(task_action_reconnect_external, run_task_action_reconnect_external_rejects_request_without_session) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ReconnectExternalReq request;
  atorbit::dsc::session::session_router session_router;
  fake_upstream_buffer_cursor cursor;
  atorbit::dsc::forwarding::reconnect_replay replay_engine(&cursor);

  auto result = atorbit::dsc::service::logic::action::run_task_action_reconnect_external(
      runtime, rpc_context, request, session_router, replay_engine);

  CASE_EXPECT_EQ(atorbit::dsc::service::app::ERROR_CODE_INVALID_ARGUMENT, result);
}

CASE_TEST(task_action_reconnect_external, run_task_action_reconnect_external_rejects_duplicate_live_session) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ReconnectExternalReq request;
  atorbit::dsc::session::session_router session_router;
  fake_upstream_buffer_cursor cursor;
  atorbit::dsc::forwarding::reconnect_replay replay_engine(&cursor);

  rpc_context.inbound_connection_handle = 1002;
  rpc_context.controller_route_key = "dsc://region-cn-east/controller-a";
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  request.set_session(9527, 100);

  auto result = atorbit::dsc::service::logic::action::run_task_action_reconnect_external(
      runtime, rpc_context, request, session_router, replay_engine);

  CASE_EXPECT_EQ(11, result);
}

CASE_TEST(task_action_reconnect_external, run_task_action_reconnect_external_returns_resume_failed_on_route_mismatch) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ReconnectExternalReq request;
  atorbit::dsc::session::session_router session_router;
  fake_upstream_buffer_cursor cursor;
  atorbit::dsc::forwarding::reconnect_replay replay_engine(&cursor);

  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.mark_disconnected(9527, 1001);
  rpc_context.inbound_connection_handle = 1002;
  rpc_context.controller_route_key = "dsc://region-cn-east/controller-b";
  request.set_session(9527, 100);

  auto result = atorbit::dsc::service::logic::action::run_task_action_reconnect_external(
      runtime, rpc_context, request, session_router, replay_engine);

  CASE_EXPECT_EQ(13, result);
}

CASE_TEST(task_action_reconnect_external, run_task_action_reconnect_external_writes_replay_messages_after_resume) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ReconnectExternalReq request;
  atorbit::dsc::session::session_router session_router;
  fake_upstream_buffer_cursor cursor;
  atorbit::dsc::forwarding::reconnect_replay replay_engine(&cursor);

  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.mark_disconnected(9527, 1001);
  rpc_context.inbound_connection_handle = 1002;
  rpc_context.controller_route_key = "dsc://region-cn-east/controller-a";
  request.set_session(9527, 100);

  auto result = atorbit::dsc::service::logic::action::run_task_action_reconnect_external(
      runtime, rpc_context, request, session_router, replay_engine);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_TRUE(rpc_context.reconnect_resumed);
  CASE_EXPECT_EQ(9527, rpc_context.reconnect_unique_id);
  CASE_EXPECT_EQ(1, rpc_context.replay_message_count);
  CASE_EXPECT_EQ(101, rpc_context.replay_seq[0]);
  CASE_EXPECT_EQ(7001, rpc_context.replay_dsa_id[0]);
  CASE_EXPECT_EQ(8001, rpc_context.replay_ds_id[0]);
  CASE_EXPECT_EQ(cursor.replay_payload_text(), rpc_context.replay_payload[0]);
}
#include "task_action_send_to_ds.pseudo.h"

namespace {

class fake_agent_message_sender : public atorbit::dsc::forwarding::agent_message_sender {
public:
  int call_count = 0;

  atorbit::dsc::forwarding::result_code_t send_to_agent(const atorbit::dsc::session::ds_composite_key_t& ds_key,
                                                        const char* payload,
                                                        unsigned long long ack_seq,
                                                        bool require_ack) override {
    (void)ds_key;
    (void)payload;
    (void)ack_seq;
    (void)require_ack;
    ++call_count;
    return 0;
  }
};

}  // namespace

CASE_TEST(task_action_send_to_ds, run_task_action_send_to_ds_rejects_owner_mismatch) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::SendToDSReq request;
  atorbit::dsc::session::session_router session_router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::reliable_forwarder reliable_forwarder(&session_router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.connect(9528, 1002, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  request.set_request(9528, 7001, 8001, "hello", true, 101);

  auto result = atorbit::dsc::service::logic::action::run_task_action_send_to_ds(
      runtime, rpc_context, request, session_router, reliable_forwarder);

  CASE_EXPECT_EQ(13, result);
  CASE_EXPECT_EQ(0, sender.call_count);
}

CASE_TEST(task_action_send_to_ds, run_task_action_send_to_ds_forwards_when_owner_matches) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::SendToDSReq request;
  atorbit::dsc::session::session_router session_router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::reliable_forwarder reliable_forwarder(&session_router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  request.set_request(9527, 7001, 8001, "hello", true, 101);

  auto result = atorbit::dsc::service::logic::action::run_task_action_send_to_ds(
      runtime, rpc_context, request, session_router, reliable_forwarder);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, sender.call_count);
  CASE_EXPECT_EQ(1, reliable_forwarder.pending_ack_count());
  CASE_EXPECT_TRUE(rpc_context.send_to_ds_response_written);
  CASE_EXPECT_EQ(7001, rpc_context.send_to_ds_dsa_id);
  CASE_EXPECT_EQ(8001, rpc_context.send_to_ds_ds_id);
  CASE_EXPECT_TRUE(rpc_context.send_to_ds_require_ack);
  CASE_EXPECT_EQ(101, rpc_context.send_to_ds_ack_seq);
}
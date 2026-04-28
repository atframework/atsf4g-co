#include "../../dsc/forwarding/pending_ack_queue.pseudo.h"
#include "../../dsc/forwarding/reconnect_replay.pseudo.h"
#include "../../dsc/forwarding/reliable_forwarder.pseudo.h"
#include "../../dsc/forwarding/upstream_buffer_store.pseudo.h"
#include "../../dsc/registry/agent_registry.pseudo.h"
#include "../../dsc/scheduler/scheduler_service.pseudo.h"
#include "../../dsc/service/logic/action/task_action_handle_upstream_message.pseudo.h"
#include "../../dsc/session/launch_flow.pseudo.h"
#include "../../dsc/session/session_router.pseudo.h"

namespace {

class fake_agent_message_sender : public atorbit::dsc::forwarding::agent_message_sender {
public:
  int call_count = 0;
  const char* last_payload = nullptr;

  atorbit::dsc::forwarding::result_code_t send_to_agent(const atorbit::dsc::session::ds_composite_key_t& ds_key,
                                                        const char* payload,
                                                        unsigned long long ack_seq,
                                                        bool require_ack) override {
    (void)ds_key;
    (void)ack_seq;
    (void)require_ack;
    ++call_count;
    last_payload = payload;
    return 0;
  }
};

class fake_start_ds_sender : public atorbit::dsc::session::start_ds_sender {
public:
  int call_count = 0;

  atorbit::dsc::session::result_code_t send_start_ds(const atorbit::dsc::session::launch_dispatch_request_t& request) override {
    (void)request;
    ++call_count;
    return 0;
  }
};

class fake_external_upstream_sender : public atorbit::dsc::service::logic::action::external_upstream_sender {
public:
  int call_count = 0;
  int next_result = 0;

  atorbit::dsc::forwarding::result_code_t send_upstream_message(
      atorbit::dsc::session::unique_id_t owner_unique_id,
      const atorbit::dsc::forwarding::buffered_upstream_message_t& message) override {
    (void)owner_unique_id;
    (void)message;
    ++call_count;
    return next_result;
  }
};

}  // namespace

CASE_TEST(failure_regression_matrix, disconnect_recovery_replays_buffered_upstream_after_owner_reconnects) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::ForwardFromDSReq request;
  atorbit::dsc::session::session_router session_router;
  fake_external_upstream_sender sender;
  atorbit::dsc::forwarding::upstream_buffer_store upstream_buffer_store(4, 30000);
  atorbit::dsc::forwarding::reconnect_replay replay_engine(&upstream_buffer_store);
  atorbit::dsc::forwarding::buffered_upstream_message_t replay_messages[8] = {};
  atorbit::dsc::session::ds_composite_key_t ds_key;
  unsigned long long replay_count = 0;

  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  session_router.mark_disconnected(9527, 1001);
  request.set_request(7001, 8001, 101, "up-101");

  auto upstream_result = atorbit::dsc::service::logic::action::run_task_action_handle_upstream_message(
      runtime, rpc_context, request, session_router, sender, upstream_buffer_store);
  auto reconnect_result = session_router.reconnect(9527, 1002, "dsc://region-cn-east/controller-a", 100);
  auto replay_result = replay_engine.collect_replay_messages(9527, 100, replay_messages, 8, replay_count);

  CASE_EXPECT_EQ(0, upstream_result);
  CASE_EXPECT_EQ(0, reconnect_result);
  CASE_EXPECT_EQ(0, replay_result);
  CASE_EXPECT_EQ(1, replay_count);
  CASE_EXPECT_EQ(101, replay_messages[0].seq);
}

CASE_TEST(failure_regression_matrix, late_ack_is_ignored_after_pending_ack_has_timed_out) {
  atorbit::dsc::session::session_router session_router;
  fake_agent_message_sender sender;
  atorbit::dsc::forwarding::pending_ack_queue pending_ack_queue;
  atorbit::dsc::forwarding::pending_ack_record_t expired_records[4] = {};
  atorbit::dsc::forwarding::reliable_forwarder reliable_forwarder(&session_router, &sender, &pending_ack_queue);
  atorbit::dsc::session::ds_composite_key_t ds_key;

  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);

  auto forward_result = reliable_forwarder.forward_to_ds(9527, ds_key, "hello", true, 101);
  auto expired_count = reliable_forwarder.collect_expired_pending_acks(21000, expired_records, 4);
  auto late_ack_result = reliable_forwarder.complete_pending_ack(ds_key, 101);

  CASE_EXPECT_EQ(0, forward_result);
  CASE_EXPECT_EQ(1, expired_count);
  CASE_EXPECT_EQ(-1, late_ack_result);
  CASE_EXPECT_EQ(0, reliable_forwarder.pending_ack_count());
}

CASE_TEST(failure_regression_matrix, stale_inflight_response_is_rejected_after_timeout_prune) {
  atorbit::dsc::registry::agent_registry agent_registry;
  atorbit::dsc::registry::agent_registration_t registration;
  atorbit::dsc::registry::load_snapshot_t load;
  atorbit::dsc::scheduler::scheduler_service scheduler_service(&agent_registry);
  fake_start_ds_sender sender;
  atorbit::dsc::session::launch_flow launch_flow(&sender);
  atorbit::dsc::scheduler::launch_request_t scheduler_request;
  atorbit::dsc::session::launch_dispatch_request_t dispatch_request;

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  agent_registry.upsert_registered_agent(registration);
  load.cpu_available = 8.0;
  load.memory_available_mb = 8192.0;
  agent_registry.update_agent_load(7001, load);

  scheduler_request.owner_unique_id = 9527;
  scheduler_request.target_region = "region-cn-east";
  scheduler_request.expected_cpu = 1.0;
  scheduler_request.expected_memory_mb = 1024.0;
  auto selected = scheduler_service.select_agent_for_launch(scheduler_request);

  dispatch_request.owner_unique_id = 9527;
  dispatch_request.agent_id = selected.agent_id;
  dispatch_request.request_id = 5001;
  dispatch_request.expected_cpu = 1.0;
  dispatch_request.expected_memory_mb = 1024.0;
  dispatch_request.now_ms = 1000;

  auto dispatch_result = launch_flow.reserve_and_dispatch(dispatch_request);
  auto pruned_count = launch_flow.prune_expired_inflight(61000);
  auto stale_response_result = launch_flow.complete_inflight(selected.agent_id, 5001);

  CASE_EXPECT_TRUE(selected.found);
  CASE_EXPECT_EQ(0, dispatch_result);
  CASE_EXPECT_EQ(1, pruned_count);
  CASE_EXPECT_EQ(-1, stale_response_result);
  CASE_EXPECT_EQ(0, launch_flow.inflight_count(selected.agent_id));
}
#include "launch_flow.pseudo.h"

namespace {

class fake_start_ds_sender : public atorbit::dsc::session::start_ds_sender {
public:
  int call_count = 0;
  atorbit::dsc::session::launch_dispatch_request_t last_request;
  int next_result = 0;

  atorbit::dsc::session::result_code_t send_start_ds(const atorbit::dsc::session::launch_dispatch_request_t& request) override {
    ++call_count;
    last_request = request;
    return next_result;
  }
};

}  // namespace

CASE_TEST(launch_flow, reserve_and_dispatch_tracks_inflight_before_sending_start_ds) {
  fake_start_ds_sender sender;
  atorbit::dsc::session::launch_flow flow(&sender);
  atorbit::dsc::session::launch_dispatch_request_t request;

  request.owner_unique_id = 9527;
  request.agent_id = 7001;
  request.request_id = 5001;
  request.expected_cpu = 1.0;
  request.expected_memory_mb = 1024.0;
  request.now_ms = 1000;

  auto result = flow.reserve_and_dispatch(request);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, sender.call_count);
  CASE_EXPECT_EQ(1, flow.inflight_count(7001));
}

CASE_TEST(launch_flow, reserve_and_dispatch_rolls_back_inflight_when_send_fails) {
  fake_start_ds_sender sender;
  atorbit::dsc::session::launch_flow flow(&sender);
  atorbit::dsc::session::launch_dispatch_request_t request;

  sender.next_result = -1;
  request.owner_unique_id = 9527;
  request.agent_id = 7001;
  request.request_id = 5001;
  request.expected_cpu = 1.0;
  request.expected_memory_mb = 1024.0;
  request.now_ms = 1000;

  auto result = flow.reserve_and_dispatch(request);

  CASE_EXPECT_EQ(-3, result);
  CASE_EXPECT_EQ(0, flow.inflight_count(7001));
}

CASE_TEST(launch_flow, prune_expired_inflight_removes_timed_out_entries) {
  fake_start_ds_sender sender;
  atorbit::dsc::session::launch_flow flow(&sender);
  atorbit::dsc::session::launch_dispatch_request_t request;

  request.owner_unique_id = 9527;
  request.agent_id = 7001;
  request.request_id = 5001;
  request.expected_cpu = 1.0;
  request.expected_memory_mb = 1024.0;
  request.now_ms = 1000;
  flow.reserve_and_dispatch(request);

  auto pruned_count = flow.prune_expired_inflight(61000);
  auto stale_complete_result = flow.complete_inflight(7001, 5001);

  CASE_EXPECT_EQ(1, pruned_count);
  CASE_EXPECT_EQ(-1, stale_complete_result);
  CASE_EXPECT_EQ(0, flow.inflight_count(7001));
}
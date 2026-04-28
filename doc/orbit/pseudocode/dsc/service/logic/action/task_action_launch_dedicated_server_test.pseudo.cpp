#include "task_action_launch_dedicated_server.pseudo.h"

namespace {

class fake_start_ds_sender : public atorbit::dsc::session::start_ds_sender {
public:
  int call_count = 0;

  atorbit::dsc::session::result_code_t send_start_ds(const atorbit::dsc::session::launch_dispatch_request_t& request) override {
    (void)request;
    ++call_count;
    return 0;
  }
};

}  // namespace

CASE_TEST(task_action_launch_dedicated_server, run_task_action_launch_dedicated_server_selects_agent_and_dispatches_start_ds) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::app::rpc_context_t rpc_context;
  atorbit::dsc::service::app::LaunchDedicatedServerReq request;
  atorbit::dsc::registry::agent_registry agent_registry;
  atorbit::dsc::registry::agent_registration_t registration;
  atorbit::dsc::registry::load_snapshot_t load;
  atorbit::dsc::scheduler::scheduler_service scheduler_service(&agent_registry);
  fake_start_ds_sender sender;
  atorbit::dsc::session::launch_flow launch_flow(&sender);
  const char* custom_args[] = {"-log"};

  registration.agent_id = 7001;
  registration.region = "region-cn-east";
  agent_registry.upsert_registered_agent(registration);
  load.cpu_available = 8.0;
  load.memory_available_mb = 8192.0;
  agent_registry.update_agent_load(7001, load);

  request.set_request(9527, "region-cn-east", 1.0, 1024.0, custom_args, 1);

  auto result = atorbit::dsc::service::logic::action::run_task_action_launch_dedicated_server(
      runtime, rpc_context, request, scheduler_service, launch_flow);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, sender.call_count);
  CASE_EXPECT_EQ(1, launch_flow.inflight_count(7001));
}
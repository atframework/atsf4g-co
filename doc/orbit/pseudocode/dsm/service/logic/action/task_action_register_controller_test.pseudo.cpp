#include "task_action_register_controller.pseudo.h"

CASE_TEST(task_action_register_controller, run_task_action_register_controller_inserts_catalog_record) {
  atorbit::dsm::service::app::runtime_handle_t runtime = 0;
  atorbit::dsm::service::app::rpc_context_t rpc_context;
  atorbit::dsm::service::app::RegisterControllerReq request;
  atorbit::dsm::topology::cluster_catalog cluster_catalog;

  request.set_request(5001, "region-cn-east", "dsc://region-cn-east/controller-a");

  auto result = atorbit::dsm::service::logic::action::run_task_action_register_controller(
      runtime, rpc_context, request, cluster_catalog);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_TRUE(cluster_catalog.has_controller(5001));
  CASE_EXPECT_TRUE(rpc_context.register_controller_response_written);
  CASE_EXPECT_EQ(5001, rpc_context.registered_controller_id);
  CASE_EXPECT_EQ("region-cn-east", rpc_context.registered_region);
}
// Copyright 2026 atframework

// Sampling contract test for the orbit SDK SS consumer path (IMPLEMENTATION_PLAN.md 阶段 9):
// rpc::servertocontrollerservice::launch_client sends a request to an orbit controller node; the SS
// mock engine answers it offline. Unlike rank/dtmq/dtcoordsvr, orbit has no dedicated discovery_selector
// field, so no hpa_scaling_ready metadata label is required on the mock node.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/server_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <chrono>

#include "config/extern_service_types.h"
#include "frame/test_macros.h"
#include "logic/logic_server_setup.h"
#include "rpc/servertocontrollerservice/servertocontrollerservice.atfw.gen.h"

CASE_TEST(component_orbit, orbit_sdk_launch_client_contract) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  atframework::testing::mock_node node;
  node.set_id(0x1D0001)
      .set_name("unit-test-orbit-controller")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kOrbitControllerSvr))
      .set_type_name("orbit-controller")
      .set_zone_id(1);
  auto remote = test.discovery().add_node(node);
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }

  auto rule = test.ss().mock(rpc::servertocontrollerservice::get_full_name_of_launch_client(),
                             orbit::STCLaunchClientReq::descriptor()->full_name(),
                             orbit::CTSLaunchClientRsp::descriptor()->full_name(),
                             [](const atframework::testing::ss_request_view &request,
                                google::protobuf::Message &response) -> rpc::result_code_type {
                               const auto &typed_request = static_cast<const orbit::STCLaunchClientReq &>(request.body);
                               CASE_EXPECT_EQ(0x1D0001, static_cast<int64_t>(request.target_node_id));
                               CASE_EXPECT_EQ("unit-test-orbit-match-tag", typed_request.args().match_tag());
                               static_cast<orbit::CTSLaunchClientRsp &>(response).set_error_code(0);
                               RPC_RETURN_CODE(0);
                             });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_MSG_INFO() << "mock registration failed: " << test.ss().get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto task =
      test.run_task("orbit_launch_client", std::chrono::seconds{3}, [](rpc::context &ctx) -> rpc::result_code_type {
        orbit::STCLaunchClientReq req;
        req.mutable_args()->set_match_tag("unit-test-orbit-match-tag");
        req.mutable_server_identity()->set_unique_id(0x1D0002);
        orbit::CTSLaunchClientRsp rsp;

        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::servertocontrollerservice::launch_client(ctx, 0x1D0001, req, rsp));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(0, rsp.error_code());
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(1,
                 static_cast<int>(test.ss().calls(rpc::servertocontrollerservice::get_full_name_of_launch_client())));

  CASE_EXPECT_EQ(0, test.stop());
}

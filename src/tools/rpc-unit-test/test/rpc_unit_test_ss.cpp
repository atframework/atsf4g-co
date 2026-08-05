// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>

#include "dispatcher/ss_msg_dispatcher.h"
#include "frame/test_macros.h"
#include "rpc/logic/logiccommonservice.atfw.gen.h"
#include "rpc/router/routerservice.atfw.gen.h"

namespace {
static atframework::testing::mock_node make_remote_node(uint64_t id, const char *name) {
  atframework::testing::mock_node node;
  node.set_id(id).set_name(name).set_type_id(4097).set_type_name("rpc-unit-test-remote").set_zone_id(1);
  return node;
}
}  // namespace

CASE_TEST(rpc_unit_test, ss_unary_suspend_resume) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x130001, "unit-test-remote-ss"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  auto rule = test.ss().mock(
      PROJECT_NAMESPACE ".RouterService/router_transfer", PROJECT_NAMESPACE_ID::SSRouterTransferReq::descriptor()->full_name(),
      PROJECT_NAMESPACE_ID::SSRouterTransferRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view &request, google::protobuf::Message &) -> int {
        const auto &typed_request = static_cast<const PROJECT_NAMESPACE_ID::SSRouterTransferReq &>(request.body);
        CASE_EXPECT_EQ(7, typed_request.object().object_type_id());
        CASE_EXPECT_EQ(0x130001, static_cast<int64_t>(request.target_node_id));
        return 0;
      });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_MSG_INFO() << "mock registration failed: " << test.ss().get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto task = test.run_task(
      "ss_unary", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::SSRouterTransferReq req_body;
        req_body.mutable_object()->set_object_type_id(7);
        PROJECT_NAMESPACE_ID::SSRouterTransferRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::router::router_transfer(ctx, 0x130001, req_body, rsp_body));
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_FALSE(result.task_timed_out);
  CASE_EXPECT_FALSE(result.runtime_poisoned);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(PROJECT_NAMESPACE ".RouterService/router_transfer")));
  const atframework::testing::ss_call_record *record = test.ss().call_at(0);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_EQ(0x130001, static_cast<int64_t>(record->target_node_id));
    CASE_EXPECT_FALSE(record->is_stream);
    CASE_EXPECT_TRUE(record->matched_rule);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE ".SSRouterTransferReq", record->request_type_url);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_unary_business_error_code) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x130011, "unit-test-remote-ss-err"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  auto rule = test.ss().mock(
      PROJECT_NAMESPACE ".RouterService/router_transfer", PROJECT_NAMESPACE_ID::SSRouterTransferReq::descriptor()->full_name(),
      PROJECT_NAMESPACE_ID::SSRouterTransferRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view &, google::protobuf::Message &) -> int { return -54321; });
  CASE_EXPECT_TRUE(!!rule);

  auto task = test.run_task(
      "ss_unary_error", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::SSRouterTransferReq req_body;
        PROJECT_NAMESPACE_ID::SSRouterTransferRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::router::router_transfer(ctx, 0x130011, req_body, rsp_body));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(-54321, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_unmatched_unary_fast_fail) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x130021, "unit-test-remote-ss-unmatched"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  // No mock rule registered: the unmatched unary call must fail fast instead of waiting to timeout.
  auto task = test.run_task(
      "ss_unmatched", std::chrono::seconds{30}, [](rpc::context &ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::SSRouterTransferReq req_body;
        PROJECT_NAMESPACE_ID::SSRouterTransferRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::router::router_transfer(ctx, 0x130021, req_body, rsp_body));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_FALSE(result.task_timed_out);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_NE(0, result.result_code);
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(PROJECT_NAMESPACE ".RouterService/router_transfer")));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_stream_record_only) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x130031, "unit-test-remote-ss-stream"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "ss_stream", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::SSGlobalLogicSetServerTimeSync req_body;
        req_body.set_global_now_offset(12345);
        int32_t res = rpc::logic::set_server_time(ctx, 0x130031, req_body);
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // The stream message is recorded by the engine; no response is injected.
  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(PROJECT_NAMESPACE ".LogicCommonService/set_server_time")));
  const atframework::testing::ss_call_record *record = test.ss().call_at(0);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_TRUE(record->is_stream);
    CASE_EXPECT_EQ(0x130031, static_cast<int64_t>(record->target_node_id));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_broadcast_to_all_nodes) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote_a = test.discovery().add_node(make_remote_node(0x130041, "unit-test-remote-ss-broadcast-a"));
  auto remote_b = test.discovery().add_node(make_remote_node(0x130042, "unit-test-remote-ss-broadcast-b"));
  CASE_EXPECT_TRUE(!!remote_a);
  CASE_EXPECT_TRUE(!!remote_b);
  if (!remote_a || !remote_b) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "ss_broadcast", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::SSGlobalLogicSetServerTimeSync req_body;
        req_body.set_global_now_offset(6789);
        // Empty index selects the whole global discovery set.
        int32_t res = rpc::logic::broadcast::set_server_time(ctx, req_body, ss_msg_logic_index{});
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(2, static_cast<int>(test.ss().calls(PROJECT_NAMESPACE ".LogicCommonService/set_server_time")));
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().outbound_count_to(0x130041)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().outbound_count_to(0x130042)));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_expectation_verified_at_stop) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x130051, "unit-test-remote-ss-expect"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  auto rule = test.ss().mock(
      PROJECT_NAMESPACE ".RouterService/router_transfer", PROJECT_NAMESPACE_ID::SSRouterTransferReq::descriptor()->full_name(),
      PROJECT_NAMESPACE_ID::SSRouterTransferRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view &, google::protobuf::Message &) -> int { return 0; });

  test.ss().expect(PROJECT_NAMESPACE ".RouterService/router_transfer").times(1).to_node(0x130051);

  auto task = test.run_task(
      "ss_expect", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::SSRouterTransferReq req_body;
        PROJECT_NAMESPACE_ID::SSRouterTransferRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::router::router_transfer(ctx, 0x130051, req_body, rsp_body));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_EQ(0, result.result_code);

  // The expectation is satisfied, stop() stays 0.
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_failed_expectation_reported_at_stop) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  // No call will ever happen; the expectation must be reported by stop().
  test.ss().expect(PROJECT_NAMESPACE ".RouterService/router_transfer").times(1);

  CASE_EXPECT_EQ(2, test.stop());
  CASE_EXPECT_FALSE(test.get_diagnostic().empty());
}

CASE_TEST(rpc_unit_test, ss_mock_registration_validation) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  // Invalid name format.
  auto bad_rule = test.ss().mock("no-separator", PROJECT_NAMESPACE_ID::SSRouterTransferReq::descriptor()->full_name(),
                                 PROJECT_NAMESPACE_ID::SSRouterTransferRsp::descriptor()->full_name(),
                                 [](const atframework::testing::ss_request_view &, google::protobuf::Message &) -> int {
                                   return 0;
                                 });
  CASE_EXPECT_FALSE(!!bad_rule);
  CASE_EXPECT_FALSE(test.ss().get_diagnostic().empty());

  // Unknown service.
  auto unknown_rule = test.ss().mock(PROJECT_NAMESPACE ".NoSuchService/method", PROJECT_NAMESPACE_ID::SSRouterTransferReq::descriptor()->full_name(),
                                     PROJECT_NAMESPACE_ID::SSRouterTransferRsp::descriptor()->full_name(),
                                     [](const atframework::testing::ss_request_view &, google::protobuf::Message &) -> int {
                                       return 0;
                                     });
  CASE_EXPECT_FALSE(!!unknown_rule);

  // Request type mismatch.
  auto mismatch_rule = test.ss().mock(
      PROJECT_NAMESPACE ".RouterService/router_transfer", PROJECT_NAMESPACE_ID::SSGlobalLogicSetServerTimeSync::descriptor()->full_name(),
      PROJECT_NAMESPACE_ID::SSRouterTransferRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view &, google::protobuf::Message &) -> int { return 0; });
  CASE_EXPECT_FALSE(!!mismatch_rule);

  CASE_EXPECT_EQ(0, test.stop());
}

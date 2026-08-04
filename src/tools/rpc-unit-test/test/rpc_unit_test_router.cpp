// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rpc_unit_test.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>
#include <string>

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_router.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include "dispatcher/ss_msg_dispatcher.h"
#include "frame/test_macros.h"
#include "rpc/internal/rpc_template_ss_message.h"
#include "rpc/unit_test/rpcunittestservice.atfw.gen.h"

namespace {
constexpr uint32_t kRouterTypeId = PROJECT_NAMESPACE_ID::EN_ROT_UNIT_TEST;
constexpr uint32_t kZoneId = 1;
constexpr uint64_t kObjectId = 42;
constexpr uint64_t kRouterServerId = 0x150001;

static atsf4g::testing::mock_node make_remote_node(uint64_t id, const char *name) {
  atsf4g::testing::mock_node node;
  node.set_id(id).set_name(name).set_type_id(4097).set_type_name("rpc-unit-test-remote").set_zone_id(kZoneId);
  return node;
}

static bool start_with_router(atsf4g::testing::runtime &test) {
  atsf4g::testing::runtime_options options;
  options.features = {atsf4g::testing::feature::ss, atsf4g::testing::feature::router};
  return 0 == test.start(options) && test.is_running();
}
}  // namespace

CASE_TEST(rpc_unit_test, router_unary_suspend_resume) {
  atsf4g::testing::runtime test;
  CASE_EXPECT_TRUE(start_with_router(test));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  auto router_server = test.discovery().add_node(make_remote_node(kRouterServerId, "unit-test-router-server"));
  CASE_EXPECT_TRUE(!!router_server);
  auto *manager = test.router().create_manager(kRouterTypeId);
  CASE_EXPECT_TRUE(nullptr != manager);
  if (!router_server || nullptr == manager) {
    test.stop();
    return;
  }
  auto object = manager->add_object(router_object_base::key_t{kRouterTypeId, kZoneId, kObjectId}, kRouterServerId);
  CASE_EXPECT_TRUE(!!object);

  auto rule = rpc::unit_test::mock::rpc_unit_test_router_unary(
      test.ss(),
      [](const atsf4g::testing::ss_request<rpc_unit_test::RpcUnitTestRouterReq> &request,
         rpc_unit_test::RpcUnitTestRouterRsp &response) -> int {
        CASE_EXPECT_EQ("hello-router", request.body.payload());
        response.set_echo(request.body.payload());
        return 0;
      });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_MSG_INFO() << "mock registration failed: " << test.ss().get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto task = test.run_task(
      "router_unary", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc_unit_test::RpcUnitTestRouterReq req_body;
        req_body.set_payload("hello-router");
        rpc_unit_test::RpcUnitTestRouterRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_router_unary(ctx, kRouterTypeId, kZoneId, kObjectId, req_body, rsp_body));
        CASE_EXPECT_EQ("hello-router", rsp_body.echo());
        RPC_RETURN_CODE(res);
      });
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

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls("rpc_unit_test.RpcUnitTestService/rpc_unit_test_router_unary")));
  const atsf4g::testing::ss_call_record *record = test.ss().call_at(0);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    // Router messages are finally delivered to the router server resolved from the cache object.
    CASE_EXPECT_EQ(kRouterServerId, record->target_node_id);
    CASE_EXPECT_FALSE(record->is_stream);
    CASE_EXPECT_TRUE(record->matched_rule);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, router_unary_missing_cache_fast_fail) {
  atsf4g::testing::runtime test;
  CASE_EXPECT_TRUE(start_with_router(test));
  if (!test.is_running()) {
    return;
  }

  auto *manager = test.router().create_manager(kRouterTypeId);
  CASE_EXPECT_TRUE(nullptr != manager);
  if (nullptr == manager) {
    test.stop();
    return;
  }
  // No object seeded: mutable_cache must fail fast without any SS traffic.

  auto task = test.run_task(
      "router_unary_missing", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc_unit_test::RpcUnitTestRouterReq req_body;
        rpc_unit_test::RpcUnitTestRouterRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_router_unary(ctx, kRouterTypeId, kZoneId, 0xDEADBEEF, req_body, rsp_body));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND, result.result_code);
  CASE_EXPECT_EQ(0, static_cast<int>(test.ss().calls("rpc_unit_test.RpcUnitTestService/rpc_unit_test_router_unary")));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, router_stream_record_only) {
  atsf4g::testing::runtime test;
  CASE_EXPECT_TRUE(start_with_router(test));
  if (!test.is_running()) {
    return;
  }

  auto router_server = test.discovery().add_node(make_remote_node(kRouterServerId, "unit-test-router-server-stream"));
  auto *manager = test.router().create_manager(kRouterTypeId);
  CASE_EXPECT_TRUE(!!router_server);
  CASE_EXPECT_TRUE(nullptr != manager);
  if (!router_server || nullptr == manager) {
    test.stop();
    return;
  }
  auto object = manager->add_object(router_object_base::key_t{kRouterTypeId, kZoneId, kObjectId}, kRouterServerId);
  CASE_EXPECT_TRUE(!!object);

  auto task = test.run_task(
      "router_stream", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc_unit_test::RpcUnitTestRouterReq req_body;
        req_body.set_payload("router-stream");
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_router_stream(ctx, kRouterTypeId, kZoneId, kObjectId, req_body));
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
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls("rpc_unit_test.RpcUnitTestService/rpc_unit_test_router_stream")));
  const atsf4g::testing::ss_call_record *record = test.ss().call_at(0);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_TRUE(record->is_stream);
    CASE_EXPECT_EQ(kRouterServerId, record->target_node_id);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, user_rpc_unary_head_fields) {
  atsf4g::testing::runtime test;
  atsf4g::testing::runtime_options options;
  options.features = {atsf4g::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x140011, "unit-test-remote-user-rpc"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  auto rule = test.ss().mock<rpc_unit_test::RpcUnitTestEchoReq, rpc_unit_test::RpcUnitTestEchoRsp>(
      "rpc_unit_test.RpcUnitTestService/rpc_unit_test_user",
      [](const atsf4g::testing::ss_request<rpc_unit_test::RpcUnitTestEchoReq> &request,
         rpc_unit_test::RpcUnitTestEchoRsp &response) -> int {
        CASE_EXPECT_EQ(kZoneId, request.head.player_zone_id());
        CASE_EXPECT_EQ(10001, static_cast<int64_t>(request.head.player_user_id()));
        CASE_EXPECT_EQ("openid-x", request.head.player_open_id());
        response.set_echo(request.body.payload());
        return 0;
      });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "user_rpc", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc_unit_test::RpcUnitTestEchoReq req_body;
        req_body.set_payload("user-rpc");
        rpc_unit_test::RpcUnitTestEchoRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_user(ctx, 0x140011, kZoneId, 10001, "openid-x", req_body, rsp_body));
        CASE_EXPECT_EQ("user-rpc", rsp_body.echo());
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls("rpc_unit_test.RpcUnitTestService/rpc_unit_test_user")));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, broadcast_per_target_policy) {
  atsf4g::testing::runtime test;
  atsf4g::testing::runtime_options options;
  options.features = {atsf4g::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote_a = test.discovery().add_node(make_remote_node(0x140041, "unit-test-remote-broadcast-a"));
  auto remote_b = test.discovery().add_node(make_remote_node(0x140042, "unit-test-remote-broadcast-b"));
  CASE_EXPECT_TRUE(!!remote_a);
  CASE_EXPECT_TRUE(!!remote_b);
  if (!remote_a || !remote_b) {
    test.stop();
    return;
  }

  // Only node A gets an explicit rule; node B falls back to the default one-way record+drop policy.
  atsf4g::testing::ss_rule_options options_a;
  options_a.match_node_id = 0x140041;
  auto rule_a = rpc::unit_test::mock::rpc_unit_test_broadcast(
      test.ss(),
      [](const atsf4g::testing::ss_request<rpc_unit_test::RpcUnitTestEchoReq> &request,
         rpc_unit_test::RpcUnitTestEchoRsp &) -> int {
        CASE_EXPECT_EQ("per-target", request.body.payload());
        CASE_EXPECT_EQ(0x140041, static_cast<int64_t>(request.target_node_id));
        return 0;
      },
      options_a);
  CASE_EXPECT_TRUE(!!rule_a);
  if (!rule_a) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "broadcast_per_target", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc_unit_test::RpcUnitTestEchoReq req_body;
        req_body.set_payload("per-target");
        // Empty index selects the whole global discovery set.
        int32_t res = rpc::unit_test::broadcast::rpc_unit_test_broadcast(ctx, req_body, ss_msg_logic_index{});
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
  CASE_EXPECT_EQ(2, static_cast<int>(test.ss().calls("rpc_unit_test.RpcUnitTestService/rpc_unit_test_broadcast")));
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().outbound_count_to(0x140041)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.transport().outbound_count_to(0x140042)));

  // Each target is recorded independently: A matched the rule, B used the default policy.
  bool saw_matched_a = false;
  bool saw_default_b = false;
  for (size_t i = 0; i < 2; ++i) {
    const atsf4g::testing::ss_call_record *record = test.ss().call_at(i);
    if (nullptr == record) {
      continue;
    }
    if (0x140041 == record->target_node_id && record->matched_rule) {
      saw_matched_a = true;
    }
    if (0x140042 == record->target_node_id && !record->matched_rule) {
      saw_default_b = true;
    }
  }
  CASE_EXPECT_TRUE(saw_matched_a);
  CASE_EXPECT_TRUE(saw_default_b);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, no_wait_immediate_return) {
  atsf4g::testing::runtime test;
  atsf4g::testing::runtime_options options;
  options.features = {atsf4g::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x140021, "unit-test-remote-no-wait"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "no_wait", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc_unit_test::RpcUnitTestEchoReq req_body;
        req_body.set_payload("no-wait");
        rpc_unit_test::RpcUnitTestEchoRsp rsp_body;
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_no_wait(ctx, 0x140021, req_body, rsp_body, true, nullptr));
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
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls("rpc_unit_test.RpcUnitTestService/rpc_unit_test_no_wait")));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, no_wait_wait_later_response) {
  atsf4g::testing::runtime test;
  atsf4g::testing::runtime_options options;
  options.features = {atsf4g::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_remote_node(0x140031, "unit-test-remote-wait-later"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  auto rule = test.ss().mock<rpc_unit_test::RpcUnitTestEchoReq, rpc_unit_test::RpcUnitTestEchoRsp>(
      "rpc_unit_test.RpcUnitTestService/rpc_unit_test_no_wait",
      [](const atsf4g::testing::ss_request<rpc_unit_test::RpcUnitTestEchoReq> &request,
         rpc_unit_test::RpcUnitTestEchoRsp &response) -> int {
        response.set_echo(request.body.payload());
        return 0;
      });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "wait_later", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc_unit_test::RpcUnitTestEchoReq req_body;
        req_body.set_payload("wait-later");
        rpc_unit_test::RpcUnitTestEchoRsp rsp_body;
        dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_no_wait(ctx, 0x140031, req_body, rsp_body, false, &await_options));
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }
        res = RPC_AWAIT_CODE_RESULT(rpc::internal::wait_and_unpack_ss_response(
            ctx, rsp_body, "rpc_unit_test.RpcUnitTestService/rpc_unit_test_no_wait",
            "rpc_unit_test.RpcUnitTestEchoRsp", await_options));
        CASE_EXPECT_EQ("wait-later", rsp_body.echo());
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rpc_unit_test.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_dns.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <utility>
#include <vector>

#include "frame/test_macros.h"
#include "rpc/db/local_db_interface.atfw.gen.h"
#include "rpc/dns/lookup.h"
#include "rpc/rpc_utils.h"
#include "rpc/unit_test/rpcunittestservice.atfw.gen.h"

CASE_TEST(rpc_unit_test, runtime_start_stop) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::dns, atfw::testing::feature::db};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_empty_task) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  auto task = test.run_task("empty_task", std::chrono::seconds{2},
                            [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
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

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_task_business_result_code) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto task = test.run_task("business_error", std::chrono::seconds{2},
                            [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(42); });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(42, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_task_timeout) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto task =
      test.run_task("task_timeout", std::chrono::milliseconds{300}, [](rpc::context &ctx) -> rpc::result_code_type {
        // Wait on a custom type/sequence that is never resumed; the task-level timeout must fire.
        static int never_resumed_rpc_type;
        auto await_options = dispatcher_make_default<dispatcher_await_options>();
        await_options.sequence = 1;
        await_options.timeout = std::chrono::seconds{30};
        auto res = RPC_AWAIT_CODE_RESULT(rpc::custom_wait(ctx, &never_resumed_rpc_type, await_options));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_TRUE(result.task_timed_out);
  CASE_EXPECT_FALSE(result.runtime_poisoned);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, runtime_consecutive_fixture) {
  // A second runtime must start cleanly after the previous one stopped: no leaked app, task or
  // process-level state may block a consecutive fixture.
  for (int i = 0; i < 2; ++i) {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;
    options.features = {atfw::testing::feature::ss};

    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      CASE_MSG_INFO() << "fixture " << i << " start failed: " << test.get_diagnostic() << '\n';
      return;
    }

    auto task = test.run_task("consecutive", std::chrono::seconds{2},
                              [i](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(i); });
    if (!task.empty()) {
      auto result = test.wait(task, std::chrono::seconds{5});
      CASE_EXPECT_TRUE(result.task_exited);
      CASE_EXPECT_EQ(i, result.result_code);
    }

    CASE_EXPECT_EQ(0, test.stop());
  }
}

// Required combined smoke (see doc/docs/development/rpc-unit-test.md): one task performs DNS lookup -> SS RPC ->
// DB write/read in a single coroutine, and the outer wait drives everything to completion.
CASE_TEST(rpc_unit_test, combined_dns_ss_db_smoke) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::dns, atfw::testing::feature::db};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();

  // DNS: resolve the directory name of the remote node.
  auto dns_rule = test.dns().mock_a("directory.unit-test.local", "10.7.7.7");
  CASE_EXPECT_TRUE(!!dns_rule);

  // SS: echo service on the "resolved" node.
  atfw::testing::mock_node node;
  node.set_id(0x130081)
      .set_name("unit-test-combined-remote")
      .set_type_id(4097)
      .set_type_name("rpc-unit-test-remote")
      .set_zone_id(1);
  auto remote = test.discovery().add_node(node);
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }
  auto ss_rule = test.ss().mock(
      rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user(),
      rpc_unit_test::RpcUnitTestEchoReq::descriptor()->full_name(),
      rpc_unit_test::RpcUnitTestEchoRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view &request, google::protobuf::Message &response) -> rpc::result_code_type {
        const auto &typed_request = static_cast<const rpc_unit_test::RpcUnitTestEchoReq &>(request.body);
        static_cast<rpc_unit_test::RpcUnitTestEchoRsp &>(response).set_echo("combined:" + typed_request.payload());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ss_rule);
  if (!dns_rule || !ss_rule) {
    test.stop();
    return;
  }

  auto task =
      test.run_task("combined_dns_ss_db", std::chrono::seconds{4}, [](rpc::context &ctx) -> rpc::result_code_type {
        // 1. DNS lookup.
        std::vector<rpc::dns::address_record> records;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "directory.unit-test.local", records));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(records.size()));
        if (res < 0 || records.empty()) {
          RPC_RETURN_CODE(res < 0 ? res : -1);
        }

        // 2. SS RPC to the node selected by the mocked directory.
        rpc_unit_test::RpcUnitTestEchoReq req_body;
        req_body.set_payload(records[0].address);
        rpc_unit_test::RpcUnitTestEchoRsp rsp_body;
        res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_user(ctx, 0x130081, 1, 10001, "openid-combined", req_body, rsp_body));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ("combined:10.7.7.7", rsp_body.echo());
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }

        // 3. DB write + read of the SS result.
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> store{ctx};
        store->set_open_id("openid-combined");
        store->set_user_id(88);
        uint64_t version = 0;
        res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, std::move(store), version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }

        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> loaded{ctx};
        version = 0;
        res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-combined", *loaded, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(88, static_cast<int>(loaded->user_id()));
        CASE_EXPECT_EQ(1, static_cast<int>(version));
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // All three engines observed exactly one call each.
  CASE_EXPECT_EQ(1, static_cast<int>(test.dns().calls("directory.unit-test.local")));
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user())));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls("login_auth", atfw::testing::mock_db::op_type::kv_set)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls("login_auth", atfw::testing::mock_db::op_type::kv_get_all)));

  CASE_EXPECT_EQ(0, test.stop());
}

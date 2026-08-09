// Copyright 2026 atframework

// Compiled mirror of the README.md examples: every snippet shown in the
// README is exercised here so documentation and API never drift apart.

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
#include "rpc/unit_test/rpcunittestservice.atfw.gen.h"

namespace {
atfw::testing::mock_node make_example_remote(uint64_t id, const char *name) {
  atfw::testing::mock_node node;
  node.set_id(id).set_name(name).set_type_id(4097).set_type_name("rpc-unit-test-remote").set_zone_id(1);
  return node;
}
}  // namespace

// README: minimal fixture skeleton (runtime + run_task + wait).
CASE_TEST(rpc_unit_test_readme, minimal_fixture) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  auto task = test.run_task("hello", std::chrono::seconds{2},
                            [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// README: discovery node + typed SS mock (engine level and generated <service>::mock).
CASE_TEST(rpc_unit_test_readme, ss_mock_and_expectations) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_example_remote(0x130091, "unit-test-readme-remote"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  // Engine-level typed rule: full RPC name + request/response type names.
  auto rule = test.ss().mock(
      rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user(),
      rpc_unit_test::RpcUnitTestEchoReq::descriptor()->full_name(),
      rpc_unit_test::RpcUnitTestEchoRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view &request, google::protobuf::Message &response) -> rpc::result_code_type {
        const auto &req = static_cast<const rpc_unit_test::RpcUnitTestEchoReq &>(request.body);
        static_cast<rpc_unit_test::RpcUnitTestEchoRsp &>(response).set_echo("hello " + req.payload());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);

  test.ss().expect(rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user()).times(1).to_node(0x130091);

  auto task = test.run_task("readme_ss", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    rpc_unit_test::RpcUnitTestEchoReq req_body;
    req_body.set_payload("world");
    rpc_unit_test::RpcUnitTestEchoRsp rsp_body;
    int32_t res = RPC_AWAIT_CODE_RESULT(
        rpc::unit_test::rpc_unit_test_user(ctx, 0x130091, 1, 10001, "openid-readme", req_body, rsp_body));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ("hello world", rsp_body.echo());
    RPC_RETURN_CODE(res);
  });
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // History assertions.
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user())));

  // stop() verifies expectations first; a failed expectation makes stop() non-zero.
  CASE_EXPECT_EQ(0, test.stop());
}

// README: DNS rule.
CASE_TEST(rpc_unit_test_readme, dns_mock) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::dns};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto rule = test.dns().mock_a("example.unit-test.local", "192.0.2.1");
  CASE_EXPECT_TRUE(!!rule);

  auto task = test.run_task("readme_dns", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    std::vector<rpc::dns::address_record> records;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "example.unit-test.local", records));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(records.size()));
    if (!records.empty()) {
      CASE_EXPECT_EQ("192.0.2.1", records[0].address);
    }
    RPC_RETURN_CODE(res);
  });
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// README: generated per-table typed DB mock handler (SS style) + default in-memory backend fallthrough.
CASE_TEST(rpc_unit_test_readme, db_mock_handler_and_backend) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::db};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();

  // Typed handler for one table interface; rpc::unit_test::db_mock_meta carries the CAS version.
  auto get_rule = rpc::db::login_auth::mock::get_all(
      [](rpc::context &, const PROJECT_NAMESPACE_ID::table_login_auth &input,
         PROJECT_NAMESPACE_ID::table_login_auth &output, rpc::unit_test::db_mock_meta &meta) -> rpc::result_code_type {
        output.set_open_id(input.open_id());
        output.set_user_id(7);
        meta.version = 3;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!get_rule);

  auto task = test.run_task("readme_db", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    // Intercepted: served by the handler above.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> loaded{ctx};
    uint64_t version = 0;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-readme", loaded, version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(7, static_cast<int>(loaded->user_id()));
    CASE_EXPECT_EQ(3, static_cast<int>(version));

    // Not intercepted: replace/remove_all fall through to the in-memory backend.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> store{ctx};
    store->set_open_id("openid-other");
    store->set_user_id(9);
    version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, std::move(store), version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));
    RPC_RETURN_CODE(res);
  });
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // Only the fallthrough write reached the engine hook; the intercepted read did not.
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls("login_auth", atfw::testing::mock_db::op_type::kv_set)));
  CASE_EXPECT_EQ(0, static_cast<int>(test.db().calls("login_auth", atfw::testing::mock_db::op_type::kv_get_all)));

  CASE_EXPECT_EQ(0, test.stop());
}

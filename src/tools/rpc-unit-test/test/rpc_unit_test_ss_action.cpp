// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rpc_unit_test.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/runtime.h>
#include <atframework/testing/ss_action.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "dispatcher/task_action_ss_req_base.h"
#include "frame/test_macros.h"
#include "rpc/router/routerservice.atfw.gen.h"
#include "rpc/unit_test/rpcunittestservice.atfw.gen.h"

namespace {

constexpr int32_t kExpectedActionError = -713241;
constexpr uint64_t kExpectedSourceNodeId = 0x1300A1;
constexpr uint64_t kExpectedSourceTaskId = 0xA101;
constexpr uint64_t kExpectedSourceSequence = 0xA102;
constexpr const char* kExpectedSourceNodeName = "rpc-unit-test-action-source";

struct ss_action_observation {
  size_t calls = 0;
  std::string payload;
  std::string rpc_name;
  std::string type_url;
  uint64_t source_node_id = 0;
  std::string source_node_name;
  uint64_t source_task_id = 0;
  uint64_t sequence = 0;

  void reset() { *this = {}; }
};

ss_action_observation g_ss_action_observation;

class rpc_unit_test_inbound_action
    : public task_action_ss_rpc_base<rpc_unit_test::RpcUnitTestEchoReq, rpc_unit_test::RpcUnitTestEchoRsp> {
 public:
  using base_type = task_action_ss_rpc_base<rpc_unit_test::RpcUnitTestEchoReq, rpc_unit_test::RpcUnitTestEchoRsp>;
  using result_type = base_type::result_type;
  using rpc_request_type = base_type::rpc_request_type;
  using base_type::operator();

  explicit rpc_unit_test_inbound_action(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}
  ~rpc_unit_test_inbound_action() override = default;

  const char* name() const override { return "rpc_unit_test_inbound_action"; }

  result_type operator()() override {
    disable_response_message();

    auto& request = get_request_body();
    const auto& head = get_request().head();
    ++g_ss_action_observation.calls;
    g_ss_action_observation.payload = request.payload();
    g_ss_action_observation.source_node_id = head.node_id();
    g_ss_action_observation.source_node_name = head.node_name();
    g_ss_action_observation.source_task_id = head.source_task_id();
    g_ss_action_observation.sequence = head.sequence();
    if (head.has_rpc_request()) {
      g_ss_action_observation.rpc_name = head.rpc_request().rpc_name();
      g_ss_action_observation.type_url = head.rpc_request().type_url();
    }

    if (request.payload() == "return-error") {
      TASK_ACTION_RETURN_CODE(kExpectedActionError);
    }
    TASK_ACTION_RETURN_CODE(0);
  }

  int on_success() override { return get_result(); }
  int on_failed() override { return get_result(); }
};

rpc_unit_test::RpcUnitTestEchoReq make_temporary_request() {
  rpc_unit_test::RpcUnitTestEchoReq result;
  result.set_payload("inbound-action");
  return result;
}

atfw::testing::ss_action_invoke_options make_temporary_options() {
  atfw::testing::ss_action_invoke_options result{rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user()};
  result.source.node_id = kExpectedSourceNodeId;
  result.source.node_name = kExpectedSourceNodeName;
  result.source.source_task_id = kExpectedSourceTaskId;
  result.source.sequence = kExpectedSourceSequence;
  return result;
}

}  // namespace

CASE_TEST(rpc_unit_test, ss_action_invoke_preserves_request_and_source) {
  g_ss_action_observation.reset();

  atfw::testing::runtime test;
  atfw::testing::runtime_options runtime_options;
  runtime_options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(runtime_options));
  if (!test.is_running()) {
    return;
  }

  auto task =
      test.run_task("ss_action_invoke", std::chrono::seconds{2}, [](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<rpc_unit_test_inbound_action>(
            ctx, make_temporary_request(), make_temporary_options())));
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1u, g_ss_action_observation.calls);
  CASE_EXPECT_EQ("inbound-action", g_ss_action_observation.payload);
  CASE_EXPECT_EQ(rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user(), g_ss_action_observation.rpc_name);
  CASE_EXPECT_EQ(rpc_unit_test::RpcUnitTestEchoReq::descriptor()->full_name(), g_ss_action_observation.type_url);
  CASE_EXPECT_EQ(kExpectedSourceNodeId, g_ss_action_observation.source_node_id);
  CASE_EXPECT_EQ(kExpectedSourceNodeName, g_ss_action_observation.source_node_name);
  CASE_EXPECT_EQ(kExpectedSourceTaskId, g_ss_action_observation.source_task_id);
  CASE_EXPECT_EQ(kExpectedSourceSequence, g_ss_action_observation.sequence);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_action_invoke_rejects_rpc_request_mismatch) {
  g_ss_action_observation.reset();

  atfw::testing::runtime test;
  atfw::testing::runtime_options runtime_options;
  runtime_options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(runtime_options));
  if (!test.is_running()) {
    return;
  }

  rpc_unit_test::RpcUnitTestEchoReq request;
  atfw::testing::ss_action_invoke_options invoke_options{rpc::router::packer::get_full_name_of_router_transfer()};
  auto task =
      test.run_task("ss_action_mismatch", std::chrono::seconds{2},
                    [request, invoke_options](rpc::context& ctx) -> rpc::result_code_type {
                      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                          atfw::testing::invoke_ss_action<rpc_unit_test_inbound_action>(ctx, request, invoke_options)));
                    });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, result.result_code);
  CASE_EXPECT_EQ(0u, g_ss_action_observation.calls);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, ss_action_invoke_propagates_action_result) {
  g_ss_action_observation.reset();

  atfw::testing::runtime test;
  atfw::testing::runtime_options runtime_options;
  runtime_options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(runtime_options));
  if (!test.is_running()) {
    return;
  }

  rpc_unit_test::RpcUnitTestEchoReq request;
  request.set_payload("return-error");
  atfw::testing::ss_action_invoke_options invoke_options{rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user()};
  auto task =
      test.run_task("ss_action_result", std::chrono::seconds{2},
                    [request, invoke_options](rpc::context& ctx) -> rpc::result_code_type {
                      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                          atfw::testing::invoke_ss_action<rpc_unit_test_inbound_action>(ctx, request, invoke_options)));
                    });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(kExpectedActionError, result.result_code);
  CASE_EXPECT_EQ(1u, g_ss_action_observation.calls);
  CASE_EXPECT_EQ(0, test.stop());
}

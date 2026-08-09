// Copyright 2026 atframework

#include <atframework/testing/mock_cs.h>
#include <atframework/testing/runtime.h>

#include <logic/session_manager.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "frame/test_macros.h"

namespace {
constexpr uint64_t kGatewayNodeId = 0x81000001;
constexpr uint64_t kSessionId = 1001;
constexpr uint64_t kSessionIdCapture = 1002;

using op_type = atfw::testing::cs_downstream_record::op_type;

bool start_cs_runtime(atfw::testing::runtime &test) {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::cs};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }
  return true;
}

bool session_exists(uint64_t node_id, uint64_t session_id) {
  session::key_t key;
  key.node_id = node_id;
  key.session_id = session_id;
  return !!session_manager::me()->find(key);
}

// The remove path completes asynchronously inside the logout task; pump until the process-lifetime
// session_manager actually drops the session so a later fixture can reuse the ids.
void pump_until_session_removed(atfw::testing::runtime &test, uint64_t node_id, uint64_t session_id) {
  for (int i = 0; i < 256 && session_exists(node_id, session_id); ++i) {
    test.pump_once();
  }
}
}  // namespace

CASE_TEST(rpc_unit_test, cs_session_lifecycle_and_unknown_rpc_error_response) {
  atfw::testing::runtime test;
  if (!start_cs_runtime(test)) {
    return;
  }

  auto client = test.cs().create_client(kGatewayNodeId, kSessionId);
  CASE_EXPECT_TRUE(!!client);

  // kAddSession goes through the real dispatch path and creates a real session.
  CASE_EXPECT_EQ(0, client.add());
  CASE_EXPECT_TRUE(session_exists(kGatewayNodeId, kSessionId));

  // Posting an unregistered RPC produces a downstream error response through the real send path.
  atframework::CSMsg request;
  request.mutable_head()->mutable_rpc_request()->set_rpc_name("unit_test.unknown_rpc");
  request.mutable_head()->set_client_sequence(7);
  int32_t res = client.post(request);
  CASE_EXPECT_TRUE(res < 0);

  const atfw::testing::cs_downstream_record *record = nullptr;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const atfw::testing::cs_downstream_record *candidate = test.cs().call_at(i);
    if (nullptr != candidate && candidate->op == op_type::post) {
      record = candidate;
    }
  }
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_EQ(static_cast<int>(kSessionId), static_cast<int>(record->session_id));
    atframework::CSMsg response;
    CASE_EXPECT_TRUE(response.ParseFromString(record->message.body().post().content()));
    CASE_EXPECT_TRUE(response.head().error_code() < 0);
    CASE_EXPECT_EQ(7, static_cast<int>(response.head().client_sequence()));
  }

  // kRemoveSession closes through the real remove path.
  CASE_EXPECT_EQ(0, client.remove());
  pump_until_session_removed(test, kGatewayNodeId, kSessionId);
  CASE_EXPECT_FALSE(session_exists(kGatewayNodeId, kSessionId));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, cs_downstream_capture_data_kickoff_set_router) {
  atfw::testing::runtime test;
  if (!start_cs_runtime(test)) {
    return;
  }

  auto client = test.cs().create_client(kGatewayNodeId, kSessionIdCapture);
  CASE_EXPECT_EQ(0, client.add());

  // Downstream data: captured and parsed, never touches the bus node.
  const char payload[] = "cs-ec";
  CASE_EXPECT_EQ(0, cs_msg_dispatcher::me()->send_data(kGatewayNodeId, kSessionIdCapture, payload, 5));

  // Kickoff carries reason and message.
  CASE_EXPECT_EQ(0, cs_msg_dispatcher::me()->send_kickoff(kGatewayNodeId, kSessionIdCapture, 42, "bye"));

  // Set router request/response chain.
  CASE_EXPECT_EQ(0, cs_msg_dispatcher::me()->send_set_router(kGatewayNodeId, kSessionIdCapture, 123, "unit-test-svc"));
  CASE_EXPECT_EQ(0, client.set_router_rsp(0, 123, "unit-test-svc"));

  CASE_EXPECT_EQ(3, static_cast<int>(test.cs().call_count()));

  const atfw::testing::cs_downstream_record *post_record = test.cs().call_at(0);
  CASE_EXPECT_TRUE(nullptr != post_record);
  if (nullptr != post_record) {
    CASE_EXPECT_EQ(static_cast<int>(op_type::post), static_cast<int>(post_record->op));
    CASE_EXPECT_EQ(static_cast<int>(kSessionIdCapture), static_cast<int>(post_record->session_id));
    CASE_EXPECT_EQ("cs-ec", post_record->message.body().post().content());
  }

  const atfw::testing::cs_downstream_record *kickoff_record = test.cs().call_at(1);
  CASE_EXPECT_TRUE(nullptr != kickoff_record);
  if (nullptr != kickoff_record) {
    CASE_EXPECT_EQ(static_cast<int>(op_type::kickoff), static_cast<int>(kickoff_record->op));
    CASE_EXPECT_EQ(42, kickoff_record->message.body().kickoff_session().reason());
    CASE_EXPECT_EQ("bye", kickoff_record->message.body().kickoff_session().message());
    CASE_EXPECT_EQ(42, kickoff_record->message.head().error_code());
  }

  const atfw::testing::cs_downstream_record *router_record = test.cs().call_at(2);
  CASE_EXPECT_TRUE(nullptr != router_record);
  if (nullptr != router_record) {
    CASE_EXPECT_EQ(static_cast<int>(op_type::set_router), static_cast<int>(router_record->op));
    CASE_EXPECT_EQ(123, static_cast<int>(router_record->message.body().set_router_req().target_service_id()));
    CASE_EXPECT_EQ("unit-test-svc", router_record->message.body().set_router_req().target_service_name());
  }

  CASE_EXPECT_EQ(1, static_cast<int>(test.cs().calls(op_type::post)));
  CASE_EXPECT_EQ(3, static_cast<int>(test.cs().calls_to(kSessionIdCapture)));

  CASE_EXPECT_EQ(0, client.remove());
  pump_until_session_removed(test, kGatewayNodeId, kSessionIdCapture);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, cs_broadcast_and_multi_session_observability) {
  atfw::testing::runtime test;
  if (!start_cs_runtime(test)) {
    return;
  }

  const char payload[] = "notify";
  CASE_EXPECT_EQ(0, cs_msg_dispatcher::me()->broadcast_data(kGatewayNodeId, payload, 6));

  std::vector<uint64_t> session_ids = {11, 22};
  CASE_EXPECT_EQ(0, cs_msg_dispatcher::me()->broadcast_data(kGatewayNodeId, session_ids, payload, 6));

  CASE_EXPECT_EQ(2, static_cast<int>(test.cs().calls(op_type::broadcast)));

  const atfw::testing::cs_downstream_record *plain = test.cs().call_at(0);
  CASE_EXPECT_TRUE(nullptr != plain);
  if (nullptr != plain) {
    CASE_EXPECT_EQ(0, static_cast<int>(plain->session_id));
    CASE_EXPECT_TRUE(plain->session_ids.empty());
    CASE_EXPECT_EQ("notify", plain->message.body().post().content());
  }

  const atfw::testing::cs_downstream_record *multi = test.cs().call_at(1);
  CASE_EXPECT_TRUE(nullptr != multi);
  if (nullptr != multi) {
    CASE_EXPECT_EQ(2, static_cast<int>(multi->session_ids.size()));
    if (multi->session_ids.size() >= 2) {
      CASE_EXPECT_EQ(11, static_cast<int>(multi->session_ids[0]));
      CASE_EXPECT_EQ(22, static_cast<int>(multi->session_ids[1]));
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, cs_send_error_injection_and_session_not_found_kickoff) {
  atfw::testing::runtime test;
  if (!start_cs_runtime(test)) {
    return;
  }

  // Posting without a session triggers the real session-not-found kickoff.
  auto client = test.cs().create_client(kGatewayNodeId, kSessionId);
  atframework::CSMsg request;
  request.mutable_head()->mutable_rpc_request()->set_rpc_name("unit_test.unknown_rpc");
  int32_t res = client.post(request);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, res);

  const atfw::testing::cs_downstream_record *kickoff_record = test.cs().call_at(0);
  CASE_EXPECT_TRUE(nullptr != kickoff_record);
  if (nullptr != kickoff_record) {
    CASE_EXPECT_EQ(static_cast<int>(op_type::kickoff), static_cast<int>(kickoff_record->op));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_CRT_SESSION_NOT_FOUND,
                   kickoff_record->message.body().kickoff_session().reason());
  }

  // Error injection: captured sends report the injected result code.
  test.cs().clear_history();
  test.cs().set_send_error(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                 cs_msg_dispatcher::me()->send_data(kGatewayNodeId, kSessionId, "x", 1));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                 cs_msg_dispatcher::me()->send_kickoff(kGatewayNodeId, kSessionId, 1, "x"));
  CASE_EXPECT_EQ(2, static_cast<int>(test.cs().call_count()));

  test.cs().clear_send_error();
  CASE_EXPECT_EQ(0, cs_msg_dispatcher::me()->send_data(kGatewayNodeId, kSessionId, "x", 1));

  CASE_EXPECT_EQ(0, test.stop());
}

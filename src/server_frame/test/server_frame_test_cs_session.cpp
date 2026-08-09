// Copyright 2026 atframework

#include <cstdint>

#include <atframework/testing/mock_cs.h>
#include <atframework/testing/runtime.h>

#include "dispatcher/cs_msg_dispatcher.h"
#include "frame/test_macros.h"
#include "logic/session_manager.h"

namespace {
constexpr uint64_t kGatewayNodeId = 0x82000001;
constexpr uint64_t kSessionId = 2001;

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

// server_frame component: kAddSession/kRemoveSession drive the real session_manager, and the
// dispatcher's own send_data/send_kickoff APIs deliver downstream through the unified gateway send.
CASE_TEST(server_frame_unit_test, cs_session_manager_lifecycle_and_downstream_capture) {
  atfw::testing::runtime test;
  if (!start_cs_runtime(test)) {
    return;
  }

  auto client = test.cs().create_client(kGatewayNodeId, kSessionId);
  CASE_EXPECT_TRUE(!!client);

  // Add goes through cs_msg_dispatcher::dispatch(kAddSession) and creates a real session.
  CASE_EXPECT_EQ(0, client.add());
  CASE_EXPECT_TRUE(session_exists(kGatewayNodeId, kSessionId));

  // Downstream data and kickoff through the dispatcher's public send APIs are captured with the
  // real session id attached.
  const char payload[] = "server-frame-data";
  CASE_EXPECT_EQ(
      0, cs_msg_dispatcher::me()->send_data(kGatewayNodeId, kSessionId, payload, sizeof("server-frame-data") - 1));
  CASE_EXPECT_EQ(0, cs_msg_dispatcher::me()->send_kickoff(kGatewayNodeId, kSessionId, 7, "server-frame-bye"));

  // Remove closes through the real logout path and drops the session from session_manager.
  CASE_EXPECT_EQ(0, client.remove());
  pump_until_session_removed(test, kGatewayNodeId, kSessionId);
  CASE_EXPECT_FALSE(session_exists(kGatewayNodeId, kSessionId));

  using op_type = atfw::testing::cs_downstream_record::op_type;
  const atfw::testing::cs_downstream_record *post_record = nullptr;
  const atfw::testing::cs_downstream_record *kickoff_record = nullptr;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const atfw::testing::cs_downstream_record *candidate = test.cs().call_at(i);
    if (nullptr == candidate || candidate->session_id != kSessionId) {
      continue;
    }
    if (op_type::post == candidate->op) {
      post_record = candidate;
    }
    if (op_type::kickoff == candidate->op) {
      kickoff_record = candidate;
    }
  }
  CASE_EXPECT_TRUE(nullptr != post_record);
  if (nullptr != post_record) {
    CASE_EXPECT_EQ("server-frame-data", post_record->message.body().post().content());
  }
  CASE_EXPECT_TRUE(nullptr != kickoff_record);
  if (nullptr != kickoff_record) {
    CASE_EXPECT_EQ(7, kickoff_record->message.body().kickoff_session().reason());
    CASE_EXPECT_EQ("server-frame-bye", kickoff_record->message.body().kickoff_session().message());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

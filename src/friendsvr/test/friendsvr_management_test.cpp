// Copyright 2026 atframework
//
// Unit test skeleton for the friendsvr-management service (rpc-unit-test based).
//
// Boots one atfw::testing::runtime (SS) per case and drives the placeholder management_get_all SS action
// directly through atfw::testing::invoke_ss_action, exercising the real SSMsg pack/unpack and
// task-manager create/start/wait path without the dispatcher registry. Extend with real request/response
// and stored-state assertions as the action gains behavior.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/friend_management_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/runtime.h>
#include <atframework/testing/ss_action.h>

#include <chrono>
#include <cstdint>

#include "frame/test_macros.h"

#include "logic/action/task_action_management_get_all.h"
#include "rpc/friends/friendmanagementservice.atfw.gen.h"

namespace {
// Synthetic upstream node that "sent" the inbound request; any non-zero id works for the placeholder action.
constexpr uint64_t kUpstreamSourceNodeId = 0x11000001;
}  // namespace

CASE_TEST(friendsvr_management, management_get_all_placeholder) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options runtime_options;
  runtime_options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(runtime_options));
  if (!test.is_running()) {
    return;
  }

  atframework::friends::SSFriendManagementGetAllReq request;

  atfw::testing::ss_action_invoke_options invoke_options{rpc::friends::packer::get_full_name_of_management_get_all()};
  invoke_options.source.node_id = kUpstreamSourceNodeId;
  invoke_options.source.node_name = "friendsvr-management-test";

  auto task = test.run_task(
      "management_get_all_placeholder", std::chrono::seconds{2},
      [request, invoke_options](rpc::context &ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            atfw::testing::invoke_ss_action<task_action_management_get_all>(ctx, request, invoke_options)));
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

  CASE_EXPECT_EQ(0, test.stop());
}

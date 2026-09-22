// Copyright 2026 atframework
//
// Unit test skeleton for the friendsvr-management service (rpc-unit-test based).
//
// Boots one atfw::testing::runtime (SS) per case and drives the management_subscribe SS action directly through
// atfw::testing::invoke_ss_action, exercising the real SSMsg pack/unpack and task-manager create/start/wait path
// without the dispatcher registry. invoke_ss_action cannot populate SSRouterHead, so the action's router guard
// deterministically rejects the request with EN_SYS_PARAM. Extend with router-head injection and stored-state
// assertions once the helper grows router support.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/friend_management_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/runtime.h>
#include <atframework/testing/ss_action.h>

#include <chrono>
#include <cstdint>

#include "frame/test_macros.h"

#include "logic/action/task_action_management_subscribe.h"    // IWYU pragma: keep
#include "rpc/friend_api/friendmanagementservice.atfw.gen.h"  // IWYU pragma: keep

namespace {
// Synthetic upstream node that "sent" the inbound request; the action sends its response back to this node.
constexpr uint64_t kUpstreamSourceNodeId = 0x11000001;
// Router-less invocation still carries a representative subscriber identity in the request body.
constexpr uint32_t kSubscriberZoneId = 1;
constexpr uint64_t kSubscriberUserId = 0xF12345;
}  // namespace

CASE_TEST(friendsvr_management, management_subscribe_basic_flow) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options runtime_options;
  runtime_options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(runtime_options));
  if (!test.is_running()) {
    return;
  }

  atframework::friend_api::SSFriendSubscribeReq request;
  auto* subscriber_user_key = request.mutable_subscriber()->mutable_subscriber_user_key();
  subscriber_user_key->set_zone_id(kSubscriberZoneId);
  subscriber_user_key->set_user_id(kSubscriberUserId);
  request.set_need_allocate_event_id(true);

  atfw::testing::ss_action_invoke_options invoke_options{
      rpc::friend_api::packer::get_full_name_of_management_subscribe()};
  invoke_options.source.node_id = kUpstreamSourceNodeId;
  invoke_options.source.node_name = "friendsvr-management-test";

  auto task = test.run_task(
      "management_subscribe_basic_flow", std::chrono::seconds{2},
      [request, invoke_options](rpc::context &ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            atfw::testing::invoke_ss_action<task_action_management_subscribe>(ctx, request, invoke_options)));
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
  // invoke_ss_action leaves SSRouterHead empty, so the action's router guard rejects with EN_SYS_PARAM.
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

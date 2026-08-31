// Copyright 2026 atframework

// Offline state-transition tests for lobbysvr user_matching_manager.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/runtime.h>

#include <rpc/rpc_context.h>

#include <chrono>
#include <memory>

#include "data/user.h"
#include "frame/test_macros.h"
#include "logic/matching/user_matching_manager.h"

CASE_TEST(lobbysvr_user_matching, reports_active_matching_states) {
  auto user_inst = user::create(10002, 1, "matching-state-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  auto& manager = user_inst->get_user_matching_manager();
  auto set_status = [&ctx, &manager](PROJECT_NAMESPACE_ID::EnMatchingRoomStatus status, bool has_matching_id) {
    PROJECT_NAMESPACE_ID::table_user table;
    auto* snapshot = table.mutable_matching_data()->mutable_view();
    if (has_matching_id) {
      snapshot->set_matching_id("matching-state");
    }
    snapshot->set_status(status);
    manager.init_from_table_data(ctx, table);
  };

  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, true);
  CASE_EXPECT_TRUE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, true);
  CASE_EXPECT_TRUE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE, true);
  CASE_EXPECT_TRUE(manager.is_in_matching());

  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_INVALID, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, false);
  CASE_EXPECT_FALSE(manager.is_in_matching());
}

CASE_TEST(lobbysvr_user_matching, rejects_stale_unit_requests) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto user_inst = user::create(10003, 1, "matching-stale-unit-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    test.stop();
    return;
  }

  auto cancel_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  auto cancel_response = std::make_shared<PROJECT_NAMESPACE_ID::SCMatchingCancelRsp>();
  auto confirm_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  auto confirm_response = std::make_shared<PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp>();

  auto task = test.run_task(
      "matching.rejects_stale_unit_requests", std::chrono::seconds{2},
      [user_inst, cancel_result, cancel_response, confirm_result,
       confirm_response](rpc::context& ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::table_user table;
        auto* view = table.mutable_matching_data()->mutable_view();
        view->set_matching_id("matching-current");
        view->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
        view->set_last_event_id(7);
        view->mutable_unit()->set_unit_id(1003);

        auto& manager = user_inst->get_user_matching_manager();
        manager.init_from_table_data(ctx, table);

        PROJECT_NAMESPACE_ID::CSMatchingCancelReq stale_cancel;
        stale_cancel.set_unit_id(9999);
        *cancel_result = RPC_AWAIT_CODE_RESULT(manager.cancel_matching(ctx, stale_cancel, *cancel_response));

        PROJECT_NAMESPACE_ID::CSMatchingConfirmReq stale_confirm;
        stale_confirm.set_unit_id(9999);
        stale_confirm.set_confirmed(true);
        *confirm_result = RPC_AWAIT_CODE_RESULT(manager.confirm_matching(ctx, stale_confirm, *confirm_response));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto task_result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(task_result.task_exited);
  CASE_EXPECT_FALSE(task_result.hard_timed_out);
  CASE_EXPECT_EQ(0, task_result.result_code);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, *cancel_result);
  CASE_EXPECT_TRUE(cancel_response->has_view());
  CASE_EXPECT_EQ(1003, cancel_response->view().unit_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, *confirm_result);
  CASE_EXPECT_TRUE(confirm_response->has_view());
  CASE_EXPECT_EQ(1003, confirm_response->view().unit_id());

  CASE_EXPECT_EQ(0, test.stop());
}

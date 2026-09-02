// Copyright 2026 atframework

// Offline state-transition tests for lobbysvr user_matching_manager.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <rpc/rpc_context.h>
#include <time/time_utility.h>

#include <chrono>
#include <memory>
#include <vector>

#include "config/extern_service_types.h"
#include "data/user.h"
#include "frame/test_macros.h"
#include "logic/logic_server_setup.h"
#include "logic/matching/user_matching_manager.h"
#include "logic/orbit/user_orbit_manager.h"
#include "rpc/matching/matching_api.h"
#include "rpc/matching/matchsvrservice.atfw.gen.h"

CASE_TEST(lobbysvr_user_matching, reports_active_matching_states) {
  auto user_inst = user::create(10002, 1, "matching-state-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  auto& manager = user_inst->get_user_matching_manager();
  auto set_status = [&ctx, &manager](PROJECT_NAMESPACE_ID::EnMatchingUnitLifecycleStatus status, bool has_unit_id) {
    PROJECT_NAMESPACE_ID::table_user table;
    auto* snapshot = table.mutable_matching_data()->mutable_view();
    if (has_unit_id) {
      snapshot->mutable_unit()->set_unit_id(10002);
    }
    snapshot->set_status(status);
    manager.init_from_table_data(ctx, table);
  };

  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, true);
  CASE_EXPECT_TRUE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING, true);
  CASE_EXPECT_TRUE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE, true);
  CASE_EXPECT_TRUE(manager.is_in_matching());

  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_INVALID, true);
  CASE_EXPECT_FALSE(manager.is_in_matching());
  set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, false);
  CASE_EXPECT_FALSE(manager.is_in_matching());
}

CASE_TEST(lobbysvr_user_matching, preserves_matching_when_login_recovery_has_no_matchsvr) {
  constexpr uint64_t kUnavailableMatchsvrId = 0x1E00FF;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto user_inst = user::create(10008, 1, "matching-recovery-no-matchsvr-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    test.stop();
    return;
  }

  auto recovery_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  auto task = test.run_task("matching.preserves_matching_when_login_recovery_has_no_matchsvr", std::chrono::seconds{2},
                            [user_inst, recovery_result](rpc::context& ctx) -> rpc::result_code_type {
                              PROJECT_NAMESPACE_ID::table_user table;
                              auto* matching_data = table.mutable_matching_data();
                              matching_data->set_matchsvr_server_id(kUnavailableMatchsvrId);
                              auto* view = matching_data->mutable_view();
                              view->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE);
                              view->set_last_event_id(9);
                              view->mutable_unit()->set_unit_id(1008);
                              view->mutable_orbit_room_key()->set_client_id("matching-recovery-must-not-join-orbit");

                              auto& manager = user_inst->get_user_matching_manager();
                              manager.init_from_table_data(ctx, table);
                              *recovery_result = RPC_AWAIT_CODE_RESULT(manager.login_init(ctx));
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
  CASE_EXPECT_NE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS, *recovery_result);
  CASE_EXPECT_TRUE(user_inst->get_user_matching_manager().is_in_matching());
  CASE_EXPECT_EQ(1008, user_inst->get_user_matching_manager().get_view().unit().unit_id());
  CASE_EXPECT_FALSE(user_inst->get_user_orbit_manager().is_orbit_room_exist());

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(lobbysvr_user_matching, hands_off_missing_creating_battle_to_orbit_during_login_recovery) {
  constexpr uint64_t kMatchsvrId = 0x1E0001;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  atfw::testing::mock_node node;
  node.set_id(kMatchsvrId)
      .set_name("unit-test-matchsvr")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr))
      .set_type_name("matchsvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }

  rpc::unit_test::ss_mock_rule_options rule_options;
  rule_options.match_node_id = kMatchsvrId;
  rule_options.times = 1;
  auto rule = rpc::matching::mock::matching_heart_bear(
      [](rpc::context&, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
         PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        CASE_EXPECT_EQ(1009, request.unit_id());
        CASE_EXPECT_NE(0, request.subscriber_server_id());
        CASE_EXPECT_TRUE(request.has_heartbeat_data());
        if (request.has_heartbeat_data()) {
          CASE_EXPECT_EQ(10009, request.heartbeat_data().user_key().user_id());
          CASE_EXPECT_EQ(1, request.heartbeat_data().user_key().zone_id());
          CASE_EXPECT_EQ(0, request.heartbeat_data().acknowledge_event_id());
        }
        response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_NOT_FOUND);
        RPC_RETURN_CODE(0);
      },
      rule_options);
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    test.stop();
    return;
  }

  auto user_inst = user::create(10009, 1, "matching-recovery-missing-unit-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    test.stop();
    return;
  }

  auto recovery_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND);
  auto task =
      test.run_task("matching.hands_off_missing_creating_battle_to_orbit_during_login_recovery",
                    std::chrono::seconds{2}, [user_inst, recovery_result](rpc::context& ctx) -> rpc::result_code_type {
                      PROJECT_NAMESPACE_ID::table_user table;
                      auto* matching_data = table.mutable_matching_data();
                      matching_data->set_matchsvr_server_id(kMatchsvrId);
                      auto* view = matching_data->mutable_view();
                      view->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE);
                      view->set_last_event_id(10);
                      view->mutable_unit()->set_unit_id(1009);
                      view->mutable_orbit_room_key()->set_client_id("missing-matching-handoff-to-orbit");

                      auto& manager = user_inst->get_user_matching_manager();
                      manager.init_from_table_data(ctx, table);
                      *recovery_result = RPC_AWAIT_CODE_RESULT(manager.login_init(ctx));
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
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SUCCESS, *recovery_result);
  CASE_EXPECT_FALSE(user_inst->get_user_matching_manager().is_in_matching());
  CASE_EXPECT_EQ(0, user_inst->get_user_matching_manager().get_view().unit().unit_id());
  CASE_EXPECT_TRUE(user_inst->get_user_orbit_manager().is_orbit_room_exist());

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(lobbysvr_user_matching, sends_acknowledgement_in_periodic_matching_heartbeat) {
  constexpr uint64_t kMatchsvrIdA = 0x1E0001;
  constexpr uint64_t kMatchsvrIdB = 0x1E0002;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  atfw::testing::mock_node node_a;
  node_a.set_id(kMatchsvrIdA)
      .set_name("unit-test-matchsvr-a")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr))
      .set_type_name("matchsvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node_a));
  atfw::testing::mock_node node_b;
  node_b.set_id(kMatchsvrIdB)
      .set_name("unit-test-matchsvr-b")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr))
      .set_type_name("matchsvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node_b));
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
  const uint64_t default_matchsvr_id = rpc::matching_api::get_matchsvr_server_id();
  CASE_EXPECT_NE(0, default_matchsvr_id);
  const uint64_t stored_matchsvr_id = default_matchsvr_id == kMatchsvrIdA ? kMatchsvrIdB : kMatchsvrIdA;
  CASE_EXPECT_NE(default_matchsvr_id, stored_matchsvr_id);

  auto captured_requests = std::make_shared<std::vector<PROJECT_NAMESPACE_ID::SSMatchingCheckReq>>();
  rpc::unit_test::ss_mock_rule_options rule_options;
  rule_options.match_node_id = stored_matchsvr_id;
  rule_options.times = 2;
  auto rule = rpc::matching::mock::matching_heart_bear(
      [captured_requests](rpc::context&, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
                          PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        captured_requests->emplace_back(request);
        response.set_result(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        response.set_matching_id("periodic-heartbeat-room");
        response.mutable_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
        response.mutable_snapshot()->set_last_event_id(7);
        response.mutable_snapshot()->mutable_unit()->set_unit_id(1010);
        RPC_RETURN_CODE(0);
      },
      rule_options);
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    test.stop();
    return;
  }

  auto user_inst = user::create(10010, 1, "matching-periodic-heartbeat-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    test.stop();
    return;
  }

  rpc::context init_ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::table_user table;
  auto* matching_data = table.mutable_matching_data();
  matching_data->set_acknowledge_event_id(7);
  matching_data->set_matchsvr_server_id(stored_matchsvr_id);
  matching_data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
  matching_data->mutable_view()->set_last_event_id(7);
  matching_data->mutable_view()->mutable_unit()->set_unit_id(1010);
  user_inst->get_user_matching_manager().init_from_table_data(init_ctx, table);

  auto run_refresh = [&test, &user_inst](const char* name) {
    auto task = test.run_task(name, std::chrono::seconds{2}, [user_inst](rpc::context& ctx) -> rpc::result_code_type {
      user_inst->get_user_matching_manager().refresh_feature_limit_second(ctx);
      RPC_RETURN_CODE(0);
    });
    if (task.empty()) {
      return false;
    }
    auto result = test.wait(task, std::chrono::seconds{5});
    return result.task_exited && !result.hard_timed_out && result.result_code == 0;
  };
  auto pump_until_calls = [&test](size_t count) {
    for (int i = 0; i < 64 && test.ss().calls(rpc::matching::packer::get_full_name_of_matching_heart_bear()) < count;
         ++i) {
      test.pump_once();
    }
    return test.ss().calls(rpc::matching::packer::get_full_name_of_matching_heart_bear()) >= count;
  };

  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  CASE_EXPECT_TRUE(run_refresh("matching.periodic_heartbeat.first"));
  CASE_EXPECT_TRUE(pump_until_calls(1));
  CASE_EXPECT_EQ(1, static_cast<int>(captured_requests->size()));
  if (captured_requests->size() == 1) {
    const auto& request = captured_requests->front();
    CASE_EXPECT_EQ(1010, request.unit_id());
    CASE_EXPECT_NE(0, request.subscriber_server_id());
    CASE_EXPECT_TRUE(request.has_heartbeat_data());
    if (request.has_heartbeat_data()) {
      CASE_EXPECT_EQ(10010, request.heartbeat_data().user_key().user_id());
      CASE_EXPECT_EQ(1, request.heartbeat_data().user_key().zone_id());
      CASE_EXPECT_EQ(7, request.heartbeat_data().acknowledge_event_id());
    }
  }

  CASE_EXPECT_TRUE(run_refresh("matching.periodic_heartbeat.throttled"));
  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::matching::packer::get_full_name_of_matching_heart_bear())));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{2});
  CASE_EXPECT_TRUE(run_refresh("matching.periodic_heartbeat.second"));
  CASE_EXPECT_TRUE(pump_until_calls(2));
  CASE_EXPECT_EQ(2, static_cast<int>(captured_requests->size()));

  atfw::util::time::time_utility::reset_global_now_offset();
  CASE_EXPECT_EQ(0, test.stop());
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
        view->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
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
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_MATCHING, *cancel_result);
  CASE_EXPECT_TRUE(cancel_response->has_view());
  CASE_EXPECT_EQ(1003, cancel_response->view().unit_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, *confirm_result);
  CASE_EXPECT_TRUE(confirm_response->has_view());
  CASE_EXPECT_EQ(1003, confirm_response->view().unit_id());

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(lobbysvr_user_matching, acknowledges_superseded_unit_without_overwriting_active_state) {
  auto user_inst = user::create(10004, 1, "matching-superseded-unit-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::table_user table;
  auto* local_data = table.mutable_matching_data();
  local_data->set_acknowledge_event_id(7);
  local_data->mutable_view()->set_last_event_id(7);
  local_data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
  local_data->mutable_view()->mutable_unit()->set_unit_id(2004);

  auto& manager = user_inst->get_user_matching_manager();
  manager.init_from_table_data(ctx, table);

  PROJECT_NAMESPACE_ID::SSMatchingEventSync superseded_sync;
  superseded_sync.set_unit_id(1004);
  superseded_sync.mutable_unit_view()->set_last_event_id(3);
  superseded_sync.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED);
  superseded_sync.mutable_unit_view()->mutable_unit()->set_unit_id(1004);
  auto sync_result = manager.acknowledge_matching_sync(ctx, superseded_sync);
  CASE_EXPECT_TRUE(sync_result.accepted);
  CASE_EXPECT_FALSE(sync_result.has_pending_event);
  CASE_EXPECT_EQ(3, sync_result.acknowledge_event_id);
  CASE_EXPECT_EQ(2004, manager.get_view().unit().unit_id());
  CASE_EXPECT_EQ(7, manager.get_last_event_id());

  superseded_sync.mutable_unit_view()->mutable_unit()->set_unit_id(9999);
  sync_result = manager.acknowledge_matching_sync(ctx, superseded_sync);
  CASE_EXPECT_FALSE(sync_result.accepted);
  CASE_EXPECT_EQ(2004, manager.get_view().unit().unit_id());
}

CASE_TEST(lobbysvr_user_matching, advances_business_cursor_only_after_confirm_success) {
  constexpr uint64_t kSourceMatchsvrId = 0x1E0021;
  auto user_inst = user::create(10005, 1, "matching-event-idempotence-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::table_user table;
  auto* local_data = table.mutable_matching_data();
  local_data->set_acknowledge_event_id(1);
  local_data->mutable_view()->set_last_event_id(1);
  local_data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
  local_data->mutable_view()->mutable_unit()->set_unit_id(1005);

  auto& manager = user_inst->get_user_matching_manager();
  manager.init_from_table_data(ctx, table);

  PROJECT_NAMESPACE_ID::SSMatchingEventSync sync;
  sync.set_unit_id(1005);
  sync.mutable_unit_view()->set_last_event_id(2);
  sync.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING);
  sync.mutable_unit_view()->mutable_unit()->set_unit_id(1005);
  auto* event = sync.add_event_logs();
  event->set_event_id(2);
  event->set_event_type(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_CONFIRM_REQUIRED);

  auto first = manager.acknowledge_matching_sync(ctx, sync, kSourceMatchsvrId);
  CASE_EXPECT_TRUE(first.accepted);
  CASE_EXPECT_TRUE(first.has_pending_event);
  CASE_EXPECT_EQ(2, manager.get_last_event_id());
  CASE_EXPECT_EQ(1, first.acknowledge_event_id);
  CASE_EXPECT_EQ(2, first.confirm_event_id);
  PROJECT_NAMESPACE_ID::table_user before_confirm_dump;
  CASE_EXPECT_EQ(0, manager.dump(ctx, before_confirm_dump));
  CASE_EXPECT_EQ(2, before_confirm_dump.matching_data().view().last_event_id());
  CASE_EXPECT_EQ(1, before_confirm_dump.matching_data().acknowledge_event_id());
  CASE_EXPECT_EQ(kSourceMatchsvrId, before_confirm_dump.matching_data().matchsvr_server_id());

  auto concurrent_duplicate = manager.acknowledge_matching_sync(ctx, sync);
  CASE_EXPECT_TRUE(concurrent_duplicate.accepted);
  CASE_EXPECT_TRUE(concurrent_duplicate.has_pending_event);
  CASE_EXPECT_EQ(1, concurrent_duplicate.acknowledge_event_id);
  CASE_EXPECT_EQ(0, concurrent_duplicate.confirm_event_id);

  CASE_EXPECT_FALSE(manager.finish_matching_event(ctx, 1005, 2, false));
  auto retry = manager.acknowledge_matching_sync(ctx, sync);
  CASE_EXPECT_TRUE(retry.has_pending_event);
  CASE_EXPECT_EQ(2, retry.confirm_event_id);
  CASE_EXPECT_TRUE(manager.finish_matching_event(ctx, 1005, 2, true));
  PROJECT_NAMESPACE_ID::table_user after_confirm_dump;
  CASE_EXPECT_EQ(0, manager.dump(ctx, after_confirm_dump));
  CASE_EXPECT_EQ(2, after_confirm_dump.matching_data().view().last_event_id());
  CASE_EXPECT_EQ(2, after_confirm_dump.matching_data().acknowledge_event_id());

  auto completed_duplicate = manager.acknowledge_matching_sync(ctx, sync);
  CASE_EXPECT_TRUE(completed_duplicate.accepted);
  CASE_EXPECT_FALSE(completed_duplicate.has_pending_event);
  CASE_EXPECT_EQ(2, completed_duplicate.acknowledge_event_id);
  CASE_EXPECT_EQ(0, completed_duplicate.confirm_event_id);
}

CASE_TEST(lobbysvr_user_matching, treats_obsolete_confirm_event_as_completed_after_state_query) {
  auto user_inst = user::create(10006, 1, "matching-obsolete-confirm-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::table_user table;
  auto* local_data = table.mutable_matching_data();
  local_data->set_acknowledge_event_id(1);
  local_data->mutable_view()->set_last_event_id(1);
  local_data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING);
  local_data->mutable_view()->mutable_unit()->set_unit_id(1006);

  auto& manager = user_inst->get_user_matching_manager();
  manager.init_from_table_data(ctx, table);

  PROJECT_NAMESPACE_ID::SSMatchingEventSync sync;
  sync.set_unit_id(1006);
  sync.mutable_unit_view()->set_last_event_id(3);
  sync.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE);
  sync.mutable_unit_view()->mutable_unit()->set_unit_id(1006);
  auto* confirm_event = sync.add_event_logs();
  confirm_event->set_event_id(2);
  confirm_event->set_event_type(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_CONFIRM_REQUIRED);
  auto* changed_event = sync.add_event_logs();
  changed_event->set_event_id(3);
  changed_event->set_event_type(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_VIEW_CHANGED);

  auto result = manager.acknowledge_matching_sync(ctx, sync);
  CASE_EXPECT_TRUE(result.accepted);
  CASE_EXPECT_FALSE(result.has_pending_event);
  CASE_EXPECT_EQ(3, result.acknowledge_event_id);
  CASE_EXPECT_EQ(0, result.confirm_event_id);
  CASE_EXPECT_EQ(3, manager.get_last_event_id());
}

CASE_TEST(lobbysvr_user_matching, derives_pending_confirm_from_recovery_snapshot) {
  auto user_inst = user::create(10007, 1, "matching-snapshot-recovery-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::table_user table;
  auto* local_data = table.mutable_matching_data();
  local_data->set_acknowledge_event_id(1);
  local_data->mutable_view()->set_last_event_id(1);
  local_data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
  local_data->mutable_view()->mutable_unit()->set_unit_id(1007);

  auto& manager = user_inst->get_user_matching_manager();
  manager.init_from_table_data(ctx, table);

  PROJECT_NAMESPACE_ID::SSMatchingEventSync snapshot;
  snapshot.set_unit_id(1007);
  snapshot.mutable_unit_view()->set_last_event_id(4);
  snapshot.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING);
  snapshot.mutable_unit_view()->mutable_unit()->set_unit_id(1007);

  auto result = manager.acknowledge_matching_sync(ctx, snapshot);
  CASE_EXPECT_TRUE(result.accepted);
  CASE_EXPECT_TRUE(result.has_pending_event);
  CASE_EXPECT_EQ(1, result.acknowledge_event_id);
  CASE_EXPECT_EQ(4, result.confirm_event_id);
  CASE_EXPECT_TRUE(manager.finish_matching_event(ctx, 1007, 4, true));

  PROJECT_NAMESPACE_ID::table_user persisted;
  CASE_EXPECT_EQ(0, manager.dump(ctx, persisted));
  CASE_EXPECT_EQ(4, persisted.matching_data().view().last_event_id());
  CASE_EXPECT_EQ(4, persisted.matching_data().acknowledge_event_id());

  snapshot.mutable_unit_view()->set_last_event_id(5);
  snapshot.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE);
  result = manager.acknowledge_matching_sync(ctx, snapshot);
  CASE_EXPECT_FALSE(result.has_pending_event);
  CASE_EXPECT_EQ(5, result.acknowledge_event_id);
  CASE_EXPECT_EQ(0, result.confirm_event_id);
}

// Copyright 2026 atframework

// Offline state-transition tests for lobbysvr user_matching_manager.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.level.config.pb.h>
#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/config/lobbysvr_config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>
#include <protocol/pbdesc/match_service.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_resource.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <rpc/rpc_context.h>
#include <time/time_utility.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "config/extern_service_types.h"
#include "config/logic_config.h"
#include "data/user.h"
#include "frame/test_macros.h"
#include "lobbysvr_test_runtime_helper.h"    // NOLINT: build/include_subdir
#include "lobbysvr_test_user_team_common.h"  // NOLINT: build/include_subdir
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
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  int64_t heartbeat_interval_seconds = server_cfg.matching().heartbeat_interval().seconds();
  if (heartbeat_interval_seconds <= 0) {
    heartbeat_interval_seconds = 2;
  }
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

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{heartbeat_interval_seconds - 1});
  atfw::util::time::time_utility::update();
  CASE_EXPECT_TRUE(run_refresh("matching.periodic_heartbeat.before_interval"));
  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::matching::packer::get_full_name_of_matching_heart_bear())));

  if (heartbeat_interval_seconds <= 0) {
    heartbeat_interval_seconds = 2;
  }
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{heartbeat_interval_seconds + 1});
  CASE_EXPECT_TRUE(run_refresh("matching.periodic_heartbeat.second"));
  CASE_EXPECT_TRUE(pump_until_calls(2));
  CASE_EXPECT_EQ(2, static_cast<int>(captured_requests->size()));

  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
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

CASE_TEST(lobbysvr_user_matching, rejects_superseded_unit_without_overwriting_active_state) {
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
  CASE_EXPECT_FALSE(sync_result.accepted);
  CASE_EXPECT_FALSE(sync_result.has_pending_event);
  CASE_EXPECT_EQ(0, sync_result.acknowledge_event_id);
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

CASE_TEST(lobbysvr_user_matching, ignores_unregistered_and_previous_unit_sync) {
  auto user_inst = user::create(10011, 1, "matching-unregistered-sync-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  auto& manager = user_inst->get_user_matching_manager();
  PROJECT_NAMESPACE_ID::SSMatchingEventSync sync;
  sync.set_unit_id(1011);
  sync.mutable_unit_view()->mutable_unit()->set_unit_id(1011);
  sync.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
  sync.mutable_unit_view()->set_last_event_id(1);
  CASE_EXPECT_FALSE(manager.acknowledge_matching_sync(ctx, sync).accepted);
  CASE_EXPECT_EQ(0, manager.get_view().unit().unit_id());

  PROJECT_NAMESPACE_ID::table_user table;
  table.mutable_matching_data()->set_acknowledge_event_id(9);
  auto* view = table.mutable_matching_data()->mutable_view();
  view->mutable_unit()->set_unit_id(2011);
  view->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT);
  view->set_last_event_id(9);
  manager.init_from_table_data(ctx, table);
  CASE_EXPECT_FALSE(manager.acknowledge_matching_sync(ctx, sync).accepted);
  CASE_EXPECT_EQ(2011, manager.get_view().unit().unit_id());
  CASE_EXPECT_EQ(9, manager.get_last_event_id());
}

CASE_TEST(lobbysvr_user_matching, retains_terminal_view_and_stops_heartbeat_when_unit_is_recycled) {
  constexpr uint64_t kMatchsvrId = 0x1E0031;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  atfw::testing::mock_node node;
  node.set_id(kMatchsvrId)
      .set_name("terminal-matchsvr")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr));
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
  rpc::unit_test::ss_mock_rule_options rule_options;
  rule_options.match_node_id = kMatchsvrId;
  auto rule = rpc::matching::mock::matching_heart_bear(
      [](rpc::context&, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
         PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        CASE_EXPECT_EQ(1012, request.unit_id());
        CASE_EXPECT_EQ(9, request.heartbeat_data().acknowledge_event_id());
        response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND);
        RPC_RETURN_CODE(0);
      },
      rule_options);
  CASE_EXPECT_TRUE(!!rule);
  auto user_inst = user::create(10012, 1, "terminal-heartbeat-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst || !rule) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
      test, "matching.terminal_recycled", [user_inst](rpc::context& ctx) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        PROJECT_NAMESPACE_ID::table_user table;
        auto* data = table.mutable_matching_data();
        data->set_matchsvr_server_id(kMatchsvrId);
        data->set_acknowledge_event_id(9);
        data->mutable_view()->mutable_unit()->set_unit_id(1012);
        data->mutable_view()->set_last_event_id(9);
        data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT);
        manager.init_from_table_data(ctx, table);
        PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(manager.query_matchsvr_snapshot(ctx, 1012, kMatchsvrId, response)));
        manager.try_send_heartbeat(ctx);
        PROJECT_NAMESPACE_ID::SCMatchingCheckRsp client_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager.check_matching(ctx, client_response)));
        CASE_EXPECT_EQ(1012, client_response.view().unit_id());
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT,
                       client_response.view().status());
        RPC_RETURN_CODE(0);
      }));
  lobbysvr_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::matching::packer::get_full_name_of_matching_heart_bear()));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(lobbysvr_user_matching, registers_new_unit_before_create_and_accepts_early_sync) {
  constexpr uint64_t kMatchsvrId = 0x1E0032;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::db, atfw::testing::feature::resource};
  options.setup_callback = [](atfw::testing::runtime& runtime) {
    PROJECT_NAMESPACE_ID::config::ExcelLevel level;
    level.set_level_id(91001);
    level.set_level_type(1);
    level.set_matching_pool_id(91001);
    level.set_client_template_id(91001);
    org::xresloader::pb::xresloader_datablocks blocks;
    blocks.mutable_header()->set_hash_code("lobby-new-matching-level");
    blocks.add_data_block(level.SerializeAsString());
    runtime.resource().set_file("level.bytes", blocks.SerializeAsString());
    PROJECT_NAMESPACE_ID::config::ExcelMatchingPool pool;
    pool.set_id(91001);
    pool.set_unit_max_size(1);
    pool.set_faction_user_max_size(1);
    pool.set_max_faction_cout_limit(2);
    pool.set_max_user_cout_limit(2);
    pool.set_search_timeout_seconds(120);
    pool.set_confirm_timeout_seconds(15);
    blocks.clear_data_block();
    blocks.add_data_block(pool.SerializeAsString());
    runtime.resource().set_file("matching_pool.bytes", blocks.SerializeAsString());
    return 0;
  };
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  atfw::testing::mock_node node;
  node.set_id(kMatchsvrId)
      .set_name("new-round-matchsvr")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr))
      .set_type_name("matchsvr")
      .add_label("hpa_scaling_ready", "1");
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
  if (logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
  auto user_inst = user::create(10013, 1, "new-matching-round-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  rpc::unit_test::ss_mock_rule_options rule_options;
  rule_options.match_node_id = kMatchsvrId;
  auto rule = rpc::matching::mock::create_matching(
      [user_inst](rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCreateReq& request,
                  PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        CASE_EXPECT_NE(0, request.unit().unit_id());
        CASE_EXPECT_NE(1013, request.unit().unit_id());
        CASE_EXPECT_EQ(request.unit().unit_id(), manager.get_view().unit().unit_id());
        CASE_EXPECT_EQ(kMatchsvrId, manager.get_current_matchsvr_server_id());
        CASE_EXPECT_EQ(0, manager.get_last_event_id());
        CASE_EXPECT_EQ("next-version", request.scope().battle_version());
        CASE_EXPECT_EQ(1, request.unit().acceptable_level_ids_size());
        if (request.unit().acceptable_level_ids_size() == 1) {
          CASE_EXPECT_EQ(91001, request.unit().acceptable_level_ids(0));
        }
        // WAL 可早于 create 的回包到达；旧轮 ACK=9 不能影响新轮 event=1。
        PROJECT_NAMESPACE_ID::SSMatchingEventSync sync;
        sync.set_unit_id(request.unit().unit_id());
        *sync.mutable_unit_view()->mutable_unit() = request.unit();
        sync.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
        sync.mutable_unit_view()->set_last_event_id(1);
        const auto result = manager.acknowledge_matching_sync(ctx, sync, kMatchsvrId);
        CASE_EXPECT_TRUE(result.accepted);
        CASE_EXPECT_EQ(1, result.acknowledge_event_id);
        response.set_matching_id("new-matching-round-room");
        *response.mutable_snapshot() = sync.unit_view();
        response.mutable_snapshot()->set_last_event_id(0);
        RPC_RETURN_CODE(0);
      },
      rule_options);
  CASE_EXPECT_TRUE(!!rule);
  CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
      test, "matching.start_after_terminal", [user_inst](rpc::context& ctx) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        PROJECT_NAMESPACE_ID::table_user table;
        auto* data = table.mutable_matching_data();
        data->set_matchsvr_server_id(kMatchsvrId);
        data->set_acknowledge_event_id(9);
        data->mutable_view()->mutable_unit()->set_unit_id(1013);
        data->mutable_view()->set_last_event_id(9);
        data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT);
        data->mutable_level_data()->set_battle_version("next-version");
        data->mutable_level_data()->mutable_level_select()->add_level_ids(91001);
        data->add_matched_users()->mutable_room_key()->set_client_id("previous-battle");
        manager.init_from_table_data(ctx, table);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager.start_matching(ctx)));
        PROJECT_NAMESPACE_ID::table_user persisted;
        CASE_EXPECT_EQ(0, manager.dump(ctx, persisted));
        CASE_EXPECT_EQ(1, persisted.matching_data().acknowledge_event_id());
        CASE_EXPECT_EQ(1, persisted.matching_data().view().last_event_id());
        CASE_EXPECT_EQ("next-version", persisted.matching_data().level_data().battle_version());
        CASE_EXPECT_EQ(1, persisted.matching_data().matched_users_size());
        RPC_RETURN_CODE(0);
      }));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::matching::packer::get_full_name_of_create_matching()));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(lobbysvr_user_matching, old_heartbeat_cannot_clear_new_subscribed_unit) {
  constexpr uint64_t kMatchsvrId = 0x1E0033;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  atfw::testing::mock_node node;
  node.set_id(kMatchsvrId)
      .set_name("late-heartbeat-matchsvr")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr));
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
  auto user_inst = user::create(10014, 1, "late-heartbeat-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto received_new_heartbeat = std::make_shared<bool>(false);
  rpc::unit_test::ss_mock_rule_options rule_options;
  rule_options.match_node_id = kMatchsvrId;
  auto rule = rpc::matching::mock::matching_heart_bear(
      [user_inst, received_new_heartbeat](rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
                                          PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        if (request.unit_id() == 1014) {
          CASE_EXPECT_EQ(9, request.heartbeat_data().acknowledge_event_id());
          PROJECT_NAMESPACE_ID::DMatchingTeamSyncView next;
          next.set_unit_id(2014);
          next.set_subscriber_server_id(kMatchsvrId);
          manager.subscribe_matching_unit(ctx, next);
          CASE_EXPECT_EQ(2014, manager.get_view().unit().unit_id());
          response.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND);
        } else {
          CASE_EXPECT_EQ(2014, request.unit_id());
          CASE_EXPECT_EQ(0, request.heartbeat_data().acknowledge_event_id());
          *received_new_heartbeat = true;
          response.set_matching_id("new-team-room");
          response.mutable_snapshot()->mutable_unit()->set_unit_id(2014);
          response.mutable_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
        }
        RPC_RETURN_CODE(0);
      },
      rule_options);
  CASE_EXPECT_TRUE(!!rule);
  CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
      test, "matching.old_heartbeat", [user_inst](rpc::context& ctx) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        PROJECT_NAMESPACE_ID::table_user table;
        auto* data = table.mutable_matching_data();
        data->set_matchsvr_server_id(kMatchsvrId);
        data->set_acknowledge_event_id(9);
        data->mutable_view()->mutable_unit()->set_unit_id(1014);
        data->mutable_view()->set_last_event_id(9);
        data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT);
        manager.init_from_table_data(ctx, table);
        PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_MATCHING,
                       RPC_AWAIT_CODE_RESULT(manager.query_matchsvr_snapshot(ctx, 1014, kMatchsvrId, response)));
        CASE_EXPECT_EQ(2014, manager.get_view().unit().unit_id());
        CASE_EXPECT_TRUE(manager.is_in_matching());
        // 旧事件处理任务恢复后也不得再发一个携带新轮 ACK 的旧 Unit 心跳。
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_MATCHING,
                       RPC_AWAIT_CODE_RESULT(manager.query_matchsvr_snapshot(ctx, 1014, kMatchsvrId, response)));
        RPC_RETURN_CODE(0);
      }));
  for (int i = 0; i < 64 && !*received_new_heartbeat; ++i) {
    test.pump_once();
  }
  CASE_EXPECT_TRUE(*received_new_heartbeat);
  CASE_EXPECT_EQ(2014, user_inst->get_user_matching_manager().get_view().unit().unit_id());
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::matching::packer::get_full_name_of_matching_heart_bear()));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(lobbysvr_user_matching, repeated_subscription_preserves_ack_and_completed_view) {
  constexpr uint64_t kMatchsvrId = 0x1E0034;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  atfw::testing::mock_node node;
  node.set_id(kMatchsvrId)
      .set_name("repeated-subscription-matchsvr")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kMatchSvr));
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
  auto requests = std::make_shared<std::vector<PROJECT_NAMESPACE_ID::SSMatchingCheckReq>>();
  rpc::unit_test::ss_mock_rule_options rule_options;
  rule_options.match_node_id = kMatchsvrId;
  auto rule = rpc::matching::mock::matching_heart_bear(
      [requests](rpc::context&, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
                 PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response) -> rpc::result_code_type {
        requests->push_back(request);
        response.set_matching_id("repeated-subscription-room");
        response.mutable_snapshot()->mutable_unit()->set_unit_id(1015);
        response.mutable_snapshot()->set_last_event_id(request.heartbeat_data().acknowledge_event_id());
        response.mutable_snapshot()->set_status(
            request.heartbeat_data().acknowledge_event_id() == 8
                ? PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT
                : PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
        RPC_RETURN_CODE(0);
      },
      rule_options);
  CASE_EXPECT_TRUE(!!rule);
  auto user_inst = user::create(10015, 1, "repeated-subscription-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst || !rule) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  constexpr uint64_t kSessionId = 1015;
  atfw::testing::mock_client client;
  CASE_EXPECT_TRUE(team_test::bind_client_session(test, user_inst, kSessionId, client, false));
  const auto dirty_rpc_name = rpc::lobbysvrclientservice::packer::get_full_name_of_user_dirty_chg_sync();
  const size_t dirty_baseline = lobbysvr_test::find_stream_post_indices(test, kSessionId, dirty_rpc_name).size();
  CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
      test, "matching.repeated_subscription", [user_inst](rpc::context& ctx) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        PROJECT_NAMESPACE_ID::table_user table;
        auto* data = table.mutable_matching_data();
        data->set_matchsvr_server_id(kMatchsvrId);
        data->set_acknowledge_event_id(7);
        data->mutable_view()->mutable_unit()->set_unit_id(1015);
        data->mutable_view()->set_last_event_id(7);
        data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
        manager.init_from_table_data(ctx, table);
        PROJECT_NAMESPACE_ID::DMatchingTeamSyncView subscription;
        subscription.set_unit_id(1015);
        subscription.set_subscriber_server_id(kMatchsvrId);
        manager.subscribe_matching_unit(ctx, subscription);
        manager.subscribe_matching_unit(ctx, subscription);
        PROJECT_NAMESPACE_ID::table_user persisted;
        CASE_EXPECT_EQ(0, manager.dump(ctx, persisted));
        CASE_EXPECT_EQ(7, persisted.matching_data().acknowledge_event_id());
        RPC_RETURN_CODE(0);
      }));
  for (int i = 0; i < 64 && requests->empty(); ++i) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(1, requests->size());
  if (!requests->empty()) {
    CASE_EXPECT_EQ(7, requests->front().heartbeat_data().acknowledge_event_id());
  }
  CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
      test, "matching.terminal_ack", [user_inst](rpc::context& ctx) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        PROJECT_NAMESPACE_ID::SSMatchingEventSync sync;
        sync.set_unit_id(1015);
        sync.mutable_unit_view()->mutable_unit()->set_unit_id(1015);
        sync.mutable_unit_view()->set_last_event_id(8);
        sync.mutable_unit_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT);
        CASE_EXPECT_EQ(8, manager.acknowledge_matching_sync(ctx, sync, kMatchsvrId).acknowledge_event_id);
        PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager.query_matchsvr_snapshot(ctx, 1015, kMatchsvrId, response)));
        RPC_RETURN_CODE(0);
      }));
  CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
      test, "matching.completed_subscription", [user_inst](rpc::context& ctx) -> rpc::result_code_type {
        auto& manager = user_inst->get_user_matching_manager();
        PROJECT_NAMESPACE_ID::DMatchingTeamSyncView subscription;
        subscription.set_unit_id(1015);
        subscription.set_subscriber_server_id(kMatchsvrId);
        manager.subscribe_matching_unit(ctx, subscription);
        manager.try_send_heartbeat(ctx);
        CASE_EXPECT_FALSE(manager.is_in_matching());
        CASE_EXPECT_EQ(1015, manager.get_view().unit().unit_id());
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT, manager.get_view().status());
        RPC_RETURN_CODE(0);
      }));
  lobbysvr_test::pump_rounds(test, 8);
  CASE_EXPECT_EQ(2, requests->size());
  // 最终 ACK 和重复订阅都不应把终态推送替换成空视图，也不应产生额外 dirty 推送。
  const auto dirty_posts = lobbysvr_test::find_stream_post_indices(test, kSessionId, dirty_rpc_name);
  CASE_EXPECT_EQ(dirty_baseline + 1, dirty_posts.size());
  if (dirty_posts.size() > dirty_baseline) {
    const auto* record = test.cs().call_at(dirty_posts.back());
    CASE_EXPECT_TRUE(record != nullptr);
    if (record) {
      atframework::CSMsg message;
      PROJECT_NAMESPACE_ID::SCUserDirtyChgSync body;
      CASE_EXPECT_TRUE(message.ParseFromString(record->message.body().post().content()));
      CASE_EXPECT_TRUE(body.ParseFromString(message.body_bin()));
      CASE_EXPECT_TRUE(body.has_dirty_matching_chg());
      CASE_EXPECT_EQ(1015, body.dirty_matching_chg().client_view().unit_id());
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT,
                     body.dirty_matching_chg().client_view().status());
    }
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(lobbysvr_user_matching, new_subscription_cannot_discard_unfinished_business_effects) {
  auto user_inst = user::create(10016, 1, "unfinished-matching-effects-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  auto& manager = user_inst->get_user_matching_manager();
  PROJECT_NAMESPACE_ID::table_user table;
  auto* data = table.mutable_matching_data();
  data->set_matchsvr_server_id(0x1E0035);
  data->set_acknowledge_event_id(7);
  data->mutable_view()->mutable_unit()->set_unit_id(1016);
  data->mutable_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED);
  data->mutable_view()->set_last_event_id(8);
  data->mutable_view()->mutable_orbit_room_key()->set_client_id("pending-orbit-handoff");
  manager.init_from_table_data(ctx, table);
  PROJECT_NAMESPACE_ID::DMatchingTeamSyncView subscription;
  subscription.set_unit_id(2016);
  subscription.set_subscriber_server_id(0x1E0035);
  manager.subscribe_matching_unit(ctx, subscription);
  CASE_EXPECT_EQ(1016, manager.get_view().unit().unit_id());
  CASE_EXPECT_EQ("pending-orbit-handoff", manager.get_view().orbit_room_key().client_id());
  PROJECT_NAMESPACE_ID::table_user persisted;
  CASE_EXPECT_EQ(0, manager.dump(ctx, persisted));
  CASE_EXPECT_EQ(7, persisted.matching_data().acknowledge_event_id());
}

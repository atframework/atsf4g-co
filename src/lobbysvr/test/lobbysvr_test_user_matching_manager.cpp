// Copyright 2026 atframework

// Offline state-transition tests for lobbysvr user_matching_manager.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_cs.h>
#include <atframework/testing/runtime.h>

#include <rpc/rpc_context.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "data/session.h"
#include "data/user.h"
#include "frame/test_macros.h"
#include "logic/matching/user_matching_manager.h"
#include "logic/session_manager.h"
#include "rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h"

namespace {

std::vector<PROJECT_NAMESPACE_ID::SCMatchingLogSync> collect_matching_log_syncs(atfw::testing::runtime& test,
                                                                                uint64_t session_id) {
  std::vector<PROJECT_NAMESPACE_ID::SCMatchingLogSync> result;
  for (size_t index = 0; index < test.cs().call_count(); ++index) {
    const auto* record = test.cs().call_at(index);
    if (record == nullptr || record->op != atfw::testing::cs_downstream_record::op_type::post ||
        record->session_id != session_id) {
      continue;
    }

    atframework::CSMsg message;
    if (!message.ParseFromString(record->message.body().post().content()) || !message.head().has_rpc_stream() ||
        message.head().rpc_stream().rpc_name() !=
            rpc::lobbysvrclientservice::packer::get_full_name_of_matching_log_sync()) {
      continue;
    }

    PROJECT_NAMESPACE_ID::SCMatchingLogSync sync;
    if (sync.ParseFromString(message.body_bin())) {
      result.emplace_back(std::move(sync));
    }
  }
  return result;
}

}  // namespace

CASE_TEST(lobbysvr_user_matching, advances_view_revision_only_for_client_changes_and_room_switches) {
  auto user_inst = user::create(10001, 1, "matching-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::table_user table;
  auto* persisted_data = table.mutable_matching_data();
  persisted_data->mutable_legacy_snapshot()->set_matching_id("finished-matching");
  persisted_data->mutable_legacy_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED);
  persisted_data->mutable_legacy_snapshot()->set_last_event_id(8);
  auto* persisted_unit = persisted_data->mutable_legacy_snapshot()->add_units();
  persisted_unit->set_unit_id(1001);
  persisted_unit->add_users()->mutable_user_key()->set_user_id(10001);
  persisted_unit->mutable_users(0)->mutable_user_key()->set_zone_id(1);
  persisted_data->set_last_event_id(8);

  auto& manager = user_inst->get_user_matching_manager();
  manager.init_from_table_data(ctx, table);
  CASE_EXPECT_EQ(8, manager.get_last_event_id());
  CASE_EXPECT_FALSE(manager.is_in_matching());
  CASE_EXPECT_TRUE(manager.is_dirty());
  PROJECT_NAMESPACE_ID::table_user migrated_table;
  CASE_EXPECT_EQ(0, manager.dump(ctx, migrated_table));
  CASE_EXPECT_TRUE(migrated_table.matching_data().has_view());
  CASE_EXPECT_FALSE(migrated_table.matching_data().has_legacy_snapshot());
  CASE_EXPECT_EQ(1001, migrated_table.matching_data().view().unit().unit_id());
  CASE_EXPECT_EQ(1, migrated_table.matching_data().client_view_revision());

  PROJECT_NAMESPACE_ID::SSMatchingEventSync new_matching_snapshot;
  new_matching_snapshot.set_matching_id("new-matching");
  new_matching_snapshot.mutable_player_view()->set_matching_id("new-matching");
  new_matching_snapshot.mutable_player_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
  new_matching_snapshot.mutable_player_view()->set_last_event_id(1);
  new_matching_snapshot.mutable_player_view()->mutable_unit()->set_unit_id(1002);
  manager.acknowledge_matching_sync(ctx, new_matching_snapshot);

  CASE_EXPECT_EQ(1, manager.get_last_event_id());
  CASE_EXPECT_EQ(1, manager.get_view().last_event_id());
  CASE_EXPECT_TRUE(manager.is_in_matching());
  CASE_EXPECT_EQ(2, manager.get_client_view().view_revision());
  CASE_EXPECT_EQ(1002, manager.get_client_view().unit_id());

  PROJECT_NAMESPACE_ID::SSMatchingEventSync source_switch_sync;
  source_switch_sync.set_matching_id("new-matching");
  protobuf_copy_message(*source_switch_sync.mutable_player_view(), manager.get_view());
  source_switch_sync.mutable_player_view()->set_last_event_id(2);
  auto* switch_event = source_switch_sync.add_event_logs();
  switch_event->set_event_id(2);
  switch_event->set_room_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
  switch_event->mutable_remove_unit()->mutable_unit()->set_unit_id(1002);
  switch_event->mutable_remove_unit()->set_switch_to_matching_id("switched-matching");
  manager.acknowledge_matching_sync(ctx, source_switch_sync);

  CASE_EXPECT_EQ(2, manager.get_last_event_id());
  CASE_EXPECT_EQ(2, manager.get_client_view().view_revision());

  PROJECT_NAMESPACE_ID::SSMatchingEventSync target_switch_sync;
  target_switch_sync.set_matching_id("switched-matching");
  protobuf_copy_message(*target_switch_sync.mutable_player_view(), manager.get_view());
  target_switch_sync.mutable_player_view()->set_matching_id("switched-matching");
  target_switch_sync.mutable_player_view()->set_last_event_id(1);
  manager.acknowledge_matching_sync(ctx, target_switch_sync);

  CASE_EXPECT_EQ(1, manager.get_last_event_id());
  CASE_EXPECT_EQ(3, manager.get_client_view().view_revision());

  PROJECT_NAMESPACE_ID::SSMatchingEventSync timeout_sync;
  timeout_sync.set_matching_id("switched-matching");
  protobuf_copy_message(*timeout_sync.mutable_player_view(), manager.get_view());
  timeout_sync.mutable_player_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT);
  timeout_sync.mutable_player_view()->set_last_event_id(2);
  auto* timeout_event = timeout_sync.add_event_logs();
  timeout_event->set_event_id(2);
  timeout_event->set_room_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT);
  timeout_event->set_timeout(123);
  manager.acknowledge_matching_sync(ctx, timeout_sync);

  CASE_EXPECT_EQ(2, manager.get_last_event_id());
  CASE_EXPECT_EQ(2, manager.get_view().last_event_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT, manager.get_view().status());
  CASE_EXPECT_FALSE(manager.is_in_matching());
  CASE_EXPECT_EQ(4, manager.get_client_view().view_revision());
}

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

CASE_TEST(lobbysvr_user_matching, keeps_view_revision_stable_for_wal_only_updates) {
  constexpr uint64_t kGatewayNodeId = 0x82000001;
  constexpr uint64_t kSessionId = 10003;

  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::cs};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto client = test.cs().create_client(kGatewayNodeId, kSessionId);
  CASE_EXPECT_TRUE(!!client);
  CASE_EXPECT_EQ(0, client.add());

  session::key_t session_key;
  session_key.node_id = kGatewayNodeId;
  session_key.session_id = kSessionId;
  auto sess = session_manager::me()->find(session_key);
  auto user_inst = user::create(10003, 1, "matching-sync-test-user");
  CASE_EXPECT_TRUE(!!sess);
  CASE_EXPECT_TRUE(!!user_inst);
  if (!sess || !user_inst) {
    test.stop();
    return;
  }

  PROJECT_NAMESPACE_ID::SSMatchingEventSync incremental_sync;
  incremental_sync.set_matching_id("matching-sync-presence");
  incremental_sync.mutable_player_view()->set_matching_id("matching-sync-presence");
  incremental_sync.mutable_player_view()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
  incremental_sync.mutable_player_view()->set_last_event_id(1);
  incremental_sync.mutable_player_view()->mutable_unit()->set_unit_id(1003);
  auto* event = incremental_sync.add_event_logs();
  event->set_event_id(1);
  event->set_room_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
  event->mutable_add_unit();

  PROJECT_NAMESPACE_ID::SSMatchingEventSync snapshot_sync;
  snapshot_sync.set_matching_id("matching-sync-presence");
  protobuf_copy_message(*snapshot_sync.mutable_player_view(), incremental_sync.player_view());
  snapshot_sync.mutable_player_view()->set_last_event_id(2);
  auto stale_cancel_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  auto stale_cancel_response = std::make_shared<PROJECT_NAMESPACE_ID::SCMatchingCancelRsp>();
  auto stale_confirm_result = std::make_shared<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  auto stale_confirm_response = std::make_shared<PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp>();

  auto task =
      test.run_task("matching.forward_snapshot_presence", std::chrono::seconds{2},
                    [user_inst, sess, incremental_sync, snapshot_sync, stale_cancel_result, stale_cancel_response,
                     stale_confirm_result, stale_confirm_response](rpc::context& ctx) -> rpc::result_code_type {
                      sess->set_user(user_inst);
                      user_inst->set_session(ctx, sess);
                      auto& manager = user_inst->get_user_matching_manager();
                      manager.acknowledge_matching_sync(ctx, incremental_sync);
                      manager.acknowledge_matching_sync(ctx, snapshot_sync);

                      PROJECT_NAMESPACE_ID::CSMatchingCancelReq stale_cancel;
                      stale_cancel.set_unit_id(9999);
                      *stale_cancel_result =
                          RPC_AWAIT_CODE_RESULT(manager.cancel_matching(ctx, stale_cancel, *stale_cancel_response));

                      PROJECT_NAMESPACE_ID::CSMatchingConfirmReq stale_confirm;
                      stale_confirm.set_unit_id(9999);
                      stale_confirm.set_confirmed(true);
                      *stale_confirm_result =
                          RPC_AWAIT_CODE_RESULT(manager.confirm_matching(ctx, stale_confirm, *stale_confirm_response));
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
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, *stale_cancel_result);
  CASE_EXPECT_TRUE(stale_cancel_response->has_view());
  CASE_EXPECT_EQ(1003, stale_cancel_response->view().unit_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, *stale_confirm_result);
  CASE_EXPECT_TRUE(stale_confirm_response->has_view());
  CASE_EXPECT_EQ(1003, stale_confirm_response->view().unit_id());

  auto sync_messages = collect_matching_log_syncs(test, kSessionId);
  CASE_EXPECT_EQ(2, sync_messages.size());
  if (sync_messages.size() == 2) {
    CASE_EXPECT_TRUE(sync_messages[0].has_client_view());
    CASE_EXPECT_TRUE(sync_messages[1].has_client_view());
    CASE_EXPECT_EQ(1003, sync_messages[0].client_view().unit_id());
    CASE_EXPECT_EQ(1, sync_messages[0].client_view().view_revision());
    CASE_EXPECT_EQ(sync_messages[0].client_view().view_revision(), sync_messages[1].client_view().view_revision());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

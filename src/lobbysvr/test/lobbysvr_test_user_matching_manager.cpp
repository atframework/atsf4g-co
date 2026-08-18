// Copyright 2026 atframework

// Offline state-transition tests for lobbysvr user_matching_manager.

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_cs.h>
#include <atframework/testing/runtime.h>

#include <rpc/rpc_context.h>

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

CASE_TEST(lobbysvr_user_matching, resets_wal_cursor_when_matching_id_changes) {
  auto user_inst = user::create(10001, 1, "matching-test-user");
  CASE_EXPECT_TRUE(!!user_inst);
  if (!user_inst) {
    return;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::table_user table;
  auto* persisted_data = table.mutable_matching_data();
  persisted_data->mutable_snapshot()->set_matching_id("finished-matching");
  persisted_data->mutable_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED);
  persisted_data->mutable_snapshot()->set_last_event_id(8);
  persisted_data->set_last_event_id(8);
  persisted_data->set_client_acknowledge_event_id(8);

  auto& manager = user_inst->get_user_matching_manager();
  manager.init_from_table_data(ctx, table);
  CASE_EXPECT_EQ(8, manager.get_last_event_id());
  CASE_EXPECT_FALSE(manager.is_in_matching());

  PROJECT_NAMESPACE_ID::SSMatchingEventSync new_matching_snapshot;
  new_matching_snapshot.set_matching_id("new-matching");
  new_matching_snapshot.mutable_room_snapshot()->set_matching_id("new-matching");
  new_matching_snapshot.mutable_room_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
  new_matching_snapshot.mutable_room_snapshot()->set_last_event_id(1);
  manager.acknowledge_matching_sync(ctx, new_matching_snapshot);

  CASE_EXPECT_EQ(1, manager.get_last_event_id());
  CASE_EXPECT_EQ(1, manager.get_snapshot().last_event_id());
  CASE_EXPECT_TRUE(manager.is_in_matching());

  PROJECT_NAMESPACE_ID::SSMatchingEventSync timeout_sync;
  timeout_sync.set_matching_id("new-matching");
  auto* timeout_event = timeout_sync.add_event_logs();
  timeout_event->set_event_id(2);
  timeout_event->set_room_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT);
  timeout_event->set_timeout(123);
  manager.acknowledge_matching_sync(ctx, timeout_sync);

  CASE_EXPECT_EQ(2, manager.get_last_event_id());
  CASE_EXPECT_EQ(2, manager.get_snapshot().last_event_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT, manager.get_snapshot().status());
  CASE_EXPECT_FALSE(manager.is_in_matching());
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
    auto* snapshot = table.mutable_matching_data()->mutable_snapshot();
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

CASE_TEST(lobbysvr_user_matching, forwards_snapshot_only_when_source_contains_one) {
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
  auto* event = incremental_sync.add_event_logs();
  event->set_event_id(1);
  event->set_room_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
  event->mutable_add_unit();

  PROJECT_NAMESPACE_ID::SSMatchingEventSync snapshot_sync;
  snapshot_sync.set_matching_id("matching-sync-presence");
  snapshot_sync.mutable_room_snapshot()->set_matching_id("matching-sync-presence");
  snapshot_sync.mutable_room_snapshot()->set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING);
  snapshot_sync.mutable_room_snapshot()->set_last_event_id(1);

  auto task =
      test.run_task("matching.forward_snapshot_presence", std::chrono::seconds{2},
                    [user_inst, sess, incremental_sync, snapshot_sync](rpc::context& ctx) -> rpc::result_code_type {
                      sess->set_user(user_inst);
                      user_inst->set_session(ctx, sess);
                      auto& manager = user_inst->get_user_matching_manager();
                      manager.acknowledge_matching_sync(ctx, incremental_sync);
                      manager.acknowledge_matching_sync(ctx, snapshot_sync);
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

  auto sync_messages = collect_matching_log_syncs(test, kSessionId);
  CASE_EXPECT_EQ(2, sync_messages.size());
  if (sync_messages.size() == 2) {
    CASE_EXPECT_EQ(1, sync_messages[0].event_logs_size());
    CASE_EXPECT_FALSE(sync_messages[0].has_snapshot());
    CASE_EXPECT_TRUE(sync_messages[1].has_snapshot());
    CASE_EXPECT_EQ("matching-sync-presence", sync_messages[1].snapshot().matching_id());
  }

  CASE_EXPECT_EQ(0, test.stop());
}

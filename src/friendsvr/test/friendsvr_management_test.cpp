// Copyright 2026 atframework
//
// Offline management tests with real RPC actions, router ownership, participant state and WAL notifications.
// Discovery, transport and database operations use the rpc-unit-test runtime's in-memory adapters.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/friend_management_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>
#include <atframework/testing/ss_action.h>

#include <config/logic_config.h>
#include <router/router_friend_manager.h>
#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/friend_api/friend_algorithm.h>
#include <rpc/friend_api/friendmanagementnotifyservice.atfw.gen.h>
#include <rpc/rpc_context.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "app/handle_ss_rpc_friendmanagementservice.atfw.gen.h"
#include "data/friend_object.h"
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

CASE_TEST(friendsvr_management, subscribe_without_router_rejects_request) {
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
      "subscribe_without_router_rejects_request", std::chrono::seconds{2},
      [request, invoke_options](rpc::context& ctx) -> rpc::result_code_type {
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

namespace {
using atfw::friend_api::DFriendEvent;
using atfw::friend_api::DFriendSubscribeKey;
using atfw::friend_api::friend_object;
using atfw::friend_api::SSFriendManagementEventSync;
using atfw::friend_api::table_friend_blob_data;
constexpr uint64_t kNotificationNodeId = 0x11000002;

static void run_friend_case(const std::function<rpc::result_code_type(rpc::context&, atfw::testing::runtime&)>& body,
                            bool service = false) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  // These cases use immediate in-memory boundaries. Keep their combined hard deadlines below CTest's 120 seconds.
  options.teardown_deadline = std::chrono::seconds{1};
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::db};
  if (service) {
    options.features.push_back(atfw::testing::feature::router);
    options.setup_callback = [](atfw::testing::runtime&) -> int {
      atfw::friend_api::router_friend_manager::me()->set_create_object_fn(friend_object::create);
      return handle::friend_api::register_handles_for_friendmanagementservice();
    };
  }
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_friend>();
  if (service) {
    // Production starts the router timer on the first common-module tick before serving requests.
    CASE_EXPECT_GE(router_manager_set::me()->tick(), 0);
  }
  atfw::testing::mock_node node;
  node.set_id(kNotificationNodeId).set_name("friend-subscriber").set_zone_id(1);
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
  auto task =
      test.run_task("friend_management", std::chrono::seconds{1},
                    [&body, &test, service](rpc::context& ctx) -> rpc::result_code_type {
                      auto result = RPC_AWAIT_CODE_RESULT(body(ctx, test));
                      if (service) {
                        auto manager = atfw::friend_api::router_friend_manager::me();
                        std::vector<router_object_base::key_t> keys;
                        manager->foreach_object([&keys](const atfw::friend_api::router_friend_manager::ptr_t& cache) {
                          keys.push_back(cache->get_key());
                          return true;
                        });
                        manager->foreach_cache([&keys](const atfw::friend_api::router_friend_manager::ptr_t& cache) {
                          keys.push_back(cache->get_key());
                          return true;
                        });
                        for (const auto& key : keys) {
                          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager->remove_cache(ctx, key, nullptr, nullptr)));
                        }
                        manager->set_create_object_fn({});
                      }
                      RPC_RETURN_CODE(result);
                    });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{2});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_FALSE(result.hard_timed_out);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

static DFriendSubscribeKey make_subscription() {
  DFriendSubscribeKey result;
  result.mutable_subscriber_user_key()->set_zone_id(1);
  result.mutable_subscriber_user_key()->set_user_id(1001);
  return result;
}

static DFriendEvent make_friend_event(uint64_t user_id) {
  DFriendEvent result;
  result.mutable_add_friend_data()->mutable_user_key()->set_zone_id(1);
  result.mutable_add_friend_data()->mutable_user_key()->set_user_id(user_id);
  return result;
}

static int64_t append_event(rpc::context& ctx, friend_object& object, const DFriendEvent& event) {
  int32_t result = 0;
  atfw::friend_api::friend_wal_publisher_context parameter{ctx, result};
  auto log = object.get_wal_publisher().allocate_log(ctx.logical_now(), event.event_case(), parameter, event);
  CASE_EXPECT_TRUE(!!log);
  if (!log) {
    return 0;
  }
  auto event_id = log->event_id();
  auto status = object.get_wal_publisher().emplace_back_log(std::move(log), parameter);
  CASE_EXPECT_EQ(static_cast<int>(atfw::util::distributed_system::wal_result_code::kOk), static_cast<int>(status));
  return event_id;
}

// Inspect the actual transport packets at the flush boundary, including empty and duplicate packets.
static std::vector<SSFriendManagementEventSync> notifications(atfw::testing::runtime& test) {
  std::vector<SSFriendManagementEventSync> result;
  for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
    const auto* record = test.transport().outbound_at(i);
    if (record == nullptr || record->target_node_id != kNotificationNodeId) {
      continue;
    }
    atfw::SSMsg envelope;
    CASE_EXPECT_TRUE(envelope.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size())));
    if (envelope.head().rpc_stream().rpc_name() != rpc::friend_api::packer::get_full_name_of_management_event_sync()) {
      continue;
    }
    SSFriendManagementEventSync message;
    CASE_EXPECT_TRUE(message.ParseFromString(envelope.body_bin()));
    result.push_back(std::move(message));
  }
  return result;
}
}  // namespace

CASE_TEST(friendsvr_management, permanent_friend_survives_save_and_reload) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    CASE_EXPECT_TRUE(!!object);
    if (!object) {
      RPC_RETURN_CODE(-1);
    }
    append_event(ctx, *object, make_friend_event(2002));
    PROJECT_NAMESPACE_ID::table_friend stored;
    CASE_EXPECT_EQ(0, object->dump(ctx, stored));
    CASE_EXPECT_EQ(1, stored.blob_data().friend_list_size());
    CASE_EXPECT_EQ(1u, object->get_current_friend_count());
    auto restored = friend_object::create(ctx, 1, 1001);
    restored->load(ctx, stored, 3);
    CASE_EXPECT_EQ(1u, restored->get_current_friend_count());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, cache_round_trips_router_lock_and_clears_missing_lock) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
    const auto save_time = std::chrono::system_clock::from_time_t(123456) + std::chrono::microseconds{321};
    auto source = atfw::friend_api::friend_cache::create(ctx, 1, 1001);
    source->set_router_server(kNotificationNodeId, 17, save_time);
    source->mutable_statistics().set_daily_invitee(11);
    PROJECT_NAMESPACE_ID::table_friend stored;
    CASE_EXPECT_EQ(0, source->dump(ctx, stored));
    CASE_EXPECT_TRUE(stored.has_router_lock());
    CASE_EXPECT_EQ(kNotificationNodeId, stored.router_lock().router_server_id());
    CASE_EXPECT_EQ(17u, stored.router_lock().router_version());
    CASE_EXPECT_TRUE(save_time == protobuf_to_system_clock(stored.router_lock().router_save_timepoint()));

    for (size_t mode = 0; mode < 2; ++mode) {
      auto restored = atfw::friend_api::friend_cache::create(ctx, 1, 1001);
      PROJECT_NAMESPACE_ID::table_friend input;
      input.CopyFrom(stored);
      if (mode == 0) {
        restored->load(ctx, input, 23);
      } else {
        restored->load_and_move_db(ctx, std::move(input), 23);
      }
      CASE_EXPECT_EQ(kNotificationNodeId, restored->get_router_server_id());
      CASE_EXPECT_EQ(17u, restored->get_router_server_version());
      CASE_EXPECT_TRUE(save_time == restored->get_router_server_save_timepoint());
      CASE_EXPECT_EQ(23u, restored->get_db_version());
      CASE_EXPECT_EQ(11u, restored->get_statistics().daily_invitee());

      PROJECT_NAMESPACE_ID::table_friend missing_lock;
      missing_lock.CopyFrom(stored);
      missing_lock.clear_router_lock();
      if (mode == 0) {
        restored->load(ctx, missing_lock, 24);
      } else {
        restored->load_and_move_db(ctx, std::move(missing_lock), 24);
      }
      CASE_EXPECT_EQ(0u, restored->get_router_server_id());
      CASE_EXPECT_EQ(0u, restored->get_router_server_version());
      CASE_EXPECT_TRUE(std::chrono::system_clock::from_time_t(0) == restored->get_router_server_save_timepoint());
      CASE_EXPECT_EQ(24u, restored->get_db_version());
      CASE_EXPECT_EQ(11u, restored->get_statistics().daily_invitee());

      // Reusing an output message must overwrite its previous nonzero lock and keep the field present.
      PROJECT_NAMESPACE_ID::table_friend output;
      output.CopyFrom(stored);
      CASE_EXPECT_EQ(0, restored->dump(ctx, output));
      CASE_EXPECT_TRUE(output.has_router_lock());
      CASE_EXPECT_EQ(0u, output.router_lock().router_server_id());
      CASE_EXPECT_EQ(0u, output.router_lock().router_version());
      CASE_EXPECT_EQ(0, output.router_lock().router_save_timepoint().seconds());
      CASE_EXPECT_EQ(0, output.router_lock().router_save_timepoint().nanos());
    }
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, snapshot_replaces_statistics_and_private_transaction_data) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    object->refresh_feature_limit(ctx);
    object->mutable_statistics().set_daily_invitee(17);
    table_friend_blob_data snapshot;
    snapshot.mutable_transaction_storage()->set_type_url("private-transaction");
    snapshot.mutable_statistics()->set_daily_invitee(99);
    object->dump(ctx, snapshot, false);
    CASE_EXPECT_EQ(17u, snapshot.statistics().daily_invitee());
    CASE_EXPECT_FALSE(snapshot.has_transaction_storage());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, initial_subscription_sends_empty_snapshot_once) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend empty;
    object->load(ctx, empty, 1);
    object->refresh_feature_limit(ctx);
    object->subscribe(ctx, make_subscription(), kNotificationNodeId);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    auto packets = notifications(test);
    CASE_EXPECT_EQ(1u, packets.size());
    if (!packets.empty()) {
      CASE_EXPECT_EQ(1, packets[0].event_target_size());
      CASE_EXPECT_TRUE(packets[0].event_target(0).has_snapshot());
      CASE_EXPECT_EQ(0, packets[0].event_target(0).snapshot().friend_list_size());
      CASE_EXPECT_EQ(1001u, packets[0].event_target(0).subscriber_key(0).user_id());
    }
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(1u, notifications(test).size());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, incremental_notifications_stop_after_unsubscribe) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend empty;
    object->load(ctx, empty, 1);
    auto subscription = make_subscription();
    object->subscribe(ctx, subscription, kNotificationNodeId);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    size_t before = notifications(test).size();
    auto event_id = append_event(ctx, *object, make_friend_event(2002));
    object->refresh_feature_limit(ctx);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    auto packets = notifications(test);
    CASE_EXPECT_EQ(before + 1, packets.size());
    if (packets.size() > before) {
      const auto& target = packets.back().event_target(0);
      CASE_EXPECT_TRUE(target.has_increase());
      CASE_EXPECT_EQ(1, target.increase().event_log_size());
      if (target.increase().event_log_size() == 1) {
        CASE_EXPECT_EQ(event_id, target.increase().event_log(0).event_id());
        CASE_EXPECT_EQ(2002u, target.increase().event_log(0).add_friend_data().user_key().user_id());
      }
    }
    object->refresh_feature_limit(ctx);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(packets.size(), notifications(test).size());
    object->unsubscribe(ctx, subscription);
    append_event(ctx, *object, make_friend_event(3003));
    object->refresh_feature_limit(ctx);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(packets.size(), notifications(test).size());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, reconnect_at_current_checkpoint_does_not_repeat_snapshot) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend empty;
    object->load(ctx, empty, 1);
    auto subscription = make_subscription();
    object->subscribe(ctx, subscription, kNotificationNodeId);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    auto packets = notifications(test);
    CASE_EXPECT_EQ(1u, packets.size());
    if (packets.size() != 1) {
      RPC_RETURN_CODE(-1);
    }
    CASE_EXPECT_TRUE(packets[0].event_target(0).has_snapshot());

    const auto event_id = append_event(ctx, *object, make_friend_event(2002));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    packets = notifications(test);
    CASE_EXPECT_EQ(2u, packets.size());
    if (packets.size() != 2) {
      RPC_RETURN_CODE(-1);
    }
    CASE_EXPECT_TRUE(packets[1].event_target(0).has_increase());
    CASE_EXPECT_EQ(event_id, packets[1].event_target(0).increase().event_log(0).event_id());

    subscription.mutable_last_received()->set_sequence(event_id);
    object->unsubscribe(ctx, subscription);
    object->subscribe(ctx, subscription, kNotificationNodeId);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(2u, notifications(test).size());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, reconnect_replays_events_after_checkpoint_without_snapshot) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend empty;
    object->load(ctx, empty, 1);
    auto subscription = make_subscription();
    object->subscribe(ctx, subscription, kNotificationNodeId);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(1u, notifications(test).size());

    const auto received_event_id = append_event(ctx, *object, make_friend_event(2002));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(2u, notifications(test).size());
    subscription.mutable_last_received()->set_sequence(received_event_id);
    object->unsubscribe(ctx, subscription);

    const auto missing_event_id = append_event(ctx, *object, make_friend_event(3003));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(2u, notifications(test).size());

    object->refresh_feature_limit(ctx);
    object->subscribe(ctx, subscription, kNotificationNodeId);
    object->refresh_feature_limit(ctx);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    auto packets = notifications(test);
    CASE_EXPECT_EQ(3u, packets.size());
    if (packets.size() == 3) {
      const auto& target = packets.back().event_target(0);
      CASE_EXPECT_TRUE(target.has_increase());
      CASE_EXPECT_EQ(1, target.increase().event_log_size());
      if (target.increase().event_log_size() == 1) {
        CASE_EXPECT_EQ(missing_event_id, target.increase().event_log(0).event_id());
        CASE_EXPECT_EQ(3003u, target.increase().event_log(0).add_friend_data().user_key().user_id());
      }
    }
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, participant_keys_follow_str2int_conversion) {
  // Parentheses prevent the Windows max macro from expanding these calls.
  // NOLINTBEGIN(readability-redundant-parentheses)
  const auto maximum_zone = (std::numeric_limits<uint32_t>::max)();
  const auto maximum_user = (std::numeric_limits<uint64_t>::max)();
  // NOLINTEND(readability-redundant-parentheses)
  auto maximum = rpc::friend_api::friend_key_to_transaction_participator_key(maximum_zone, maximum_user);
  auto parsed = rpc::friend_api::transaction_participator_key_to_friend_key(maximum);
  CASE_EXPECT_EQ(maximum_zone, parsed.first);
  CASE_EXPECT_EQ(maximum_user, parsed.second);
  auto padded = rpc::friend_api::transaction_participator_key_to_friend_key("friend:0001:0002");
  CASE_EXPECT_EQ(1u, padded.first);
  CASE_EXPECT_EQ(2u, padded.second);
  auto hexadecimal = rpc::friend_api::transaction_participator_key_to_friend_key("friend:0x10:0x20");
  CASE_EXPECT_EQ(16u, hexadecimal.first);
  CASE_EXPECT_EQ(32u, hexadecimal.second);
  auto octal = rpc::friend_api::transaction_participator_key_to_friend_key(R"(friend:\10:\20)");
  CASE_EXPECT_EQ(8u, octal.first);
  CASE_EXPECT_EQ(16u, octal.second);
  auto truncated = rpc::friend_api::transaction_participator_key_to_friend_key("friend:1:2suffix");
  CASE_EXPECT_EQ(1u, truncated.first);
  CASE_EXPECT_EQ(2u, truncated.second);
  for (const auto* invalid : {
           "invalid1:2",
           "friend:1/2",
           "friend:1suffix:2",
           "friend:1:",
       }) {
    auto key = rpc::friend_api::transaction_participator_key_to_friend_key(invalid);
    CASE_EXPECT_EQ(0u, key.first);
    CASE_EXPECT_EQ(0u, key.second);
  }
}

namespace {
static rpc::result_code_type create_service_object(rpc::context& ctx, uint64_t user_id) {
  auto manager = atfw::friend_api::router_friend_manager::me();
  atfw::friend_api::router_friend_manager::ptr_t cache;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      manager->mutable_object(ctx, cache, router_object_base::key_t{manager->get_type_id(), 1, user_id}, nullptr)));
}

static friend_object::ptr_t service_object(uint64_t user_id) {
  auto manager = atfw::friend_api::router_friend_manager::me();
  auto cache = manager->get_object(router_object_base::key_t{manager->get_type_id(), 1, user_id});
  return cache ? std::static_pointer_cast<friend_object>(cache->get_object()) : nullptr;
}

static atfw::friend_api::SSFriendTransactionPrepareReq make_transaction(rpc::context& ctx, const std::string& uuid,
                                                                        const std::vector<DFriendEvent>& events) {
  atfw::friend_api::SSFriendTransactionPrepareReq request;
  auto* storage = request.mutable_transaction_request()->mutable_storage();
  storage->mutable_metadata()->set_transaction_uuid(uuid);
  storage->mutable_metadata()->set_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  protobuf_from_system_clock(*storage->mutable_metadata()->mutable_prepare_timepoint(), ctx.logical_now());
  protobuf_from_system_clock(*storage->mutable_metadata()->mutable_expire_timepoint(),
                             ctx.logical_now() + std::chrono::minutes{5});
  storage->mutable_configure()->set_resolve_max_times(3);
  storage->mutable_configure()->mutable_resolve_retry_interval()->set_seconds(60);
  PROJECT_NAMESPACE_ID::friend_transaction_data data;
  for (const auto& event : events) {
    *data.add_event_data() = event;
  }
  CASE_EXPECT_TRUE(storage->mutable_participator_data()->PackFrom(data));
  return request;
}

static rpc::result_code_type prepare_transaction(rpc::context& ctx, uint64_t user_id,
                                                 atfw::friend_api::SSFriendTransactionPrepareReq request) {
  atfw::friend_api::SSFriendTransactionPrepareRsp response;
  auto result = RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_transaction_prepare(
      ctx, PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, user_id, request, response));
  RPC_RETURN_CODE(result < 0 ? result : response.client_result());
}

static rpc::result_code_type commit_transaction(rpc::context& ctx, uint64_t user_id, const std::string& uuid) {
  atfw::friend_api::SSFriendTransactionCommitReq request;
  request.mutable_transaction_request()->set_transaction_uuid(uuid);
  atfw::friend_api::SSFriendTransactionCommitRsp response;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_transaction_commit(
      ctx, PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, user_id, request, response)));
}

static rpc::result_code_type reject_transaction(rpc::context& ctx, uint64_t user_id, const std::string& uuid) {
  atfw::friend_api::SSFriendTransactionRejectReq request;
  request.mutable_transaction_request()->set_transaction_uuid(uuid);
  atfw::friend_api::SSFriendTransactionRejectRsp response;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_transaction_reject(
      ctx, PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, user_id, request, response)));
}

static rpc::result_code_type read_stored(rpc::context& ctx, uint64_t user_id,
                                         PROJECT_NAMESPACE_ID::table_friend& stored) {
  uint64_t version = 0;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::get_all(ctx, 1, user_id, stored, version)));
}

static rpc::result_code_type save_service_object(rpc::context& ctx, uint64_t user_id) {
  auto manager = atfw::friend_api::router_friend_manager::me();
  auto cache = manager->get_cache(router_object_base::key_t{manager->get_type_id(), 1, user_id});
  if (!cache) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE);
  }
  router_object_base::io_task_guard save_guard;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(cache->save(ctx, nullptr, save_guard)));
}

static DFriendEvent make_invitation(bool incoming, uint64_t from, uint64_t to) {
  DFriendEvent event;
  auto* invitation = incoming ? event.mutable_add_inviter() : event.mutable_add_invitee();
  invitation->mutable_from_user()->set_zone_id(1);
  invitation->mutable_from_user()->set_user_id(from);
  invitation->mutable_to_user()->set_zone_id(1);
  invitation->mutable_to_user()->set_user_id(to);
  return event;
}
}  // namespace

CASE_TEST(friendsvr_management, generated_subscribe_uses_service_and_persistent_router) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        auto manager = atfw::friend_api::router_friend_manager::me();
        atfw::friend_api::router_friend_manager::ptr_t cache;
        router_object_base::key_t key{manager->get_type_id(), 1, 1001};
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager->mutable_object(ctx, cache, key, nullptr)));
        if (!cache) {
          RPC_RETURN_CODE(-1);
        }
        atfw::friend_api::SSFriendSubscribeReq request;
        *request.mutable_subscriber() = make_subscription();
        request.set_need_allocate_event_id(true);
        atfw::friend_api::SSFriendSubscribeRsp response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_subscribe(ctx, manager->get_type_id(), 1,
                                                                                      1001, request, response)));
        auto object = std::static_pointer_cast<friend_object>(cache->get_object());
        int32_t result = 0;
        atfw::friend_api::friend_wal_publisher_context parameter{ctx, result};
        auto subscriber =
            object->get_wal_publisher().find_subscriber(request.subscriber().subscriber_user_key(), parameter);
        CASE_EXPECT_TRUE(!!subscriber);
        if (subscriber) {
          CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(),
                         subscriber->get_private_data().subscriber_server_node_id);
        }
        PROJECT_NAMESPACE_ID::table_friend stored;
        uint64_t version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::get_all(ctx, 1, 1001, stored, version)));
        CASE_EXPECT_TRUE(stored.has_router_lock());
        CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), stored.router_lock().router_server_id());
        CASE_EXPECT_EQ(cache->get_router_version(), stored.router_lock().router_version());
        CASE_EXPECT_TRUE(object->get_router_server_save_timepoint() ==
                         protobuf_to_system_clock(stored.router_lock().router_save_timepoint()));
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, sdk_partial_query_reads_router_lock_and_persists_empty_lock) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> record{ctx};
        record->set_zone_id(1);
        record->set_user_id(1001);
        record->mutable_router_lock()->set_router_server_id(kNotificationNodeId);
        record->mutable_router_lock()->set_router_version(17);
        *record->mutable_router_lock()->mutable_router_save_timepoint() = protobuf_from_system_clock(ctx.logical_now());
        *record->mutable_blob_data()->add_friend_list() = make_friend_event(2002).add_friend_data();
        uint64_t db_version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::insert(ctx, record, &db_version)));

        auto manager = atfw::friend_api::router_friend_manager::me();
        manager->set_create_object_fn({});
        atfw::friend_api::router_friend_manager::ptr_t cache;
        router_object_base::key_t key{manager->get_type_id(), 1, 1001};
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager->mutable_cache(ctx, cache, key, nullptr)));
        if (!cache || !cache->get_object()) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_EQ(kNotificationNodeId, cache->get_router_server_id());
        CASE_EXPECT_EQ(17u, cache->get_router_version());
        CASE_EXPECT_EQ(kNotificationNodeId, cache->get_object()->get_router_server_id());
        CASE_EXPECT_TRUE(protobuf_to_system_clock(record->router_lock().router_save_timepoint()) ==
                         cache->get_object()->get_router_server_save_timepoint());
        CASE_EXPECT_EQ(0, cache->get_object()->get_db_data().friend_list_size());
        uint64_t router_server_id = 0;
        uint64_t router_version = 0;
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(manager->pull_online_server(ctx, key, router_server_id, router_version)));
        CASE_EXPECT_EQ(kNotificationNodeId, router_server_id);
        CASE_EXPECT_EQ(17u, router_version);

        // An explicitly present empty message replaces the entire old lock without changing the friend list.
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> update{ctx};
        update->set_zone_id(1);
        update->set_user_id(1001);
        update->mutable_router_lock();
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::replace(ctx, update, db_version)));
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        CASE_EXPECT_TRUE(stored.has_router_lock());
        CASE_EXPECT_EQ(0u, stored.router_lock().router_server_id());
        CASE_EXPECT_EQ(0u, stored.router_lock().router_version());
        CASE_EXPECT_FALSE(stored.router_lock().has_router_save_timepoint());
        CASE_EXPECT_EQ(1, stored.blob_data().friend_list_size());
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(manager->pull_online_server(ctx, key, router_server_id, router_version)));
        CASE_EXPECT_EQ(0u, router_server_id);
        CASE_EXPECT_EQ(0u, router_version);
        CASE_EXPECT_EQ(0u, cache->get_router_server_id());
        CASE_EXPECT_EQ(0u, cache->get_router_version());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, expired_router_lock_preserves_version_before_takeover) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        const auto expired_save_time =
            ctx.logical_now() - protobuf_to_system_clock(logic_config::me()->get_cfg_router().object_free_timeout()) -
            std::chrono::seconds{1};
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> record{ctx};
        record->set_zone_id(1);
        record->set_user_id(1001);
        record->mutable_router_lock()->set_router_server_id(kNotificationNodeId);
        record->mutable_router_lock()->set_router_version(17);
        *record->mutable_router_lock()->mutable_router_save_timepoint() = protobuf_from_system_clock(expired_save_time);
        *record->mutable_blob_data()->add_friend_list() = make_friend_event(2002).add_friend_data();
        uint64_t db_version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::insert(ctx, record, &db_version)));

        atfw::friend_api::router_friend_cache cache(ctx, 1, 1001);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(cache.pull_cache(ctx, nullptr)));
        CASE_EXPECT_EQ(0u, cache.get_router_server_id());
        CASE_EXPECT_EQ(17u, cache.get_router_version());
        CASE_EXPECT_EQ(0u, cache.get_object()->get_router_server_id());
        CASE_EXPECT_EQ(17u, cache.get_object()->get_router_server_version());
        CASE_EXPECT_TRUE(expired_save_time == cache.get_object()->get_router_server_save_timepoint());

        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), stored.router_lock().router_server_id());
        CASE_EXPECT_EQ(18u, stored.router_lock().router_version());
        CASE_EXPECT_TRUE(expired_save_time < protobuf_to_system_clock(stored.router_lock().router_save_timepoint()));
        CASE_EXPECT_EQ(1u, service_object(1001)->get_current_friend_count());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, transaction_prepare_is_durable_and_commit_can_be_saved_later) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto request = make_transaction(ctx, "durable-add", {make_friend_event(2002)});
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        atfw::distributed_system::transaction_participator_snapshot pending;
        CASE_EXPECT_TRUE(stored.blob_data().transaction_storage().UnpackTo(&pending));
        CASE_EXPECT_EQ(1, pending.running_transaction_size());
        CASE_EXPECT_EQ(0u, service_object(1001)->get_current_friend_count());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "durable-add")));
        CASE_EXPECT_EQ(1u, service_object(1001)->get_current_friend_count());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(save_service_object(ctx, 1001)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        CASE_EXPECT_EQ(1, stored.blob_data().friend_list_size());
        CASE_EXPECT_TRUE(stored.blob_data().transaction_storage().UnpackTo(&pending));
        CASE_EXPECT_EQ(0, pending.running_transaction_size());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "durable-add")));
        CASE_EXPECT_EQ(1u, service_object(1001)->get_current_friend_count());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, invitation_acceptance_and_friend_removal_update_both_users) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        for (uint64_t user_id : {1001u, 2002u}) {
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, user_id)));
          auto invite = make_invitation(user_id == 2002, 1001, 2002);
          CASE_EXPECT_EQ(
              0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, user_id, make_transaction(ctx, "invite", {invite}))));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, user_id, "invite")));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, user_id, "invite")));
        }
        CASE_EXPECT_EQ(1u, service_object(1001)->get_statistics().daily_invitee());
        CASE_EXPECT_EQ(1u, service_object(2002)->get_statistics().daily_inviter());
        CASE_EXPECT_EQ(1u, service_object(2002)->get_current_inviter_count());
        for (uint64_t user_id : {1001u, 2002u}) {
          DFriendEvent remove;
          auto invitation = make_invitation(user_id == 2002, 1001, 2002);
          if (user_id == 1001) {
            *remove.mutable_remove_invitee() = invitation.add_invitee();
          } else {
            *remove.mutable_remove_inviter() = invitation.add_inviter();
          }
          auto request = make_transaction(ctx, "accept", {remove, make_friend_event(user_id == 1001 ? 2002 : 1001)});
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, user_id, request)));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, user_id, "accept")));
          table_friend_blob_data snapshot;
          service_object(user_id)->dump(ctx, snapshot, false);
          CASE_EXPECT_EQ(1, snapshot.friend_list_size());
          CASE_EXPECT_EQ(0, snapshot.inviter_list_size());
          CASE_EXPECT_EQ(0, snapshot.invitee_list_size());
          DFriendEvent deletion;
          *deletion.mutable_remove_friend_data() = make_friend_event(user_id == 1001 ? 2002 : 1001).add_friend_data();
          CASE_EXPECT_EQ(
              0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, user_id, make_transaction(ctx, "delete", {deletion}))));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, user_id, "delete")));
          CASE_EXPECT_EQ(0u, service_object(user_id)->get_current_friend_count());
        }
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, rejected_transaction_leaves_no_relationship_or_reservation) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto request = make_transaction(ctx, "rejected-invite", {make_invitation(false, 1001, 2002)});
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(reject_transaction(ctx, 1001, "rejected-invite")));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(reject_transaction(ctx, 1001, "rejected-invite")));
        auto object = service_object(1001);
        CASE_EXPECT_TRUE(object->is_empty());
        CASE_EXPECT_EQ(0u, object->get_statistics().daily_invitee());
        CASE_EXPECT_TRUE(object->get_transaction_handle().get_running_transactions().empty());
        // The transaction SDK treats late messages for a removed running entry as an idempotent success.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "rejected-invite")));
        CASE_EXPECT_TRUE(object->is_empty());
        CASE_EXPECT_EQ(0u, object->get_statistics().daily_invitee());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, expired_relationship_removal_is_delivered_once) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend empty;
    object->load(ctx, empty, 1);
    auto event = make_friend_event(2002);
    auto expiration = ctx.logical_now() + std::chrono::hours{1};
    protobuf_from_system_clock(*event.mutable_add_friend_data()->mutable_expired_time(), expiration);
    append_event(ctx, *object, event);
    object->subscribe(ctx, make_subscription(), kNotificationNodeId);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    auto before = notifications(test).size();
    auto cleanup_time = expiration + std::chrono::seconds{logic_config::me()->get_const_settings().time_tolerate()};
    object->cleanup_invalid_friends(ctx, cleanup_time);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(0u, object->get_current_friend_count());
    auto packets = notifications(test);
    CASE_EXPECT_EQ(before + 1, packets.size());
    if (packets.size() > before) {
      const auto& logs = packets.back().event_target(0).increase().event_log();
      CASE_EXPECT_EQ(1, logs.size());
      if (!logs.empty()) {
        CASE_EXPECT_TRUE(logs.Get(0).has_remove_friend_data());
        CASE_EXPECT_EQ(2002u, logs.Get(0).remove_friend_data().user_key().user_id());
      }
    }
    object->cleanup_invalid_friends(ctx, cleanup_time);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(packets.size(), notifications(test).size());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, pending_snapshot_suppresses_covered_incremental_events) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend empty;
    object->load(ctx, empty, 1);
    object->subscribe(ctx, make_subscription(), kNotificationNodeId);
    append_event(ctx, *object, make_friend_event(2002));
    object->refresh_feature_limit(ctx);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    auto packets = notifications(test);
    CASE_EXPECT_EQ(1u, packets.size());
    if (!packets.empty()) {
      CASE_EXPECT_EQ(1, packets[0].event_target_size());
      CASE_EXPECT_EQ(1, packets[0].event_target(0).snapshot().friend_list_size());
    }
    object->refresh_feature_limit(ctx);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
    CASE_EXPECT_EQ(1u, notifications(test).size());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, update_existing_friend_at_capacity_does_not_consume_another_slot) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend stored;
    stored.mutable_blob_data()->set_event_id_allocator(200);
    for (uint64_t user_id = 2000; user_id < 2200; ++user_id) {
      auto* item = stored.mutable_blob_data()->add_friend_list();
      item->mutable_user_key()->set_zone_id(1);
      item->mutable_user_key()->set_user_id(user_id);
      item->set_event_id(static_cast<int64_t>(user_id - 1999));
    }
    object->load(ctx, stored, 1);
    PROJECT_NAMESPACE_ID::friend_transaction_data data;
    *data.add_event_data() = make_friend_event(2002);
    CASE_EXPECT_EQ(0, object->check_prepare_transcation(ctx, "update-existing", data.event_data()));
    *data.mutable_event_data(0) = make_friend_event(3003);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_MAX_NUMBER_LIMIT,
                   object->check_prepare_transcation(ctx, "new-friend", data.event_data()));
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, transaction_options_share_stable_read_only_objects) {
  run_friend_case([](rpc::context&, atfw::testing::runtime&) -> rpc::result_code_type {
    auto* timeout = logic_config::me()->mutable_logic_cfg()->mutable_transaction()->mutable_timeout();
    const auto previous_timeout = *timeout;
    timeout->set_seconds(13);
    const auto& normal = rpc::friend_api::get_normal_transaction_options();
    const auto& force_commit = rpc::friend_api::get_force_commit_transaction_options();
    const auto shared_timeout = normal.timeout;
    CASE_EXPECT_FALSE(normal.force_commit);
    CASE_EXPECT_TRUE(force_commit.force_commit);
    CASE_EXPECT_EQ(shared_timeout, force_commit.timeout);
    timeout->set_seconds(29);
    CASE_EXPECT_EQ(&normal, &rpc::friend_api::get_normal_transaction_options());
    CASE_EXPECT_EQ(&force_commit, &rpc::friend_api::get_force_commit_transaction_options());
    CASE_EXPECT_EQ(shared_timeout, normal.timeout);
    CASE_EXPECT_EQ(shared_timeout, force_commit.timeout);
    *timeout = previous_timeout;
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, prepared_events_receive_identifiers_on_commit_after_reload) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto request = make_transaction(ctx, "stable-events", {make_invitation(false, 1001, 2002)});
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        atfw::distributed_system::transaction_participator_snapshot snapshot;
        CASE_EXPECT_TRUE(stored.blob_data().transaction_storage().UnpackTo(&snapshot));
        if (snapshot.running_transaction_size() != 1) {
          RPC_RETURN_CODE(-1);
        }
        PROJECT_NAMESPACE_ID::friend_transaction_data events;
        CASE_EXPECT_TRUE(snapshot.running_transaction(0).participator_data().UnpackTo(&events));
        CASE_EXPECT_EQ(1, events.event_data_size());
        if (events.event_data_size() != 1) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_EQ(0, events.event_data(0).event_id());
        auto object = service_object(1001);
        object->load(ctx, stored, object->get_db_version());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "stable-events")));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(save_service_object(ctx, 1001)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        CASE_EXPECT_EQ(1, stored.blob_data().invitee_list_size());
        if (stored.blob_data().invitee_list_size() == 1) {
          CASE_EXPECT_GT(stored.blob_data().invitee_list(0).event_id(), 0);
        }
        CASE_EXPECT_EQ(1u, stored.blob_data().statistics().daily_invitee());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, prepared_event_ids_remain_unassigned_when_dumping) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    auto request = make_transaction(ctx, "pack-on-dump", {make_friend_event(2002), make_friend_event(3003)});
    atfw::distributed_system::SSParticipatorTransactionPrepareRsp response;
    atfw::friend_api::friend_transaction_participator_handle::storage_ptr_type running;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->get_transaction_handle().prepare(
                          ctx, std::move(*request.mutable_transaction_request()), response, running)));
    CASE_EXPECT_TRUE(!!running);
    if (!running) {
      RPC_RETURN_CODE(-1);
    }
    PROJECT_NAMESPACE_ID::friend_transaction_data events;
    CASE_EXPECT_TRUE(running->participator_data().UnpackTo(&events));
    CASE_EXPECT_EQ(2, events.event_data_size());
    CASE_EXPECT_EQ(0, events.event_data(0).event_id());
    CASE_EXPECT_EQ(0, events.event_data(1).event_id());

    atfw::friend_api::table_friend_blob_data blob;
    object->dump(ctx, blob, true);
    atfw::distributed_system::transaction_participator_snapshot snapshot;
    CASE_EXPECT_TRUE(blob.transaction_storage().UnpackTo(&snapshot));
    CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
    if (snapshot.running_transaction_size() != 1) {
      RPC_RETURN_CODE(-1);
    }
    CASE_EXPECT_TRUE(snapshot.running_transaction(0).participator_data().UnpackTo(&events));
    CASE_EXPECT_EQ(0, events.event_data(0).event_id());
    CASE_EXPECT_EQ(0, events.event_data(1).event_id());
    CASE_EXPECT_EQ(0, blob.event_id_allocator());
    CASE_EXPECT_TRUE(running->participator_data().UnpackTo(&events));
    CASE_EXPECT_EQ(0, events.event_data(0).event_id());
    CASE_EXPECT_EQ(0, events.event_data(1).event_id());

    object->remove_transaction_data(ctx, "pack-on-dump");
    object->dump(ctx, blob, true);
    CASE_EXPECT_TRUE(blob.transaction_storage().UnpackTo(&snapshot));
    CASE_EXPECT_TRUE(snapshot.running_transaction(0).participator_data().UnpackTo(&events));
    CASE_EXPECT_EQ(0, events.event_data(0).event_id());
    CASE_EXPECT_EQ(0, events.event_data(1).event_id());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, snapshot_replacement_discards_old_transaction_and_wal_state) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
    auto object = friend_object::create(ctx, 1, 1001);
    auto request = make_transaction(ctx, "old-state", {make_friend_event(2002)});
    atfw::distributed_system::SSParticipatorTransactionPrepareRsp response;
    atfw::friend_api::friend_transaction_participator_handle::storage_ptr_type storage;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->get_transaction_handle().prepare(
                          ctx, std::move(*request.mutable_transaction_request()), response, storage)));
    append_event(ctx, *object, make_friend_event(3003));
    PROJECT_NAMESPACE_ID::table_friend replacement;
    object->load(ctx, replacement, 10);
    CASE_EXPECT_TRUE(object->get_transaction_handle().get_running_transactions().empty());
    CASE_EXPECT_TRUE(object->get_wal_publisher().get_log_manager().get_all_logs().empty());
    PROJECT_NAMESPACE_ID::friend_transaction_data fresh_data;
    *fresh_data.add_event_data() = make_friend_event(4004);
    google::protobuf::Any raw;
    CASE_EXPECT_TRUE(raw.PackFrom(fresh_data));
    auto parsed = object->mutable_transaction_participator_data(ctx, "old-state",
                                                                atfw::friend_api::friend_key_type{1, 1001}, raw);
    CASE_EXPECT_TRUE(!!parsed);
    if (parsed) {
      CASE_EXPECT_EQ(4004u, parsed->event_data(0).add_friend_data().user_key().user_id());
    }
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, wal_restores_only_meaningful_checkpoints_and_advances_event_ids) {
  run_friend_case([](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
    auto fresh = friend_object::create(ctx, 1, 1001);
    PROJECT_NAMESPACE_ID::table_friend empty;
    fresh->load(ctx, empty, 1);
    CASE_EXPECT_TRUE(fresh->get_wal_publisher().get_log_manager().get_global_ingore_key() == nullptr);
    const auto* initial_removed_key = fresh->get_wal_publisher().get_log_manager().get_last_removed_key();
    CASE_EXPECT_TRUE(initial_removed_key != nullptr);
    if (initial_removed_key != nullptr) {
      CASE_EXPECT_EQ(1, *initial_removed_key);
    }
    const auto first_id = fresh->allocate_event_id();
    CASE_EXPECT_GT(first_id, 1000000);
    PROJECT_NAMESPACE_ID::table_friend fresh_dump;
    CASE_EXPECT_EQ(0, fresh->dump(ctx, fresh_dump));
    CASE_EXPECT_EQ(0, fresh_dump.blob_data().global_finished_event_id());
    CASE_EXPECT_EQ(1, fresh_dump.blob_data().wal_removed_event_id());
    CASE_EXPECT_EQ(first_id, fresh_dump.blob_data().event_id_allocator());

    auto restored = friend_object::create(ctx, 1, 1002);
    PROJECT_NAMESPACE_ID::table_friend stored;
    stored.mutable_blob_data()->set_event_id_allocator(50);
    stored.mutable_blob_data()->set_global_finished_event_id(60);
    stored.mutable_blob_data()->set_wal_removed_event_id(70);
    restored->load(ctx, stored, 2);
    const auto& wal = restored->get_wal_publisher().get_log_manager();
    CASE_EXPECT_TRUE(wal.get_global_ingore_key() != nullptr);
    CASE_EXPECT_TRUE(wal.get_last_removed_key() != nullptr);
    if (wal.get_global_ingore_key() != nullptr && wal.get_last_removed_key() != nullptr) {
      CASE_EXPECT_EQ(60, *wal.get_global_ingore_key());
      CASE_EXPECT_EQ(70, *wal.get_last_removed_key());
    }
    CASE_EXPECT_EQ(71, restored->allocate_event_id());
    PROJECT_NAMESPACE_ID::table_friend saved;
    CASE_EXPECT_EQ(0, restored->dump(ctx, saved));
    CASE_EXPECT_EQ(60, saved.blob_data().global_finished_event_id());
    CASE_EXPECT_EQ(70, saved.blob_data().wal_removed_event_id());
    CASE_EXPECT_EQ(71, saved.blob_data().event_id_allocator());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(friendsvr_management, reserved_gift_and_invalid_events_cannot_enter_management_transactions) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        std::vector<DFriendEvent> invalid(4);
        invalid[0].mutable_add_gift()->set_gift_id(7);
        invalid[1].mutable_remove_gift()->set_gift_id(7);
        invalid[2].mutable_add_friend_data()->mutable_user_key()->set_user_id(2002);
        for (size_t i = 0; i < invalid.size(); ++i) {
          auto request = make_transaction(ctx, "invalid-" + std::to_string(i), {invalid[i]});
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                         RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        }
        auto invalid_id = make_friend_event(2002);
        invalid_id.set_event_id(-1);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                       RPC_AWAIT_CODE_RESULT(
                           prepare_transaction(ctx, 1001, make_transaction(ctx, "negative-event-id", {invalid_id}))));
        auto object = service_object(1001);
        CASE_EXPECT_TRUE(object->get_transaction_handle().get_running_transactions().empty());
        CASE_EXPECT_TRUE(object->is_empty());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, prepare_storage_failure_is_returned_and_retry_persists) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto failure = rpc::db::user_friend::mock::replace([](rpc::context&, const PROJECT_NAMESPACE_ID::table_friend&,
                                                              rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
        });
        auto request = make_transaction(ctx, "save-failed", {make_friend_event(2002)});
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED,
                       RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        CASE_EXPECT_EQ(0u, service_object(1001)->get_current_friend_count());
        failure.reset();
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        atfw::distributed_system::transaction_participator_snapshot snapshot;
        CASE_EXPECT_TRUE(stored.blob_data().transaction_storage().UnpackTo(&snapshot));
        CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, concurrent_record_creation_reloads_the_winning_record) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
        auto rule = test.db().mock_table("user_friend");
        rule.on(atfw::testing::mock_db::op_type::kv_insert, [&test](atfw::testing::db_table_context& call) {
          PROJECT_NAMESPACE_ID::table_friend winner;
          winner.CopyFrom(*call.input_table);
          winner.mutable_router_lock()->set_router_server_id(logic_config::me()->get_local_server_id());
          winner.mutable_router_lock()->set_router_version(9);
          winner.mutable_blob_data()->set_event_id_allocator(10);
          auto* friend_data = winner.mutable_blob_data()->add_friend_list();
          *friend_data = make_friend_event(2002).add_friend_data();
          friend_data->set_event_id(10);
          test.db().set_raw_kv(call.key, winner.GetTypeName(), winner.SerializeAsString(), 7);
          call.return_code = PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS;
          return true;
        });
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto object = service_object(1001);
        CASE_EXPECT_TRUE(!!object);
        if (object) {
          CASE_EXPECT_EQ(1u, object->get_current_friend_count());
          CASE_EXPECT_EQ(9u, object->get_router_server_version());
        }
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, remove_inviter_actions_and_gm_reset_are_idempotent) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto object = service_object(1001);
        append_event(ctx, *object, make_invitation(true, 2002, 1001));
        append_event(ctx, *object, make_invitation(true, 3003, 1001));
        atfw::friend_api::SSFriendRemoveOneInviterReq remove_one;
        remove_one.mutable_inviter_user_key()->set_zone_id(1);
        remove_one.mutable_inviter_user_key()->set_user_id(2002);
        atfw::friend_api::SSFriendRemoveOneInviterRsp remove_one_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_remove_one_inviter(
                              ctx, PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, 1001, remove_one, remove_one_response)));
        CASE_EXPECT_EQ(1u, object->get_current_inviter_count());
        atfw::friend_api::SSFriendRemoveAllInviterReq remove_all;
        atfw::friend_api::SSFriendRemoveAllInviterRsp remove_all_response;
        for (size_t i = 0; i < 2; ++i) {
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_remove_all_inviter(
                                ctx, PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, 1001, remove_all, remove_all_response)));
          CASE_EXPECT_EQ(0u, object->get_current_inviter_count());
        }
        CASE_EXPECT_EQ(2u, object->get_statistics().daily_inviter());
        atfw::friend_api::SSFriendGMResetLimitReq reset;
        atfw::friend_api::SSFriendGMResetLimitRsp reset_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_gm_reset_limit(
                              ctx, PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, 1001, reset, reset_response)));
        CASE_EXPECT_EQ(0u, object->get_statistics().daily_inviter());
        CASE_EXPECT_EQ(0u, object->get_statistics().sum_inviter());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, router_release_persists_ownership_and_rolls_back_on_failure) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto manager = atfw::friend_api::router_friend_manager::me();
        auto cache = manager->get_object({manager->get_type_id(), 1, 1001});
        auto object = service_object(1001);
        append_event(ctx, *object, make_friend_event(2002));
        {
          router_object_base::io_task_guard guard;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(cache->save(ctx, nullptr, guard)));
        }
        CASE_EXPECT_EQ(object->get_router_server_version(), cache->get_router_version());
        const auto version = object->get_router_server_version();
        auto failure = rpc::db::user_friend::mock::replace([](rpc::context&, const PROJECT_NAMESPACE_ID::table_friend&,
                                                              rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
        });
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED,
                       RPC_AWAIT_CODE_RESULT(manager->remove_friend_object(ctx, 1, 1001, nullptr)));
        CASE_EXPECT_TRUE(cache->is_writable());
        CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(), object->get_router_server_id());
        CASE_EXPECT_EQ(version, object->get_router_server_version());
        CASE_EXPECT_EQ(version, cache->get_router_version());
        failure.reset();
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager->remove_friend_object(ctx, 1, 1001, nullptr)));
        CASE_EXPECT_FALSE(cache->is_writable());
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        CASE_EXPECT_TRUE(stored.has_router_lock());
        CASE_EXPECT_EQ(0u, stored.router_lock().router_server_id());
        // Changing the owner advances the router version; persisting it advances the version once more.
        CASE_EXPECT_EQ(version + 2, stored.router_lock().router_version());
        CASE_EXPECT_EQ(stored.router_lock().router_version(), object->get_router_server_version());
        CASE_EXPECT_EQ(stored.router_lock().router_version(), cache->get_router_version());
        CASE_EXPECT_TRUE(object->get_router_server_save_timepoint() ==
                         protobuf_to_system_clock(stored.router_lock().router_save_timepoint()));
        CASE_EXPECT_EQ(1, stored.blob_data().friend_list_size());
        uint64_t router_server_id = logic_config::me()->get_local_server_id();
        uint64_t router_version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              manager->pull_online_server(ctx, cache->get_key(), router_server_id, router_version)));
        CASE_EXPECT_EQ(0u, router_server_id);
        CASE_EXPECT_EQ(version + 2, router_version);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        CASE_EXPECT_EQ(1u, service_object(1001)->get_current_friend_count());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, transactions_committed_out_of_order_deliver_both_changes) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto object = service_object(1001);
        object->subscribe(ctx, make_subscription(), kNotificationNodeId);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                              ctx, 1001, make_transaction(ctx, "first", {make_friend_event(2002)}))));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                              ctx, 1001, make_transaction(ctx, "second", {make_friend_event(3003)}))));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "second")));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "first")));
        CASE_EXPECT_EQ(2u, object->get_current_friend_count());
        auto packets = notifications(test);
        CASE_EXPECT_EQ(3u, packets.size());
        if (packets.size() == 3) {
          CASE_EXPECT_EQ(3003u,
                         packets[1].event_target(0).increase().event_log(0).add_friend_data().user_key().user_id());
          CASE_EXPECT_EQ(2002u,
                         packets[2].event_target(0).increase().event_log(0).add_friend_data().user_key().user_id());
        }
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
        CASE_EXPECT_EQ(packets.size(), notifications(test).size());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, replay_after_partial_wal_application_finishes_without_duplicate_statistics) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto request = make_transaction(ctx, "partial", {make_invitation(false, 1001, 2002), make_friend_event(3003)});
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, request)));
        auto object = service_object(1001);
        const auto& pending = object->get_transaction_handle().get_running_transactions();
        CASE_EXPECT_EQ(1u, pending.size());
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        atfw::distributed_system::transaction_participator_snapshot snapshot;
        CASE_EXPECT_TRUE(stored.blob_data().transaction_storage().UnpackTo(&snapshot));
        if (snapshot.running_transaction_size() != 1) {
          RPC_RETURN_CODE(-1);
        }
        PROJECT_NAMESPACE_ID::friend_transaction_data events;
        CASE_EXPECT_TRUE(snapshot.running_transaction(0).participator_data().UnpackTo(&events));
        if (events.event_data_size() != 2) {
          RPC_RETURN_CODE(-1);
        }
        // Model a local action failure after its first WAL event, before the participant leaves the running set.
        const atfw::friend_api::friend_key_type self_key{1, 1001};
        auto pending_transaction = pending.find("partial");
        CASE_EXPECT_TRUE(pending_transaction != pending.end());
        if (pending_transaction == pending.end() || !pending_transaction->second.storage) {
          RPC_RETURN_CODE(-1);
        }
        auto cached_events = object->mutable_transaction_participator_data(
            ctx, "partial", self_key, pending_transaction->second.storage->participator_data());
        CASE_EXPECT_TRUE(!!cached_events);
        if (!cached_events) {
          RPC_RETURN_CODE(-1);
        }
        const auto first_event_id = append_event(ctx, *object, cached_events->event_data(0));
        CASE_EXPECT_GT(first_event_id, 0);
        cached_events->mutable_event_data(0)->set_event_id(first_event_id);
        CASE_EXPECT_EQ(0, cached_events->event_data(1).event_id());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(save_service_object(ctx, 1001)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        CASE_EXPECT_TRUE(stored.blob_data().transaction_storage().UnpackTo(&snapshot));
        CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
        if (snapshot.running_transaction_size() != 1) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_TRUE(snapshot.running_transaction(0).participator_data().UnpackTo(&events));
        CASE_EXPECT_EQ(2, events.event_data_size());
        if (events.event_data_size() != 2) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_EQ(first_event_id, events.event_data(0).event_id());
        CASE_EXPECT_EQ(0, events.event_data(1).event_id());
        object->load(ctx, stored, object->get_db_version());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "partial")));
        CASE_EXPECT_EQ(1u, object->get_current_friend_count());
        CASE_EXPECT_EQ(1u, object->get_statistics().daily_invitee());
        atfw::friend_api::table_friend_blob_data final_state;
        object->dump(ctx, final_state, false);
        CASE_EXPECT_EQ(1, final_state.friend_list_size());
        if (final_state.friend_list_size() == 1) {
          CASE_EXPECT_GT(final_state.friend_list(0).event_id(), first_event_id);
        }
        CASE_EXPECT_TRUE(object->get_transaction_handle().get_running_transactions().empty());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, commit_succeeds_before_fast_save_retry) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                   ctx, 1001, make_transaction(ctx, "commit-save-failed", {make_invitation(false, 1001, 2002)}))));
        auto failure = rpc::db::user_friend::mock::replace([](rpc::context&, const PROJECT_NAMESPACE_ID::table_friend&,
                                                              rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
        });
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "commit-save-failed")));
        CASE_EXPECT_EQ(1u, service_object(1001)->get_statistics().daily_invitee());
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED,
                       RPC_AWAIT_CODE_RESULT(save_service_object(ctx, 1001)));
        failure.reset();
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "commit-save-failed")));
        CASE_EXPECT_EQ(1u, service_object(1001)->get_statistics().daily_invitee());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(save_service_object(ctx, 1001)));
        PROJECT_NAMESPACE_ID::table_friend stored;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
        CASE_EXPECT_EQ(1, stored.blob_data().invitee_list_size());
        CASE_EXPECT_EQ(1u, stored.blob_data().statistics().daily_invitee());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, sdk_cache_cannot_become_a_service_object) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        atfw::friend_api::router_friend_manager::me()->set_create_object_fn(atfw::friend_api::friend_cache::create);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        atfw::friend_api::SSFriendSubscribeReq request;
        *request.mutable_subscriber() = make_subscription();
        atfw::friend_api::SSFriendSubscribeRsp response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE,
                       RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_subscribe(
                           ctx, PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, 1001, request, response)));
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, reconnect_replays_later_committed_event_above_checkpoint) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto object = service_object(1001);
        auto subscription = make_subscription();
        object->subscribe(ctx, subscription, kNotificationNodeId);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                              ctx, 1001, make_transaction(ctx, "late", {make_friend_event(2002)}))));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                              ctx, 1001, make_transaction(ctx, "early", {make_friend_event(3003)}))));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "early")));
        auto packets = notifications(test);
        CASE_EXPECT_EQ(2u, packets.size());
        if (packets.size() != 2) {
          RPC_RETURN_CODE(-1);
        }
        subscription.mutable_last_received()->set_sequence(
            packets.back().event_target(0).increase().event_log(0).event_id());
        object->unsubscribe(ctx, subscription);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "late")));
        CASE_EXPECT_EQ(2u, object->get_current_friend_count());
        const auto& committed_logs = object->get_wal_publisher().get_log_manager().get_all_logs();
        CASE_EXPECT_EQ(2u, committed_logs.size());
        if (committed_logs.size() == 2) {
          CASE_EXPECT_EQ(committed_logs.front()->event_id(), subscription.last_received().sequence());
          CASE_EXPECT_GT(committed_logs.back()->event_id(), subscription.last_received().sequence());
        }
        object->refresh_feature_limit(ctx);
        object->subscribe(ctx, subscription, kNotificationNodeId);
        object->refresh_feature_limit(ctx);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(object->send_notification(ctx)));
        packets = notifications(test);
        CASE_EXPECT_EQ(3u, packets.size());
        if (packets.size() == 3) {
          CASE_EXPECT_TRUE(packets.back().event_target(0).has_increase());
          CASE_EXPECT_EQ(1, packets.back().event_target(0).increase().event_log_size());
          if (packets.back().event_target(0).increase().event_log_size() == 1) {
            CASE_EXPECT_EQ(
                2002u, packets.back().event_target(0).increase().event_log(0).add_friend_data().user_key().user_id());
          }
        }
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, prepare_rejects_supplied_event_identifiers) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        auto event = make_friend_event(2002);
        event.set_event_id(1);
        CASE_EXPECT_EQ(
            PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
            RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, make_transaction(ctx, "supplied", {event}))));
        auto object = service_object(1001);
        CASE_EXPECT_TRUE(object->get_transaction_handle().get_running_transactions().empty());
        CASE_EXPECT_EQ(0u, object->get_current_friend_count());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                              ctx, 1001, make_transaction(ctx, "supplied", {make_friend_event(2002)}))));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "supplied")));
        CASE_EXPECT_EQ(1u, object->get_current_friend_count());
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, force_commit_cannot_discard_prepared_transaction_data) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                              ctx, 1001, make_transaction(ctx, "prepared-force", {make_friend_event(2002)}))));
        auto object = service_object(1001);
        const auto& running = object->get_transaction_handle().get_running_transactions();
        auto prepared = running.find("prepared-force");
        CASE_EXPECT_TRUE(prepared != running.end());
        if (prepared == running.end() || !prepared->second.storage) {
          RPC_RETURN_CODE(-1);
        }
        const atfw::friend_api::friend_key_type self_key{1, 1001};
        auto original_data = object->mutable_transaction_participator_data(
            ctx, "prepared-force", self_key, prepared->second.storage->participator_data());
        CASE_EXPECT_TRUE(!!original_data);
        if (!original_data) {
          RPC_RETURN_CODE(-1);
        }
        const auto event_id = original_data->event_data(0).event_id();
        CASE_EXPECT_EQ(0, event_id);

        auto force_request = make_transaction(ctx, "prepared-force", {make_friend_event(3003)});
        force_request.mutable_transaction_request()->mutable_storage()->mutable_configure()->set_force_commit(true);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN,
                       RPC_AWAIT_CODE_RESULT(prepare_transaction(ctx, 1001, force_request)));
        auto retained_data = object->mutable_transaction_participator_data(
            ctx, "prepared-force", self_key, prepared->second.storage->participator_data());
        CASE_EXPECT_TRUE(!!retained_data);
        if (!retained_data) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_EQ(original_data.get(), retained_data.get());
        CASE_EXPECT_EQ(event_id, retained_data->event_data(0).event_id());

        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_transaction(
                              ctx, 1001, make_transaction(ctx, "prepared-force", {make_friend_event(3003)}))));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(commit_transaction(ctx, 1001, "prepared-force")));
        atfw::friend_api::table_friend_blob_data snapshot;
        object->dump(ctx, snapshot, false);
        CASE_EXPECT_EQ(1, snapshot.friend_list_size());
        if (snapshot.friend_list_size() == 1) {
          CASE_EXPECT_EQ(2002u, snapshot.friend_list(0).user_key().user_id());
          CASE_EXPECT_GT(snapshot.friend_list(0).event_id(), 0);
          CASE_EXPECT_EQ(original_data->event_data(0).event_id(), snapshot.friend_list(0).event_id());
        }
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, sdk_delegator_finishes_before_fast_save) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime&) -> rpc::result_code_type {
        using client_type = atfw::distributed_system::transaction_client_handle;
        auto delegator = atfw::util::memory::make_strong_rc<client_type::vtable_type>(
            rpc::friend_api::get_default_transaction_delegator());
        client_type client{delegator};
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 1001)));
        for (bool reject : {false, true}) {
          auto request = make_transaction(ctx, reject ? "sdk-reject" : "sdk-commit", {make_friend_event(2002)});
          client_type::storage_type storage;
          *storage.data.mutable_metadata() = request.transaction_request().storage().metadata();
          *storage.data.mutable_configure() = request.transaction_request().storage().configure();
          client_type::participator_type participator;
          participator.set_participator_key(rpc::friend_api::friend_key_to_transaction_participator_key(1, 1001));
          *participator.mutable_participator_data() = request.transaction_request().storage().participator_data();
          client_type::transaction_participator_failure_reason reason;
          CASE_EXPECT_EQ(
              0, RPC_AWAIT_CODE_RESULT(delegator->prepare_participator(ctx, client, storage, participator, reason)));
          auto failure =
              rpc::db::user_friend::mock::replace([](rpc::context&, const PROJECT_NAMESPACE_ID::table_friend&,
                                                     rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
                RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
              });
          auto& finish = reject ? delegator->reject_participator : delegator->commit_participator;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(finish(ctx, client, storage, participator)));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED,
                         RPC_AWAIT_CODE_RESULT(save_service_object(ctx, 1001)));
          failure.reset();
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(finish(ctx, client, storage, participator)));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(save_service_object(ctx, 1001)));
          PROJECT_NAMESPACE_ID::table_friend stored;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(read_stored(ctx, 1001, stored)));
          atfw::distributed_system::transaction_participator_snapshot snapshot;
          CASE_EXPECT_TRUE(stored.blob_data().transaction_storage().UnpackTo(&snapshot));
          CASE_EXPECT_EQ(0, snapshot.running_transaction_size());
          CASE_EXPECT_EQ(1, stored.blob_data().friend_list_size());
        }
        RPC_RETURN_CODE(0);
      },
      true);
}

CASE_TEST(friendsvr_management, sdk_delegator_rejects_invalid_keys_before_routing) {
  run_friend_case(
      [](rpc::context& ctx, atfw::testing::runtime& test) -> rpc::result_code_type {
        using client_type = atfw::distributed_system::transaction_client_handle;
        auto delegator = atfw::util::memory::make_strong_rc<client_type::vtable_type>(
            rpc::friend_api::get_default_transaction_delegator());
        client_type client{delegator};
        client_type::storage_type storage;
        client_type::participator_type participator;
        participator.set_participator_key("friend:1/1001");
        client_type::transaction_participator_failure_reason reason;
        const auto before = test.transport().outbound_count();
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, RPC_AWAIT_CODE_RESULT(delegator->prepare_participator(
                                                                    ctx, client, storage, participator, reason)));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                       RPC_AWAIT_CODE_RESULT(delegator->commit_participator(ctx, client, storage, participator)));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                       RPC_AWAIT_CODE_RESULT(delegator->reject_participator(ctx, client, storage, participator)));
        CASE_EXPECT_EQ(before, test.transport().outbound_count());
        RPC_RETURN_CODE(0);
      },
      true);
}

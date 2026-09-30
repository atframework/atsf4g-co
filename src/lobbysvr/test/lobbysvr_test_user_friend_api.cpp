// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/com.protocol.friend_api.pb.h>
#include <protocol/pbdesc/com.protocol.user.pb.h>
#include <protocol/pbdesc/friend_management_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp.h>
#include <atframework/testing/mock_cs.h>
#include <atframework/testing/mock_db.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>
#include <atframework/testing/ss_action.h>
#include <config/excel_config_const_index.h>
#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <logic/logic_server_setup.h>
#include <router/router_friend_manager.h>
#include <router/router_user_manager.h>
#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/friend_api/friend_algorithm.h>
#include <rpc/friend_api/friendmanagementnotifyservice.atfw.gen.h>
#include <rpc/friend_api/friendmanagementservice.atfw.gen.h>
#include <rpc/internal/rpc_template_cs_message.h>
#include <rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h>
#include <rpc/transaction/dtcoordsvrservice.atfw.gen.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "app/handle_cs_rpc_lobbysvrclientservice.atfw.gen.h"
#include "app/handle_ss_rpc_friendmanagementnotifyservice.atfw.gen.h"
#include "app/handle_ss_rpc_friendmanagementservice.atfw.gen.h"
#include "data/friend_object.h"
#include "data/session.h"
#include "data/user.h"
#include "frame/test_macros.h"
#include "lobbysvr_test_runtime_helper.h"  // NOLINT: build/include_subdir
#include "logic/friend_api/task_action_management_event_sync.h"
#include "logic/friend_api/user_friend_api_manager.h"
#include "logic/session_manager.h"

namespace {
constexpr uint64_t kGatewayNodeId = 0x82000001;
constexpr uint64_t kOwnerId = 81001;

static atfw::shared::DUserIDKey user_key(uint64_t id, uint32_t zone = 1) {
  atfw::shared::DUserIDKey result;
  result.set_user_id(id);
  result.set_zone_id(zone);
  return result;
}

static atfw::friend_api::DFriendInfo friend_data(uint64_t id) {
  atfw::friend_api::DFriendInfo result;
  *result.mutable_user_key() = user_key(id);
  return result;
}

static atfw::friend_api::DFriendGift gift_data(rpc::context& ctx, int64_t id, uint64_t sender) {
  atfw::friend_api::DFriendGift result;
  result.set_gift_id(id);
  result.set_gift_type_id(7);
  *result.mutable_from_user() = user_key(sender);
  protobuf_from_system_clock(*result.mutable_expired_time(), ctx.logical_now() + std::chrono::hours{1});
  return result;
}

static std::vector<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync> dirty_pushes(atfw::testing::runtime& test) {
  std::vector<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync> result;
  for (size_t index = 0; index < test.cs().call_count(); ++index) {
    const auto* record = test.cs().call_at(index);
    if (record == nullptr || record->op != atfw::testing::cs_downstream_record::op_type::post) {
      continue;
    }
    atfw::CSMsg envelope;
    CASE_EXPECT_TRUE(envelope.ParseFromString(record->message.body().post().content()));
    if (envelope.head().rpc_stream().rpc_name() !=
        rpc::lobbysvrclientservice::packer::get_full_name_of_user_dirty_chg_sync()) {
      continue;
    }
    PROJECT_NAMESPACE_ID::SCUserDirtyChgSync message;
    CASE_EXPECT_TRUE(message.ParseFromString(envelope.body_bin()));
    result.push_back(std::move(message));
  }
  return result;
}

static void run_case(const std::function<rpc::result_code_type(rpc::context&, atfw::testing::runtime&, user&)>& body,
                     const std::function<void(atfw::testing::runtime&, user&)>& client_body = {}) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs, atfw::testing::feature::db,
                      atfw::testing::feature::router};
  options.setup_callback = [](atfw::testing::runtime&) -> int {
    atfw::friend_api::router_friend_manager::me()->set_create_object_fn(atfw::friend_api::friend_object::create);
    auto result = handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice();
    if (result < 0) {
      return result;
    }
    result = handle::friend_api::register_handles_for_friendmanagementnotifyservice();
    if (result < 0) {
      return result;
    }
    return handle::friend_api::register_handles_for_friendmanagementservice();
  };
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  // User router ownership reads atapp.area; the fixture's logic.zone_id is a separate setting.
  test.get_app()->mutable_area().set_zone_id(1);
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_friend>();
  CASE_EXPECT_GE(router_manager_set::me()->tick(), 0);
  auto client = test.cs().create_client(kGatewayNodeId, 81001);
  CASE_EXPECT_EQ(0, client.add());
  session::key_t session_key;
  session_key.node_id = kGatewayNodeId;
  session_key.session_id = 81001;
  auto connection = session_manager::me()->find(session_key);
  auto player = user::create(kOwnerId, 1, "friend-api-test");
  CASE_EXPECT_TRUE(!!connection);
  CASE_EXPECT_TRUE(!!player);
  if (!connection || !player) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto task = test.run_task("friend_api", std::chrono::seconds{2},
                            [&body, &test, player, connection](rpc::context& ctx) -> rpc::result_code_type {
                              connection->set_user(player);
                              player->set_session(ctx, connection);
                              player->get_user_friend_api_manager().refresh_feature_limit_minute(ctx);
                              player->get_user_friend_api_manager().clear_dirty();
                              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(body(ctx, test, *player)));
                            });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_FALSE(result.hard_timed_out);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  if (client_body) {
    client_body(test, *player);
  }
  CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(test, "friend.cleanup", [](rpc::context& ctx) -> rpc::result_code_type {
    auto manager = atfw::friend_api::router_friend_manager::me();
    std::vector<router_object_base::key_t> keys;
    manager->foreach_object([&keys](const atfw::friend_api::router_friend_manager::ptr_t& cache) {
      keys.push_back(cache->get_key());
      return true;
    });
    for (const auto& key : keys) {
      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(manager->remove_cache(ctx, key, nullptr, nullptr)));
    }
    manager->set_create_object_fn({});
    auto users = router_user_manager::me();
    keys.clear();
    users->foreach_object([&keys](const router_user_manager::ptr_t& cache) {
      keys.push_back(cache->get_key());
      return true;
    });
    for (const auto& key : keys) {
      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(users->remove_cache(ctx, key, nullptr, nullptr)));
    }
    users->set_create_object_fn({});
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_EQ(0, test.stop());
}

static rpc::result_code_type create_service_object(rpc::context& ctx, uint64_t id) {
  auto manager = atfw::friend_api::router_friend_manager::me();
  atfw::friend_api::router_friend_manager::ptr_t cache;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      manager->mutable_object(ctx, cache, router_object_base::key_t{manager->get_type_id(), 1, id}, nullptr)));
}

static rpc::result_code_type attach_user_router(rpc::context& ctx, atfw::testing::runtime& test, user& player) {
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_user>();
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_lock>();
  auto owner = player.shared_from_this();
  router_user_manager::me()->set_create_object_fn([owner](uint64_t, uint32_t, const std::string&) { return owner; });
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> lock{ctx};
  lock->set_user_id(player.get_user_id());
  lock->set_login_zone_id(player.get_zone_id());
  uint64_t version = 0;
  auto result = RPC_AWAIT_CODE_RESULT(rpc::db::login_lock::insert(ctx, lock, &version));
  if (result < 0) {
    RPC_RETURN_CODE(result);
  }
  router_user_private_type parameter{&lock, version, player.get_open_id()};
  router_user_manager::ptr_t cache;
  result = RPC_AWAIT_CODE_RESULT(router_user_manager::me()->mutable_object(
      ctx, cache,
      router_object_base::key_t{PROJECT_NAMESPACE_ID::EN_ROT_USER, player.get_zone_id(), player.get_user_id()},
      &parameter));
  RPC_RETURN_CODE(result);
}

template <class Request>
static atfw::CSMsg request_client(atfw::testing::runtime& test, gsl::string_view name, const Request& request) {
  atfw::CSMsg envelope;
  rpc::internal::setup_cs_rpc_request_header(*envelope.mutable_head(), "1.0.0.0", "friend-test-client",
                                             "atframework.shared.LobbysvrClientService", name,
                                             Request::descriptor()->full_name());
  envelope.mutable_head()->set_timestamp(atfw::util::time::time_utility::get_now());
  CASE_EXPECT_TRUE(request.SerializeToString(envelope.mutable_body_bin()));
  auto begin = test.cs().call_count();
  CASE_EXPECT_EQ(0, test.cs().create_client(kGatewayNodeId, 81001).post(envelope));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  do {
    for (; begin < test.cs().call_count(); ++begin) {
      const auto* record = test.cs().call_at(begin);
      if (record == nullptr || record->op != atfw::testing::cs_downstream_record::op_type::post) {
        continue;
      }
      atfw::CSMsg response;
      if (response.ParseFromString(record->message.body().post().content()) && response.head().has_rpc_response() &&
          response.head().rpc_response().rpc_name() == name) {
        return response;
      }
    }
    test.pump_once();
  } while (std::chrono::steady_clock::now() < deadline);
  CASE_EXPECT_TRUE(false);
  return {};
}

static void receive_log(rpc::context& ctx, user_friend_api_manager& manager, atfw::friend_api::DFriendEvent event) {
  atfw::friend_api::DFriendManagementNotificationEvent notification;
  *notification.mutable_friend_data_key() = user_key(kOwnerId);
  *notification.mutable_increase()->add_event_log() = std::move(event);
  manager.receive_event_sync(ctx, notification);
}

static void receive_snapshot(rpc::context& ctx, user_friend_api_manager& manager,
                             const atfw::friend_api::table_friend_blob_data& snapshot = {}) {
  atfw::friend_api::DFriendManagementNotificationEvent notification;
  *notification.mutable_friend_data_key() = user_key(kOwnerId);
  *notification.mutable_snapshot() = snapshot;
  manager.receive_event_sync(ctx, notification);
}

static std::vector<atfw::testing::ss_rule_handle> mock_coordinator(atfw::testing::runtime& test) {
  namespace dt = atfw::distributed_system;
  atfw::testing::mock_node node;
  node.set_id(0x1B0001)
      .set_name("friend-test-coordinator")
      .set_type_id(static_cast<uint32_t>(atfw::component::logic_service_type::kDtCoordSvr))
      .set_type_name("dtcoordsvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
  logic_server_last_common_module()->reload();
  std::vector<atfw::testing::ss_rule_handle> rules;
  rules.push_back(test.ss().mock(rpc::transaction::packer::get_full_name_of_create(),
                                 dt::SSDistributeTransactionCreateReq::descriptor()->full_name(),
                                 dt::SSDistributeTransactionCreateRsp::descriptor()->full_name(),
                                 [](const atfw::testing::ss_request_view&,
                                    google::protobuf::Message&) -> rpc::result_code_type { RPC_RETURN_CODE(0); }));
  rules.push_back(test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      dt::SSDistributeTransactionCommitReq::descriptor()->full_name(),
      dt::SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request, google::protobuf::Message& response) -> rpc::result_code_type {
        auto& metadata = *static_cast<dt::SSDistributeTransactionCommitRsp&>(response).mutable_metadata();
        metadata.CopyFrom(static_cast<const dt::SSDistributeTransactionCommitReq&>(request.body).metadata());
        metadata.set_status(dt::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      }));
  rules.push_back(test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject(),
      dt::SSDistributeTransactionRejectReq::descriptor()->full_name(),
      dt::SSDistributeTransactionRejectRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request, google::protobuf::Message& response) -> rpc::result_code_type {
        auto& metadata = *static_cast<dt::SSDistributeTransactionRejectRsp&>(response).mutable_metadata();
        metadata.CopyFrom(static_cast<const dt::SSDistributeTransactionRejectReq&>(request.body).metadata());
        metadata.set_status(dt::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        RPC_RETURN_CODE(0);
      }));
  return rules;
}
}  // namespace

CASE_TEST(lobbysvr_friend_api, save_preserves_permanent_friend_and_replaces_repeated_fields) {
  run_case([](rpc::context& ctx, atfw::testing::runtime&, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    manager.add_friend_cache(ctx, friend_data(81002), false);
    atfw::friend_api::DFriendGiftHistory history;
    history.set_gift_id(91);
    history.set_gift_type_id(7);
    *history.mutable_user_key() = user_key(81002);
    CASE_EXPECT_TRUE(manager.add_gift_send_list(history));
    CASE_EXPECT_TRUE(manager.add_gift_receive_list(history));
    manager.add_send_gift_times(1);
    manager.add_receive_gift_times(1);
    CASE_EXPECT_TRUE(manager.is_dirty());
    PROJECT_NAMESPACE_ID::table_user stored;
    CASE_EXPECT_EQ(0, manager.dump(ctx, stored));
    CASE_EXPECT_EQ(0, manager.dump(ctx, stored));
    CASE_EXPECT_TRUE(manager.get_friend(user_key(81002)) != nullptr);
    CASE_EXPECT_EQ(1, stored.friend_data().daily_send_list_size());
    CASE_EXPECT_EQ(1, stored.friend_data().daily_receive_list_size());
    CASE_EXPECT_EQ(1u, stored.friend_data().local_statistics().daily_send_gift_times());
    auto restored = user::create(81003, 1, "friend-restored");
    restored->get_user_friend_api_manager().init_from_table_data(ctx, stored);
    CASE_EXPECT_EQ(1u, restored->get_user_friend_api_manager().get_today_send_gift().size());
    CASE_EXPECT_EQ(1u, restored->get_user_friend_api_manager().get_local_stats().daily_receive_gift_times());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, snapshot_replacement_updates_client_and_removes_missing_records_once) {
  run_case([](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    manager.add_friend_cache(ctx, friend_data(81002));
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_EQ(1u, dirty_pushes(test).size());
    test.cs().clear_history();
    atfw::friend_api::table_friend_blob_data snapshot;
    *snapshot.add_friend_list() = friend_data(81003);
    auto* invitation = snapshot.add_inviter_list();
    *invitation->mutable_from_user() = user_key(81004);
    *invitation->mutable_to_user() = user_key(kOwnerId);
    invitation->set_event_id(55);
    protobuf_from_system_clock(*invitation->mutable_expired_time(), ctx.logical_now() + std::chrono::hours{1});
    *snapshot.mutable_statistics() = manager.get_remote_stats();
    snapshot.mutable_statistics()->set_daily_inviter(7);
    receive_snapshot(ctx, manager, snapshot);
    player.send_all_syn_msg(ctx);
    auto pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      CASE_EXPECT_EQ(3, pushes.front().dirty_friend_data().friend_event_data_size());
      bool removed = false;
      for (const auto& event : pushes.front().dirty_friend_data().friend_event_data()) {
        if (event.has_remove_friend_data()) {
          removed = true;
          CASE_EXPECT_EQ(81002u, event.remove_friend_data().user_key().user_id());
          CASE_EXPECT_GT(event.remove_friend_data().removed_time().seconds(), 0);
        }
      }
      CASE_EXPECT_TRUE(removed);
    }
    CASE_EXPECT_TRUE(manager.get_friend_cache(user_key(81002)) == nullptr);
    CASE_EXPECT_TRUE(manager.get_friend_cache(user_key(81003)) != nullptr);
    test.cs().clear_history();
    receive_snapshot(ctx, manager, snapshot);
    manager.cleanup_friend_data(ctx);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    CASE_EXPECT_TRUE(manager.get_friend_cache(user_key(81003)) != nullptr);
    CASE_EXPECT_EQ(7u, manager.get_remote_stats().daily_inviter());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, cache_updates_compare_only_expiry_and_removal_times) {
  run_case([](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    auto friend_record = friend_data(81002);
    atfw::friend_api::DFriendInvitationInfo inviter;
    *inviter.mutable_from_user() = user_key(81002);
    *inviter.mutable_to_user() = user_key(kOwnerId);
    protobuf_from_system_clock(*inviter.mutable_expired_time(), ctx.logical_now() + std::chrono::hours{1});
    auto invitee = inviter;
    *invitee.mutable_from_user() = user_key(kOwnerId);
    *invitee.mutable_to_user() = user_key(81002);
    auto value = gift_data(ctx, 101, 81002);
    auto update_cache = [&]() {
      manager.add_friend_cache(ctx, friend_record);
      manager.add_inviter_cache(ctx, inviter);
      manager.add_invitee_cache(ctx, invitee);
      manager.add_gift_cache(ctx, value);
    };
    update_cache();
    player.send_all_syn_msg(ctx);
    test.cs().clear_history();

    friend_record.set_event_id(1);
    inviter.set_event_id(2);
    invitee.set_event_id(3);
    value.set_event_id(4);
    value.set_gift_type_id(9);
    update_cache();
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    CASE_EXPECT_EQ(9, manager.get_gift_cache(101)->gift_type_id());
    CASE_EXPECT_EQ(1u, manager.get_remote_stats().daily_inviter());
    CASE_EXPECT_EQ(1u, manager.get_remote_stats().daily_invitee());

    for (auto* expiration : {friend_record.mutable_expired_time(), inviter.mutable_expired_time(),
                             invitee.mutable_expired_time(), value.mutable_expired_time()}) {
      expiration->set_nanos(expiration->nanos() == 0 ? 1 : 0);
    }
    update_cache();
    player.send_all_syn_msg(ctx);
    auto pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      const auto& events = pushes.front().dirty_friend_data().friend_event_data();
      CASE_EXPECT_EQ(4, events.size());
      if (events.size() == 4) {
        CASE_EXPECT_EQ(friend_record.expired_time().nanos(), events.Get(0).add_friend_data().expired_time().nanos());
        CASE_EXPECT_EQ(inviter.expired_time().nanos(), events.Get(1).add_inviter().expired_time().nanos());
        CASE_EXPECT_EQ(invitee.expired_time().nanos(), events.Get(2).add_invitee().expired_time().nanos());
        CASE_EXPECT_EQ(value.expired_time().nanos(), events.Get(3).add_gift().expired_time().nanos());
        CASE_EXPECT_EQ(9, events.Get(3).add_gift().gift_type_id());
      }
    }
    test.cs().clear_history();
    update_cache();
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    CASE_EXPECT_EQ(1u, manager.get_remote_stats().daily_inviter());
    CASE_EXPECT_EQ(1u, manager.get_remote_stats().daily_invitee());

    for (auto* removed : {friend_record.mutable_removed_time(), inviter.mutable_removed_time(),
                          invitee.mutable_removed_time(), value.mutable_removed_time()}) {
      protobuf_from_system_clock(*removed, ctx.logical_now());
    }
    update_cache();
    player.send_all_syn_msg(ctx);
    pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      const auto& events = pushes.front().dirty_friend_data().friend_event_data();
      CASE_EXPECT_EQ(4, events.size());
      if (events.size() == 4) {
        CASE_EXPECT_EQ(friend_record.removed_time().seconds(),
                       events.Get(0).add_friend_data().removed_time().seconds());
        CASE_EXPECT_EQ(inviter.removed_time().seconds(), events.Get(1).add_inviter().removed_time().seconds());
        CASE_EXPECT_EQ(invitee.removed_time().seconds(), events.Get(2).add_invitee().removed_time().seconds());
        CASE_EXPECT_EQ(value.removed_time().seconds(), events.Get(3).add_gift().removed_time().seconds());
      }
    }
    test.cs().clear_history();
    CASE_EXPECT_FALSE(manager.add_event_dirty(atfw::friend_api::DFriendEvent{}));
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, clear_wal_removes_live_data_and_repeated_delivery_is_silent) {
  run_case([](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    receive_snapshot(ctx, manager);
    manager.add_friend_cache(ctx, friend_data(81002), false);
    manager.add_gift_cache(ctx, gift_data(ctx, 102, 81002), false);
    auto newer = friend_data(81003);
    newer.set_event_id(20);
    manager.add_friend_cache(ctx, newer, false);
    atfw::friend_api::DFriendEvent event;
    event.set_event_id(10);
    event.set_clear_all_data(true);
    receive_log(ctx, manager, event);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_EQ(1u, manager.get_all_friend_cache().size());
    CASE_EXPECT_TRUE(manager.get_friend_cache(user_key(81003)) != nullptr);
    CASE_EXPECT_TRUE(manager.get_all_gift_cache().empty());
    auto pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      CASE_EXPECT_EQ(2, pushes.front().dirty_friend_data().friend_event_data_size());
    }
    test.cs().clear_history();
    receive_log(ctx, manager, event);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, gift_receive_filters_ids_and_retries_cleanup_without_duplicate_receipts) {
  run_case([](rpc::context& ctx, atfw::testing::runtime&, user& player) -> rpc::result_code_type {
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, kOwnerId)));
    auto& manager = player.get_user_friend_api_manager();
    manager.add_gift_cache(ctx, gift_data(ctx, 103, 81002), false);
    std::vector<int64_t> ids{999, 103, 103};
    google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendGift> received;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED,
                   RPC_AWAIT_CODE_RESULT(manager.receive_gifts(ctx, ids, &received)));
    CASE_EXPECT_EQ(1u, ids.size());
    if (ids.size() == 1) {
      CASE_EXPECT_EQ(103, ids.front());
    }
    CASE_EXPECT_EQ(1, received.size());
    if (received.size() == 1) {
      CASE_EXPECT_EQ(103, received.Get(0).gift_id());
    }
    CASE_EXPECT_EQ(1u, manager.get_local_stats().daily_receive_gift_times());
    CASE_EXPECT_TRUE(manager.is_dirty());
    CASE_EXPECT_TRUE(manager.get_gift_cache(103) != nullptr);
    ids = {103, 103};
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED,
                   RPC_AWAIT_CODE_RESULT(manager.receive_gifts(ctx, ids, &received)));
    CASE_EXPECT_TRUE(received.empty());
    CASE_EXPECT_TRUE(ids.empty());
    CASE_EXPECT_EQ(1u, manager.get_local_stats().daily_receive_gift_times());
    PROJECT_NAMESPACE_ID::table_user stored;
    CASE_EXPECT_EQ(0, manager.dump(ctx, stored));
    CASE_EXPECT_EQ(0, manager.dump(ctx, stored));
    CASE_EXPECT_EQ(1, stored.friend_data().confirm_remove_gift_ids_size());
    CASE_EXPECT_EQ(1, stored.friend_data().daily_receive_list_size());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, gift_receive_limit_has_no_output_or_persistent_side_effects) {
  run_case([](rpc::context& ctx, atfw::testing::runtime&, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    const auto limit = excel::get_const_config().friend_daily_receive_gift_limit();
    CASE_EXPECT_GT(limit, 0);
    if (limit <= 0) {
      RPC_RETURN_CODE(-1);
    }
    manager.add_receive_gift_times(static_cast<uint32_t>(limit));
    manager.clear_dirty();
    manager.add_gift_cache(ctx, gift_data(ctx, 104, 81002), false);
    std::vector<int64_t> ids{104};
    google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendGift> received;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_DAILY_RECEIVE_LIMIT,
                   RPC_AWAIT_CODE_RESULT(manager.receive_gifts(ctx, ids, &received)));
    CASE_EXPECT_TRUE(received.empty());
    CASE_EXPECT_FALSE(manager.is_dirty());
    CASE_EXPECT_TRUE(manager.get_gift_cache(104) != nullptr);
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, gift_send_limit_rejects_at_boundary) {
  run_case([](rpc::context& ctx, atfw::testing::runtime&, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    const auto limit = excel::get_const_config().friend_daily_send_gift_limit();
    CASE_EXPECT_GT(limit, 0);
    if (limit <= 0) {
      RPC_RETURN_CODE(-1);
    }
    manager.add_friend_cache(ctx, friend_data(81002), false);
    manager.add_send_gift_times(static_cast<uint32_t>(limit));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_DAILY_SEND_LIMIT,
                   RPC_AWAIT_CODE_RESULT(manager.send_gift(ctx, user_key(81002), 7)));
    CASE_EXPECT_TRUE(manager.get_today_send_gift().empty());
    CASE_EXPECT_EQ(static_cast<uint32_t>(limit), manager.get_local_stats().daily_send_gift_times());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, gift_send_reserves_history_and_quota_before_waiting_for_remote_result) {
  for (bool last_daily_chance : {false, true}) {
    run_case([last_daily_chance](rpc::context& ctx, atfw::testing::runtime& test,
                                 user& player) -> rpc::result_code_type {
      constexpr uint64_t kFriendServerId = 0x1D0001;
      atfw::testing::mock_node node;
      node.set_id(kFriendServerId).set_name("gift-test-friendsvr").set_zone_id(1);
      CASE_EXPECT_TRUE(!!test.discovery().add_node(node));
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_friend> record{ctx};
      record->set_zone_id(1);
      record->set_user_id(81002);
      record->mutable_router_lock()->set_router_server_id(kFriendServerId);
      record->mutable_router_lock()->set_router_version(1);
      protobuf_from_system_clock(*record->mutable_router_lock()->mutable_router_save_timepoint(), ctx.logical_now());
      uint64_t version = 0;
      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::user_friend::insert(ctx, record, &version)));
      auto& manager = player.get_user_friend_api_manager();
      manager.add_friend_cache(ctx, friend_data(81002), false);
      manager.add_friend_cache(ctx, friend_data(81003), false);
      const auto limit = excel::get_const_config().friend_daily_send_gift_limit();
      CASE_EXPECT_GT(limit, 1);
      if (limit <= 1) {
        RPC_RETURN_CODE(-1);
      }
      if (last_daily_chance) {
        manager.add_send_gift_times(static_cast<uint32_t>(limit - 1));
      }
      manager.clear_dirty();
      bool reached_remote = false;
      atfw::testing::ss_rule_options options;
      options.times = 1;
      auto rule = test.ss().mock(
          rpc::friend_api::packer::get_full_name_of_management_transaction_prepare(),
          atfw::friend_api::SSFriendTransactionPrepareReq::descriptor()->full_name(),
          atfw::friend_api::SSFriendTransactionPrepareRsp::descriptor()->full_name(),
          [&manager, &reached_remote, last_daily_chance, limit](
              const atfw::testing::ss_request_view& request,
              google::protobuf::Message& response) -> rpc::result_code_type {
            reached_remote = true;
            CASE_EXPECT_TRUE(request.context != nullptr);
            if (request.context == nullptr) {
              RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
            }
            CASE_EXPECT_EQ(static_cast<uint32_t>(last_daily_chance ? limit : 1),
                           manager.get_local_stats().daily_send_gift_times());
            CASE_EXPECT_EQ(1u, manager.get_today_send_gift().size());
            CASE_EXPECT_TRUE(manager.is_dirty());
            // The first send is still awaiting this response when the second send runs.
            const auto expected = last_daily_chance ? PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_DAILY_SEND_LIMIT
                                                    : PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_ALREAD_SEND;
            CASE_EXPECT_EQ(expected, RPC_AWAIT_CODE_RESULT(manager.send_gift(
                                         *request.context, user_key(last_daily_chance ? 81003 : 81002), 7)));
            static_cast<atfw::friend_api::SSFriendTransactionPrepareRsp&>(response).set_client_result(
                PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_CONFIG_NOT_FOUND);
            RPC_RETURN_CODE(0);
          },
          options);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_CONFIG_NOT_FOUND,
                     RPC_AWAIT_CODE_RESULT(manager.send_gift(ctx, user_key(81002), 7)));
      CASE_EXPECT_TRUE(reached_remote);
      CASE_EXPECT_EQ(1u, test.ss().calls(rpc::friend_api::packer::get_full_name_of_management_transaction_prepare()));
      CASE_EXPECT_EQ(static_cast<uint32_t>(last_daily_chance ? limit : 1),
                     manager.get_local_stats().daily_send_gift_times());
      CASE_EXPECT_EQ(1u, manager.get_today_send_gift().size());
      PROJECT_NAMESPACE_ID::table_user stored;
      CASE_EXPECT_EQ(0, manager.dump(ctx, stored));
      CASE_EXPECT_EQ(1, stored.friend_data().daily_send_list_size());
      CASE_EXPECT_EQ(static_cast<uint32_t>(last_daily_chance ? limit : 1),
                     stored.friend_data().local_statistics().daily_send_gift_times());
      RPC_RETURN_CODE(0);
    });
  }
}

CASE_TEST(lobbysvr_friend_api, cs_get_all_pulls_authoritative_snapshot_and_keeps_permanent_friends) {
  run_case(
      [](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(attach_user_router(ctx, test, player)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, kOwnerId)));
        auto cache = atfw::friend_api::router_friend_manager::me()->get_object(
            {PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, kOwnerId});
        if (!cache || !cache->get_object()) {
          RPC_RETURN_CODE(-1);
        }
        auto object = std::static_pointer_cast<atfw::friend_api::friend_object>(cache->get_object());
        auto value = friend_data(81009);
        CASE_EXPECT_TRUE(object->add_friend(ctx, object->allocate_event_id(), value));
        RPC_RETURN_CODE(0);
      },
      [](atfw::testing::runtime& test, user&) {
        auto response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_get_all(),
                                       atfw::friend_api::CSFriendGetAllReq{});
        CASE_EXPECT_EQ(0, response.head().error_code());
        atfw::friend_api::SCFriendGetAllRsp result;
        CASE_EXPECT_TRUE(result.ParseFromString(response.body_bin()));
        CASE_EXPECT_EQ(1, result.friend_list_size());
        if (result.friend_list_size() == 1) {
          CASE_EXPECT_EQ(81009u, result.friend_list(0).user_key().user_id());
          CASE_EXPECT_EQ(0, result.friend_list(0).expired_time().seconds());
        }
      });
}

CASE_TEST(lobbysvr_friend_api, cs_get_all_does_not_report_empty_success_when_subscription_fails) {
  run_case([](rpc::context&, atfw::testing::runtime&, user&) -> rpc::result_code_type { RPC_RETURN_CODE(0); },
           [](atfw::testing::runtime& test, user&) {
             auto response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_get_all(),
                                            atfw::friend_api::CSFriendGetAllReq{});
             CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM, response.head().error_code());
           });
}

CASE_TEST(lobbysvr_friend_api, notification_skips_offline_subscriber_and_flushes_one_batch) {
  run_case([](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(attach_user_router(ctx, test, player)));
    receive_snapshot(ctx, player.get_user_friend_api_manager());
    atfw::friend_api::SSFriendManagementEventSync notification;
    uint64_t previous_hash = 0;
    for (int index = 0; index < 2; ++index) {
      auto* target = notification.add_event_target();
      *target->mutable_friend_data_key() = user_key(kOwnerId);
      *target->add_subscriber_key() = user_key(99999);
      *target->add_subscriber_key() = user_key(kOwnerId);
      auto* event = target->mutable_increase()->add_event_log();
      event->set_event_id(20 + index);
      *event->mutable_add_friend_data() = friend_data(static_cast<uint64_t>(81100 + index));
      previous_hash = rpc::friend_api::calculate_hash_code(previous_hash, *event);
      event->set_hash_code(previous_hash);
    }
    atfw::testing::ss_action_invoke_options options{rpc::friend_api::packer::get_full_name_of_management_event_sync()};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_management_event_sync>(
                          ctx, notification, options)));
    CASE_EXPECT_EQ(2u, player.get_user_friend_api_manager().get_all_friend_cache().size());
    auto pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      CASE_EXPECT_EQ(2, pushes.front().dirty_friend_data().friend_event_data_size());
    }
    test.cs().clear_history();
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_management_event_sync>(
                          ctx, notification, options)));
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, notification_requires_snapshot_before_applying_logs) {
  run_case([](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(attach_user_router(ctx, test, player)));
    auto& manager = player.get_user_friend_api_manager();
    atfw::friend_api::SSFriendManagementEventSync notification;
    auto* target = notification.add_event_target();
    *target->mutable_friend_data_key() = user_key(kOwnerId);
    *target->add_subscriber_key() = user_key(kOwnerId);
    auto* logs = target->mutable_increase()->mutable_event_log();
    *logs->Add()->mutable_add_friend_data() = friend_data(81300);
    logs->Mutable(0)->mutable_add_friend_data()->set_event_id(1000);
    auto* inviter = logs->Add()->mutable_add_inviter();
    *inviter->mutable_from_user() = user_key(81301);
    *inviter->mutable_to_user() = user_key(kOwnerId);
    inviter->set_event_id(1001);
    protobuf_from_system_clock(*inviter->mutable_expired_time(), ctx.logical_now() + std::chrono::hours{1});
    auto* invitee = logs->Add()->mutable_add_invitee();
    *invitee = *inviter;
    *invitee->mutable_from_user() = user_key(kOwnerId);
    *invitee->mutable_to_user() = user_key(81302);
    invitee->set_event_id(1002);
    *logs->Add()->mutable_add_gift() = gift_data(ctx, 106, 81303);
    logs->Mutable(3)->mutable_add_gift()->set_event_id(1003);
    uint64_t previous_hash = 0;
    int64_t event_id = 1000;
    for (auto& event : *logs) {
      event.set_event_id(event_id++);
      previous_hash = rpc::friend_api::calculate_hash_code(previous_hash, event);
      event.set_hash_code(previous_hash);
    }
    atfw::testing::ss_action_invoke_options options{rpc::friend_api::packer::get_full_name_of_management_event_sync()};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_management_event_sync>(
                          ctx, notification, options)));
    CASE_EXPECT_TRUE(manager.get_all_friend_cache().empty());
    CASE_EXPECT_TRUE(manager.get_all_inviter_cache().empty());
    CASE_EXPECT_TRUE(manager.get_all_invitee_cache().empty());
    CASE_EXPECT_TRUE(manager.get_all_gift_cache().empty());
    CASE_EXPECT_EQ(0u, manager.get_remote_stats().daily_inviter());
    CASE_EXPECT_EQ(0u, manager.get_remote_stats().daily_invitee());
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());

    *target->mutable_snapshot()->add_friend_list() = friend_data(81304);
    target->mutable_snapshot()->mutable_friend_list(0)->set_event_id(10);
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_management_event_sync>(
                          ctx, notification, options)));
    CASE_EXPECT_EQ(1u, manager.get_all_friend_cache().size());
    CASE_EXPECT_TRUE(manager.get_friend_cache(user_key(81304)) != nullptr);
    CASE_EXPECT_TRUE(manager.get_all_inviter_cache().empty());
    CASE_EXPECT_TRUE(manager.get_all_invitee_cache().empty());
    CASE_EXPECT_TRUE(manager.get_all_gift_cache().empty());
    auto pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      CASE_EXPECT_EQ(1, pushes.front().dirty_friend_data().friend_event_data_size());
    }
    test.cs().clear_history();

    // A rejected high event ID must not advance the checkpoint or poison the new hash chain.
    google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendEvent> current_logs;
    auto* current = current_logs.Add();
    current->set_event_id(20);
    *current->mutable_add_friend_data() = friend_data(81305);
    current->mutable_add_friend_data()->set_event_id(20);
    current->set_hash_code(rpc::friend_api::calculate_hash_code(0, *current));
    CASE_EXPECT_EQ(0, manager.load_logs(ctx, current_logs));
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_EQ(2u, manager.get_all_friend_cache().size());
    CASE_EXPECT_TRUE(manager.get_friend_cache(user_key(81305)) != nullptr);
    pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      CASE_EXPECT_EQ(1, pushes.front().dirty_friend_data().friend_event_data_size());
      if (pushes.front().dirty_friend_data().friend_event_data_size() == 1) {
        CASE_EXPECT_EQ(81305u,
                       pushes.front().dirty_friend_data().friend_event_data(0).add_friend_data().user_key().user_id());
      }
    }
    test.cs().clear_history();
    receive_log(ctx, manager, *current);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, storage_and_foreign_snapshot_do_not_unlock_logs_but_empty_owner_snapshot_does) {
  run_case([](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    PROJECT_NAMESPACE_ID::table_user stored;
    *stored.mutable_friend_data()->mutable_local_statistics() = manager.get_local_stats();
    stored.mutable_friend_data()->mutable_local_statistics()->set_daily_receive_gift_times(3);
    stored.mutable_friend_data()->add_confirm_remove_gift_ids(107);
    manager.init_from_table_data(ctx, stored);
    CASE_EXPECT_EQ(3u, manager.get_local_stats().daily_receive_gift_times());

    google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendEvent> logs;
    auto* event = logs.Add();
    event->set_event_id(30);
    *event->mutable_add_gift() = gift_data(ctx, 108, 81306);
    event->mutable_add_gift()->set_event_id(30);
    event->set_hash_code(rpc::friend_api::calculate_hash_code(0, *event));
    CASE_EXPECT_EQ(0, manager.load_logs(ctx, logs));
    CASE_EXPECT_TRUE(manager.get_all_gift_cache().empty());
    atfw::friend_api::DFriendManagementNotificationEvent foreign_snapshot;
    *foreign_snapshot.mutable_friend_data_key() = user_key(kOwnerId, 2);
    foreign_snapshot.mutable_snapshot();
    manager.receive_event_sync(ctx, foreign_snapshot);
    receive_log(ctx, manager, *event);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(manager.get_all_gift_cache().empty());
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());

    receive_snapshot(ctx, manager);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(manager.get_all_gift_cache().empty());
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    receive_log(ctx, manager, *event);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_EQ(1u, manager.get_all_gift_cache().size());
    CASE_EXPECT_TRUE(manager.get_gift_cache(108) != nullptr);
    auto pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      CASE_EXPECT_EQ(1, pushes.front().dirty_friend_data().friend_event_data_size());
      if (pushes.front().dirty_friend_data().friend_event_data_size() == 1) {
        CASE_EXPECT_EQ(108, pushes.front().dirty_friend_data().friend_event_data(0).add_gift().gift_id());
      }
    }
    test.cs().clear_history();
    CASE_EXPECT_EQ(0, manager.load_logs(ctx, logs));
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    CASE_EXPECT_EQ(3u, manager.get_local_stats().daily_receive_gift_times());
    CASE_EXPECT_EQ(0, manager.dump(ctx, stored));
    CASE_EXPECT_EQ(1, stored.friend_data().confirm_remove_gift_ids_size());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, remove_all_inviters_wal_preserves_newer_records) {
  run_case([](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
    auto& manager = player.get_user_friend_api_manager();
    receive_snapshot(ctx, manager);
    for (int index = 0; index < 4; ++index) {
      atfw::friend_api::DFriendInvitationInfo invitation;
      *invitation.mutable_from_user() = user_key(static_cast<uint64_t>(81200 + index));
      *invitation.mutable_to_user() = user_key(kOwnerId);
      invitation.set_event_id(10 + 10 * index);
      protobuf_from_system_clock(*invitation.mutable_expired_time(), ctx.logical_now() + std::chrono::hours{1});
      manager.add_inviter_cache(ctx, invitation, false);
    }
    atfw::friend_api::DFriendEvent event;
    event.set_event_id(30);
    event.set_remove_all_inviter(true);
    receive_log(ctx, manager, event);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(manager.get_inviter_cache(user_key(81200)) == nullptr);
    CASE_EXPECT_TRUE(manager.get_inviter_cache(user_key(81201)) == nullptr);
    CASE_EXPECT_TRUE(manager.get_inviter_cache(user_key(81202)) != nullptr);
    CASE_EXPECT_TRUE(manager.get_inviter_cache(user_key(81203)) != nullptr);
    auto pushes = dirty_pushes(test);
    CASE_EXPECT_EQ(1u, pushes.size());
    if (pushes.size() == 1) {
      CASE_EXPECT_EQ(2, pushes.front().dirty_friend_data().friend_event_data_size());
      std::unordered_set<uint64_t> removed_ids;
      for (const auto& dirty_event : pushes.front().dirty_friend_data().friend_event_data()) {
        CASE_EXPECT_TRUE(dirty_event.has_remove_inviter());
        removed_ids.insert(dirty_event.remove_inviter().from_user().user_id());
      }
      CASE_EXPECT_EQ(1u, removed_ids.count(81200));
      CASE_EXPECT_EQ(1u, removed_ids.count(81201));
    }
    test.cs().clear_history();
    receive_log(ctx, manager, event);
    player.send_all_syn_msg(ctx);
    CASE_EXPECT_TRUE(dirty_pushes(test).empty());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_friend_api, cs_invite_accept_remove_and_reject_update_both_participants) {
  run_case(
      [](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
        auto result = RPC_AWAIT_CODE_RESULT(attach_user_router(ctx, test, player));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, kOwnerId)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 81002)));
        RPC_RETURN_CODE(0);
      },
      [](atfw::testing::runtime& test, user& player) {
        auto coordinator = mock_coordinator(test);
        auto response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_get_all(),
                                       atfw::friend_api::CSFriendGetAllReq{});
        CASE_EXPECT_EQ(0, response.head().error_code());
        atfw::friend_api::CSFriendInvitationReq invitation;
        *invitation.mutable_user_key() = user_key(81002);
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_invite(), invitation);
        CASE_EXPECT_EQ(0, response.head().error_code());
        auto& manager = player.get_user_friend_api_manager();
        CASE_EXPECT_TRUE(manager.get_invitee(user_key(81002)) != nullptr);
        auto peer_cache =
            atfw::friend_api::router_friend_manager::me()->get_object({PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, 81002});
        CASE_EXPECT_TRUE(!!peer_cache);
        if (!peer_cache) {
          return;
        }
        auto peer_object = std::static_pointer_cast<atfw::friend_api::friend_object>(peer_cache->get_object());
        CASE_EXPECT_EQ(1u, peer_object->get_current_inviter_count());
        CASE_EXPECT_EQ(1u, manager.get_remote_stats().daily_invitee());
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_invite(), invitation);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_ALREADY_SEND_INVITE, response.head().error_code());

        auto peer = user::create(81002, 1, "friend-test-peer");
        CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
            test, "friend.peer_invite", [peer](rpc::context& ctx) -> rpc::result_code_type {
              RPC_RETURN_CODE(
                  RPC_AWAIT_CODE_RESULT(peer->get_user_friend_api_manager().send_invite(ctx, user_key(kOwnerId))));
            }));
        CASE_EXPECT_TRUE(manager.get_inviter(user_key(81002)) != nullptr);
        atfw::friend_api::CSFriendAcceptInvitationReq accept;
        *accept.mutable_user_key() = user_key(81002);
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_accept_invite(), accept);
        CASE_EXPECT_EQ(0, response.head().error_code());
        CASE_EXPECT_TRUE(manager.get_friend(user_key(81002)) != nullptr);
        CASE_EXPECT_EQ(1u, peer_object->get_current_friend_count());
        CASE_EXPECT_TRUE(manager.get_all_invitee_cache().empty());
        CASE_EXPECT_TRUE(manager.get_all_inviter_cache().empty());
        CASE_EXPECT_EQ(0u, peer_object->get_current_inviter_count());

        atfw::friend_api::CSFriendRemoveReq remove;
        *remove.mutable_user_key() = user_key(81002);
        response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_remove(), remove);
        CASE_EXPECT_EQ(0, response.head().error_code());
        CASE_EXPECT_TRUE(manager.get_all_friend_cache().empty());
        CASE_EXPECT_EQ(0u, peer_object->get_current_friend_count());

        CASE_EXPECT_TRUE(lobbysvr_test::run_sync_task(
            test, "friend.peer_invite_again", [peer](rpc::context& ctx) -> rpc::result_code_type {
              RPC_RETURN_CODE(
                  RPC_AWAIT_CODE_RESULT(peer->get_user_friend_api_manager().send_invite(ctx, user_key(kOwnerId))));
            }));
        atfw::friend_api::CSFriendRejectInvitationReq reject;
        *reject.mutable_user_key() = user_key(81002);
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_reject_invite(), reject);
        CASE_EXPECT_EQ(0, response.head().error_code());
        CASE_EXPECT_TRUE(manager.get_all_inviter_cache().empty());
        CASE_EXPECT_TRUE(manager.get_all_friend_cache().empty());
        CASE_EXPECT_EQ(0u, peer_object->get_current_friend_count());
        CASE_EXPECT_EQ(2u, manager.get_remote_stats().daily_inviter());
      });
}

CASE_TEST(lobbysvr_friend_api, cs_invalid_requests_return_business_errors_without_changes) {
  run_case(
      [](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(attach_user_router(ctx, test, player)));
      },
      [](atfw::testing::runtime& test, user& player) {
        atfw::friend_api::CSFriendInvitationReq invite;
        auto response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_invite(), invite);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, response.head().error_code());
        *invite.mutable_user_key() = user_key(kOwnerId);
        response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_invite(), invite);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_CAN_NOT_INVITE_SELF, response.head().error_code());
        invite.mutable_user_key()->set_zone_id(2);
        response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_invite(), invite);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_CAN_NOT_INVITE_SELF, response.head().error_code());
        if (!excel::get_const_config().friend_allow_cross_zone()) {
          invite.mutable_user_key()->set_user_id(81002);
          response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_invite(), invite);
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_CAN_NOT_INVITE_CORSS_ZONE, response.head().error_code());
        }

        atfw::friend_api::CSFriendAcceptInvitationReq accept;
        *accept.mutable_user_key() = user_key(81002);
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_accept_invite(), accept);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITE_NOT_FOUND, response.head().error_code());
        atfw::friend_api::CSFriendRejectInvitationReq reject;
        *reject.mutable_user_key() = user_key(81002);
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_reject_invite(), reject);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITE_NOT_FOUND, response.head().error_code());
        atfw::friend_api::CSFriendRemoveReq remove;
        *remove.mutable_user_key() = user_key(81002);
        response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_remove(), remove);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_NOT_FOUND, response.head().error_code());

        response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_get_suggest(),
                                  atfw::friend_api::CSFriendGetSuggestReq{});
        CASE_EXPECT_EQ(0, response.head().error_code());
        atfw::friend_api::SCFriendGetSuggestRsp suggestions;
        CASE_EXPECT_TRUE(suggestions.ParseFromString(response.body_bin()));
        CASE_EXPECT_EQ(0, suggestions.suggest_users_size());
        CASE_EXPECT_TRUE(player.get_user_friend_api_manager().get_all_friend_cache().empty());
        CASE_EXPECT_TRUE(player.get_user_friend_api_manager().get_all_invitee_cache().empty());
        CASE_EXPECT_TRUE(dirty_pushes(test).empty());
      });
}

CASE_TEST(lobbysvr_friend_api, cs_gifts_preserve_receipts_and_send_accounting_on_transaction_failure) {
  run_case(
      [](rpc::context& ctx, atfw::testing::runtime& test, user& player) -> rpc::result_code_type {
        auto result = RPC_AWAIT_CODE_RESULT(attach_user_router(ctx, test, player));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, kOwnerId)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(create_service_object(ctx, 81002)));
        auto cache = atfw::friend_api::router_friend_manager::me()->get_object(
            {PROJECT_NAMESPACE_ID::EN_ROT_FRIEND, 1, kOwnerId});
        if (!cache || !cache->get_object()) {
          RPC_RETURN_CODE(-1);
        }
        auto object = std::static_pointer_cast<atfw::friend_api::friend_object>(cache->get_object());
        auto friend_record = friend_data(81002);
        auto gift = gift_data(ctx, 105, 81002);
        CASE_EXPECT_TRUE(object->add_friend(ctx, object->allocate_event_id(), friend_record));
        CASE_EXPECT_TRUE(object->add_gift(ctx, object->allocate_event_id(), gift));
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(player.get_user_friend_api_manager().pull_friend_data(ctx)));
      },
      [](atfw::testing::runtime& test, user& player) {
        auto& manager = player.get_user_friend_api_manager();
        atfw::friend_api::CSFriendSendGiftReq send;
        *send.mutable_user_key() = user_key(81002);
        send.set_gift_type_id(-1);
        auto response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_send_gift(), send);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_CONFIG_NOT_FOUND, response.head().error_code());
        CASE_EXPECT_TRUE(manager.get_today_send_gift().empty());
        CASE_EXPECT_EQ(0u, manager.get_local_stats().daily_send_gift_times());
        send.set_gift_type_id(7);
        response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_send_gift(), send);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED, response.head().error_code());
        CASE_EXPECT_EQ(1u, manager.get_today_send_gift().size());
        CASE_EXPECT_EQ(1u, manager.get_local_stats().daily_send_gift_times());
        response = request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_send_gift(), send);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_ALREAD_SEND, response.head().error_code());
        CASE_EXPECT_EQ(1u, manager.get_local_stats().daily_send_gift_times());

        atfw::friend_api::CSFriendReceiveGiftReq receive;
        receive.add_gift_ids(105);
        receive.add_gift_ids(105);
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_receive_gift(), receive);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED, response.head().error_code());
        atfw::friend_api::SCFriendReceiveGiftRsp received;
        CASE_EXPECT_TRUE(received.ParseFromString(response.body_bin()));
        CASE_EXPECT_EQ(1, received.receive_gifts_size());
        if (received.receive_gifts_size() == 1) {
          CASE_EXPECT_EQ(105, received.receive_gifts(0).gift_id());
        }
        response =
            request_client(test, rpc::lobbysvrclientservice::packer::get_full_name_of_friend_receive_gift(), receive);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED, response.head().error_code());
        CASE_EXPECT_TRUE(received.ParseFromString(response.body_bin()));
        CASE_EXPECT_EQ(0, received.receive_gifts_size());
        CASE_EXPECT_EQ(1u, manager.get_local_stats().daily_receive_gift_times());
        CASE_EXPECT_EQ(1u, manager.get_today_receive_gift().size());
      });
}

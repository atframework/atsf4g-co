// Copyright 2026 atframework

// Offline mock-RPC unit tests for rpc::dtmq::client_subscriber (the dtmq client SDK subscriber wrapper).
// Covers all exported interfaces grouped by behavior flow:
//   - create + options + getters (lifecycle/metadata accessors, local private data, shared callback
//     set, get_shared_subscriber_info shared-layer identity and heartbeat bookkeeping)
//   - callback setters/getters (all event callbacks, instance + static overloads, private > shared > default)
//   - heartbeat + ready (global_tick/global_has_pending_heartbeat/global_is_sending_heartbeat/
//     global_await_pending_heartbeat, on_ready, subscribe RPC mock)
//   - receive messages + events (global_receive_channel_event driving text/event/raw/custom data/
//     optimistic lock/private data/compact callbacks)
//   - destroy flow (on_destroyed via destroy message + snapshot with destroy metadata,
//     on_receive_snapshot_start/finished, is_destroyed)
//   - business RPC + local cache (send_message/find_message/find_cached_message/query_cached_message/
//     page_query_message)
//   - send convenience wrappers (send_text/send_event detail packing, send_destroy/send_reset_lock/
//     send_update request mapping, lock checker copy-in/copy-back, client_result propagation)
//   - channel lifecycle on one client_subscriber (create -> destroy -> re-create via
//     DChannelMessageDetail destroy/create events: ready -> destroyed -> ready again, no subscription
//     loss)
//   - shared_subscriber retention/reuse after all client_subscribers are destroyed (same
//     get_shared_channel_identify(), ready again immediately, re-subscribe heartbeat)
//
// The subscriber's create() requires an ExcelDtmqChannelType row (queried via
// excel::get_ExcelDtmqChannelType_by_channel_type), so every case enables feature::resource and seeds
// the mandatory excel tables (see src/server_frame/test/server_frame_test_resource.cpp). The excel
// config manager is a process-lifetime singleton, so each fixture uses a distinct resource version.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/any.h>
#include <google/protobuf/empty.pb.h>
#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/com.struct.dtmq.config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_resource.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "config/extern_service_types.h"
#include "config/logic_config.h"
#include "frame/test_macros.h"
#include "logic/logic_server_setup.h"
#include "rpc/dtmq/dtmq_client_subscriber.h"
#include "rpc/dtmq/dtmqproxysvrservice.atfw.gen.h"
#include "rpc/rpc_context.h"

namespace {
using subscriber_ptr = rpc::dtmq::client_subscriber::ptr_t;

// dtmq-proxysvr node used as the heartbeat/send_message target (consistent-hash resolution).
constexpr uint64_t kDtmqProxyNodeId = 0x1C0001;

// Build a populated dtmq_channel_type.bytes with a channel_type=0 row carrying a DChannelConfigure.
std::string make_dtmq_channel_type_bytes() {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("rpc-unit-test");
  PROJECT_NAMESPACE_ID::config::ExcelDtmqChannelType item;
  item.set_channel_type(0);
  item.set_readonly_replicate_count(1);
  auto* configure = item.mutable_channel_configure();
  configure->set_channel_type(0);
  configure->set_max_log_count(300);
  configure->set_gc_log_count(30);
  configure->mutable_heartbeat_interval()->set_seconds(300);
  configure->mutable_heartbeat_retry_interval()->set_seconds(60);
  blocks.add_data_block(item.SerializeAsString());
  return blocks.SerializeAsString();
}

// Override dtmq_channel_type with the test row; every other table comes from the mock's automatic
// snapshot of the real generated bindir (see mock_resource::bind()), so excel table set changes never
// require touching this fixture.
void seed_resource_tables(atframework::testing::mock_resource& resource) {
  resource.set_file("dtmq_channel_type.bytes", make_dtmq_channel_type_bytes());
}

// Inject the dtmq-proxysvr discovery node and replay it into the common-module discovery index.
// Returns false if injection failed (caller should stop the runtime and bail out).
bool setup_dtmq_proxy_node(atframework::testing::runtime& test) {
  atframework::testing::mock_node node;
  node.set_id(kDtmqProxyNodeId)
      .set_name("unit-test-dtmq-proxy")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtMqProxySvr))
      .set_type_name("dtmq-proxysvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  if (!remote) {
    return false;
  }

  // mock injection writes the global discovery set directly; the common-module discovery index only
  // replays existing nodes on reload (node events fire only from the etcd watch path).
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
  return true;
}

atfw::dtmq::DChannelIdKey make_channel_key(const std::string& channel_id) {
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(0);
  return channel_key;
}

rpc::dtmq::client_subscriber::subscriber_options make_subscriber_options(const std::string& subscriber_key,
                                                                         bool auto_create = true,
                                                                         bool with_private = false) {
  rpc::dtmq::client_subscriber::subscriber_options options(subscriber_key);
  options.auto_create_channel = auto_create;
  options.with_private_data = with_private;
  return options;
}

// Shared subscriber key generated by the framework is "server:<local_server_name>" (or
// "server:<server_id>" when the name is empty). The shared subscriber_key is what the proxysvr
// echoes back in subscriber_keys; event_sync must include it for global_receive_channel_event to
// forward to this subscriber. get_local_server_name() returns a gsl::string_view, so it must be
// materialized into a std::string before concatenation.
std::string shared_subscriber_key_for() {
  if (nullptr != logic_config::me() && !logic_config::me()->get_local_server_name().empty()) {
    return std::string("server:") + std::string(logic_config::me()->get_local_server_name());
  }
  uint64_t server_id = (nullptr != logic_config::me()) ? logic_config::me()->get_local_server_id() : 0;
  return std::string("server:") + std::to_string(server_id);
}

// Build a snapshot that makes the subscriber ready: create_sequence > 0 and create_sequence >
// destroy_sequence (see shared_subscriber::load_snapshot ready decision). A channel_configure is
// attached so get_configure() observes the server-pushed values after the snapshot is applied.
atfw::dtmq::DChannelSnapshot make_ready_snapshot(const atfw::dtmq::DChannelIdKey& channel_key, int64_t create_seq) {
  atfw::dtmq::DChannelSnapshot snapshot;
  auto* metadata = snapshot.mutable_channel_metadata();
  metadata->mutable_channel_key()->CopyFrom(channel_key);
  metadata->set_create_sequence(create_seq);
  metadata->mutable_create_timepoint()->set_seconds(1000);
  metadata->set_last_sequence(0);
  metadata->set_last_hash_code(0);
  auto* configure = metadata->mutable_channel_configure();
  configure->set_channel_type(channel_key.channel_type());
  configure->set_max_log_count(300);
  configure->set_gc_log_count(30);
  return snapshot;
}

// Register a subscribe RPC mock that acknowledges every heartbeat channel in subscribe_node so the
// subscriber marks the heartbeat as successful.
atframework::testing::ss_rule_handle mock_subscribe_ack(atframework::testing::runtime& test) {
  return test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_subscribe(), atfw::dtmq::SSChannelSubscribeReq::descriptor()->full_name(),
      atfw::dtmq::SSChannelSubscribeRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::dtmq::SSChannelSubscribeReq&>(request.body);
        auto& typed_response = static_cast<atfw::dtmq::SSChannelSubscribeRsp&>(response);
        for (const auto& heartbeat : typed_request.heartbeat()) {
          auto* node = typed_response.add_subscribe_node();
          node->mutable_channel_key()->CopyFrom(heartbeat.channel_key());
          node->set_server_id(request.target_node_id);
          node->set_readonly_index(heartbeat.readonly_index());
        }
        RPC_RETURN_CODE(0);
      });
}

// Drive one heartbeat round (global_tick -> internal send_heartbeat -> subscribe RPC) and wait for the
// task. Returns false when the task could not be started or finished with a non-zero result.
bool drive_heartbeat_round(atframework::testing::runtime& test, gsl::string_view task_name) {
  auto task = test.run_task(task_name, std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_await_pending_heartbeat(ctx)));
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    return false;
  }
  auto result = test.wait(task, std::chrono::seconds{8});
  return result.task_exited && 0 == result.result_code;
}

// Push one SSChannelEventSync through global_receive_channel_event in a task and wait for the task.
// Returns false when the task could not be started or finished with a non-zero result.
bool push_channel_event(atframework::testing::runtime& test, gsl::string_view task_name,
                        const atfw::dtmq::SSChannelEventSync& event_sync) {
  auto task =
      test.run_task(task_name, std::chrono::seconds{4}, [&event_sync](rpc::context& ctx) -> rpc::result_code_type {
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                              ctx, kDtmqProxyNodeId, event_sync)));
        RPC_RETURN_CODE(0);
      });
  if (task.empty()) {
    return false;
  }
  auto result = test.wait(task, std::chrono::seconds{8});
  return result.task_exited && 0 == result.result_code;
}
}  // namespace

// ============ create + options + metadata accessors ============
CASE_TEST(component_dtmq_subscriber, create_and_options) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("chan-subscriber-create");
  auto subscriber_options = make_subscriber_options("UT:create", true, false);

  // subscriber_options also supports rvalue subscriber_key construction with the same defaults.
  rpc::dtmq::client_subscriber::subscriber_options moved_options(std::string("UT:moved"));
  CASE_EXPECT_EQ("UT:moved", moved_options.subscriber_key);
  CASE_EXPECT_TRUE(moved_options.auto_create_channel);
  CASE_EXPECT_FALSE(moved_options.with_private_data);

  // Successful creation returns a non-null subscriber. nullable<ptr_t> is just ptr_t (the nullability
  // alias is a no-op annotation on non-Clang compilers), so the handle is the rc pointer directly.
  auto subscriber = rpc::dtmq::client_subscriber::create(channel_key, subscriber_options);
  CASE_EXPECT_TRUE(!!subscriber);
  if (!subscriber) {
    CASE_MSG_INFO() << "create returned null subscriber: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  // Basic identity accessors.
  CASE_EXPECT_EQ("chan-subscriber-create", subscriber->get_channel_key().channel_id());
  CASE_EXPECT_EQ(0u, subscriber->get_channel_key().channel_type());
  CASE_EXPECT_EQ("UT:create", subscriber->get_subscriber_key());

  // get_shared_subscriber_info exposes the SHARED layer subscriber (used by the heartbeat and by
  // send_update's force_update_subscribers): the framework-generated "server:<name>" key and the
  // local server id, distinct from the client subscriber_key.
  CASE_EXPECT_EQ(shared_subscriber_key_for(), subscriber->get_shared_subscriber_info().subscriber_key());
  CASE_EXPECT_EQ(logic_config::me()->get_local_server_id(),
                 subscriber->get_shared_subscriber_info().subscriber_server_id());

  // Options are reflected by their getters.
  CASE_EXPECT_TRUE(subscriber->get_option_auto_create_channel());
  CASE_EXPECT_FALSE(subscriber->get_option_with_private_data());

  // Fresh subscriber is neither ready nor destroyed.
  CASE_EXPECT_FALSE(subscriber->is_ready());
  CASE_EXPECT_FALSE(subscriber->is_destroyed());

  // Metadata accessors are safe on a fresh subscriber (default values).
  CASE_EXPECT_EQ(0, subscriber->get_create_sequence());
  CASE_EXPECT_EQ(0, subscriber->get_destroy_sequence());
  CASE_EXPECT_EQ(0, subscriber->get_last_message_sequence());
  CASE_EXPECT_EQ(0, subscriber->get_last_removed_sequence());
  CASE_EXPECT_EQ(0, subscriber->get_last_heartbeat_sequence());
  CASE_EXPECT_EQ(0, subscriber->get_custom_data_sequence());
  CASE_EXPECT_EQ(0, subscriber->get_private_data_sequence());

  // Timepoint accessors default to the epoch on a fresh subscriber.
  CASE_EXPECT_EQ(0, std::chrono::system_clock::to_time_t(subscriber->get_create_timepoint()));
  CASE_EXPECT_EQ(0, std::chrono::system_clock::to_time_t(subscriber->get_destroy_timepoint()));
  CASE_EXPECT_EQ(0, std::chrono::system_clock::to_time_t(subscriber->get_last_heartbeat_timepoint()));

  // Custom/private data content default to the protobuf default instance (empty type_url). The private
  // data accessor is gated by with_private_data=false, so it also yields the default instance.
  CASE_EXPECT_TRUE(subscriber->get_custom_data_content().type_url().empty());
  CASE_EXPECT_TRUE(subscriber->get_private_data_content().type_url().empty());

  // Configure is seeded from the ExcelDtmqChannelType row at construction (the default configure is
  // delivered to the subscriber before any snapshot), while the optimistic lock stays empty.
  CASE_EXPECT_EQ(300, subscriber->get_configure().max_log_count());
  CASE_EXPECT_EQ(30, subscriber->get_configure().gc_log_count());
  CASE_EXPECT_TRUE(subscriber->get_lock().lock_holder().empty());

  // shared_channel_identify is a positive monotonic id.
  CASE_EXPECT_TRUE(subscriber->get_shared_channel_identify() > 0);

  // local_private_data is an empty span on a fresh subscriber; set/append are observable.
  CASE_EXPECT_EQ(0u, subscriber->get_local_private_data().size());
  std::vector<uintptr_t> values{0x10, 0x20, 0x30};
  subscriber->set_local_private_data(gsl::span<uintptr_t>{values});
  CASE_EXPECT_EQ(3u, subscriber->get_local_private_data().size());
  subscriber->append_local_private_data(0x40);
  CASE_EXPECT_EQ(4u, subscriber->get_local_private_data().size());
  // Copy into a vector so the contents can be checked with bounds-safe at().
  std::vector<uintptr_t> observed_local_private_data(subscriber->get_local_private_data().begin(),
                                                     subscriber->get_local_private_data().end());
  CASE_EXPECT_EQ(0x30, observed_local_private_data.at(2));
  CASE_EXPECT_EQ(0x40, observed_local_private_data.at(3));

  // create_event_callback_set returns a fresh shared set; sharing it binds it as the shared handler.
  auto shared_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  CASE_EXPECT_TRUE(!!shared_set);
  subscriber->set_shared_event_callback_set(shared_set);
  CASE_EXPECT_TRUE(!!subscriber->get_shared_event_callback_set());

  // Two subscribers on the same channel share the same shared_channel_identify (shared IO layer).
  auto subscriber2 = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:create2"));
  CASE_EXPECT_TRUE(!!subscriber2);
  if (subscriber2) {
    CASE_EXPECT_EQ(subscriber->get_shared_channel_identify(), subscriber2->get_shared_channel_identify());
    // Both clients resolve to the exact same shared-layer subscriber info object.
    CASE_EXPECT_TRUE(&subscriber->get_shared_subscriber_info() == &subscriber2->get_shared_subscriber_info());
  }

  // Empty channel_id causes create() to return null (shared_subscriber::make_shared rejects it).
  atfw::dtmq::DChannelIdKey empty_key;
  auto bad = rpc::dtmq::client_subscriber::create(empty_key, subscriber_options);
  CASE_EXPECT_FALSE(!!bad);

  // global_tick is callable and idempotent (no subscriber timer activity required).
  auto task = test.run_task("tick_noop", std::chrono::seconds{2}, [](rpc::context& ctx) -> rpc::result_code_type {
    int32_t ticked = rpc::dtmq::client_subscriber::global_tick(ctx);
    CASE_EXPECT_GE(ticked, 0);
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ callback setters/getters (private > shared > default, static overloads) ============
CASE_TEST(component_dtmq_subscriber, callback_setters_and_getters) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("chan-subscriber-callbacks");
  auto subscriber = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:cb"));
  CASE_EXPECT_TRUE(!!subscriber);
  if (!subscriber) {
    test.stop();
    return;
  }

  // --- Default: all getters return empty callables when nothing is set. ---
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_ready()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_destroyed()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_update_custom_data()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_update_private_data()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_update_optimistic_lock()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_compact()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_receive_text()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_receive_event()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_receive_raw_message()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_receive_snapshot_start()));
  CASE_EXPECT_FALSE(static_cast<bool>(subscriber->get_event_callback_on_receive_snapshot_finished()));

  // --- Shared set: getters return the shared callbacks when no private callback is set. ---
  auto shared_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  rpc::dtmq::client_subscriber::event_callback_on_ready_t shared_ready = [](rpc::context&, const subscriber_ptr&) {};
  rpc::dtmq::client_subscriber::event_callback_on_destroy_t shared_destroy =
      [](rpc::context&, const subscriber_ptr&, int64_t, std::chrono::system_clock::time_point) {};
  rpc::dtmq::client_subscriber::event_callback_on_receive_text_t shared_text =
      [](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage&) {};
  rpc::dtmq::client_subscriber::set_event_callback_on_ready(*shared_set, shared_ready);
  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(*shared_set, shared_destroy);
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_text(*shared_set, shared_text);
  subscriber->set_shared_event_callback_set(shared_set);
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_ready()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_destroyed()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_receive_text()));
  // Static getters read directly from the set.
  CASE_EXPECT_TRUE(static_cast<bool>(rpc::dtmq::client_subscriber::get_event_callback_on_ready(*shared_set)));
  CASE_EXPECT_TRUE(static_cast<bool>(rpc::dtmq::client_subscriber::get_event_callback_on_destroyed(*shared_set)));
  CASE_EXPECT_TRUE(static_cast<bool>(rpc::dtmq::client_subscriber::get_event_callback_on_receive_text(*shared_set)));

  // --- Private set takes precedence over the shared set. ---
  // Setting a private on_ready installs a non-empty callable returned by the getter. The actual
  // private > shared > default precedence is validated end-to-end in the heartbeat_and_ready case,
  // where the private on_ready fires on the ready snapshot.
  rpc::dtmq::client_subscriber::event_callback_on_ready_t private_ready = [](rpc::context&, const subscriber_ptr&) {};
  subscriber->set_event_callback_on_ready(std::move(private_ready));  // move overload
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_ready()));
  // const-ref overload is also exercised (re-set with a copy-bound lambda).
  rpc::dtmq::client_subscriber::event_callback_on_ready_t private_ready_copy = [](rpc::context&,
                                                                                  const subscriber_ptr&) {};
  subscriber->set_event_callback_on_ready(private_ready_copy);
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_ready()));

  // Move-overload setter for the remaining callbacks (covers both set overloads).
  subscriber->set_event_callback_on_update_custom_data(
      rpc::dtmq::client_subscriber::event_callback_on_update_custom_data_t{
          [](rpc::context&, const subscriber_ptr&, int64_t, const google::protobuf::Any&) {}});
  subscriber->set_event_callback_on_update_custom_data(
      rpc::dtmq::client_subscriber::event_callback_on_update_custom_data_t{
          [](rpc::context&, const subscriber_ptr&, int64_t, const google::protobuf::Any&) {}});  // re-set
  subscriber->set_event_callback_on_update_private_data(
      rpc::dtmq::client_subscriber::event_callback_on_update_private_data_t{
          [](rpc::context&, const subscriber_ptr&, int64_t, const google::protobuf::Any&) {}});
  subscriber->set_event_callback_on_update_optimistic_lock(
      rpc::dtmq::client_subscriber::event_callback_on_update_optimistic_lock_t{
          [](rpc::context&, const subscriber_ptr&, const atfw::dtmq::DChannelOptimisticLock&,
             const atfw::dtmq::DChannelOptimisticLock&) {}});
  subscriber->set_event_callback_on_compact(
      rpc::dtmq::client_subscriber::event_callback_on_compact_t{[](rpc::context&, const subscriber_ptr&, int64_t) {}});
  subscriber->set_event_callback_on_receive_event(rpc::dtmq::client_subscriber::event_callback_on_receive_event_t{
      [](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage&) {}});
  subscriber->set_event_callback_on_receive_raw_message(
      rpc::dtmq::client_subscriber::event_callback_on_receive_raw_message_t{
          [](rpc::context&, const subscriber_ptr&, const atfw::dtmq::DChannelMessage&) {}});
  subscriber->set_event_callback_on_receive_snapshot_start(
      rpc::dtmq::client_subscriber::event_callback_on_receive_snapshot_t{
          [](rpc::context&, const subscriber_ptr&, const atfw::dtmq::DChannelSnapshot&, int32_t) {}});
  subscriber->set_event_callback_on_receive_snapshot_finished(
      rpc::dtmq::client_subscriber::event_callback_on_receive_snapshot_t{
          [](rpc::context&, const subscriber_ptr&, const atfw::dtmq::DChannelSnapshot&, int32_t) {}});
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_update_custom_data()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_update_private_data()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_update_optimistic_lock()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_compact()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_receive_event()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_receive_raw_message()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_receive_snapshot_start()));
  CASE_EXPECT_TRUE(static_cast<bool>(subscriber->get_event_callback_on_receive_snapshot_finished()));

  // --- on_receive_event_by_type_url: register, read, and clear (instance + static overloads). ---
  rpc::dtmq::client_subscriber::event_callback_on_receive_event_t type_url_cb =
      [](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage&) {};
  subscriber->set_event_callback_on_receive_event_by_type_url("type.googleapis.com/TestEvent", type_url_cb);
  CASE_EXPECT_TRUE(
      static_cast<bool>(subscriber->get_event_callback_on_receive_event_by_type_url("type.googleapis.com/TestEvent")));
  // Static overload on a set.
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_event_by_type_url(
      *shared_set, "type.googleapis.com/SharedEvent", type_url_cb);
  CASE_EXPECT_TRUE(static_cast<bool>(rpc::dtmq::client_subscriber::get_event_callback_on_receive_event_by_type_url(
      *shared_set, "type.googleapis.com/SharedEvent")));
  // Clear by passing an empty callback (instance + static).
  subscriber->set_event_callback_on_receive_event_by_type_url(
      "type.googleapis.com/TestEvent", rpc::dtmq::client_subscriber::event_callback_on_receive_event_t{});
  CASE_EXPECT_FALSE(
      static_cast<bool>(subscriber->get_event_callback_on_receive_event_by_type_url("type.googleapis.com/TestEvent")));
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_event_by_type_url(
      *shared_set, "type.googleapis.com/SharedEvent",
      rpc::dtmq::client_subscriber::event_callback_on_receive_event_t{});
  CASE_EXPECT_FALSE(static_cast<bool>(rpc::dtmq::client_subscriber::get_event_callback_on_receive_event_by_type_url(
      *shared_set, "type.googleapis.com/SharedEvent")));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ heartbeat + ready ============
CASE_TEST(component_dtmq_subscriber, heartbeat_and_ready) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  // subscribe mock must be registered before the heartbeat task fires.
  auto subscribe_rule = mock_subscribe_ack(test);
  CASE_EXPECT_TRUE(!!subscribe_rule);

  auto channel_key = make_channel_key("chan-subscriber-heartbeat");
  auto subscriber_options = make_subscriber_options("UT:hb", true, false);
  bool on_ready_called = false;
  subscriber_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  rpc::dtmq::client_subscriber::set_event_callback_on_ready(
      *subscriber_options.event_callback_set,
      [&on_ready_called](rpc::context&, const subscriber_ptr&) { on_ready_called = true; });

  auto nullable = rpc::dtmq::client_subscriber::create(channel_key, subscriber_options);
  CASE_EXPECT_TRUE(!!nullable);
  if (!nullable) {
    test.stop();
    return;
  }
  subscriber_ptr subscriber = nullable;  // nullable<ptr_t> == ptr_t

  // Creating a subscriber enqueues a pending heartbeat (first client registers).
  CASE_EXPECT_TRUE(rpc::dtmq::client_subscriber::global_has_pending_heartbeat());

  // Drive one heartbeat round: global_tick -> internal send_heartbeat -> subscribe RPC -> await.
  auto task = test.run_task("heartbeat_round", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_await_pending_heartbeat(ctx)));
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // The subscribe RPC was called at least once against the proxysvr node.
  CASE_EXPECT_GE(test.ss().calls(rpc::dtmq::packer::get_full_name_of_subscribe()), 1u);

  // After the heartbeat round the pending queue drains.
  CASE_EXPECT_FALSE(rpc::dtmq::client_subscriber::global_is_sending_heartbeat());
  // The subscriber is still not ready until a snapshot arrives; ready is driven by event_sync, not by
  // the heartbeat alone.
  CASE_EXPECT_FALSE(subscriber->is_ready());

  // Now push a ready snapshot via global_receive_channel_event to flip is_ready and fire on_ready.
  std::string shared_key = shared_subscriber_key_for();
  auto task2 = test.run_task("push_ready_snapshot", std::chrono::seconds{4},
                             [&channel_key, &shared_key](rpc::context& ctx) -> rpc::result_code_type {
                               atfw::dtmq::SSChannelEventSync event_sync;
                               event_sync.mutable_channel_snapshot()->CopyFrom(make_ready_snapshot(channel_key, 10));
                               event_sync.add_subscriber_keys(shared_key);
                               CASE_EXPECT_EQ(
                                   0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                                          ctx, kDtmqProxyNodeId, event_sync)));
                               RPC_RETURN_CODE(0);
                             });
  CASE_EXPECT_FALSE(task2.empty());
  if (!task2.empty()) {
    auto result = test.wait(task2, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_TRUE(subscriber->is_ready());
  CASE_EXPECT_TRUE(on_ready_called);

  // Ready snapshot metadata is reflected by the accessors.
  CASE_EXPECT_EQ(10, subscriber->get_create_sequence());
  CASE_EXPECT_EQ(1000, std::chrono::system_clock::to_time_t(subscriber->get_create_timepoint()));
  CASE_EXPECT_EQ(300, subscriber->get_configure().max_log_count());

  // A heartbeat round was sent above, so the heartbeat bookkeeping timepoint moved past the epoch.
  CASE_EXPECT_TRUE(subscriber->get_last_heartbeat_timepoint() > std::chrono::system_clock::from_time_t(0));
  // The shared-layer subscriber info carries the same heartbeat bookkeeping.
  CASE_EXPECT_TRUE(subscriber->get_shared_subscriber_info().last_heartbeat_timepoint().seconds() > 0);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ receive messages + events ============
CASE_TEST(component_dtmq_subscriber, receive_messages_and_events) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }
  auto subscribe_rule = mock_subscribe_ack(test);
  CASE_EXPECT_TRUE(!!subscribe_rule);

  auto channel_key = make_channel_key("chan-subscriber-receive");
  // with_private_data=true so update_private_data reaches this subscriber.
  auto subscriber_options = make_subscriber_options("UT:recv", true, true);
  subscriber_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  bool got_text = false;
  bool got_event = false;
  bool got_typed_event = false;
  bool got_raw = false;
  bool got_custom_data = false;
  bool got_private_data = false;
  bool got_lock = false;
  bool got_compact = false;
  int64_t compacted_sequence = 0;
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_text(
      *subscriber_options.event_callback_set,
      [&got_text](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage& data) {
        got_text = (data.detail().text() == "hello-event");
      });
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_event(
      *subscriber_options.event_callback_set,
      [&got_event](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage&) { got_event = true; });
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_event_by_type_url(
      *subscriber_options.event_callback_set, "type.googleapis.com/UnitTestEvent",
      rpc::dtmq::client_subscriber::event_callback_on_receive_event_t{
          [&got_typed_event](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage&) {
            got_typed_event = true;
          }});
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_raw_message(
      *subscriber_options.event_callback_set,
      [&got_raw](rpc::context&, const subscriber_ptr&, const atfw::dtmq::DChannelMessage&) { got_raw = true; });
  rpc::dtmq::client_subscriber::set_event_callback_on_update_custom_data(
      *subscriber_options.event_callback_set,
      [&got_custom_data](rpc::context&, const subscriber_ptr&, int64_t, const google::protobuf::Any&) {
        got_custom_data = true;
      });
  rpc::dtmq::client_subscriber::set_event_callback_on_update_private_data(
      *subscriber_options.event_callback_set,
      [&got_private_data](rpc::context&, const subscriber_ptr&, int64_t, const google::protobuf::Any&) {
        got_private_data = true;
      });
  rpc::dtmq::client_subscriber::set_event_callback_on_update_optimistic_lock(
      *subscriber_options.event_callback_set,
      [&got_lock](rpc::context&, const subscriber_ptr&, const atfw::dtmq::DChannelOptimisticLock&,
                  const atfw::dtmq::DChannelOptimisticLock&) { got_lock = true; });
  rpc::dtmq::client_subscriber::set_event_callback_on_compact(
      *subscriber_options.event_callback_set,
      [&got_compact, &compacted_sequence](rpc::context&, const subscriber_ptr&, int64_t compact_log_sequence) {
        got_compact = true;
        compacted_sequence = compact_log_sequence;
      });

  auto nullable = rpc::dtmq::client_subscriber::create(channel_key, subscriber_options);
  CASE_EXPECT_TRUE(!!nullable);
  if (!nullable) {
    test.stop();
    return;
  }
  subscriber_ptr subscriber = nullable;  // nullable<ptr_t> == ptr_t

  std::string shared_key = shared_subscriber_key_for();

  // Step 1: push a ready snapshot so the subscriber becomes ready (callbacks only fire when ready).
  auto ready_task = test.run_task(
      "push_ready", std::chrono::seconds{4}, [&channel_key, &shared_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::SSChannelEventSync event_sync;
        event_sync.mutable_channel_snapshot()->CopyFrom(make_ready_snapshot(channel_key, 10));
        event_sync.add_subscriber_keys(shared_key);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                              ctx, kDtmqProxyNodeId, event_sync)));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(ready_task.empty());
  if (!ready_task.empty()) {
    auto result = test.wait(ready_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());

  // Step 2: push incremental messages. Each DChannelMessage carries a distinct command_case so the
  // wal delegate routes it to the matching callback. custom_data/private_data are attached to the
  // metadata/runtime at a sequence at-or-before the message sequence so they are applied in order.
  auto event_task = test.run_task(
      "push_events", std::chrono::seconds{4}, [&channel_key, &shared_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::SSChannelEventSync event_sync;
        event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
        // custom_data + private_data pinned to sequence 11 (<= first message 11).
        event_sync.mutable_channel_metadata()->set_custom_data_sequence(11);
        event_sync.mutable_channel_metadata()->mutable_custom_data()->set_type_url("type.googleapis.com/CustomData");
        event_sync.mutable_channel_runtime()->set_private_data_sequence(11);
        event_sync.mutable_channel_runtime()->mutable_private_data()->set_type_url("type.googleapis.com/PrivateData");

        auto add_msg = [&channel_key](int64_t seq, int cmd) -> atfw::dtmq::DChannelMessage {
          atfw::dtmq::DChannelMessage msg;
          msg.set_sequence(seq);
          msg.set_channel_type(channel_key.channel_type());
          msg.mutable_create_timepoint()->set_seconds(2000);
          switch (cmd) {
            case 0:
              msg.mutable_detail()->set_text("hello-event");
              break;
            case 1:
              msg.mutable_detail()->mutable_event()->set_type_url("type.googleapis.com/UnitTestEvent");
              break;
            case 2:
              msg.mutable_detail()->mutable_reset_lock()->set_lock_holder("holder-1");
              break;
            default:
              msg.mutable_detail()->set_noop(true);
              break;
          }
          return msg;
        };
        // wal_client::receive_log validates the hash chain: each message's hash_code must equal
        // calculate_hash_code(previous_hash, message). The ready snapshot left the WAL empty, so the
        // first message skips validation; chain the rest from 0 the same way the server does.
        uint64_t previous_hash = 0;
        for (atfw::dtmq::DChannelMessage msg : {add_msg(11, 0),     // text
                                                add_msg(12, 1),     // event (+typed)
                                                add_msg(13, 2)}) {  // reset_lock
          previous_hash = rpc::dtmq::calculate_hash_code(previous_hash, msg);
          msg.set_hash_code(previous_hash);
          *event_sync.add_channel_message() = std::move(msg);
        }
        event_sync.add_subscriber_keys(shared_key);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                              ctx, kDtmqProxyNodeId, event_sync)));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(event_task.empty());
  if (!event_task.empty()) {
    auto result = test.wait(event_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // All ready-state callbacks fired.
  CASE_EXPECT_TRUE(got_text);
  CASE_EXPECT_TRUE(got_event);
  CASE_EXPECT_TRUE(got_typed_event);
  CASE_EXPECT_TRUE(got_raw);
  CASE_EXPECT_TRUE(got_custom_data);
  CASE_EXPECT_TRUE(got_private_data);
  CASE_EXPECT_TRUE(got_lock);

  // Last message sequence advanced to the highest received sequence.
  CASE_EXPECT_EQ(13, subscriber->get_last_message_sequence());
  // custom/private data sequences are now pinned.
  CASE_EXPECT_EQ(11, subscriber->get_custom_data_sequence());
  CASE_EXPECT_EQ(11, subscriber->get_private_data_sequence());
  // custom/private data content and the optimistic lock are observable through the accessors.
  CASE_EXPECT_EQ("type.googleapis.com/CustomData", subscriber->get_custom_data_content().type_url());
  CASE_EXPECT_EQ("type.googleapis.com/PrivateData", subscriber->get_private_data_content().type_url());
  CASE_EXPECT_EQ("holder-1", subscriber->get_lock().lock_holder());

  // Step 3: a compact boundary (last_removed_sequence=12) drops message 11 from the WAL, fires
  // on_compact and advances the removed boundary reported by get_last_removed_sequence().
  auto compact_task = test.run_task(
      "push_compact", std::chrono::seconds{4}, [&channel_key, &shared_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::SSChannelEventSync event_sync;
        event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
        event_sync.mutable_channel_runtime()->set_last_removed_sequence(12);
        event_sync.add_subscriber_keys(shared_key);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                              ctx, kDtmqProxyNodeId, event_sync)));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(compact_task.empty());
  if (!compact_task.empty()) {
    auto result = test.wait(compact_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_TRUE(got_compact);
  // The last removed key tracks the dropped log (11), not the requested boundary (12).
  CASE_EXPECT_EQ(11, compacted_sequence);
  CASE_EXPECT_EQ(11, subscriber->get_last_removed_sequence());
  // The compacted message is gone from the local cache while later messages stay cached.
  auto cache_ctx = atframework::testing::make_context();
  CASE_EXPECT_FALSE(subscriber->find_cached_message(cache_ctx, 11, [](const atfw::dtmq::DChannelMessage&) {}));
  CASE_EXPECT_TRUE(subscriber->find_cached_message(cache_ctx, 12, [](const atfw::dtmq::DChannelMessage&) {}));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ destroy flow + snapshot start/finished ============
CASE_TEST(component_dtmq_subscriber, destroy_flow) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }
  auto subscribe_rule = mock_subscribe_ack(test);
  CASE_EXPECT_TRUE(!!subscribe_rule);

  auto channel_key = make_channel_key("chan-subscriber-destroy");
  auto subscriber_options = make_subscriber_options("UT:destroy");
  subscriber_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  bool snapshot_start_called = false;
  bool snapshot_finished_called = false;
  bool destroyed_called = false;
  int32_t finished_result_code = 0;
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_snapshot_start(
      *subscriber_options.event_callback_set,
      [&snapshot_start_called](rpc::context&, const subscriber_ptr&, const atfw::dtmq::DChannelSnapshot&, int32_t) {
        snapshot_start_called = true;
      });
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_snapshot_finished(
      *subscriber_options.event_callback_set,
      [&snapshot_finished_called, &finished_result_code](rpc::context&, const subscriber_ptr&,
                                                         const atfw::dtmq::DChannelSnapshot&, int32_t result_code) {
        snapshot_finished_called = true;
        finished_result_code = result_code;
      });
  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
      *subscriber_options.event_callback_set,
      [&destroyed_called](rpc::context&, const subscriber_ptr&, int64_t, std::chrono::system_clock::time_point) {
        destroyed_called = true;
      });

  auto nullable = rpc::dtmq::client_subscriber::create(channel_key, subscriber_options);
  CASE_EXPECT_TRUE(!!nullable);
  if (!nullable) {
    test.stop();
    return;
  }
  subscriber_ptr subscriber = nullable;  // nullable<ptr_t> == ptr_t

  std::string shared_key = shared_subscriber_key_for();

  // Push a ready snapshot first (also exercises snapshot_start/finished callbacks).
  auto ready_task = test.run_task(
      "push_ready", std::chrono::seconds{4}, [&channel_key, &shared_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::SSChannelEventSync event_sync;
        event_sync.mutable_channel_snapshot()->CopyFrom(make_ready_snapshot(channel_key, 10));
        event_sync.add_subscriber_keys(shared_key);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                              ctx, kDtmqProxyNodeId, event_sync)));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(ready_task.empty());
  if (!ready_task.empty()) {
    auto result = test.wait(ready_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());
  CASE_EXPECT_TRUE(snapshot_start_called);
  CASE_EXPECT_TRUE(snapshot_finished_called);
  CASE_EXPECT_EQ(0, finished_result_code);

  // Push a destroy message (sequence >= create_sequence) to trigger on_destroyed.
  auto destroy_task = test.run_task(
      "push_destroy", std::chrono::seconds{4}, [&channel_key, &shared_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::SSChannelEventSync event_sync;
        event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
        atfw::dtmq::DChannelMessage destroy_msg;
        destroy_msg.set_sequence(20);  // >= create_sequence(10)
        destroy_msg.set_channel_type(channel_key.channel_type());
        destroy_msg.mutable_create_timepoint()->set_seconds(3000);
        destroy_msg.mutable_detail()->mutable_destroy()->mutable_removed_timepoint()->set_seconds(3000);
        *event_sync.add_channel_message() = destroy_msg;
        event_sync.add_subscriber_keys(shared_key);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                              ctx, kDtmqProxyNodeId, event_sync)));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(destroy_task.empty());
  if (!destroy_task.empty()) {
    auto result = test.wait(destroy_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_TRUE(destroyed_called);
  CASE_EXPECT_FALSE(subscriber->is_ready());
  CASE_EXPECT_TRUE(subscriber->is_destroyed());
  CASE_EXPECT_EQ(20, subscriber->get_destroy_sequence());
  CASE_EXPECT_EQ(3000, std::chrono::system_clock::to_time_t(subscriber->get_destroy_timepoint()));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ business RPC + local cache ============
CASE_TEST(component_dtmq_subscriber, business_rpc_and_cache) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  // subscribe (for heartbeat/ready) and send_message mocks. find_message/page_query_message stay local
  // here because the subscriber is ready; the remote RPC variants are covered in business_rpc_remote.
  auto subscribe_rule = mock_subscribe_ack(test);
  auto send_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_send_message(),
      atfw::dtmq::SSChannelSendMessageReq::descriptor()->full_name(),
      atfw::dtmq::SSChannelSendMessageRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::dtmq::SSChannelSendMessageReq&>(request.body);
        auto& typed_response = static_cast<atfw::dtmq::SSChannelSendMessageRsp&>(response);
        // Echo a lock-check result back when the caller attached a lock checker.
        if (typed_request.has_compare_and_maybe_reset_lock()) {
          CASE_EXPECT_EQ("expected-holder", typed_request.compare_and_maybe_reset_lock().expect_value().lock_holder());
          typed_response.mutable_compare_and_maybe_reset_lock()->mutable_real_value()->set_lock_holder("actual-holder");
        }
        typed_response.set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!subscribe_rule && !!send_rule);

  auto channel_key = make_channel_key("chan-subscriber-rpc");
  auto nullable = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:rpc"));
  CASE_EXPECT_TRUE(!!nullable);
  if (!nullable) {
    test.stop();
    return;
  }
  subscriber_ptr subscriber = nullable;  // nullable<ptr_t> == ptr_t

  std::string shared_key = shared_subscriber_key_for();

  // Push a ready snapshot that carries one cached message so find_cached_message/query_cached_message can hit it.
  auto ready_task = test.run_task(
      "push_ready_with_msg", std::chrono::seconds{4},
      [&channel_key, &shared_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::SSChannelEventSync event_sync;
        auto snapshot = make_ready_snapshot(channel_key, 10);
        auto* msg = snapshot.add_messages();
        msg->set_sequence(11);
        msg->set_channel_type(channel_key.channel_type());
        msg->mutable_create_timepoint()->set_seconds(2000);
        msg->mutable_detail()->set_text("cached-msg");
        snapshot.mutable_channel_metadata()->set_last_sequence(11);
        event_sync.mutable_channel_snapshot()->CopyFrom(snapshot);
        event_sync.add_subscriber_keys(shared_key);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
                              ctx, kDtmqProxyNodeId, event_sync)));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(ready_task.empty());
  if (!ready_task.empty()) {
    auto result = test.wait(ready_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());

  // find_cached_message hits the local cache for sequence 11.
  auto cache_ctx = atframework::testing::make_context();
  bool cache_hit = false;
  bool cache_found = subscriber->find_cached_message(
      cache_ctx, 11, [&cache_hit](const atfw::dtmq::DChannelMessage& m) { cache_hit = (m.sequence() == 11); });
  CASE_EXPECT_TRUE(cache_found);
  CASE_EXPECT_TRUE(cache_hit);
  // A miss returns false.
  CASE_EXPECT_FALSE(subscriber->find_cached_message(cache_ctx, 999, [](const atfw::dtmq::DChannelMessage&) {}));

  // find_message returns 0 from the local cache (no RPC) when ready and cached.
  auto find_task =
      test.run_task("find_cached", std::chrono::seconds{3}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelMessage msg;
        int32_t res = RPC_AWAIT_CODE_RESULT(subscriber->find_message(ctx, 11, msg));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(11, msg.sequence());
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(find_task.empty());
  if (!find_task.empty()) {
    auto result = test.wait(find_task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // find_message on an uncached sequence of a ready subscriber reports message-not-found without RPC.
  auto find_miss_task =
      test.run_task("find_miss", std::chrono::seconds{3}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelMessage msg;
        int32_t res = RPC_AWAIT_CODE_RESULT(subscriber->find_message(ctx, 999, msg));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_MESSAGE_NOT_FOUND, res);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(find_miss_task.empty());
  if (!find_miss_task.empty()) {
    auto result = test.wait(find_miss_task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // query_cached_message iterates the local cache with query_options.
  int64_t query_count = 0;
  rpc::dtmq::client_subscriber::query_options qopts;
  qopts.max_count = 10;
  bool has_more = subscriber->query_cached_message(
      cache_ctx,
      [&query_count](const atfw::dtmq::DChannelMessage&) {
        ++query_count;
        return true;
      },
      qopts);
  CASE_EXPECT_EQ(1, query_count);  // exactly the one cached message
  // one message in cache and the iterator reaches end -> no more
  CASE_EXPECT_FALSE(has_more);

  // page_query_message (local path when ready).
  auto page_task =
      test.run_task("page_query", std::chrono::seconds{3}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::channel_page_info page_info;
        page_info.set_page_size(10);
        page_info.set_page_start_sequence(0);
        google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage> msgs;
        int32_t res = RPC_AWAIT_CODE_RESULT(subscriber->page_query_message(ctx, page_info, msgs));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_GE(msgs.size(), 1);
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(page_task.empty());
  if (!page_task.empty()) {
    auto result = test.wait(page_task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // send_message goes through the proxysvr send_message RPC. The lock checker pair exercises the
  // compare_and_maybe_reset_lock request copy-in and response copy-back path.
  auto send_task =
      test.run_task("send_msg", std::chrono::seconds{3}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelMessageDetail detail;
        detail.set_text("from-subscriber");
        auto lock_checker = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        lock_checker->mutable_expect_value()->set_lock_holder("expected-holder");
        auto lock_checker_rsp = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        int32_t res = RPC_AWAIT_CODE_RESULT(
            subscriber->send_message(ctx, std::move(detail), lock_checker, lock_checker_rsp, true, false));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ("actual-holder", lock_checker_rsp->real_value().lock_holder());
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(send_task.empty());
  if (!send_task.empty()) {
    auto result = test.wait(send_task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_GE(test.ss().calls(rpc::dtmq::packer::get_full_name_of_send_message()), 1u);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ business RPC (remote path, subscriber not ready) ============
// When the subscriber has no ready snapshot, find_message/page_query_message resolve the proxysvr node
// by consistent hash (writable or readonly replica both land on the single mock node) and go through
// the real RPC path instead of the local WAL cache.
CASE_TEST(component_dtmq_subscriber, business_rpc_remote) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto subscribe_rule = mock_subscribe_ack(test);
  auto find_rule = test.ss().mock(rpc::dtmq::packer::get_full_name_of_find_message(),
                                  atfw::dtmq::SSChannelFindMessageReq::descriptor()->full_name(),
                                  atfw::dtmq::SSChannelFindMessageRsp::descriptor()->full_name(),
                                  [](const atframework::testing::ss_request_view& request,
                                     google::protobuf::Message& response) -> rpc::result_code_type {
                                    const auto& req =
                                        static_cast<const atfw::dtmq::SSChannelFindMessageReq&>(request.body);
                                    auto& rsp = static_cast<atfw::dtmq::SSChannelFindMessageRsp&>(response);
                                    auto* msg = rsp.mutable_channel_message();
                                    msg->set_sequence(req.sequence());
                                    msg->mutable_detail()->set_text("remote-find");
                                    RPC_RETURN_CODE(0);
                                  });
  auto page_rule = test.ss().mock(rpc::dtmq::packer::get_full_name_of_page_query_message(),
                                  atfw::dtmq::SSChannelQueryMessageReq::descriptor()->full_name(),
                                  atfw::dtmq::SSChannelQueryMessageRsp::descriptor()->full_name(),
                                  [](const atframework::testing::ss_request_view& /*request*/,
                                     google::protobuf::Message& response) -> rpc::result_code_type {
                                    auto& rsp = static_cast<atfw::dtmq::SSChannelQueryMessageRsp&>(response);
                                    auto* msg = rsp.add_channel_message();
                                    msg->set_sequence(7);
                                    msg->mutable_detail()->set_text("remote-page");
                                    rsp.mutable_page_info()->set_page_more(false);
                                    RPC_RETURN_CODE(0);
                                  });
  CASE_EXPECT_TRUE(!!subscribe_rule && !!find_rule && !!page_rule);

  // Fresh subscriber: never receives a snapshot, so it stays not-ready for the whole case.
  auto channel_key = make_channel_key("chan-subscriber-remote");
  auto nullable = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:remote"));
  CASE_EXPECT_TRUE(!!nullable);
  if (!nullable) {
    test.stop();
    return;
  }
  subscriber_ptr subscriber = nullable;  // nullable<ptr_t> == ptr_t
  CASE_EXPECT_FALSE(subscriber->is_ready());

  // find_message falls back to the find_message RPC when not ready.
  auto find_task =
      test.run_task("find_remote", std::chrono::seconds{3}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelMessage msg;
        int32_t res = RPC_AWAIT_CODE_RESULT(subscriber->find_message(ctx, 5, msg));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(5, msg.sequence());
        CASE_EXPECT_EQ("remote-find", msg.detail().text());
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(find_task.empty());
  if (!find_task.empty()) {
    auto result = test.wait(find_task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // page_query_message falls back to the page_query_message RPC when not ready.
  auto page_task =
      test.run_task("page_remote", std::chrono::seconds{3}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::channel_page_info page_info;
        page_info.set_page_size(10);
        page_info.set_page_start_sequence(0);
        google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage> msgs;
        int32_t res = RPC_AWAIT_CODE_RESULT(subscriber->page_query_message(ctx, page_info, msgs));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, msgs.size());
        CASE_EXPECT_FALSE(page_info.page_more());
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(page_task.empty());
  if (!page_task.empty()) {
    auto result = test.wait(page_task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // find_cached_message/query_cached_message require the ready local cache, so they are no-ops here.
  auto cache_ctx = atframework::testing::make_context();
  CASE_EXPECT_FALSE(subscriber->find_cached_message(cache_ctx, 5, [](const atfw::dtmq::DChannelMessage&) {}));
  CASE_EXPECT_FALSE(
      subscriber->query_cached_message(cache_ctx, [](const atfw::dtmq::DChannelMessage&) { return true; }));

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_find_message())));
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_page_query_message())));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ channel create -> destroy -> re-create on the same client_subscriber ============
// The channel lifecycle is driven by destroy/create events in DChannelMessageDetail. After a destroy
// event the SAME client_subscriber observes is_destroyed(), and a later create event with a higher
// sequence restores it to ready (on_ready fires again) - no client_subscriber re-creation, no
// unsubscribe/re-subscribe RPC, and message delivery keeps working (no subscription loss).
CASE_TEST(component_dtmq_subscriber, channel_destroy_and_recreate_restores_ready) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }
  auto subscribe_rule = mock_subscribe_ack(test);
  CASE_EXPECT_TRUE(!!subscribe_rule);

  auto channel_key = make_channel_key("chan-subscriber-channel-recreate");
  auto subscriber_options = make_subscriber_options("UT:channel-recreate");
  subscriber_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  int32_t on_ready_count = 0;
  int32_t on_destroyed_count = 0;
  bool got_text_after_recreate = false;
  rpc::dtmq::client_subscriber::set_event_callback_on_ready(
      *subscriber_options.event_callback_set,
      [&on_ready_count](rpc::context&, const subscriber_ptr&) { ++on_ready_count; });
  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
      *subscriber_options.event_callback_set,
      [&on_destroyed_count](rpc::context&, const subscriber_ptr&, int64_t, std::chrono::system_clock::time_point) {
        ++on_destroyed_count;
      });
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_text(
      *subscriber_options.event_callback_set,
      [&got_text_after_recreate](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage& data) {
        got_text_after_recreate = got_text_after_recreate || (data.detail().text() == "after-recreate");
      });

  // Exactly one client_subscriber is used for the whole channel lifecycle.
  auto subscriber = rpc::dtmq::client_subscriber::create(channel_key, subscriber_options);
  CASE_EXPECT_TRUE(!!subscriber);
  if (!subscriber) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(drive_heartbeat_round(test, "hb_round"));
  uint64_t shared_identify = subscriber->get_shared_channel_identify();
  std::string shared_key = shared_subscriber_key_for();

  // Phase 1: ready snapshot (channel created, create_sequence=10) -> the subscriber becomes ready.
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_snapshot()->CopyFrom(make_ready_snapshot(channel_key, 10));
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_ready", event_sync));
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());
  CASE_EXPECT_FALSE(subscriber->is_destroyed());
  CASE_EXPECT_EQ(1, on_ready_count);

  // Phase 2: channel destroy event (destroy detail, sequence >= create_sequence) -> the same
  // subscriber observes the destroyed state.
  uint64_t destroy_hash = 0;
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage destroy_msg;
    destroy_msg.set_sequence(20);
    destroy_msg.set_channel_type(channel_key.channel_type());
    destroy_msg.mutable_create_timepoint()->set_seconds(3000);
    destroy_msg.mutable_detail()->mutable_destroy()->mutable_removed_timepoint()->set_seconds(3000);
    // First log in the WAL (the ready snapshot carried no messages), so the hash chain starts at 0.
    destroy_hash = rpc::dtmq::calculate_hash_code(0, destroy_msg);
    destroy_msg.set_hash_code(destroy_hash);
    *event_sync.add_channel_message() = destroy_msg;
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_destroy", event_sync));
  }
  CASE_EXPECT_FALSE(subscriber->is_ready());
  CASE_EXPECT_TRUE(subscriber->is_destroyed());
  CASE_EXPECT_EQ(20, subscriber->get_destroy_sequence());
  CASE_EXPECT_EQ(3000, std::chrono::system_clock::to_time_t(subscriber->get_destroy_timepoint()));
  CASE_EXPECT_EQ(1, on_destroyed_count);

  // Phase 3: channel re-create event (create detail at a sequence above destroy_sequence) -> the same
  // subscriber returns to ready and refreshes its create metadata; on_ready fires a second time.
  uint64_t create_hash = 0;
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage create_msg;
    create_msg.set_sequence(30);
    create_msg.set_channel_type(channel_key.channel_type());
    create_msg.mutable_create_timepoint()->set_seconds(4000);
    create_msg.mutable_detail()->mutable_create()->mutable_create_timepoint()->set_seconds(4000);
    create_hash = rpc::dtmq::calculate_hash_code(destroy_hash, create_msg);
    create_msg.set_hash_code(create_hash);
    *event_sync.add_channel_message() = create_msg;
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_recreate", event_sync));
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());
  CASE_EXPECT_FALSE(subscriber->is_destroyed());
  CASE_EXPECT_EQ(30, subscriber->get_create_sequence());
  CASE_EXPECT_EQ(4000, std::chrono::system_clock::to_time_t(subscriber->get_create_timepoint()));
  // destroy_sequence is kept as history (20 < 30), which is exactly why is_destroyed() is false again.
  CASE_EXPECT_EQ(20, subscriber->get_destroy_sequence());
  CASE_EXPECT_EQ(2, on_ready_count);
  CASE_EXPECT_EQ(1, on_destroyed_count);

  // No subscription loss: the shared_subscriber was never swapped (identify unchanged), no
  // unsubscribe RPC was sent, and message delivery to the same client still works after re-create.
  CASE_EXPECT_EQ(shared_identify, subscriber->get_shared_channel_identify());
  CASE_EXPECT_EQ(0, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_unsubscribe())));
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage msg;
    msg.set_sequence(31);
    msg.set_channel_type(channel_key.channel_type());
    msg.mutable_create_timepoint()->set_seconds(4100);
    msg.mutable_detail()->set_text("after-recreate");
    msg.set_hash_code(rpc::dtmq::calculate_hash_code(create_hash, msg));
    *event_sync.add_channel_message() = msg;
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_after_recreate", event_sync));
  }
  CASE_EXPECT_TRUE(got_text_after_recreate);
  CASE_EXPECT_EQ(31, subscriber->get_last_message_sequence());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ regression: stale local chain must accept a re-created channel's create log ============
// Production incident: the server lost its cache and re-created the channel, so the new create log's
// hash chains from 0 while the client WAL still holds the previous incarnation's tip. Incremental
// validation rejected the create log (kHashCodeMismatch) on every heartbeat-driven resync and the
// subscriber never healed. create logs must re-anchor the hash chain (previous is ignored), letting
// the new incarnation link in without a snapshot.
CASE_TEST(component_dtmq_subscriber, recreate_with_stale_local_chain_heals_via_create_event) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto subscribe_rule = mock_subscribe_ack(test);
  CASE_EXPECT_TRUE(!!subscribe_rule);

  auto channel_key = make_channel_key("chan-subscriber-recreate-reanchor");
  auto subscriber_options = make_subscriber_options("UT:recreate-reanchor");
  subscriber_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  std::vector<std::string> received_texts;
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_text(
      *subscriber_options.event_callback_set,
      [&received_texts](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage& data) {
        received_texts.push_back(data.detail().text());
      });

  auto subscriber = rpc::dtmq::client_subscriber::create(channel_key, subscriber_options);
  CASE_EXPECT_TRUE(!!subscriber);
  if (!subscriber) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(drive_heartbeat_round(test, "hb_round"));
  std::string shared_key = shared_subscriber_key_for();

  // Ready snapshot of incarnation 1 (WAL left empty, so the first log chains from 0).
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_snapshot()->CopyFrom(make_ready_snapshot(channel_key, 10));
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_ready", event_sync));
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());

  // Incarnation 1 history cached locally: text@20 chained from 0.
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage msg;
    msg.set_sequence(20);
    msg.set_channel_type(channel_key.channel_type());
    msg.mutable_create_timepoint()->set_seconds(2000);
    msg.mutable_detail()->set_text("before-recreate");
    msg.set_hash_code(rpc::dtmq::calculate_hash_code(0, msg));
    *event_sync.add_channel_message() = msg;
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_old_text", event_sync));
  }
  CASE_EXPECT_EQ(20, subscriber->get_last_message_sequence());
  CASE_EXPECT_EQ(1, static_cast<int>(received_texts.size()));

  // The server lost its cache and re-created the channel: its full resend starts at the new
  // incarnation's create log (sequence 30, hash chained from 0), followed by new business logs.
  // The client WAL tip is still incarnation 1's text@20 - the create log must re-anchor the chain.
  uint64_t recreate_hash = 0;
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage create_msg;
    create_msg.set_sequence(30);
    create_msg.set_channel_type(channel_key.channel_type());
    create_msg.mutable_create_timepoint()->set_seconds(4000);
    create_msg.mutable_detail()->mutable_create()->mutable_create_timepoint()->set_seconds(4000);
    recreate_hash = rpc::dtmq::calculate_hash_code(0, create_msg);
    create_msg.set_hash_code(recreate_hash);
    *event_sync.add_channel_message() = create_msg;

    atfw::dtmq::DChannelMessage msg;
    msg.set_sequence(31);
    msg.set_channel_type(channel_key.channel_type());
    msg.mutable_create_timepoint()->set_seconds(4100);
    msg.mutable_detail()->set_text("after-recreate-reanchor");
    msg.set_hash_code(rpc::dtmq::calculate_hash_code(recreate_hash, msg));
    *event_sync.add_channel_message() = msg;

    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_recreate", event_sync));
  }
  // Healed purely by the incremental stream: no snapshot, no failure-driven heartbeat requeued.
  CASE_EXPECT_TRUE(subscriber->is_ready());
  CASE_EXPECT_EQ(30, subscriber->get_create_sequence());
  CASE_EXPECT_EQ(31, subscriber->get_last_message_sequence());
  CASE_EXPECT_FALSE(rpc::dtmq::client_subscriber::global_has_pending_heartbeat());
  CASE_EXPECT_EQ(2, static_cast<int>(received_texts.size()));
  if (received_texts.size() >= 2) {
    CASE_EXPECT_EQ(std::string("before-recreate"), received_texts[0]);
    CASE_EXPECT_EQ(std::string("after-recreate-reanchor"), received_texts[1]);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ regression: hash mismatch on a non-create log resets the advertised position ============
// When an incremental (non-create) log fails hash validation, the subscriber must stop advertising
// the stale position: last_message_sequence_/last_message_hash_code_ are reset to 0 and a heartbeat
// is queued immediately, so the server falls back to a full resend (or snapshot) and the channel
// heals instead of retrying the same failing incremental sync forever.
CASE_TEST(component_dtmq_subscriber, hash_mismatch_on_incremental_log_forces_position_reset_and_full_resync) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("chan-subscriber-hash-mismatch-resync");

  // Capture every advertised heartbeat position (last_sequence, last_hash_code) of this channel
  // while acking. The heartbeat manager is a process-wide singleton, so filter by channel id.
  std::vector<std::pair<int64_t, uint64_t>> heartbeat_positions;
  auto subscribe_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_subscribe(), atfw::dtmq::SSChannelSubscribeReq::descriptor()->full_name(),
      atfw::dtmq::SSChannelSubscribeRsp::descriptor()->full_name(),
      [&heartbeat_positions, &channel_key](const atframework::testing::ss_request_view& request,
                                           google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::dtmq::SSChannelSubscribeReq&>(request.body);
        auto& typed_response = static_cast<atfw::dtmq::SSChannelSubscribeRsp&>(response);
        for (const auto& heartbeat : typed_request.heartbeat()) {
          if (heartbeat.channel_key().channel_id() == channel_key.channel_id()) {
            heartbeat_positions.emplace_back(heartbeat.last_sequence(), heartbeat.last_hash_code());
          }
          auto* node = typed_response.add_subscribe_node();
          node->mutable_channel_key()->CopyFrom(heartbeat.channel_key());
          node->set_server_id(request.target_node_id);
          node->set_readonly_index(heartbeat.readonly_index());
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!subscribe_rule);

  auto subscriber_options = make_subscriber_options("UT:hash-mismatch-resync");
  subscriber_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  std::vector<std::string> received_texts;
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_text(
      *subscriber_options.event_callback_set,
      [&received_texts](rpc::context&, const subscriber_ptr&, const ::atfw::dtmq::DChannelMessage& data) {
        received_texts.push_back(data.detail().text());
      });

  auto subscriber = rpc::dtmq::client_subscriber::create(channel_key, subscriber_options);
  CASE_EXPECT_TRUE(!!subscriber);
  if (!subscriber) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(drive_heartbeat_round(test, "hb_round"));
  std::string shared_key = shared_subscriber_key_for();
  const size_t initial_heartbeat_count = heartbeat_positions.size();

  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_snapshot()->CopyFrom(make_ready_snapshot(channel_key, 10));
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_ready", event_sync));
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());

  // Local chain: text@20 chained from 0.
  uint64_t text20_hash = 0;
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage msg;
    msg.set_sequence(20);
    msg.set_channel_type(channel_key.channel_type());
    msg.mutable_create_timepoint()->set_seconds(2000);
    msg.mutable_detail()->set_text("chain-tip");
    text20_hash = rpc::dtmq::calculate_hash_code(0, msg);
    msg.set_hash_code(text20_hash);
    *event_sync.add_channel_message() = msg;
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_text20", event_sync));
  }
  CASE_EXPECT_EQ(20, subscriber->get_last_message_sequence());

  // A non-create incremental log whose hash does not link to the local tip must be rejected.
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage msg;
    msg.set_sequence(21);
    msg.set_channel_type(channel_key.channel_type());
    msg.mutable_create_timepoint()->set_seconds(2100);
    msg.mutable_detail()->set_text("corrupted-incremental");
    msg.set_hash_code(rpc::dtmq::calculate_hash_code(text20_hash, msg) ^ 0x1);  // corrupt the chain link
    *event_sync.add_channel_message() = msg;
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_corrupted", event_sync));
  }
  // The bad log is neither applied nor delivered...
  CASE_EXPECT_EQ(1, static_cast<int>(received_texts.size()));
  // ...the advertised position is reset to force a full resync...
  CASE_EXPECT_EQ(0, subscriber->get_last_message_sequence());
  // ...and a heartbeat is queued immediately rather than waiting out the heartbeat interval.
  CASE_EXPECT_TRUE(rpc::dtmq::client_subscriber::global_has_pending_heartbeat());

  // The driven heartbeat advertises (0, 0): the server can no longer confuse this with a valid
  // incremental position and must fall back to a full resend/snapshot.
  CASE_EXPECT_TRUE(drive_heartbeat_round(test, "hb_after_mismatch"));
  CASE_EXPECT_EQ(initial_heartbeat_count + 1, heartbeat_positions.size());
  if (!heartbeat_positions.empty()) {
    CASE_EXPECT_EQ(0, heartbeat_positions.back().first);
    CASE_EXPECT_EQ(0u, heartbeat_positions.back().second);
  }

  // The server answers with a full resend from the (re-created) channel start; the subscriber heals.
  uint64_t recreate_hash = 0;
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_metadata()->mutable_channel_key()->CopyFrom(channel_key);
    atfw::dtmq::DChannelMessage create_msg;
    create_msg.set_sequence(30);
    create_msg.set_channel_type(channel_key.channel_type());
    create_msg.mutable_create_timepoint()->set_seconds(4000);
    create_msg.mutable_detail()->mutable_create()->mutable_create_timepoint()->set_seconds(4000);
    recreate_hash = rpc::dtmq::calculate_hash_code(0, create_msg);
    create_msg.set_hash_code(recreate_hash);
    *event_sync.add_channel_message() = create_msg;

    atfw::dtmq::DChannelMessage msg;
    msg.set_sequence(31);
    msg.set_channel_type(channel_key.channel_type());
    msg.mutable_create_timepoint()->set_seconds(4100);
    msg.mutable_detail()->set_text("after-full-resync");
    msg.set_hash_code(rpc::dtmq::calculate_hash_code(recreate_hash, msg));
    *event_sync.add_channel_message() = msg;

    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_full_resync", event_sync));
  }
  CASE_EXPECT_TRUE(subscriber->is_ready());
  CASE_EXPECT_EQ(30, subscriber->get_create_sequence());
  CASE_EXPECT_EQ(31, subscriber->get_last_message_sequence());
  CASE_EXPECT_FALSE(rpc::dtmq::client_subscriber::global_has_pending_heartbeat());
  CASE_EXPECT_EQ(2, static_cast<int>(received_texts.size()));
  if (received_texts.size() >= 2) {
    CASE_EXPECT_EQ(std::string("chain-tip"), received_texts[0]);
    CASE_EXPECT_EQ(std::string("after-full-resync"), received_texts[1]);
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ shared_subscriber retention + reuse after all clients destroyed ============
// When every client_subscriber of a channel is destroyed, the underlying shared_subscriber is kept in
// the cache for shared_subscriber_gc_timeout (normalized from the heartbeat intervals by the excel
// config index, far longer than this case runs). A client_subscriber created shortly after on the
// same channel reuses that exact shared_subscriber: get_shared_channel_identify() is unchanged, and
// because the retained shared_subscriber was already ready, the new client is ready immediately.
CASE_TEST(component_dtmq_subscriber, shared_subscriber_retained_and_reused_after_all_clients_destroyed) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }
  auto subscribe_rule = mock_subscribe_ack(test);
  CASE_EXPECT_TRUE(!!subscribe_rule);

  // Two clients share the same channel (and therefore the same shared_subscriber).
  auto channel_key = make_channel_key("chan-subscriber-retention");
  auto first = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:retain-1"));
  auto second = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:retain-2"));
  CASE_EXPECT_TRUE(!!first && !!second);
  if (!first || !second) {
    test.stop();
    return;
  }
  uint64_t shared_identify = first->get_shared_channel_identify();
  CASE_EXPECT_EQ(shared_identify, second->get_shared_channel_identify());

  CASE_EXPECT_TRUE(drive_heartbeat_round(test, "hb_round"));
  std::string shared_key = shared_subscriber_key_for();
  {
    atfw::dtmq::SSChannelEventSync event_sync;
    event_sync.mutable_channel_snapshot()->CopyFrom(make_ready_snapshot(channel_key, 10));
    event_sync.add_subscriber_keys(shared_key);
    CASE_EXPECT_TRUE(push_channel_event(test, "push_ready", event_sync));
  }
  CASE_EXPECT_TRUE(first->is_ready());
  CASE_EXPECT_TRUE(second->is_ready());

  // Destroy ALL client_subscribers of the channel. The shared_subscriber switches to its gc timer
  // and is kept in the cache instead of being removed.
  first = nullptr;
  second = nullptr;

  // Re-create within the retention window: the exact same shared_subscriber instance is reused, and
  // because it was already ready the new client observes is_ready() immediately.
  auto recreated = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:retain-3"));
  CASE_EXPECT_TRUE(!!recreated);
  if (!recreated) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(shared_identify, recreated->get_shared_channel_identify());
  CASE_EXPECT_TRUE(recreated->is_ready());
  CASE_EXPECT_EQ(10, recreated->get_create_sequence());

  // The new client registers on the retained shared_subscriber and re-subscribes through the
  // heartbeat, so the proxysvr subscription stays alive.
  CASE_EXPECT_TRUE(rpc::dtmq::client_subscriber::global_has_pending_heartbeat());
  CASE_EXPECT_TRUE(drive_heartbeat_round(test, "hb_round_recreated"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ send_text / send_event (DChannelMessageDetail convenience wrappers) ============
// send_text/send_event build a DChannelMessageDetail and delegate to send_message. The mock captures
// each request so the detail payload, the sender/subscriber key override (the client subscriber_key,
// not the shared one), auto_create_channel and the lock checker copy-in/copy-back can be verified.
CASE_TEST(component_dtmq_subscriber, send_text_and_send_event) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }
  auto subscribe_rule = mock_subscribe_ack(test);

  std::vector<atfw::dtmq::SSChannelSendMessageReq> received_requests;
  auto send_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_send_message(),
      atfw::dtmq::SSChannelSendMessageReq::descriptor()->full_name(),
      atfw::dtmq::SSChannelSendMessageRsp::descriptor()->full_name(),
      [&received_requests](const atframework::testing::ss_request_view& request,
                           google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::dtmq::SSChannelSendMessageReq&>(request.body);
        auto& typed_response = static_cast<atfw::dtmq::SSChannelSendMessageRsp&>(response);
        received_requests.push_back(typed_request);
        if (typed_request.has_compare_and_maybe_reset_lock()) {
          typed_response.mutable_compare_and_maybe_reset_lock()->mutable_real_value()->set_lock_holder("actual-holder");
        }
        typed_response.set_client_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!subscribe_rule && !!send_rule);

  auto channel_key = make_channel_key("chan-subscriber-send-text");
  auto nullable = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:send-text"));
  CASE_EXPECT_TRUE(!!nullable);
  if (!nullable) {
    test.stop();
    return;
  }
  subscriber_ptr subscriber = nullable;  // nullable<ptr_t> == ptr_t

  auto task = test.run_task(
      "send_text_event", std::chrono::seconds{4}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        // send_text with the lock checker pair and auto_create_channel=true.
        auto lock_checker = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        lock_checker->mutable_expect_value()->set_lock_holder("expected-holder");
        auto lock_checker_rsp = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        int32_t res = RPC_AWAIT_CODE_RESULT(
            subscriber->send_text(ctx, "hello-send-text", lock_checker, lock_checker_rsp, true, false));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ("actual-holder", lock_checker_rsp->real_value().lock_holder());

        // send_event with default options (no lock, no auto-create).
        google::protobuf::Any event_data;
        event_data.set_type_url("type.googleapis.com/UnitTestSendEvent");
        event_data.set_value("event-value");
        res = RPC_AWAIT_CODE_RESULT(subscriber->send_event(ctx, std::move(event_data)));
        CASE_EXPECT_EQ(0, res);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_EQ(2u, received_requests.size());
  CASE_EXPECT_EQ(2, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_send_message())));
  if (received_requests.size() < 2) {
    test.stop();
    return;
  }

  // send_text: the text payload lands in message_content.detail.text, the client subscriber_key
  // overrides the shared one in both subscriber and message_content.sender_key, and the lock
  // checker is copied into the request.
  const auto& text_req = received_requests.at(0);
  CASE_EXPECT_EQ("hello-send-text", text_req.message_content().detail().text());
  CASE_EXPECT_EQ("UT:send-text", text_req.message_content().sender_key());
  CASE_EXPECT_EQ("UT:send-text", text_req.subscriber().subscriber_key());
  CASE_EXPECT_EQ(channel_key.channel_id(), text_req.channel_key().channel_id());
  CASE_EXPECT_TRUE(text_req.auto_create_channel());
  CASE_EXPECT_TRUE(text_req.has_compare_and_maybe_reset_lock());
  CASE_EXPECT_EQ("expected-holder", text_req.compare_and_maybe_reset_lock().expect_value().lock_holder());

  // send_event: the Any payload is moved into message_content.detail.event; defaults are no lock and
  // no auto-create.
  const auto& event_req = received_requests.at(1);
  CASE_EXPECT_EQ("type.googleapis.com/UnitTestSendEvent", event_req.message_content().detail().event().type_url());
  CASE_EXPECT_EQ("event-value", event_req.message_content().detail().event().value());
  CASE_EXPECT_EQ("UT:send-text", event_req.subscriber().subscriber_key());
  CASE_EXPECT_FALSE(event_req.auto_create_channel());
  CASE_EXPECT_FALSE(event_req.has_compare_and_maybe_reset_lock());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ send_destroy / send_reset_lock / send_update (channel control wrappers) ============
// send_destroy/send_reset_lock/send_update call the destroy_channel/reset_lock/update SS RPCs on the
// writable node. The mocks capture each request so field mapping (channel_key, lock checker,
// auto_create_channel, update_option fields, Any packing) and response handling (lock copy-back,
// client_result propagation) can be verified.
CASE_TEST(component_dtmq_subscriber, send_destroy_reset_lock_and_update) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }
  auto subscribe_rule = mock_subscribe_ack(test);

  std::vector<atfw::dtmq::SSChannelDestroyChannelReq> destroy_requests;
  auto destroy_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_destroy_channel(),
      atfw::dtmq::SSChannelDestroyChannelReq::descriptor()->full_name(),
      google::protobuf::Empty::descriptor()->full_name(),
      [&destroy_requests](const atframework::testing::ss_request_view& request,
                          google::protobuf::Message& /*response*/) -> rpc::result_code_type {
        destroy_requests.push_back(static_cast<const atfw::dtmq::SSChannelDestroyChannelReq&>(request.body));
        RPC_RETURN_CODE(0);
      });

  std::vector<atfw::dtmq::SSChannelResetLockReq> reset_lock_requests;
  auto reset_lock_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_reset_lock(), atfw::dtmq::SSChannelResetLockReq::descriptor()->full_name(),
      atfw::dtmq::SSChannelResetLockRsp::descriptor()->full_name(),
      [&reset_lock_requests](const atframework::testing::ss_request_view& request,
                             google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::dtmq::SSChannelResetLockReq&>(request.body);
        auto& typed_response = static_cast<atfw::dtmq::SSChannelResetLockRsp&>(response);
        reset_lock_requests.push_back(typed_request);
        typed_response.mutable_compare_and_maybe_reset_lock()->mutable_real_value()->set_lock_holder("reset-actual");
        typed_response.set_client_result(0);
        RPC_RETURN_CODE(0);
      });

  // A distinctive positive client_result verifies that send_update forwards the response
  // client_result (not just the RPC transport result) to the caller.
  constexpr int32_t kUpdateClientResult = 13579;
  std::vector<atfw::dtmq::SSChannelUpdateReq> update_requests;
  auto update_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_update(), atfw::dtmq::SSChannelUpdateReq::descriptor()->full_name(),
      atfw::dtmq::SSChannelUpdateRsp::descriptor()->full_name(),
      [&update_requests](const atframework::testing::ss_request_view& request,
                         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::dtmq::SSChannelUpdateReq&>(request.body);
        auto& typed_response = static_cast<atfw::dtmq::SSChannelUpdateRsp&>(response);
        update_requests.push_back(typed_request);
        typed_response.mutable_compare_and_maybe_reset_lock()->mutable_real_value()->set_lock_holder("update-actual");
        typed_response.set_client_result(kUpdateClientResult);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!subscribe_rule && !!destroy_rule && !!reset_lock_rule && !!update_rule);

  auto channel_key = make_channel_key("chan-subscriber-send-ctl");
  auto nullable = rpc::dtmq::client_subscriber::create(channel_key, make_subscriber_options("UT:send-ctl"));
  CASE_EXPECT_TRUE(!!nullable);
  if (!nullable) {
    test.stop();
    return;
  }
  subscriber_ptr subscriber = nullable;  // nullable<ptr_t> == ptr_t

  auto task =
      test.run_task("send_ctl", std::chrono::seconds{4}, [&subscriber](rpc::context& ctx) -> rpc::result_code_type {
        // send_destroy forwards the channel_key and the optional lock checker.
        auto destroy_lock = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        destroy_lock->mutable_expect_value()->set_lock_holder("destroy-expected");
        int32_t res = RPC_AWAIT_CODE_RESULT(subscriber->send_destroy(ctx, destroy_lock, false));
        CASE_EXPECT_EQ(0, res);

        // send_reset_lock with the lock checker pair and auto_create_channel=true.
        auto reset_lock = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        reset_lock->mutable_expect_value()->set_lock_holder("reset-expected");
        auto reset_lock_rsp = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        res = RPC_AWAIT_CODE_RESULT(subscriber->send_reset_lock(ctx, reset_lock, reset_lock_rsp, true, false));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ("reset-actual", reset_lock_rsp->real_value().lock_holder());

        // send_update exercises every update_option field: custom_data as a typed message (PackFrom
        // path), private_data as a pre-packed Any (CopyFrom path), force_update_subscribers with a
        // nullptr hole, plus the lock checker pair.
        rpc::dtmq::client_subscriber::update_option update_options;
        update_options.save = true;
        update_options.compact_sequence = 42;
        update_options.stateful_sequence = 41;
        atfw::dtmq::DChannelIdKey custom_payload;
        custom_payload.set_channel_id("custom-payload");
        update_options.custom_data = &custom_payload;
        update_options.custom_data_skip_notify = true;
        google::protobuf::Any private_payload;
        private_payload.set_type_url("type.googleapis.com/UnitTestPrivateData");
        update_options.private_data = &private_payload;
        // get_shared_subscriber_info() is documented as the input for force_update_subscribers; the
        // nullptr hole is skipped by send_update.
        const atfw::dtmq::channel_subscriber* force_update_list[] = {&subscriber->get_shared_subscriber_info(),
                                                                     nullptr};
        update_options.force_update_subscribers = gsl::span<const atfw::dtmq::channel_subscriber*>{force_update_list};
        auto update_lock = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        update_lock->mutable_expect_value()->set_lock_holder("update-expected");
        auto update_lock_rsp = atfw::util::memory::make_strong_rc<atfw::dtmq::channel_lock_checker>();
        res = RPC_AWAIT_CODE_RESULT(
            subscriber->send_update(ctx, update_options, update_lock, update_lock_rsp, true, false));
        CASE_EXPECT_EQ(13579, res);
        CASE_EXPECT_EQ("update-actual", update_lock_rsp->real_value().lock_holder());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  // send_destroy: channel_key + lock checker copied into the request.
  CASE_EXPECT_EQ(1u, destroy_requests.size());
  if (!destroy_requests.empty()) {
    CASE_EXPECT_EQ(channel_key.channel_id(), destroy_requests.at(0).channel_key().channel_id());
    CASE_EXPECT_EQ("destroy-expected",
                   destroy_requests.at(0).compare_and_maybe_reset_lock().expect_value().lock_holder());
  }

  // send_reset_lock: auto_create_channel + lock checker copied into the request.
  CASE_EXPECT_EQ(1u, reset_lock_requests.size());
  if (!reset_lock_requests.empty()) {
    CASE_EXPECT_TRUE(reset_lock_requests.at(0).auto_create_channel());
    CASE_EXPECT_EQ(channel_key.channel_id(), reset_lock_requests.at(0).channel_key().channel_id());
    CASE_EXPECT_EQ("reset-expected",
                   reset_lock_requests.at(0).compare_and_maybe_reset_lock().expect_value().lock_holder());
  }

  // send_update: every update_option field mapped. The subscriber is the SHARED layer subscriber
  // (shared_subscriber_key_for), and the typed custom_data is packed into an Any while the
  // pre-packed private_data Any is copied as-is.
  CASE_EXPECT_EQ(1u, update_requests.size());
  if (!update_requests.empty()) {
    const auto& update_req = update_requests.at(0);
    CASE_EXPECT_EQ(channel_key.channel_id(), update_req.channel_key().channel_id());
    CASE_EXPECT_EQ(shared_subscriber_key_for(), update_req.subscriber().subscriber_key());
    CASE_EXPECT_TRUE(update_req.auto_create_channel());
    CASE_EXPECT_TRUE(update_req.save());
    CASE_EXPECT_EQ(42, update_req.compact_sequence());
    CASE_EXPECT_EQ(41, update_req.stateful_sequence());
    CASE_EXPECT_EQ(1, update_req.update_others_size());
    if (update_req.update_others_size() > 0) {
      // The shared-layer subscriber info passed to force_update_subscribers is forwarded as-is.
      CASE_EXPECT_EQ(shared_subscriber_key_for(), update_req.update_others(0).subscriber_key());
    }
    CASE_EXPECT_EQ("type.googleapis.com/atframework.dtmq.DChannelIdKey", update_req.custom_data().type_url());
    atfw::dtmq::DChannelIdKey unpacked_custom;
    CASE_EXPECT_TRUE(update_req.custom_data().UnpackTo(&unpacked_custom));
    CASE_EXPECT_EQ("custom-payload", unpacked_custom.channel_id());
    CASE_EXPECT_TRUE(update_req.custom_data_skip_notify());
    CASE_EXPECT_EQ("type.googleapis.com/UnitTestPrivateData", update_req.private_data().type_url());
    CASE_EXPECT_EQ("update-expected", update_req.compare_and_maybe_reset_lock().expect_value().lock_holder());
  }

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_destroy_channel())));
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_reset_lock())));
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_update())));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ server not_found (non-auto_create pairing with task_action_subscribe) ============
// A subscriber created with auto_create_channel=false whose heartbeat is answered with
// not_found_channel_ids must reliably fire set_event_callback_on_destroyed, even though it never
// became ready (the channel was never created anywhere). An auto_create subscriber receiving the
// same response keeps the old behavior (no destroy callback while not ready): the server is expected
// to create the channel, and a not_found for it is an anomaly that must not tear down local state.
CASE_TEST(component_dtmq_subscriber, non_auto_create_destroyed_on_server_not_found) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_resource_tables(rt.resource());
    rt.resource().set_version("0.10.0.1");
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    CASE_MSG_INFO() << "discovery injection failed: " << test.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  // Subscribe mock answering every heartbeat channel with not_found_channel_ids, recording the
  // per-heartbeat auto_create flag so the test can verify it propagates from subscriber options.
  std::vector<atfw::dtmq::DChannelSyncPoint> seen_heartbeats;
  auto subscribe_rule = test.ss().mock(
      rpc::dtmq::packer::get_full_name_of_subscribe(), atfw::dtmq::SSChannelSubscribeReq::descriptor()->full_name(),
      atfw::dtmq::SSChannelSubscribeRsp::descriptor()->full_name(),
      [&seen_heartbeats](const atframework::testing::ss_request_view& request,
                         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::dtmq::SSChannelSubscribeReq&>(request.body);
        auto& typed_response = static_cast<atfw::dtmq::SSChannelSubscribeRsp&>(response);
        for (const auto& heartbeat : typed_request.heartbeat()) {
          seen_heartbeats.push_back(heartbeat);
          typed_response.add_not_found_channel_ids(heartbeat.channel_key().channel_id());
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!subscribe_rule);
  if (!subscribe_rule) {
    test.stop();
    return;
  }

  auto no_create_key = make_channel_key("chan-subscriber-nocreate-nf");
  auto no_create_options = make_subscriber_options("UT:nf-no-create", false, false);
  no_create_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  size_t no_create_destroyed_calls = 0;
  int64_t no_create_destroyed_sequence = -1;
  bool no_create_ready_called = false;
  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
      *no_create_options.event_callback_set,
      [&no_create_destroyed_calls, &no_create_destroyed_sequence](
          rpc::context&, const subscriber_ptr&, int64_t log_sequence, std::chrono::system_clock::time_point) {
        ++no_create_destroyed_calls;
        no_create_destroyed_sequence = log_sequence;
      });
  rpc::dtmq::client_subscriber::set_event_callback_on_ready(
      *no_create_options.event_callback_set,
      [&no_create_ready_called](rpc::context&, const subscriber_ptr&) { no_create_ready_called = true; });

  auto auto_create_key = make_channel_key("chan-subscriber-autocreate-nf");
  auto auto_create_options = make_subscriber_options("UT:nf-auto-create", true, false);
  auto_create_options.event_callback_set = rpc::dtmq::client_subscriber::create_event_callback_set();
  size_t auto_create_destroyed_calls = 0;
  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
      *auto_create_options.event_callback_set,
      [&auto_create_destroyed_calls](rpc::context&, const subscriber_ptr&, int64_t,
                                     std::chrono::system_clock::time_point) {
        // Increment the count of destroyed calls for the auto_create subscriber.
        ++auto_create_destroyed_calls;
      });

  auto no_create_subscriber = rpc::dtmq::client_subscriber::create(no_create_key, no_create_options);
  auto auto_create_subscriber = rpc::dtmq::client_subscriber::create(auto_create_key, auto_create_options);
  CASE_EXPECT_TRUE(!!no_create_subscriber);
  CASE_EXPECT_TRUE(!!auto_create_subscriber);
  if (!no_create_subscriber || !auto_create_subscriber) {
    test.stop();
    return;
  }

  // Both subscribers were just created, so both are queued for the same heartbeat round.
  CASE_EXPECT_TRUE(rpc::dtmq::client_subscriber::global_has_pending_heartbeat());
  CASE_EXPECT_TRUE(drive_heartbeat_round(test, "heartbeat_not_found_round"));

  // The heartbeat carried each subscriber's auto_create setting.
  bool saw_no_create_heartbeat = false;
  bool saw_auto_create_heartbeat = false;
  for (const auto& heartbeat : seen_heartbeats) {
    if (heartbeat.channel_key().channel_id() == no_create_key.channel_id()) {
      saw_no_create_heartbeat = true;
      CASE_EXPECT_FALSE(heartbeat.auto_create_channel());
    }
    if (heartbeat.channel_key().channel_id() == auto_create_key.channel_id()) {
      saw_auto_create_heartbeat = true;
      CASE_EXPECT_TRUE(heartbeat.auto_create_channel());
    }
  }
  CASE_EXPECT_TRUE(saw_no_create_heartbeat);
  CASE_EXPECT_TRUE(saw_auto_create_heartbeat);

  // The non-auto_create subscriber was destroyed by the not_found response even though it never
  // became ready; the destroy sequence is the subscriber's last known message sequence, or 1 when
  // it never saw one, so that it is always > 0 (never destroyed / no data yet) and is_destroyed()
  // becomes observable for stale restored channels.
  CASE_EXPECT_EQ(1u, no_create_destroyed_calls);
  CASE_EXPECT_EQ(1, no_create_destroyed_sequence);
  CASE_EXPECT_TRUE(no_create_subscriber->is_destroyed());
  CASE_EXPECT_FALSE(no_create_ready_called);
  CASE_EXPECT_FALSE(no_create_subscriber->is_ready());

  // The auto_create subscriber keeps waiting for the server to create the channel: no destroy
  // callback while not ready.
  CASE_EXPECT_EQ(0u, auto_create_destroyed_calls);
  CASE_EXPECT_FALSE(auto_create_subscriber->is_ready());

  CASE_EXPECT_EQ(0, test.stop());
}

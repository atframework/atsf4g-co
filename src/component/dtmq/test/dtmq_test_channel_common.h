// Copyright 2026 atframework
//
// Shared helpers for the dtmq-proxysvr server-side unit tests (mq_channel_manager + mq_channel).
//
// This header is included by both dtmq_test_channel_manager.cpp and dtmq_test_channel.cpp. It provides:
//   - excel resource seeding (mandatory tables + a populated dtmq_channel_type table)
//   - discovery node injection (local + remote dtmq-proxysvr nodes)
//   - mq_channel_manager singleton lifecycle (init once, clear between cases)
//   - channel_key/configure factories with unique ids per case
//   - DB mock seeding for rpc::db::dtmq_channel_record
//
// All cases run inside an atfw::testing::runtime fixture (SS + RESOURCE + DB + HPA). The
// mq_channel_manager singleton is process-lifetime (no reset), so ensure_manager_ready() initializes
// it exactly once and clear_manager_for_unit_test() wipes its internal maps between cases.

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/any.h>
#include <google/protobuf/empty.pb.h>

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/com.struct.dtmq.config.pb.h>
#include <protocol/config/dtmq_proxy.config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_resource.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>
#include <time/time_utility.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "atframe/atapp_conf.h"  // IWYU pragma: keep
#include "config/excel_config_dtmq_index.h"
#include "config/extern_service_types.h"
#include "config/logic_config.h"
#include "data/mq_channel.h"
#include "frame/test_macros.h"  // IWYU pragma: keep
#include "logic/logic_server_setup.h"
#include "logic/mq_channel_manager.h"
#include "rpc/db/local_db_interface.atfw.gen.h"     // IWYU pragma: keep
#include "rpc/dtmq/dtmq_client_api.h"               // IWYU pragma: keep
#include "rpc/dtmq/dtmqproxysvrservice.atfw.gen.h"  // IWYU pragma: keep
#include "rpc/rpc_context.h"
#include "utility/protobuf_mini_dumper.h"

#include "dtmq_test_channel_accessors.h"  // NOLINT(build/include_subdir)

namespace dtmq_channel_test {

// ---- Node ids used as discovery targets --------------------------------------
// Local node id matches the default runtime_options.app_id (0x11000001) so logic_config::get_local_server_id()
// resolves to the local test instance.
constexpr uint64_t kLocalNodeId = 0x11000001;
constexpr uint64_t kPeerNode1 = 0x1C0001;
constexpr uint64_t kPeerNode2 = 0x1C0002;
constexpr uint64_t kPeerNode3 = 0x1C0003;

// Excel channel_type used by tests. We seed a single row (channel_type=0) with readonly_replicate_count=2
// so that both a writable slot and two readonly replica slots exist in the consistent-hash distribution.
constexpr uint32_t kTestChannelType = 0;

// Second excel channel_type row (channel_type=1) that is always seeded as DB-backed (memory_only=false),
// used by cases that exercise the real DB save/load path.
constexpr uint32_t kTestDbBackedChannelType = 1;

// Append one dtmq_channel_type row to blocks. memory_only controls whether writable_init hits
// the DB or short-circuits to upgrade_to_writable(). readonly_replicate_count drives the replica slots.
inline void add_dtmq_channel_type_row(org::xresloader::pb::xresloader_datablocks& blocks, uint32_t channel_type,
                                      bool memory_only, uint32_t readonly_replicate_count) {
  PROJECT_NAMESPACE_ID::config::ExcelDtmqChannelType item;
  item.set_channel_type(channel_type);
  item.set_readonly_replicate_count(readonly_replicate_count);
  auto* configure = item.mutable_channel_configure();
  configure->set_channel_type(channel_type);
  configure->set_max_log_count(300);
  configure->set_gc_log_count(30);
  configure->set_memory_only(memory_only);
  configure->mutable_gc_expire_duration()->set_seconds(86400);
  configure->mutable_heartbeat_interval()->set_seconds(300);
  configure->mutable_heartbeat_retry_interval()->set_seconds(60);
  configure->mutable_subscriber_timeout()->set_seconds(700);
  blocks.add_data_block(item.SerializeAsString());
}

// Build a populated dtmq_channel_type.bytes: one row for kTestChannelType (memory_only controlled by the
// caller) plus the always-DB-backed kTestDbBackedChannelType row.
inline std::string make_dtmq_channel_type_bytes(uint32_t channel_type, bool memory_only,
                                                uint32_t readonly_replicate_count) {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("rpc-unit-test");
  add_dtmq_channel_type_row(blocks, channel_type, memory_only, readonly_replicate_count);
  if (kTestDbBackedChannelType != channel_type) {
    add_dtmq_channel_type_row(blocks, kTestDbBackedChannelType, false, readonly_replicate_count);
  }
  return blocks.SerializeAsString();
}

// Override dtmq_channel_type with the test rows; every other table comes from the mock's automatic
// snapshot of the real generated bindir (see mock_resource::bind()), so excel table set changes never
// require touching this fixture. memory_only controls whether the test channel_type is memory-only
// (skips DB load) or DB-backed.
inline void seed_resource_tables(atframework::testing::mock_resource& resource, uint32_t channel_type, bool memory_only,
                                 uint32_t readonly_replicate_count) {
  resource.set_file("dtmq_channel_type.bytes",
                    make_dtmq_channel_type_bytes(channel_type, memory_only, readonly_replicate_count));
}

// Inject a single dtmq-proxysvr discovery node and replay it into the common-module discovery index.
// Both hpa_scaling_ready and hpa_scaling_target labels are attached so the node resolves under both
// kReady and kTarget discovery modes (mq_channel uses kTarget for target_distribution_ which drives
// should_be_writable/should_be_readonly decisions).
inline bool add_dtmq_proxy_node(atframework::testing::runtime& test, uint64_t node_id,
                                const std::string& name = "unit-test-dtmq-proxy") {
  atframework::testing::mock_node node;
  node.set_id(node_id)
      .set_name(name + std::to_string(node_id))
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtMqProxySvr))
      .set_type_name("dtmq-proxysvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1")
      .add_label("hpa_scaling_target", "1");
  auto remote = test.discovery().add_node(node);
  if (!remote) {
    return false;
  }
  return true;
}

// Reload the common-module discovery index so injected nodes are visible to the HPA/discovery selectors.
inline void reload_discovery() {
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
}

// Register the dtmq_proxysvr server-instance config loader (same lambda as dtmq_proxy_main.cpp). Must be
// called in setup_callback (before app->init). Captureless lambda converts to the function pointer.
inline void register_dtmq_proxy_config_loader() {
  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app& app_, logic_config& /*cfg*/, logic_config::server_instance_config_ptr& to) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<atfw::dtmq::config::dtmq_proxysvr_cfg>();
        app_.parse_configures_into(*config_ptr, "dtmq_proxysvr", "ATAPP_DTMQ_PROXYSVR");
        to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });
}

// Initialize the mq_channel_manager singleton exactly once (it is process-lifetime). Subsequent calls
// are no-ops. Returns 0 on success.
inline int ensure_manager_init() {
  static const int init_result = mq_channel_manager::me()->init();
  return init_result;
}

// Wipe the manager's internal maps so the next case starts clean. The singleton itself persists.
inline void clear_manager_for_unit_test() {
  if (mq_channel_manager::is_instance_destroyed()) {
    return;
  }
  MqChannelManagerUnitTest::clear_all_channels(*mq_channel_manager::me());
}

// Build a channel_key with a unique id (counter-suffixed) to avoid cross-case collisions.
inline atfw::dtmq::DChannelIdKey make_channel_key(const std::string& tag, uint32_t channel_type) {
  static std::atomic<uint64_t> counter{1};
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id("ut-chan-" + tag + "-" + std::to_string(counter.fetch_add(1)));
  channel_key.set_channel_type(channel_type);
  return channel_key;
}

// Fetch the DChannelConfigure for a channel_type from the loaded excel. Returns a default-constructed
// configure if the excel row is missing (tests seed one row, so this should not happen).
inline atfw::dtmq::DChannelConfigure get_configure_for(uint32_t channel_type) {
  atfw::dtmq::DChannelConfigure configure;
  auto cfg = excel::get_dtmq_channel_configure(channel_type);
  if (cfg) {
    configure = *cfg;
  }
  return configure;
}

// Register the DB message factory for table_dtmq_channel_record so the in-memory mock_db can deserialize
// stored bytes. Call after test.start().
inline void register_dtmq_db_message_type(atframework::testing::runtime& test) {
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>();
}

// Register the writable-side subscribe mock used by readonly_init(). In production the writable node
// sends a snapshot asynchronously after accepting the subscription. The offline fixture has no second
// dtmq-proxysvr process, so the typed handler applies an equivalent snapshot to the local readonly object.
// Keep the RAII handle alive across the whole runtime; discarding it would immediately unregister the rule.
inline bool register_readonly_subscribe_mock() {
  static rpc::unit_test::mock_rule_handle subscribe_rule;
  subscribe_rule = rpc::dtmq::mock::subscribe([](rpc::context& ctx, const atfw::dtmq::SSChannelSubscribeReq& request,
                                                 atfw::dtmq::SSChannelSubscribeRsp& response) -> rpc::result_code_type {
    for (const auto& heartbeat : request.heartbeat()) {
      auto channel = mq_channel_manager::me()->get_channel(heartbeat.channel_key().channel_id());
      if (!channel) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
      }

      channel->set_created(ctx, atfw::util::time::time_utility::now(), 1);
      atfw::dtmq::channel_snapshot snapshot;
      channel->dump_snapshot(ctx, snapshot);
      snapshot.set_replicate_index(channel->get_current_replicate_index());
      if (!channel->load_snapshot(ctx, std::move(snapshot))) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
      }

      auto* subscribe_node = response.add_subscribe_node();
      subscribe_node->mutable_channel_key()->CopyFrom(heartbeat.channel_key());
      subscribe_node->set_server_id(channel->get_ready_distribution_writable_server_id());
      subscribe_node->set_readonly_index(channel->get_current_replicate_index());
    }
    RPC_RETURN_CODE(0);
  });
  return !!subscribe_rule;
}

// Shared runtime setup for mq_channel/mq_channel_manager/action cases. peer_node_count controls the
// distribution cardinality: one peer makes writable/readonly sharing deterministic; three peers allow
// finding a channel for which the local node owns no replica (used by task forwarding tests).
inline bool start_dtmq_proxysvr_runtime(atframework::testing::runtime& test, uint32_t readonly_replicate_count = 2,
                                        bool memory_only = true, size_t peer_node_count = 2) {
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource,
                      atframework::testing::feature::db, atframework::testing::feature::hpa};
  options.app_id = kLocalNodeId;
  options.setup_callback = [readonly_replicate_count, memory_only](atframework::testing::runtime& rt) -> int {
    seed_resource_tables(rt.resource(), kTestChannelType, memory_only, readonly_replicate_count);
    rt.resource().set_version("0.10.0.1");
    register_dtmq_proxy_config_loader();
    return 0;
  };

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }

  if (!add_dtmq_proxy_node(test, kLocalNodeId, "ut-local-")) {
    CASE_MSG_INFO() << "local discovery injection failed: " << test.get_diagnostic() << '\n';
    return false;
  }

  constexpr uint64_t kPeerNodeIds[] = {kPeerNode1, kPeerNode2, kPeerNode3};
  if (peer_node_count > sizeof(kPeerNodeIds) / sizeof(kPeerNodeIds[0])) {
    CASE_MSG_INFO() << "too many peer nodes requested: " << peer_node_count << '\n';
    return false;
  }
  for (size_t index = 0; index < peer_node_count; ++index) {
    if (!add_dtmq_proxy_node(test, kPeerNodeIds[index], "ut-peer-")) {
      CASE_MSG_INFO() << "peer discovery injection failed: " << test.get_diagnostic() << '\n';
      return false;
    }
  }

  reload_discovery();
  register_dtmq_db_message_type(test);
  if (!register_readonly_subscribe_mock()) {
    CASE_MSG_INFO() << "subscribe mock registration failed: " << test.ss().get_diagnostic() << '\n';
    return false;
  }

  if (ensure_manager_init() < 0) {
    CASE_MSG_INFO() << "mq_channel_manager init failed\n";
    return false;
  }
  mq_channel_manager::me()->reload();

  // Force every new channel to build ready/target distribution caches for this fixture's discovery set.
  static std::atomic<int64_t> revision_counter{1};
  MqChannelManagerUnitTest::set_latest_server_etcd_revision(*mq_channel_manager::me(), revision_counter.fetch_add(1));
  clear_manager_for_unit_test();
  return true;
}

// Find a channel_id that hashes to the local node (writable) for the given number of nodes. This makes
// writable-local cases deterministic regardless of hash function internals.
// Tries up to max_trials ids of the form prefix-<n> and returns the first whose get_target_server_id
// (kWritable, kTarget) == kLocalNodeId.
inline std::string find_local_writable_channel_id(const std::string& prefix, uint64_t local_id, size_t max_trials = 200,
                                                  uint32_t channel_type = kTestChannelType) {
  for (size_t i = 0; i < max_trials; ++i) {
    atfw::dtmq::DChannelIdKey key;
    key.set_channel_id(prefix + "-" + std::to_string(i));
    key.set_channel_type(channel_type);
    uint64_t sid = 0;
    sid = rpc::dtmq::get_target_server_id(key, rpc::dtmq::replicate_type::kWritable, 0,
                                          logic_hpa_discovery_select_mode::kTarget);
    if (sid == local_id) {
      return prefix + "-" + std::to_string(i);
    }
  }
  return prefix + "-fallback";
}

// Find a channel_id that hashes to a peer node (not local) for writable. Returns the peer server_id in
// out_forward_id, or 0 if none found within max_trials.
inline std::string find_remote_writable_channel_id(const std::string& prefix, uint64_t local_id,
                                                   uint64_t& out_forward_id, size_t max_trials = 200) {
  for (size_t i = 0; i < max_trials; ++i) {
    atfw::dtmq::DChannelIdKey key;
    key.set_channel_id(prefix + "-" + std::to_string(i));
    key.set_channel_type(kTestChannelType);
    uint64_t sid = 0;
    sid = rpc::dtmq::get_target_server_id(key, rpc::dtmq::replicate_type::kWritable, 0,
                                          logic_hpa_discovery_select_mode::kTarget);
    if (sid != local_id && sid != 0) {
      out_forward_id = sid;
      return prefix + "-" + std::to_string(i);
    }
  }
  out_forward_id = 0;
  return prefix + "-fallback";
}

// Find a channel_id + replicate_index such that the given replicate_index maps to the local node.
// Returns the channel_id and sets out_replicate_index to the matching index (1..N). If none found,
// out_replicate_index is set to 0 and a fallback id is returned.
inline std::string find_local_readonly_channel_id(const std::string& prefix, uint64_t local_id,
                                                  uint64_t& out_replicate_index, size_t max_trials = 200) {
  for (size_t i = 0; i < max_trials; ++i) {
    atfw::dtmq::DChannelIdKey key;
    key.set_channel_id(prefix + "-" + std::to_string(i));
    key.set_channel_type(kTestChannelType);
    uint64_t writable_server_id = rpc::dtmq::get_target_server_id(key, rpc::dtmq::replicate_type::kWritable, 0,
                                                                  logic_hpa_discovery_select_mode::kTarget);
    if (writable_server_id == local_id || writable_server_id == 0) {
      continue;
    }
    // Check each readonly replica index (1 and 2 for readonly_replicate_count=2)
    for (uint64_t ridx = 1; ridx <= 2; ++ridx) {
      uint64_t sid = 0;
      sid = rpc::dtmq::get_target_server_id(key, rpc::dtmq::replicate_type::kReadonly, ridx,
                                            logic_hpa_discovery_select_mode::kTarget);
      if (sid == local_id) {
        out_replicate_index = ridx;
        return prefix + "-" + std::to_string(i);
      }
    }
  }
  out_replicate_index = 0;
  return prefix + "-fallback";
}

// Find a channel for which the local node owns neither writable nor readonly replicas. With four
// discovery nodes and two readonly replicas such a channel always has one excluded node, so trying
// several hashes makes the forwarding precondition deterministic without hardcoding hash results.
inline std::string find_remote_readable_channel_id(const std::string& prefix, uint64_t local_id,
                                                   size_t max_trials = 400) {
  for (size_t i = 0; i < max_trials; ++i) {
    atfw::dtmq::DChannelIdKey key;
    key.set_channel_id(prefix + "-" + std::to_string(i));
    key.set_channel_type(kTestChannelType);
    uint64_t writable_server_id = rpc::dtmq::get_target_server_id(key, rpc::dtmq::replicate_type::kWritable, 0,
                                                                  logic_hpa_discovery_select_mode::kTarget);
    if (writable_server_id == 0 || writable_server_id == local_id) {
      continue;
    }

    bool local_readonly = false;
    for (uint64_t replicate_index = 1; replicate_index <= 2; ++replicate_index) {
      uint64_t readonly_server_id = rpc::dtmq::get_target_server_id(
          key, rpc::dtmq::replicate_type::kReadonly, replicate_index, logic_hpa_discovery_select_mode::kTarget);
      if (readonly_server_id == 0 || readonly_server_id == local_id) {
        local_readonly = true;
        break;
      }
    }
    if (!local_readonly) {
      return key.channel_id();
    }
  }
  return {};
}

// Find a channel_id where the writable target is the local node AND a specific replicate_index also
// maps to the local node (writable node doubles as a readonly replica). Used to test the
// "readonly replicate index resolves to local, but channel is writable" reuse path.
inline std::string find_writable_and_readonly_local_channel_id(const std::string& prefix, uint64_t local_id,
                                                               uint64_t& out_replicate_index, size_t max_trials = 200) {
  for (size_t i = 0; i < max_trials; ++i) {
    atfw::dtmq::DChannelIdKey key;
    key.set_channel_id(prefix + "-" + std::to_string(i));
    key.set_channel_type(kTestChannelType);
    uint64_t writable_sid = 0;
    writable_sid = rpc::dtmq::get_target_server_id(key, rpc::dtmq::replicate_type::kWritable, 0,
                                                   logic_hpa_discovery_select_mode::kTarget);
    if (writable_sid != local_id) {
      continue;
    }
    // Find a readonly index that also maps to local
    for (uint64_t ridx = 1; ridx <= 2; ++ridx) {
      uint64_t sid = 0;
      sid = rpc::dtmq::get_target_server_id(key, rpc::dtmq::replicate_type::kReadonly, ridx,
                                            logic_hpa_discovery_select_mode::kTarget);
      if (sid == local_id) {
        out_replicate_index = ridx;
        return prefix + "-" + std::to_string(i);
      }
    }
  }
  out_replicate_index = 0;
  return prefix + "-fallback";
}

// RAII guard that advances the global now offset (time_utility::now() and everything derived from it,
// including channel timers/subscriber expiry) and restores the previous offset on destruction. Task and
// runtime timeouts run on sys_now() and are not affected.
class global_now_offset_guard {
 public:
  explicit global_now_offset_guard(std::chrono::system_clock::duration advance_by)
      : previous_(atfw::util::time::time_utility::get_global_now_offset()) {
    atfw::util::time::time_utility::set_global_now_offset(previous_ + advance_by);
  }
  ~global_now_offset_guard() { atfw::util::time::time_utility::set_global_now_offset(previous_); }

  global_now_offset_guard(const global_now_offset_guard&) = delete;
  global_now_offset_guard& operator=(const global_now_offset_guard&) = delete;

 private:
  std::chrono::system_clock::duration previous_;
};

// mq_channel_manager 的定时器轮随单例跨 case 存活：之前 case 用 global_now_offset_guard 推进过
// mq_channel_manager::tick() 之后，jiffies_timer::last_tick_ 会停留在“未来”，后续 case 若用更小的
// 偏移调用 tick() 会因 expires <= last_tick_ 而直接返回 0（定时器不触发）。这里把场景偏移叠加上
// 已继承的未来 tick，保证每次 tick() 看到的时间单调递增。
inline std::chrono::system_clock::duration manager_tick_safe_offset(
    std::chrono::system_clock::duration scenario_offset) {
  // 与 mq_channel_manager.cpp 中 chrono_to_timer_tick 保持一致：128ms 一格。
  const time_t real_now_tick = static_cast<time_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(atfw::util::time::time_utility::now().time_since_epoch())
          .count() /
      128);
  const time_t inherited_tick = MqChannelManagerUnitTest::get_timer_last_tick(*mq_channel_manager::me());
  if (inherited_tick > real_now_tick) {
    // 向上取整到下一个 tick 边界，确保 now_tick 严格大于继承的 last_tick_。
    return scenario_offset + std::chrono::milliseconds((inherited_tick - real_now_tick + 1) * 128);
  }
  return scenario_offset;
}

}  // namespace dtmq_channel_test

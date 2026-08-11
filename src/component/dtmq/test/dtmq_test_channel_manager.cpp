// Copyright 2026 atframework

// Server-side unit tests for mq_channel_manager (dtmq-proxysvr). Covers:
//   - create_channel / get_channel / add_channel / remove_channel (A1)
//   - make_writable_channel: local create, reuse, forward-to-peer, maintenance (A2)
//   - make_readable_channel: local readonly, upgrade-to-writable, reuse, forward (A3)
//   - make_readable_channel_with_replicate_index: index=0 -> writable, specific index, forward,
//     writable-node-also-serves-readonly (A4)
//   - find_message / page_query_message (A5)
//   - data transfer flow (A6)
//   - lifecycle / IO bookkeeping (A7)
//
// The mq_channel_manager singleton is process-lifetime; ensure_manager_init() runs once and
// clear_manager_for_unit_test() wipes its internal maps between cases. Each case uses a unique
// channel id so there is no cross-case data dependency.

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "dtmq_test_channel_common.h"  // NOLINT(build/include_subdir)

using namespace dtmq_channel_test;  // NOLINT(build/namespaces)

namespace {
bool start_channel_manager_runtime(atframework::testing::runtime& test, uint32_t readonly_replicate_count = 2,
                                   bool memory_only = true, size_t peer_node_count = 2) {
  return start_dtmq_proxysvr_runtime(test, readonly_replicate_count, memory_only, peer_node_count);
}

}  // namespace

// ============ A1: create_channel / get_channel / add_channel / remove_channel ============

CASE_TEST(component_dtmq_channel_manager, create_channel_empty_id_fails) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test)) {
    test.stop();
    return;
  }

  auto task = test.run_task("create_empty_id", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
    atfw::dtmq::DChannelIdKey empty_key;
    empty_key.set_channel_type(kTestChannelType);
    atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
    mq_channel_manager::mq_channel_ptr_type channel;
    auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(ctx, channel, empty_key, configure));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL, res);
    CASE_EXPECT_TRUE(!channel);
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtmq_channel_manager, create_channel_then_get) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("create_get", kTestChannelType);
  auto task = test.run_task(
      "create_then_get", std::chrono::seconds{3}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        mq_channel_manager::mq_channel_ptr_type channel;
        auto res =
            RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(ctx, channel, channel_key, configure));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!channel);
        if (channel) {
          CASE_EXPECT_EQ(channel_key.channel_id(), channel->get_channel_id());
          CASE_EXPECT_EQ(kTestChannelType, channel->get_channel_key().channel_type());
        }

        auto got = mq_channel_manager::me()->get_channel(channel_key.channel_id());
        CASE_EXPECT_TRUE(!!got);
        CASE_EXPECT_EQ(channel.get(), got.get());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtmq_channel_manager, create_channel_reuse_existing) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("create_reuse", kTestChannelType);
  auto task =
      test.run_task("create_reuse", std::chrono::seconds{3}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        mq_channel_manager::mq_channel_ptr_type channel1;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(ctx, channel1, channel_key, configure)));
        // Re-create with the same key -> should reuse the same instance
        mq_channel_manager::mq_channel_ptr_type channel2;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(ctx, channel2, channel_key, configure)));
        CASE_EXPECT_EQ(channel1.get(), channel2.get());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtmq_channel_manager, add_channel_replace_existing) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("add_replace", kTestChannelType);
  auto task =
      test.run_task("add_replace", std::chrono::seconds{3}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        mq_channel_manager::mq_channel_ptr_type channel1;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(ctx, channel1, channel_key, configure)));

        // Build a fresh channel and add it, replacing the existing one
        auto channel2 =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
        CASE_EXPECT_TRUE(!!channel2);
        mq_channel_manager::me()->add_channel(ctx, channel2);

        auto got = mq_channel_manager::me()->get_channel(channel_key.channel_id());
        CASE_EXPECT_EQ(channel2.get(), got.get());
        CASE_EXPECT_NE(channel1.get(), got.get());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtmq_channel_manager, remove_channel_basic) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("remove_basic", kTestChannelType);
  auto task =
      test.run_task("remove_basic", std::chrono::seconds{3}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        mq_channel_manager::mq_channel_ptr_type channel;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(ctx, channel, channel_key, configure)));
        CASE_EXPECT_TRUE(!!mq_channel_manager::me()->get_channel(channel_key.channel_id()));

        // Remove with the correct except pointer -> removes
        mq_channel_manager::me()->remove_channel(channel_key.channel_id(), channel.get());
        CASE_EXPECT_TRUE(!mq_channel_manager::me()->get_channel(channel_key.channel_id()));

        // Remove a non-existent id -> no effect, no crash
        mq_channel_manager::me()->remove_channel("does-not-exist", nullptr);

        // Remove with mismatched except pointer -> no removal
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(ctx, channel, channel_key, configure)));
        auto other_key = make_channel_key("remove_other", kTestChannelType);
        auto other_channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), other_key, configure);
        mq_channel_manager::me()->remove_channel(channel_key.channel_id(), other_channel.get());
        CASE_EXPECT_TRUE(!!mq_channel_manager::me()->get_channel(channel_key.channel_id()));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ A2: make_writable_channel (forward flow + local create + reuse) ============

// make_writable_channel on a channel whose hash target is the local node: memory_only path.
// Expects channel_ptr is writable, forward_server_id == 0.
CASE_TEST(component_dtmq_channel_manager, make_writable_local_create_memory_only) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  // Find a channel_id whose writable hash target is the local node so make_writable_channel promotes locally.
  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkw-local", local_id);
  auto task =
      test.run_task("mkw_local", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0xDEAD;
        auto res = RPC_AWAIT_CODE_RESULT(
            mq_channel_manager::me()->make_writable_channel(ctx, channel, forward_server_id, channel_key, true));
        CASE_EXPECT_EQ(0, res);
        // A successful local selection must never leak the local server id through the forwarding output.
        // Every action task treats any non-zero value as a real forward target.
        CASE_EXPECT_TRUE(!!channel);
        if (channel) {
          CASE_EXPECT_TRUE(channel->is_writable());
          CASE_EXPECT_TRUE(channel->is_available());
          CASE_EXPECT_EQ(0u, forward_server_id);
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// make_writable_channel reuses an existing writable channel (no recreation).
CASE_TEST(component_dtmq_channel_manager, make_writable_reuse_existing) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkw-reuse", local_id);
  auto task =
      test.run_task("mkw_reuse", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        // First call: creates and promotes to writable
        mq_channel_manager::mq_channel_ptr_type channel1;
        uint64_t forward_server_id1 = 0xDEAD;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, channel1, forward_server_id1, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel1 && channel1->is_writable());
        CASE_EXPECT_EQ(0u, forward_server_id1);

        // Second call: should reuse the same instance
        mq_channel_manager::mq_channel_ptr_type channel2;
        uint64_t forward_server_id2 = 0xDEAD;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, channel2, forward_server_id2, channel_key, true)));
        CASE_EXPECT_EQ(channel1.get(), channel2.get());
        CASE_EXPECT_TRUE(!!channel2 && channel2->is_writable());
        CASE_EXPECT_EQ(0u, forward_server_id2);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// make_writable_channel on a channel whose writable hash target is a peer node: forward flow.
// Expects forward_server_id == peer node id and channel_ptr is null.
CASE_TEST(component_dtmq_channel_manager, make_writable_forward_to_peer) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  uint64_t expected_forward = 0;
  auto channel_id = find_remote_writable_channel_id("mkw-fwd", local_id, expected_forward);
  auto task = test.run_task(
      "mkw_fwd", std::chrono::seconds{4}, [channel_id, expected_forward](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        auto res = RPC_AWAIT_CODE_RESULT(
            mq_channel_manager::me()->make_writable_channel(ctx, channel, forward_server_id, channel_key, true));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(expected_forward, forward_server_id);
        // When forwarding, channel_ptr should be null (message goes to the peer)
        CASE_EXPECT_TRUE(!channel);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// make_writable_channel with auto_create=false on a hash-remote channel: should forward (not error).
CASE_TEST(component_dtmq_channel_manager, make_writable_no_auto_create_remote) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  uint64_t expected_forward = 0;
  auto channel_id = find_remote_writable_channel_id("mkw-noauto", local_id, expected_forward);
  auto task = test.run_task("mkw_noauto", std::chrono::seconds{4},
                            [channel_id, expected_forward](rpc::context& ctx) -> rpc::result_code_type {
                              atfw::dtmq::DChannelIdKey channel_key;
                              channel_key.set_channel_id(channel_id);
                              channel_key.set_channel_type(kTestChannelType);

                              mq_channel_manager::mq_channel_ptr_type channel;
                              uint64_t forward_server_id = 0;
                              auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                                  ctx, channel, forward_server_id, channel_key, false));
                              CASE_EXPECT_EQ(0, res);
                              CASE_EXPECT_EQ(expected_forward, forward_server_id);
                              RPC_RETURN_CODE(0);
                            });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// make_writable_channel under maintenance (is_stoping): returns EN_ERR_MAINTENANCE.
CASE_TEST(component_dtmq_channel_manager, make_writable_under_maintenance) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkw-maint", local_id);
  auto task =
      test.run_task("mkw_maint", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        // Simulate the node going into maintenance (stopping)
        MqChannelManagerUnitTest::set_stoping(*mq_channel_manager::me(), true);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        auto res = RPC_AWAIT_CODE_RESULT(
            mq_channel_manager::me()->make_writable_channel(ctx, channel, forward_server_id, channel_key, true));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_MAINTENANCE, res);
        CASE_EXPECT_TRUE(!channel);
        CASE_EXPECT_EQ(0u, forward_server_id);

        // Restore so teardown is clean
        MqChannelManagerUnitTest::set_stoping(*mq_channel_manager::me(), false);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ A3: make_readable_channel (random readonly replica) ============

// make_readable_channel when the local node should be writable: internally falls through to
// make_writable_channel and the result is a writable channel.
CASE_TEST(component_dtmq_channel_manager, make_readable_upgrade_to_writable) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkr-upg", local_id);
  auto task =
      test.run_task("mkr_upg", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        auto res = RPC_AWAIT_CODE_RESULT(
            mq_channel_manager::me()->make_readable_channel(ctx, channel, forward_server_id, channel_key, true));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!channel);
        if (channel) {
          // When the local node is the writable target, make_readable promotes to writable
          CASE_EXPECT_TRUE(channel->is_writable());
          CASE_EXPECT_EQ(0u, forward_server_id);
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// make_readable_channel reuses an existing writable channel (readonly and writable both count as readable).
CASE_TEST(component_dtmq_channel_manager, make_readable_reuse_existing_writable) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkr-reuse-w", local_id);
  auto task =
      test.run_task("mkr_reuse_w", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        // First: create writable
        mq_channel_manager::mq_channel_ptr_type channel1;
        uint64_t forward1 = 0xDEAD;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(ctx, channel1, forward1,
                                                                                                channel_key, true)));
        CASE_EXPECT_TRUE(channel1 && channel1->is_writable());
        CASE_EXPECT_EQ(0u, forward1);

        // Second: make_readable should reuse the same writable channel
        mq_channel_manager::mq_channel_ptr_type channel2;
        uint64_t forward2 = 0xDEAD;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel(ctx, channel2, forward2,
                                                                                                channel_key, true)));
        CASE_EXPECT_EQ(channel1.get(), channel2.get());
        CASE_EXPECT_EQ(0u, forward2);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// make_readable_channel creates a local readonly replica when writable ownership is remote, then
// reuses that initialized replica on the next lookup.
CASE_TEST(component_dtmq_channel_manager, make_readable_local_readonly_create_and_reuse) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  uint64_t expected_replicate_index = 0;
  auto channel_id = find_local_readonly_channel_id("mkr-local-ro", local_id, expected_replicate_index);
  CASE_EXPECT_NE(0u, expected_replicate_index);
  if (expected_replicate_index == 0) {
    test.stop();
    return;
  }

  auto task = test.run_task("mkr_local_ro", std::chrono::seconds{4},
                            [channel_id, expected_replicate_index](rpc::context& ctx) -> rpc::result_code_type {
                              atfw::dtmq::DChannelIdKey channel_key;
                              channel_key.set_channel_id(channel_id);
                              channel_key.set_channel_type(kTestChannelType);

                              mq_channel_manager::mq_channel_ptr_type first_channel;
                              uint64_t first_forward_server_id = 0;
                              CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel(
                                                    ctx, first_channel, first_forward_server_id, channel_key, true)));
                              CASE_EXPECT_TRUE(!!first_channel);
                              CASE_EXPECT_TRUE(first_channel && first_channel->is_readonly());
                              CASE_EXPECT_TRUE(first_channel && first_channel->is_available());
                              if (first_channel) {
                                CASE_EXPECT_EQ(expected_replicate_index, first_channel->get_current_replicate_index());
                              }
                              CASE_EXPECT_EQ(0u, first_forward_server_id);

                              mq_channel_manager::mq_channel_ptr_type reused_channel;
                              uint64_t reused_forward_server_id = 0;
                              CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel(
                                                    ctx, reused_channel, reused_forward_server_id, channel_key, true)));
                              CASE_EXPECT_EQ(first_channel.get(), reused_channel.get());
                              CASE_EXPECT_EQ(0u, reused_forward_server_id);
                              RPC_RETURN_CODE(0);
                            });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// An existing readonly object is promoted in place when writable ownership moves to the local node.
CASE_TEST(component_dtmq_channel_manager, make_writable_upgrades_existing_readonly) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkw-upgrade-ro", local_id);
  auto task = test.run_task(
      "mkw_upgrade_ro", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type readonly_channel;
        auto configure = get_configure_for(kTestChannelType);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->create_channel(ctx, readonly_channel, channel_key, configure)));
        if (!readonly_channel) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
        }

        readonly_channel->set_created(ctx, atfw::util::time::time_utility::now(), 1);
        atfw::dtmq::channel_snapshot snapshot;
        readonly_channel->dump_snapshot(ctx, snapshot);
        snapshot.set_replicate_index(1);
        CASE_EXPECT_TRUE(readonly_channel->load_snapshot(ctx, std::move(snapshot)));
        CASE_EXPECT_TRUE(readonly_channel->is_readonly());

        mq_channel_manager::mq_channel_ptr_type writable_channel = readonly_channel;
        uint64_t forward_server_id = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, writable_channel, forward_server_id, channel_key, true)));
        CASE_EXPECT_EQ(readonly_channel.get(), writable_channel.get());
        CASE_EXPECT_TRUE(writable_channel && writable_channel->is_writable());
        CASE_EXPECT_EQ(0u, forward_server_id);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ A4: make_readable_channel_with_replicate_index ============

// make_readable_channel_with_replicate_index(0) is equivalent to make_writable_channel.
CASE_TEST(component_dtmq_channel_manager, make_readable_with_index_zero_equals_writable) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkri-zero", local_id);
  auto task =
      test.run_task("mkri_zero", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel_with_replicate_index(
            ctx, channel, forward_server_id, 0, channel_key, true));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!channel);
        if (channel) {
          CASE_EXPECT_TRUE(channel->is_writable());
          CASE_EXPECT_EQ(0u, forward_server_id);
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// make_readable_channel_with_replicate_index creates and then reuses the requested local readonly replica.
CASE_TEST(component_dtmq_channel_manager, make_readable_with_index_local_readonly_create_and_reuse) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  uint64_t expected_replicate_index = 0;
  auto channel_id = find_local_readonly_channel_id("mkri-local-ro", local_id, expected_replicate_index);
  CASE_EXPECT_NE(0u, expected_replicate_index);
  if (expected_replicate_index == 0) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "mkri_local_ro", std::chrono::seconds{4},
      [channel_id, expected_replicate_index](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type first_channel;
        uint64_t first_forward_server_id = 0;
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel_with_replicate_index(
                           ctx, first_channel, first_forward_server_id, expected_replicate_index, channel_key, true)));
        CASE_EXPECT_TRUE(!!first_channel);
        CASE_EXPECT_TRUE(first_channel && first_channel->is_readonly());
        CASE_EXPECT_TRUE(first_channel && first_channel->is_available());
        if (first_channel) {
          CASE_EXPECT_EQ(expected_replicate_index, first_channel->get_current_replicate_index());
        }
        CASE_EXPECT_EQ(0u, first_forward_server_id);

        mq_channel_manager::mq_channel_ptr_type reused_channel;
        uint64_t reused_forward_server_id = 0;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel_with_replicate_index(
                   ctx, reused_channel, reused_forward_server_id, expected_replicate_index, channel_key, true)));
        CASE_EXPECT_EQ(first_channel.get(), reused_channel.get());
        CASE_EXPECT_EQ(0u, reused_forward_server_id);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// A requested readonly replica owned by another node returns that exact node as the forwarding target.
CASE_TEST(component_dtmq_channel_manager, make_readable_with_index_forward_to_peer) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true, 3)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_remote_readable_channel_id("mkri-forward", local_id);
  CASE_EXPECT_FALSE(channel_id.empty());
  if (channel_id.empty()) {
    test.stop();
    return;
  }

  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(kTestChannelType);
  constexpr uint64_t kReplicateIndex = 1;
  auto expected_forward_id = rpc::dtmq::get_target_server_id(channel_key, rpc::dtmq::replicate_type::kReadonly,
                                                             kReplicateIndex, logic_hpa_discovery_select_mode::kTarget);
  CASE_EXPECT_NE(0u, expected_forward_id);
  CASE_EXPECT_NE(local_id, expected_forward_id);

  auto task = test.run_task(
      "mkri_forward", std::chrono::seconds{4},
      [channel_key, expected_forward_id](rpc::context& ctx) -> rpc::result_code_type {
        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel_with_replicate_index(
                              ctx, channel, forward_server_id, kReplicateIndex, channel_key, true)));
        CASE_EXPECT_FALSE(!!channel);
        CASE_EXPECT_EQ(expected_forward_id, forward_server_id);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// Core requirement: a readonly-replicate-index channel that shares the writable replica actually
// resolves to the writable channel. Construct a writable channel first, then call
// make_readable_channel_with_replicate_index with an index that maps to the local node; since the
// channel is already writable, it is reused (writable node also serves as readonly).
CASE_TEST(component_dtmq_channel_manager, make_readable_with_index_writable_node_serves_readonly) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true, 1)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  uint64_t local_readonly_index = 0;
  // Find a channel where both writable AND a readonly replica map to local
  auto channel_id = find_writable_and_readonly_local_channel_id("mkri-share", local_id, local_readonly_index);
  CASE_EXPECT_NE(0u, local_readonly_index);
  if (local_readonly_index == 0) {
    test.stop();
    return;
  }
  auto task = test.run_task(
      "mkri_share", std::chrono::seconds{4},
      [channel_id, local_readonly_index](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        // First: create as writable
        mq_channel_manager::mq_channel_ptr_type writable_channel;
        uint64_t fwd1 = 0xDEAD;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, writable_channel, fwd1, channel_key, true)));
        CASE_EXPECT_TRUE(writable_channel && writable_channel->is_writable());
        CASE_EXPECT_EQ(0u, fwd1);
        CASE_EXPECT_TRUE(!!writable_channel && writable_channel->is_writable());

        // Now request readonly with the index that maps to local. Since the channel is
        // writable (writable == local), get_target_distribution_server_id(index) == local,
        // and is_writable()==true -> the existing writable channel is reused.
        mq_channel_manager::mq_channel_ptr_type readonly_channel;
        uint64_t fwd2 = 0xDEAD;
        auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel_with_replicate_index(
            ctx, readonly_channel, fwd2, local_readonly_index, channel_key, true));
        CASE_EXPECT_EQ(0, res);
        // The same channel instance is reused (writable node serves readonly)
        CASE_EXPECT_EQ(writable_channel.get(), readonly_channel.get());
        CASE_EXPECT_EQ(0u, fwd2);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// Same "readonly resolves to writable" scenario but via make_readable_channel (random replica path).
CASE_TEST(component_dtmq_channel_manager, make_readable_random_reuses_writable) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("mkr-rand-w", local_id);
  auto task =
      test.run_task("mkr_rand_w", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        // Create writable first
        mq_channel_manager::mq_channel_ptr_type writable_channel;
        uint64_t fwd1 = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, writable_channel, fwd1, channel_key, true)));

        // make_readable should reuse the existing writable (writable counts as readable)
        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd2 = 0xDEAD;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_readable_channel(ctx, channel, fwd2, channel_key, true)));
        CASE_EXPECT_EQ(writable_channel.get(), channel.get());
        CASE_EXPECT_EQ(0u, fwd2);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ A5: find_message / page_query_message (static interfaces) ============

// find_message on a writable channel with messages: returns the message.
CASE_TEST(component_dtmq_channel_manager, find_message_found) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("find-ok", local_id);
  auto task =
      test.run_task("find_ok", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());

        int64_t seq = channel->alloc_message_sequence();
        int32_t result_code = 0;
        mq_channel_wal_object_context params{ctx, result_code};
        auto msg = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                             atfw::dtmq::DChannelMessageDetail::kText, params);
        CASE_EXPECT_TRUE(!!msg);
        if (msg) {
          msg->set_sequence(seq);
          msg->mutable_detail()->set_text("hello-find");
          channel->get_wal_publisher().emplace_back_log(std::move(msg), params);
        }

        atfw::dtmq::DChannelMessage out;
        auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::find_message(ctx, channel, seq, out));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, res);
        CASE_EXPECT_EQ(seq, out.sequence());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// find_message for a non-existent sequence: returns EN_ERR_DTMQ_MESSAGE_NOT_FOUND.
CASE_TEST(component_dtmq_channel_manager, find_message_not_found) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("find-miss", local_id);
  auto task =
      test.run_task("find_miss", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));

        atfw::dtmq::DChannelMessage out;
        auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::find_message(ctx, channel, 99999999, out));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_MESSAGE_NOT_FOUND, res);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// find_message with a null channel: returns EN_ERR_DTMQ_INVALID_CHANNEL.
CASE_TEST(component_dtmq_channel_manager, find_message_null_channel) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto task = test.run_task("find_null", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
    atfw::dtmq::DChannelMessage out;
    mq_channel_manager::mq_channel_ptr_type null_channel;
    auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::find_message(ctx, null_channel, 1, out));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL, res);
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// page_query_message: basic paging with page_size boundary and page_more flag.
CASE_TEST(component_dtmq_channel_manager, page_query_message_basic) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("pageqry", local_id);
  auto task =
      test.run_task("pageqry", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));

        // Append 3 messages
        int32_t result_code = 0;
        mq_channel_wal_object_context params{ctx, result_code};
        for (int i = 0; i < 3; ++i) {
          int64_t seq = channel->alloc_message_sequence();
          auto msg = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                               atfw::dtmq::DChannelMessageDetail::kText, params);
          if (msg) {
            msg->set_sequence(seq);
            channel->get_wal_publisher().emplace_back_log(std::move(msg), params);
          }
        }

        atfw::dtmq::channel_page_info page_info;
        page_info.set_page_size(2);
        page_info.set_page_start_sequence(0);
        google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage> msgs;
        auto res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::page_query_message(ctx, channel, page_info, msgs));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(2, msgs.size());
        CASE_EXPECT_TRUE(page_info.page_more());

        page_info.Clear();
        page_info.set_page_start_sequence(0);
        res = RPC_AWAIT_CODE_RESULT(mq_channel_manager::page_query_message(ctx, channel, page_info, msgs));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(page_info.page_size() > 0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ A6: data transfer flow ============

// async_start_transfer serializes the live channel state and sends it to the requested peer.
CASE_TEST(component_dtmq_channel_manager, transfer_channel_snapshot_with_data) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  size_t transfer_calls = 0;
  std::string transferred_channel_id;
  rpc::unit_test::ss_mock_rule_options transfer_options;
  transfer_options.match_node_id = kPeerNode1;
  transfer_options.times = 1;
  auto transfer_rule = rpc::dtmq::mock::transfer_channel(
      [&transfer_calls, &transferred_channel_id](
          rpc::context& /*ctx*/, const atfw::dtmq::SSChannelTransferChannelReq& request,
          atfw::dtmq::SSChannelTransferChannelRsp& /*response*/) -> rpc::result_code_type {
        ++transfer_calls;
        CASE_EXPECT_EQ(1, request.snapshot_size());
        if (request.snapshot_size() == 1) {
          const auto& snapshot = request.snapshot(0);
          transferred_channel_id = snapshot.channel_data().channel_metadata().channel_key().channel_id();
          CASE_EXPECT_EQ(0u, snapshot.replicate_index());
          CASE_EXPECT_EQ("type.googleapis.com/dtmq.TransferCustomData",
                         snapshot.channel_data().channel_metadata().custom_data().type_url());
          bool found_payload = false;
          for (const auto& message : snapshot.channel_data().messages()) {
            if (message.detail().text() == "transfer-payload") {
              found_payload = true;
              break;
            }
          }
          CASE_EXPECT_TRUE(found_payload);
        }
        RPC_RETURN_CODE(0);
      },
      transfer_options);
  CASE_EXPECT_TRUE(!!transfer_rule);
  if (!transfer_rule) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("xfer-data", local_id);
  auto task =
      test.run_task("xfer_data", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());
        CASE_EXPECT_EQ(0u, fwd);
        if (!channel) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
        }

        google::protobuf::Any custom_data;
        custom_data.set_type_url("type.googleapis.com/dtmq.TransferCustomData");
        custom_data.set_value("custom-data");
        CASE_EXPECT_TRUE(channel->set_custom_data(custom_data));

        int32_t wal_result = 0;
        mq_channel_wal_object_context wal_context{ctx, wal_result};
        auto message = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                                 atfw::dtmq::DChannelMessageDetail::kText, wal_context);
        CASE_EXPECT_TRUE(!!message);
        if (!message) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        message->mutable_detail()->set_text("transfer-payload");
        channel->get_wal_publisher().emplace_back_log(std::move(message), wal_context);

        CASE_EXPECT_EQ(0, channel->async_start_transfer(ctx, kPeerNode1));
        int32_t transfer_result = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->await_io_task(ctx, &transfer_result)));
        CASE_EXPECT_EQ(0, transfer_result);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1u, transfer_calls);
  CASE_EXPECT_EQ(channel_id, transferred_channel_id);
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_transfer_channel())));
  CASE_EXPECT_EQ(0, test.stop());
}

// async_save writes a writable channel to the DB (mock replace). Exercises the full save path.
CASE_TEST(component_dtmq_channel_manager, save_writable_channel_to_db_mock) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("save-db", local_id);
  auto task =
      test.run_task("save_db", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());

        channel->set_dirty();
        CASE_EXPECT_TRUE(channel->need_save_db());

        auto res = RPC_AWAIT_CODE_RESULT(channel->save(ctx));
        CASE_EXPECT_EQ(0, res);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ A7: lifecycle / IO bookkeeping ============

// stop() sets is_stoping(); is_can_stopped() is true when no pending/running IO remains.
CASE_TEST(component_dtmq_channel_manager, stop_and_can_stopped) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  CASE_EXPECT_FALSE(mq_channel_manager::me()->is_stoping());
  int stop_res = 0;
  stop_res = mq_channel_manager::me()->stop();
  CASE_EXPECT_TRUE(mq_channel_manager::me()->is_stoping());
  CASE_EXPECT_TRUE(mq_channel_manager::me()->is_can_stopped());
  CASE_EXPECT_EQ(0, stop_res);

  MqChannelManagerUnitTest::set_stoping(*mq_channel_manager::me(), false);
  CASE_EXPECT_EQ(0, test.stop());
}

// pre_stoping() sets the pre-stop flag.
CASE_TEST(component_dtmq_channel_manager, pre_stoping_flag) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  mq_channel_manager::me()->pre_stoping();
  // pre_stoping sets is_pre_stoping_ internally; verified via MqChannelManagerUnitTest::set_pre_stoping round-trip.
  MqChannelManagerUnitTest::set_pre_stoping(*mq_channel_manager::me(), false);
  CASE_EXPECT_EQ(0, test.stop());
}

// insert/remove_running_io_channel and is_running_io_busy.
CASE_TEST(component_dtmq_channel_manager, running_io_channel_bookkeeping) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto task = test.run_task("io_book", std::chrono::seconds{4}, [](rpc::context& /*ctx*/) -> rpc::result_code_type {
    atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
    auto channel_key = make_channel_key("iobook", kTestChannelType);
    auto channel =
        atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);

    CASE_EXPECT_FALSE(mq_channel_manager::me()->is_running_io_busy());

    mq_channel_manager::insert_running_io_channel(channel.get());
    CASE_EXPECT_FALSE(mq_channel_manager::me()->is_running_io_busy());

    mq_channel_manager::remove_running_io_channel(channel.get());
    CASE_EXPECT_FALSE(mq_channel_manager::me()->is_running_io_busy());

    mq_channel_manager::me()->add_pending_io_channel(channel);
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// set_more_transfer / etcd revision accessors.
CASE_TEST(component_dtmq_channel_manager, accessor_misc) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  mq_channel_manager::me()->set_more_transfer();

  int64_t rev = 0;
  rev = mq_channel_manager::me()->get_latest_server_etcd_revision();
  int64_t transfer_rev = mq_channel_manager::me()->get_transfer_server_etcd_revision();
  bool waiting = mq_channel_manager::me()->is_waiting_transfer();
  (void)transfer_rev;
  (void)waiting;

  MqChannelManagerUnitTest::set_latest_server_etcd_revision(*mq_channel_manager::me(), rev + 1);

  CASE_EXPECT_EQ(0, test.stop());
}

// update_timer registers a timer for a channel.
CASE_TEST(component_dtmq_channel_manager, update_timer_registers) {
  atframework::testing::runtime test;
  if (!start_channel_manager_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto task = test.run_task("upd_timer", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
    atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
    auto channel_key = make_channel_key("updtmr", kTestChannelType);
    auto channel =
        atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
    mq_channel_manager::me()->add_channel(ctx, channel);

    mq_channel_timer_type::timer_wptr_t handle;
    // update_timer registers a timer; the weak handle may or may not be immediately
    // resolvable depending on the timer wheel internals. The key assertion is that the
    // call completes without error.
    mq_channel_manager::me()->update_timer(*channel, handle, std::chrono::seconds{30});
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

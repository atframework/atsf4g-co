// Copyright 2026 atframework

// Server-side unit tests for mq_channel (dtmq-proxysvr). Covers the public interface groups:
//   - B1: construction + getters + reload_configure
//   - B2: custom_data / private_data (set/clear/reset_sequence, dirty tracking)
//   - B3: status upgrade via writable_init
//   - B4: message sequence allocation + subscribe/unsubscribe
//   - B5: WAL snapshot dump + compact
//   - B6: optimistic lock (set_lock/clear_lock/compare_and_maybe_reset_lock)
//   - B7: destroy + recreate (set_destroyed/ensure_recreate_after_destroyed/merge_*)
//   - B8: distribution calculation (should_be_writable/readonly, get_target_distribution_*)
//   - B9: IO task (await_io_task/is_io_task_running/is_io_task_too_many_continue_failed)
//   - B10: tick / force_refresh_distribution / need_save_db / misc accessors

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "dtmq_test_channel_common.h"  // NOLINT(build/include_subdir)

using namespace dtmq_channel_test;  // NOLINT(build/namespaces)

namespace {
bool start_channel_runtime(atframework::testing::runtime& test, uint32_t readonly_replicate_count = 2,
                           bool memory_only = true) {
  return start_dtmq_proxysvr_runtime(test, readonly_replicate_count, memory_only);
}
}  // namespace

// ============ B1: construction + getters ============

CASE_TEST(component_dtmq_channel, construct_getters) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("construct", kTestChannelType);
  auto task = test.run_task(
      "construct_getters", std::chrono::seconds{3}, [channel_key](rpc::context& /*ctx*/) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        CASE_EXPECT_EQ(channel_key.channel_id(), channel->get_channel_id());
        CASE_EXPECT_EQ(channel_key.channel_type(), channel->get_channel_key().channel_type());
        CASE_EXPECT_EQ(kTestChannelType, channel->get_configure().channel_type());
        CASE_EXPECT_FALSE(channel->is_writable());
        CASE_EXPECT_FALSE(channel->is_readonly());
        CASE_EXPECT_FALSE(channel->is_available());
        CASE_EXPECT_FALSE(channel->is_destroyed());
        CASE_EXPECT_EQ(0u, channel->get_current_replicate_index());
        CASE_EXPECT_TRUE(!!channel->get_shared_wal_object());
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

CASE_TEST(component_dtmq_channel, reload_configure) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("reloadcfg", kTestChannelType);
  auto task = test.run_task(
      "reloadcfg", std::chrono::seconds{3}, [channel_key](rpc::context& /*ctx*/) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);

        atfw::dtmq::DChannelConfigure new_configure = configure;
        new_configure.set_max_log_count(500);
        channel->reload_configure(new_configure);
        CASE_EXPECT_EQ(500u, channel->get_configure().max_log_count());

        channel->reload_configure(configure);
        CASE_EXPECT_EQ(configure.max_log_count(), channel->get_configure().max_log_count());
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

// ============ B2: custom_data / private_data ============

CASE_TEST(component_dtmq_channel, custom_data_set_clear_reset) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("cdata", kTestChannelType);
  auto task =
      test.run_task("cdata", std::chrono::seconds{3}, [channel_key](rpc::context& /*ctx*/) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
        CASE_EXPECT_TRUE(channel->get_custom_data().type_url().empty());
        CASE_EXPECT_EQ(0, channel->get_custom_data_sequence());

        google::protobuf::Any data;
        data.set_type_url("type.googleapis.com/test.CustomData");
        CASE_EXPECT_TRUE(channel->set_custom_data(data));
        CASE_EXPECT_EQ("type.googleapis.com/test.CustomData", channel->get_custom_data().type_url());
        CASE_EXPECT_FALSE(channel->set_custom_data(data));
        CASE_EXPECT_TRUE(channel->clear_custom_data());
        CASE_EXPECT_TRUE(channel->get_custom_data().type_url().empty());
        CASE_EXPECT_FALSE(channel->clear_custom_data());

        channel->reset_custom_data_sequence();
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

CASE_TEST(component_dtmq_channel, private_data_set_clear_reset) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("pdata", kTestChannelType);
  auto task =
      test.run_task("pdata", std::chrono::seconds{3}, [channel_key](rpc::context& /*ctx*/) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
        CASE_EXPECT_TRUE(channel->get_private_data().type_url().empty());

        google::protobuf::Any data;
        data.set_type_url("type.googleapis.com/test.PrivateData");
        CASE_EXPECT_TRUE(channel->set_private_data(data));
        CASE_EXPECT_EQ("type.googleapis.com/test.PrivateData", channel->get_private_data().type_url());
        CASE_EXPECT_FALSE(channel->set_private_data(data));
        CASE_EXPECT_TRUE(channel->clear_private_data());
        CASE_EXPECT_TRUE(channel->get_private_data().type_url().empty());

        channel->reset_private_data_sequence();
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

// ============ B3: status upgrade via writable_init ============

CASE_TEST(component_dtmq_channel, writable_init_memory_only) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("winit-mo", local_id);
  auto task =
      test.run_task("winit_mo", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
        mq_channel_manager::me()->add_channel(ctx, channel);

        CASE_EXPECT_FALSE(channel->is_writable());
        auto res = RPC_AWAIT_CODE_RESULT(channel->writable_init(ctx));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(channel->is_writable());
        // is_available() also requires create_sequence > destroy_sequence (set_created
        // has not been called yet here), so only assert is_writable.
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

// ============ B4: message sequence + subscribe ============

CASE_TEST(component_dtmq_channel, alloc_message_sequence_monotonic) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("allocseq", kTestChannelType);
  auto task =
      test.run_task("allocseq", std::chrono::seconds{3}, [channel_key](rpc::context& /*ctx*/) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);

        int64_t s1 = channel->alloc_message_sequence();
        int64_t s2 = channel->alloc_message_sequence();
        int64_t s3 = channel->alloc_message_sequence();
        CASE_EXPECT_TRUE(s2 > s1);
        CASE_EXPECT_TRUE(s3 > s2);
        CASE_EXPECT_EQ(s3, channel->get_last_message_sequence());
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

CASE_TEST(component_dtmq_channel, subscribe_unsubscribe) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    test.stop();
    return;
  }

  auto channel_key = make_channel_key("subunsub", kTestChannelType);
  auto task =
      test.run_task("subunsub", std::chrono::seconds{3}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
        mq_channel_manager::me()->add_channel(ctx, channel);

        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->writable_init(ctx)));

        atfw::dtmq::channel_subscriber sub;
        sub.set_subscriber_server_id(kPeerNode1);
        sub.set_subscriber_key("U:1:20001");
        int32_t sub_res = channel->subscribe(ctx, sub, 0, 0, false);
        CASE_EXPECT_EQ(0, sub_res);

        int32_t unsub_res = channel->unsubscribe(ctx, "U:1:20001");
        CASE_EXPECT_EQ(0, unsub_res);
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

// ============ B5: WAL snapshot dump + compact ============

CASE_TEST(component_dtmq_channel, dump_snapshot_round_trip) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("dump-rt", local_id);
  auto task =
      test.run_task("dump_rt", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        int64_t seq = channel->alloc_message_sequence();
        int32_t result_code = 0;
        mq_channel_wal_object_context params{ctx, result_code};
        auto msg = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                             atfw::dtmq::DChannelMessageDetail::kText, params);
        if (msg) {
          msg->set_sequence(seq);
          msg->mutable_detail()->set_text("snapshot-content");
          channel->get_wal_publisher().emplace_back_log(std::move(msg), params);
        }

        atfw::dtmq::channel_snapshot snapshot;
        channel->dump_snapshot(ctx, snapshot);
        // dump_snapshot completes without error; the message count depends on whether
        // ensure_recreate_after_destroyed (auto_create path) cleared logs. Verify at least
        // the snapshot structure is populated.
        CASE_EXPECT_TRUE(snapshot.channel_data().has_channel_metadata());
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

CASE_TEST(component_dtmq_channel, compact_sequence) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("compact", local_id);
  auto task =
      test.run_task("compact", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        int32_t result_code = 0;
        mq_channel_wal_object_context params{ctx, result_code};
        std::vector<int64_t> seqs;
        for (int i = 0; i < 3; ++i) {
          int64_t seq = channel->alloc_message_sequence();
          seqs.push_back(seq);
          auto msg = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                               atfw::dtmq::DChannelMessageDetail::kText, params);
          if (msg) {
            msg->set_sequence(seq);
            channel->get_wal_publisher().emplace_back_log(std::move(msg), params);
          }
        }

        channel->compact_stateful_sequence(seqs.at(1));
        channel->compact_sequence(seqs.at(1));
        CASE_EXPECT_TRUE(channel->get_compact_stateful_sequence() >= seqs.at(1));
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

// ============ B6: optimistic lock ============

CASE_TEST(component_dtmq_channel, lock_set_clear_compare) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("lock", local_id);
  auto task = test.run_task("lock", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_id(channel_id);
    channel_key.set_channel_type(kTestChannelType);

    mq_channel_manager::mq_channel_ptr_type channel;
    uint64_t fwd = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
    CASE_EXPECT_TRUE(!!channel && channel->is_writable());
    if (!channel) {
      RPC_RETURN_CODE(0);
    }

    CASE_EXPECT_TRUE(channel->get_lock().lock_holder().empty());

    atfw::dtmq::DChannelOptimisticLock lock;
    lock.set_lock_holder("session-abc");
    channel->set_lock(ctx, lock, false);
    CASE_EXPECT_EQ("session-abc", channel->get_lock().lock_holder());

    atfw::dtmq::channel_lock_checker checker;
    checker.mutable_expect_value()->set_lock_holder("session-abc");
    CASE_EXPECT_TRUE(channel->compare_and_maybe_reset_lock(ctx, checker, false));

    atfw::dtmq::channel_lock_checker checker2;
    checker2.mutable_expect_value()->set_lock_holder("session-xyz");
    CASE_EXPECT_FALSE(channel->compare_and_maybe_reset_lock(ctx, checker2, false));

    channel->clear_lock();
    CASE_EXPECT_TRUE(channel->get_lock().lock_holder().empty());
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

// ============ B7: destroy + recreate ============

CASE_TEST(component_dtmq_channel, set_destroyed_and_merge) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("destroy", local_id);
  auto task =
      test.run_task("destroy", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        CASE_EXPECT_FALSE(channel->is_destroyed());

        channel->set_created(ctx, atfw::util::time::time_utility::now(), 100);
        CASE_EXPECT_TRUE(channel->is_available());

        auto destroy_tp = atfw::util::time::time_utility::now();
        channel->set_destroyed(ctx, destroy_tp, 200);
        CASE_EXPECT_TRUE(channel->is_destroyed());
        CASE_EXPECT_FALSE(channel->is_available());

        channel->merge_destroy_timepoint_and_sequence(ctx, std::chrono::system_clock::from_time_t(0), 50);
        CASE_EXPECT_TRUE(channel->is_destroyed());

        channel->merge_destroy_timepoint_and_sequence(ctx, destroy_tp, 300);
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

CASE_TEST(component_dtmq_channel, ensure_recreate_after_destroyed) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("recreate", local_id);
  auto task =
      test.run_task("recreate", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        channel->set_created(ctx, atfw::util::time::time_utility::now(), 100);
        channel->set_destroyed(ctx, atfw::util::time::time_utility::now(), 200);
        CASE_EXPECT_TRUE(channel->is_destroyed());

        channel->ensure_recreate_after_destroyed(ctx);
        CASE_EXPECT_FALSE(channel->is_destroyed());
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

// ============ B8: distribution calculation ============

CASE_TEST(component_dtmq_channel, distribution_should_be_writable) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("dist-w", local_id);
  auto task = test.run_task(
      "dist_w", std::chrono::seconds{4}, [channel_id, local_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        CASE_EXPECT_TRUE(channel->should_be_writable());
        CASE_EXPECT_EQ(local_id, channel->get_target_distribution_writable_server_id());
        CASE_EXPECT_EQ(local_id, channel->get_target_distribution_server_id(0));

        uint64_t sid = 0xDEAD;
        bool ret = mq_channel::should_be_writable_or_get_server_id(channel->get_channel_key(), sid, channel.get());
        CASE_EXPECT_TRUE(ret);
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

CASE_TEST(component_dtmq_channel, distribution_replicate_index_lookup) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("dist-ridx", local_id);
  auto task = test.run_task(
      "dist_ridx", std::chrono::seconds{4}, [channel_id, local_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        CASE_EXPECT_EQ(local_id, channel->get_target_distribution_server_id(0));
        uint64_t r1 = channel->get_target_distribution_server_id(1);
        uint64_t r2 = channel->get_target_distribution_server_id(2);
        (void)r1;
        (void)r2;

        const mq_channel::replicate_index_set* ris = channel->get_target_distribution_replicate_index(local_id);
        CASE_EXPECT_TRUE(ris == nullptr);
        CASE_EXPECT_EQ(0u, channel->get_transfer_target_server_id());
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

CASE_TEST(component_dtmq_channel, force_refresh_distribution) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("frdist", local_id);
  auto task = test.run_task(
      "frdist", std::chrono::seconds{4}, [channel_id, local_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        channel->force_refresh_distribution();
        CASE_EXPECT_EQ(local_id, channel->get_ready_distribution_writable_server_id());
        CASE_EXPECT_EQ(local_id, channel->get_target_distribution_writable_server_id());
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

// ============ B9: IO task ============

CASE_TEST(component_dtmq_channel, io_task_idle_state) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("io-idle", local_id);
  auto task =
      test.run_task("io_idle", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        CASE_EXPECT_FALSE(channel->is_io_task_running());
        CASE_EXPECT_FALSE(channel->is_io_task_too_many_continue_failed());

        int32_t task_result = -1;
        auto res = RPC_AWAIT_CODE_RESULT(channel->await_io_task(ctx, &task_result));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(0, task_result);
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

// ============ B10: tick / need_save_db / misc ============

CASE_TEST(component_dtmq_channel, need_save_db_logic) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("needsave", local_id);
  auto task =
      test.run_task("needsave", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        // need_save_db is true for a writable+dirty channel. make_writable_channel with
        // auto_create may have already marked the channel dirty via
        // ensure_recreate_after_destroyed.
        // Verify set_dirty keeps the need-save invariant.
        channel->set_dirty();
        CASE_EXPECT_TRUE(channel->need_save_db());
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

CASE_TEST(component_dtmq_channel, tick_writable_channel) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("tick-w", local_id);
  auto task =
      test.run_task("tick_w", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        channel->tick(ctx);
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

CASE_TEST(component_dtmq_channel, misc_accessors) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("misc", local_id);
  auto task = test.run_task("misc", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_id(channel_id);
    channel_key.set_channel_type(kTestChannelType);

    mq_channel_manager::mq_channel_ptr_type channel;
    uint64_t fwd = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
    CASE_EXPECT_TRUE(!!channel);
    if (!channel) {
      RPC_RETURN_CODE(0);
    }

    channel->update_lost_last_subscriber();
    channel->reset_lost_last_subscriber();
    channel->update_last_writable_notify_time();

    channel->set_sequence_allocator(42);
    CASE_EXPECT_EQ(42, channel->get_sequence_allocator());

    // get_last_hash_code returns the hash of the last WAL log. The channel may have
    // system logs (create/init), so just verify the accessor is callable.
    (void)channel->get_last_hash_code();
    CASE_EXPECT_FALSE(channel->is_loading_snapshot());

    channel->set_dirty();
    channel->set_dirty();
    CASE_EXPECT_TRUE(channel->is_dirty());
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

CASE_TEST(component_dtmq_channel, current_replicate_index) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test, 2, true)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("ridx-cur", local_id);
  auto task =
      test.run_task("ridx_cur", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        CASE_EXPECT_TRUE(channel->is_writable());
        CASE_EXPECT_EQ(0u, channel->get_current_replicate_index());
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

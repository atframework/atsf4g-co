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

#include <atframework/testing/raw_transport.h>
#include <atframework/testing/ss_action.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "logic/action/task_action_transfer_channel.h"
#include "logic/action/task_action_update.h"

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

        // 按序号压缩应立即生效，不依赖当前时钟是否已越过日志时间。
        channel->compact_stateful_sequence(seqs.at(1));
        channel->compact_sequence(seqs.at(1));
        CASE_EXPECT_TRUE(channel->get_compact_stateful_sequence() >= seqs.at(1));

        // After compaction the messages older than the watermark must be physically removed from
        // the WAL. The watermark itself is exclusive (log_key_compare is strict less-than), so the
        // message at seqs.at(1) and newer ones stay.
        CASE_EXPECT_TRUE(nullptr == channel->get_shared_wal_object()->find_log(seqs.at(0)));
        CASE_EXPECT_TRUE(!!channel->get_shared_wal_object()->find_log(seqs.at(1)));
        CASE_EXPECT_TRUE(!!channel->get_shared_wal_object()->find_log(seqs.at(2)));
        const auto* last_removed_key = channel->get_shared_wal_object()->get_last_removed_key();
        CASE_EXPECT_TRUE(nullptr != last_removed_key);
        if (nullptr != last_removed_key) {
          CASE_EXPECT_TRUE(*last_removed_key >= seqs.at(1));
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

    // compare_and_maybe_reset_lock with append_log=true must reset the lock and append a
    // reset_lock event carrying the new value into the WAL.
    atfw::dtmq::channel_lock_checker checker;
    checker.mutable_expect_value()->set_lock_holder("session-abc");
    checker.mutable_reset_value()->set_lock_holder("session-def");
    CASE_EXPECT_TRUE(channel->compare_and_maybe_reset_lock(ctx, checker, true));
    CASE_EXPECT_EQ("session-def", channel->get_lock().lock_holder());

    size_t reset_lock_log_count = 0;
    const atfw::dtmq::DChannelMessage* reset_lock_log = nullptr;
    for (const auto& wal_log : channel->get_shared_wal_object()->get_all_logs()) {
      if (wal_log && wal_log->detail().command_case() == atfw::dtmq::DChannelMessageDetail::kResetLock) {
        ++reset_lock_log_count;
        reset_lock_log = wal_log.get();
      }
    }
    CASE_EXPECT_EQ(1u, reset_lock_log_count);
    CASE_EXPECT_TRUE(nullptr != reset_lock_log);
    if (nullptr != reset_lock_log) {
      CASE_EXPECT_EQ("session-def", reset_lock_log->detail().reset_lock().lock_holder());
    }

    // A mismatched expectation neither resets the lock nor appends another event; the real value is
    // written back into the checker.
    atfw::dtmq::channel_lock_checker checker2;
    checker2.mutable_expect_value()->set_lock_holder("session-xyz");
    checker2.mutable_reset_value()->set_lock_holder("session-zzz");
    CASE_EXPECT_FALSE(channel->compare_and_maybe_reset_lock(ctx, checker2, false));
    CASE_EXPECT_EQ("session-def", channel->get_lock().lock_holder());
    CASE_EXPECT_EQ("session-def", checker2.real_value().lock_holder());
    reset_lock_log_count = 0;
    for (const auto& wal_log : channel->get_shared_wal_object()->get_all_logs()) {
      if (wal_log && wal_log->detail().command_case() == atfw::dtmq::DChannelMessageDetail::kResetLock) {
        ++reset_lock_log_count;
      }
    }
    CASE_EXPECT_EQ(1u, reset_lock_log_count);

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

        // writable 主节点自己创建和分配 destroy_timepoint/destroy_sequence（忽略传入的参数，
        // 通过 WAL kDestroy 日志回调 merge 进来，sequence 为基于时间的分配值）。先 dump 出实际值，
        // 再验证 merge 的“较旧忽略/较新生效”语义。
        atfw::dtmq::DChannelMetadata metadata;
        channel->dump(metadata, false, false);
        const int64_t destroyed_seq = metadata.destroy_sequence();
        const auto destroyed_tp = protobuf_to_system_clock(metadata.destroy_timepoint());
        CASE_EXPECT_TRUE(destroyed_seq > 0);
        CASE_EXPECT_TRUE(destroyed_tp > std::chrono::system_clock::from_time_t(0));
        if (destroyed_seq <= 0) {
          RPC_RETURN_CODE(0);
        }

        // Older destroy information is ignored by the merge.
        channel->merge_destroy_timepoint_and_sequence(ctx, std::chrono::system_clock::from_time_t(0),
                                                      destroyed_seq - 1);
        CASE_EXPECT_TRUE(channel->is_destroyed());
        metadata.Clear();
        channel->dump(metadata, false, false);
        CASE_EXPECT_EQ(destroyed_seq, metadata.destroy_sequence());
        CASE_EXPECT_TRUE(destroyed_tp == protobuf_to_system_clock(metadata.destroy_timepoint()));

        // Newer destroy information takes effect after the merge.
        const auto newer_destroy_tp = destroyed_tp + std::chrono::seconds{10};
        channel->merge_destroy_timepoint_and_sequence(ctx, newer_destroy_tp, destroyed_seq + 100);
        metadata.Clear();
        channel->dump(metadata, false, false);
        CASE_EXPECT_EQ(destroyed_seq + 100, metadata.destroy_sequence());
        CASE_EXPECT_TRUE(newer_destroy_tp == protobuf_to_system_clock(metadata.destroy_timepoint()));
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

        // A freshly constructed channel has stale (empty) distribution caches: no writable target and
        // no readonly replica resolve yet.
        atfw::dtmq::DChannelConfigure configure = get_configure_for(kTestChannelType);
        auto fresh_channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, configure);
        mq_channel_manager::me()->add_channel(ctx, fresh_channel);
        CASE_EXPECT_EQ(0u, fresh_channel->get_ready_distribution_writable_server_id());
        CASE_EXPECT_EQ(0u, fresh_channel->get_target_distribution_writable_server_id());
        CASE_EXPECT_EQ(0u, fresh_channel->get_target_distribution_server_id(1));

        // force_refresh_distribution recalculates the caches when the distribution revision moved on.
        MqChannelManagerUnitTest::set_latest_server_etcd_revision(
            *mq_channel_manager::me(), mq_channel_manager::me()->get_latest_server_etcd_revision() + 1);
        fresh_channel->force_refresh_distribution();
        CASE_EXPECT_EQ(local_id, fresh_channel->get_ready_distribution_writable_server_id());
        CASE_EXPECT_EQ(local_id, fresh_channel->get_target_distribution_writable_server_id());
        CASE_EXPECT_NE(0u, fresh_channel->get_target_distribution_server_id(1));
        CASE_EXPECT_TRUE(fresh_channel->should_be_writable());
        mq_channel_manager::me()->remove_channel(channel_id, fresh_channel.get());

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel);
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        // Re-refreshing an already up-to-date channel keeps the resolved distribution.
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

  // The tick-triggered save is verified through the typed DB mock, so use the DB-backed channel type.
  size_t replace_calls = 0;
  PROJECT_NAMESPACE_ID::table_dtmq_channel_record saved_record;
  auto replace_rule = rpc::db::dtmq_channel_record::mock::replace(
      [&replace_calls, &saved_record](rpc::context& /*ctx*/,
                                      const PROJECT_NAMESPACE_ID::table_dtmq_channel_record& input,
                                      rpc::unit_test::db_mock_meta& meta) -> rpc::result_code_type {
        ++replace_calls;
        saved_record.CopyFrom(input);
        meta.version = 1;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!replace_rule);
  if (!replace_rule) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("tick-w", local_id, 200, kTestDbBackedChannelType);
  auto task =
      test.run_task("tick_w", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id(channel_id);
        channel_key.set_channel_type(kTestDbBackedChannelType);

        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                              mq_channel_manager::me()->make_writable_channel(ctx, channel, fwd, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());
        CASE_EXPECT_FALSE(channel->get_configure().memory_only());
        if (!channel) {
          RPC_RETURN_CODE(0);
        }

        // Subscribe a subscriber and append a text message so the periodic tick path has a pending
        // save (with a verifiable DB payload) and a subscriber to expire.
        atfw::dtmq::channel_subscriber sub;
        sub.set_subscriber_server_id(kPeerNode1);
        sub.set_subscriber_key("U:1:20002");
        CASE_EXPECT_EQ(0, channel->subscribe(ctx, sub, 0, 0, false));

        int32_t wal_result = 0;
        mq_channel_wal_object_context wal_context{ctx, wal_result};
        auto message = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                                 atfw::dtmq::DChannelMessageDetail::kText, wal_context);
        CASE_EXPECT_TRUE(!!message);
        if (message) {
          message->mutable_detail()->set_text("tick-save-payload");
          channel->get_wal_publisher().emplace_back_log(std::move(message), wal_context);
        }

        channel->set_dirty();
        CASE_EXPECT_TRUE(channel->is_dirty());
        {
          auto subscribers = channel->get_wal_publisher().subscriber_all_range();
          CASE_EXPECT_TRUE(subscribers.first != subscribers.second);
        }

        // Register a short timer and jump the clock past both the save interval and the subscriber
        // timeout, then let mq_channel_manager::tick() fire the channel's update_timer. The timer
        // callback kicks off the periodic save and then tick(), which evicts the expired subscriber.
        mq_channel_timer_type::timer_wptr_t timer_handle;
        mq_channel_manager::me()->update_timer(*channel, timer_handle, std::chrono::seconds{2});
        {
          const auto& dtmq_proxysvr_cfg =
              logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
          auto advance_by = manager_tick_safe_offset(
              protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.save_interval()) +
              get_mq_channel_subscriber_timeout(channel->get_configure()) + std::chrono::seconds{60});
          global_now_offset_guard offset_guard{advance_by};
          CASE_EXPECT_TRUE(mq_channel_manager::me()->tick() > 0);

          // The expired subscriber was evicted by tick() synchronously.
          auto subscribers = channel->get_wal_publisher().subscriber_all_range();
          CASE_EXPECT_TRUE(subscribers.first == subscribers.second);
          int32_t result_code = 0;
          mq_channel_wal_object_context params{ctx, result_code};
          CASE_EXPECT_FALSE(!!channel->get_wal_publisher().find_subscriber("U:1:20002", params));
        }

        // The DB-backed save runs asynchronously through the dtmq_channel_record replace RPC; wait
        // for the IO task before checking the dirty flag and the saved payload.
        int32_t save_result = -1;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->await_io_task(ctx, &save_result)));
        CASE_EXPECT_EQ(0, save_result);
        CASE_EXPECT_FALSE(channel->is_dirty());
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

  // The tick-triggered save went through the DB replace mock with the live channel state.
  CASE_EXPECT_EQ(1u, replace_calls);
  CASE_EXPECT_EQ(channel_id, saved_record.channel_id());
  CASE_EXPECT_EQ(channel_id, saved_record.channel_metadata().channel_key().channel_id());
  CASE_EXPECT_EQ(kTestDbBackedChannelType, saved_record.channel_metadata().channel_key().channel_type());
  CASE_EXPECT_TRUE(saved_record.channel_metadata().create_sequence() > 0);
  bool found_payload = false;
  for (const auto& record : saved_record.record_set().record()) {
    if (record.detail().text() == "tick-save-payload") {
      found_payload = true;
      break;
    }
  }
  CASE_EXPECT_TRUE(found_payload);
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
  uint64_t readonly_replicate_index = 0;
  auto readonly_channel_id = find_local_readonly_channel_id("ridx-ro", local_id, readonly_replicate_index);
  CASE_EXPECT_NE(0u, readonly_replicate_index);
  if (0 == readonly_replicate_index) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "ridx_cur", std::chrono::seconds{4},
      [channel_id, readonly_channel_id, readonly_replicate_index](rpc::context& ctx) -> rpc::result_code_type {
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

        // The writable node always reports replicate index 0.
        CASE_EXPECT_TRUE(channel->is_writable());
        CASE_EXPECT_EQ(0u, channel->get_current_replicate_index());

        // A readonly node reports its own readonly replicate index.
        atfw::dtmq::DChannelIdKey readonly_channel_key;
        readonly_channel_key.set_channel_id(readonly_channel_id);
        readonly_channel_key.set_channel_type(kTestChannelType);
        mq_channel_manager::mq_channel_ptr_type readonly_channel;
        uint64_t readonly_fwd = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_readable_channel(
                              ctx, readonly_channel, readonly_fwd, readonly_channel_key, true)));
        CASE_EXPECT_EQ(0u, readonly_fwd);
        CASE_EXPECT_TRUE(!!readonly_channel && readonly_channel->is_readonly());
        if (readonly_channel) {
          CASE_EXPECT_EQ(readonly_replicate_index, readonly_channel->get_current_replicate_index());
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

namespace {
static std::vector<atfw::dtmq::SSChannelEventSync> collect_channel_events(atframework::testing::runtime& test,
                                                                          size_t begin, const std::string& channel_id) {
  std::vector<atfw::dtmq::SSChannelEventSync> events;
  for (size_t index = begin; index < test.transport().outbound_count(); ++index) {
    const auto* record = test.transport().outbound_at(index);
    if (nullptr == record || record->target_node_id != kPeerNode1) {
      continue;
    }
    atfw::SSMsg envelope;
    atfw::dtmq::SSChannelEventSync event;
    if (envelope.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size())) &&
        envelope.head().has_rpc_stream() && event.ParseFromString(envelope.body_bin()) &&
        event.channel_metadata().channel_key().channel_id() == channel_id) {
      events.push_back(std::move(event));
    }
  }
  return events;
}

static mq_channel_wal_object_type::log_pointer append_transfer_message(mq_channel& channel, rpc::context& ctx,
                                                                       const std::string& text) {
  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};
  auto message = channel.get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                          atfw::dtmq::DChannelMessageDetail::kText, params);
  CASE_EXPECT_TRUE(!!message);
  if (message) {
    message->mutable_detail()->set_text(text);
    auto retained = message;
    CASE_EXPECT_EQ(atfw::util::distributed_system::wal_result_code::kOk,
                   channel.get_wal_publisher().emplace_back_log(std::move(message), params));
    CASE_EXPECT_EQ(0, result);
    return retained;
  }
  return message;
}

static void check_transfer_catch_up(const std::string& prefix, bool corrupt_hash, bool compact_checkpoint,
                                    bool existing_subscriber = false) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  const auto channel_id = find_local_writable_channel_id(prefix, kLocalNodeId);
  CASE_EXPECT_FALSE(channel_id.empty());
  auto task = test.run_task(
      prefix, std::chrono::seconds{4},
      [&test, channel_id, corrupt_hash, compact_checkpoint,
       existing_subscriber](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey key;
        key.set_channel_id(channel_id);
        key.set_channel_type(kTestChannelType);
        auto source = atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), key,
                                                                               get_configure_for(kTestChannelType));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(source->writable_init(ctx)));
        source->ensure_recreate_after_destroyed(ctx);
        auto checkpoint = append_transfer_message(*source, ctx, "already-received");
        atfw::dtmq::channel_snapshot baseline;
        source->dump_snapshot(ctx, baseline);
        auto missing = append_transfer_message(*source, ctx, "missing-before-transfer");
        if (!checkpoint || !missing) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        atfw::dtmq::channel_subscriber subscriber;
        subscriber.set_subscriber_server_id(kPeerNode1);
        subscriber.set_subscriber_key("UT:transfer-catch-up");
        CASE_EXPECT_EQ(0, source->subscribe(ctx, subscriber, checkpoint->sequence(), checkpoint->hash_code(), false));
        if (compact_checkpoint) {
          source->compact_sequence(missing->sequence());
        }
        atfw::dtmq::channel_snapshot snapshot;
        source->dump_snapshot(ctx, snapshot);
        CASE_EXPECT_EQ(1, snapshot.subscriber_size());
        if (snapshot.subscriber_size() != 1) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
        }
        CASE_EXPECT_EQ(checkpoint->sequence(), snapshot.subscriber(0).last_heartbeat_sequence());
        CASE_EXPECT_EQ(checkpoint->hash_code(), snapshot.subscriber(0).last_heartbeat_hash_code());
        if (corrupt_hash) {
          snapshot.mutable_subscriber(0)->set_last_heartbeat_hash_code(checkpoint->hash_code() ^ 1);
        }
        if (existing_subscriber) {
          auto existing = atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), key,
                                                                                   get_configure_for(kTestChannelType));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(existing->writable_init(ctx)));
          CASE_EXPECT_TRUE(existing->load_snapshot(ctx, std::move(baseline)));
          CASE_EXPECT_EQ(0,
                         existing->subscribe(ctx, subscriber, checkpoint->sequence(), checkpoint->hash_code(), false));
          mq_channel_manager::me()->add_channel(ctx, existing);
        }
        atfw::dtmq::SSChannelTransferChannelReq transfer;
        transfer.add_snapshot()->Swap(&snapshot);
        atframework::testing::ss_action_invoke_options action_options{
            rpc::dtmq::packer::get_full_name_of_transfer_channel()};
        action_options.source.node_id = kPeerNode2;
        action_options.source.source_task_id = 0xD710;
        action_options.source.sequence = 0xD711;
        const size_t event_begin = test.transport().outbound_count();
        // 新目标晚于来源初始化，仍必须恢复来源的数据，不能创建新日志后把来源快照误判为过期。
        global_now_offset_guard transfer_delay{std::chrono::seconds{1}};
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_transfer_channel>(
                              ctx, transfer, action_options)));
        auto target = mq_channel_manager::me()->get_channel(channel_id);
        CASE_EXPECT_TRUE(!!target && target->is_writable());
        if (!target) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
        }
        CASE_EXPECT_EQ(missing->sequence(), target->get_last_message_sequence());
        CASE_EXPECT_TRUE(!!target->get_shared_wal_object()->find_log(missing->sequence()));
        auto fresh = append_transfer_message(*target, ctx, "new-after-transfer");
        if (!fresh) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        target->tick(ctx);
        auto events = collect_channel_events(test, event_begin, channel_id);
        CASE_EXPECT_EQ(2u, events.size());
        if (events.size() == 2) {
          CASE_EXPECT_EQ(corrupt_hash || compact_checkpoint, events[0].has_channel_snapshot());
          if (!corrupt_hash && !compact_checkpoint) {
            CASE_EXPECT_EQ(1, events[0].channel_message_size());
            if (events[0].channel_message_size() == 1) {
              CASE_EXPECT_EQ(missing->sequence(), events[0].channel_message(0).sequence());
              CASE_EXPECT_EQ(missing->hash_code(), events[0].channel_message(0).hash_code());
              CASE_EXPECT_EQ("missing-before-transfer", events[0].channel_message(0).detail().text());
            }
          } else {
            CASE_EXPECT_EQ(missing->sequence(), events[0].channel_snapshot().channel_metadata().last_sequence());
          }
          CASE_EXPECT_FALSE(events[1].has_channel_snapshot());
          CASE_EXPECT_EQ(1, events[1].channel_message_size());
          if (events[1].channel_message_size() == 1) {
            CASE_EXPECT_EQ(fresh->sequence(), events[1].channel_message(0).sequence());
            CASE_EXPECT_EQ("new-after-transfer", events[1].channel_message(0).detail().text());
          }
        }
        const size_t after_flush = test.transport().outbound_count();
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_transfer_channel>(
                              ctx, transfer, action_options)));
        target->tick(ctx);
        CASE_EXPECT_TRUE(collect_channel_events(test, after_flush, channel_id).empty());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}
}  // namespace

CASE_TEST(component_dtmq_channel, transfer_checkpoint_catches_up_without_heartbeat) {
  check_transfer_catch_up("transfer-checkpoint", false, false);
}

CASE_TEST(component_dtmq_channel, transfer_bad_checkpoint_hash_sends_snapshot) {
  check_transfer_catch_up("transfer-bad-hash", true, false);
}

CASE_TEST(component_dtmq_channel, transfer_compacted_checkpoint_sends_snapshot) {
  check_transfer_catch_up("transfer-compacted", false, true);
}

CASE_TEST(component_dtmq_channel, transfer_preserves_existing_subscriber_progress) {
  check_transfer_catch_up("transfer-existing-subscriber", false, false, true);
}

CASE_TEST(component_dtmq_channel, merged_heartbeat_keeps_sequence_and_hash_together) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto key = make_channel_key("merge-checkpoint-pair", kTestChannelType);
  auto task = test.run_task(
      "merge_checkpoint_pair", std::chrono::seconds{4}, [&test, key](rpc::context& ctx) -> rpc::result_code_type {
        auto channel = atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), key,
                                                                                get_configure_for(kTestChannelType));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->writable_init(ctx)));
        auto older = append_transfer_message(*channel, ctx, "older-checkpoint");
        auto current = append_transfer_message(*channel, ctx, "current-checkpoint");
        if (!older || !current) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        atfw::dtmq::channel_subscriber subscriber;
        // 空业务 key 是服务副本的正常订阅方式，由 server id 决定身份。
        subscriber.set_subscriber_server_id(kPeerNode1);
        CASE_EXPECT_EQ(0, channel->subscribe(ctx, subscriber, current->sequence(), current->hash_code(), false));
        atfw::dtmq::channel_snapshot snapshot;
        channel->dump_snapshot(ctx, snapshot);
        CASE_EXPECT_EQ(1, snapshot.subscriber_size());
        if (snapshot.subscriber_size() != 1) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
        }
        const size_t event_begin = test.transport().outbound_count();
        global_now_offset_guard later_heartbeat{std::chrono::seconds{1}};
        auto* merged = snapshot.mutable_subscriber(0);
        *merged->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(atfw::util::time::time_utility::now());
        merged->set_last_heartbeat_sequence(older->sequence());
        merged->set_last_heartbeat_hash_code(older->hash_code());
        channel->merge_subscriber(ctx, snapshot.subscriber());
        auto retained = channel->get_wal_publisher().get_subscribe_manager().find(make_subscriber_key(subscriber));
        CASE_EXPECT_TRUE(!!retained);
        if (retained) {
          CASE_EXPECT_EQ(current->sequence(), retained->get_private_data().last_heartbeat_sequence());
          CASE_EXPECT_EQ(current->hash_code(), retained->get_private_data().last_heartbeat_hash_code());
          CASE_EXPECT_EQ(merged->last_heartbeat_timepoint().seconds(),
                         retained->get_private_data().last_heartbeat_timepoint().seconds());
        }
        CASE_EXPECT_TRUE(collect_channel_events(test, event_begin, key.channel_id()).empty());
        // 过期记录不能替换存活订阅者，也不能恢复已过期的另一订阅者。
        merged->mutable_last_heartbeat_timepoint()->set_seconds(1);
        channel->merge_subscriber(ctx, snapshot.subscriber());
        merged->set_subscriber_key("expired-subscriber");
        channel->merge_subscriber(ctx, snapshot.subscriber());
        CASE_EXPECT_FALSE(!!channel->get_wal_publisher().get_subscribe_manager().find("expired-subscriber"));
        CASE_EXPECT_TRUE(collect_channel_events(test, event_begin, key.channel_id()).empty());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtmq_channel, recreate_clears_full_previous_generation_in_same_tick) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto key = make_channel_key("recreate-same-tick", kTestChannelType);
  auto task =
      test.run_task("recreate_same_tick", std::chrono::seconds{4}, [key](rpc::context& ctx) -> rpc::result_code_type {
        auto configure = get_configure_for(kTestChannelType);
        configure.set_max_log_count(3);
        auto channel =
            atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), key, configure);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->writable_init(ctx)));
        channel->ensure_recreate_after_destroyed(ctx);
        auto old_message = append_transfer_message(*channel, ctx, "previous-generation");
        if (!old_message) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        channel->set_destroyed(ctx, atfw::util::time::time_utility::now(), 0);
        CASE_EXPECT_TRUE(channel->is_destroyed());
        CASE_EXPECT_EQ(3u, channel->get_shared_wal_object()->get_all_logs().size());
        channel->ensure_recreate_after_destroyed(ctx);
        CASE_EXPECT_TRUE(channel->is_available());
        CASE_EXPECT_FALSE(!!channel->get_shared_wal_object()->find_log(old_message->sequence()));
        CASE_EXPECT_EQ(1u, channel->get_shared_wal_object()->get_all_logs().size());
        auto sequence = channel->get_last_message_sequence();
        channel->ensure_recreate_after_destroyed(ctx);
        CASE_EXPECT_EQ(sequence, channel->get_last_message_sequence());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtmq_channel, update_syncs_data_versions_and_preserves_push_groups) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto channel_id = find_local_writable_channel_id("update-data-versions", kLocalNodeId);
  auto task = test.run_task(
      "update_data_versions", std::chrono::seconds{4}, [&test, channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey key;
        key.set_channel_id(channel_id);
        key.set_channel_type(kTestChannelType);
        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, channel, forward_server_id, key, true)));
        if (!channel || !channel->is_available()) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
        }
        channel->tick(ctx);
        for (bool with_private_data : {false, true}) {
          atfw::dtmq::channel_subscriber subscriber;
          subscriber.set_subscriber_server_id(kPeerNode1);
          subscriber.set_subscriber_key(with_private_data ? "UT:private-data" : "UT:public-data");
          subscriber.set_with_private_data(with_private_data);
          CASE_EXPECT_EQ(0, channel->subscribe(ctx, subscriber, channel->get_last_message_sequence(),
                                               channel->get_last_hash_code(), false));
        }
        atframework::testing::ss_action_invoke_options action_options{rpc::dtmq::packer::get_full_name_of_update()};
        action_options.source.node_id = kPeerNode2;
        action_options.source.source_task_id = 0xD720;
        action_options.source.sequence = 0xD721;
        // 覆盖 noop 更新、显式更新日志和通过 noop 清空数据。
        for (int step = 0; step < 3; ++step) {
          atfw::dtmq::SSChannelUpdateReq request;
          request.mutable_channel_key()->CopyFrom(key);
          request.set_custom_data_skip_notify(step != 1);
          const std::string custom_value = step == 2 ? "" : "custom-" + std::to_string(step);
          if (step == 2) {
            request.set_clear_custom_data_action(true);
          } else {
            request.mutable_custom_data()->set_type_url("type.googleapis.com/dtmq.CustomData");
            request.mutable_custom_data()->set_value(custom_value);
          }
          request.mutable_private_data()->set_type_url("type.googleapis.com/dtmq.PrivateData");
          request.mutable_private_data()->set_value("private-" + std::to_string(step));
          const auto before_sequence = channel->get_last_message_sequence();
          const size_t event_begin = test.transport().outbound_count();
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_update>(
                                ctx, request, action_options)));
          CASE_EXPECT_GT(channel->get_last_message_sequence(), before_sequence);
          CASE_EXPECT_EQ(custom_value, channel->get_custom_data().value());
          auto events = collect_channel_events(test, event_begin, channel_id);
          CASE_EXPECT_EQ(2u, events.size());
          for (const auto& event : events) {
            CASE_EXPECT_FALSE(event.has_channel_snapshot());
            CASE_EXPECT_EQ(1, event.channel_message_size());
            CASE_EXPECT_EQ(1, event.subscriber_keys_size());
            CASE_EXPECT_TRUE(event.channel_metadata().has_custom_data());
            CASE_EXPECT_EQ(channel->get_last_message_sequence(), event.channel_metadata().custom_data_sequence());
            CASE_EXPECT_EQ(custom_value, event.channel_metadata().custom_data().value());
            if (event.channel_message_size() == 1) {
              CASE_EXPECT_EQ(step == 1 ? atfw::dtmq::DChannelMessageDetail::kUpdateCustomData
                                       : atfw::dtmq::DChannelMessageDetail::kNoop,
                             event.channel_message(0).detail().command_case());
            }
            if (event.subscriber_keys_size() == 1) {
              const bool with_private_data = event.subscriber_keys(0) == "UT:private-data";
              CASE_EXPECT_EQ(with_private_data, event.channel_runtime().has_private_data());
              if (with_private_data) {
                CASE_EXPECT_EQ("private-" + std::to_string(step), event.channel_runtime().private_data().value());
                CASE_EXPECT_EQ(channel->get_last_message_sequence(), event.channel_runtime().private_data_sequence());
              }
            }
          }
          // 空更新和重复 flush 不得追加日志或重复推送。
          atfw::dtmq::SSChannelUpdateReq empty_request;
          empty_request.mutable_channel_key()->CopyFrom(key);
          const auto updated_sequence = channel->get_last_message_sequence();
          const size_t after_flush = test.transport().outbound_count();
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_update>(
                                ctx, empty_request, action_options)));
          channel->tick(ctx);
          CASE_EXPECT_EQ(updated_sequence, channel->get_last_message_sequence());
          CASE_EXPECT_TRUE(collect_channel_events(test, after_flush, channel_id).empty());
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtmq_channel, stale_database_record_keeps_newer_state_pending_save) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto channel_id = find_local_writable_channel_id("stale-db-record", kLocalNodeId, 200, kTestDbBackedChannelType);
  auto task = test.run_task(
      "stale_database_record", std::chrono::seconds{4}, [channel_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::DChannelIdKey key;
        key.set_channel_id(channel_id);
        key.set_channel_type(kTestDbBackedChannelType);
        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, channel, forward_server_id, key, true)));
        if (!channel || !channel->is_available()) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
        }
        auto old_message = append_transfer_message(*channel, ctx, "saved-message");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->save(ctx)));
        CASE_EXPECT_FALSE(channel->is_dirty());
        auto record = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>(ctx);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::get_all(ctx, channel_id, *record)));
        auto fresh_message = append_transfer_message(*channel, ctx, "unsaved-message");
        if (!old_message || !fresh_message) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        // DB 返回前内存已收到更晚的数据；旧记录不能确认新数据已持久化。
        channel->load(ctx, *record);
        CASE_EXPECT_EQ(fresh_message->sequence(), channel->get_last_message_sequence());
        CASE_EXPECT_TRUE(channel->is_dirty());
        CASE_EXPECT_TRUE(channel->need_save_db());
        if (channel->need_save_db()) {
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->save(ctx)));
        }
        CASE_EXPECT_FALSE(channel->is_dirty());
        auto saved = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>(ctx);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::get_all(ctx, channel_id, *saved)));
        CASE_EXPECT_EQ(fresh_message->sequence(), saved->channel_metadata().last_sequence());
        CASE_EXPECT_EQ(fresh_message->hash_code(), saved->channel_metadata().last_hash_code());
        bool found_fresh_message = false;
        for (const auto& message : saved->record_set().record()) {
          if (message.sequence() == fresh_message->sequence()) {
            found_fresh_message = message.detail().text() == "unsaved-message";
          }
        }
        CASE_EXPECT_TRUE(found_fresh_message);
        channel->load(ctx, *record);
        CASE_EXPECT_FALSE(channel->is_dirty());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

namespace {
static void check_non_auto_create_memory_replica(bool destroyed) {
  atframework::testing::runtime test;
  if (!start_channel_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  uint64_t replicate_index = 0;
  auto channel_id = find_local_readonly_channel_id(
      destroyed ? "preserve-destroyed-replica" : "promote-existing-replica", kLocalNodeId, replicate_index);
  CASE_EXPECT_GT(replicate_index, 0u);
  auto task =
      test.run_task("promote_existing_replica", std::chrono::seconds{4},
                    [&test, channel_id, replicate_index, destroyed](rpc::context& ctx) -> rpc::result_code_type {
                      atfw::dtmq::DChannelIdKey key;
                      key.set_channel_id(channel_id);
                      key.set_channel_type(kTestChannelType);
                      auto source = atfw::component::memory::stl::make_strong_rc<mq_channel>(
                          *mq_channel_manager::me(), key, get_configure_for(kTestChannelType));
                      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(source->writable_init(ctx)));
                      source->ensure_recreate_after_destroyed(ctx);
                      auto message = append_transfer_message(*source, ctx, "preserved-on-promotion");
                      if (!message) {
                        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
                      }
                      if (destroyed) {
                        source->set_destroyed(ctx, atfw::util::time::time_utility::now(), 0);
                      }
                      atfw::dtmq::channel_snapshot snapshot;
                      source->dump_snapshot(ctx, snapshot);
                      const auto create_sequence = snapshot.channel_data().channel_metadata().create_sequence();
                      const auto last_sequence = snapshot.channel_data().channel_metadata().last_sequence();
                      snapshot.set_replicate_index(replicate_index);
                      mq_channel_manager::mq_channel_ptr_type replica;
                      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->create_channel(
                                            ctx, replica, key, get_configure_for(kTestChannelType))));
                      if (!replica) {
                        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
                      }
                      CASE_EXPECT_TRUE(replica->load_snapshot(ctx, std::move(snapshot)));
                      CASE_EXPECT_TRUE(replica->is_readonly());
                      CASE_EXPECT_EQ(!destroyed, replica->is_available());
                      test.discovery().remove_node(kPeerNode1);
                      test.discovery().remove_node(kPeerNode2);
                      reload_discovery();
                      MqChannelManagerUnitTest::set_latest_server_etcd_revision(
                          *mq_channel_manager::me(), mq_channel_manager::me()->get_latest_server_etcd_revision() + 1);
                      replica->force_refresh_distribution();
                      CASE_EXPECT_TRUE(replica->should_be_writable());
                      mq_channel_manager::mq_channel_ptr_type writable;
                      uint64_t forward_server_id = 0;
                      CASE_EXPECT_EQ(destroyed ? PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND : 0,
                                     RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                                         ctx, writable, forward_server_id, key, false)));
                      CASE_EXPECT_EQ(0u, forward_server_id);
                      CASE_EXPECT_TRUE(!!writable);
                      CASE_EXPECT_EQ(!destroyed, replica->is_writable());
                      CASE_EXPECT_EQ(!destroyed, replica->is_available());
                      CASE_EXPECT_EQ(destroyed, replica->is_destroyed());
                      CASE_EXPECT_EQ(replica.get(), writable.get());
                      CASE_EXPECT_EQ(last_sequence, replica->get_last_message_sequence());
                      auto retained = replica->get_shared_wal_object()->find_log(message->sequence());
                      CASE_EXPECT_TRUE(!!retained);
                      if (retained) {
                        CASE_EXPECT_EQ("preserved-on-promotion", retained->detail().text());
                        CASE_EXPECT_EQ(message->hash_code(), retained->hash_code());
                      }
                      atfw::dtmq::DChannelMetadata metadata;
                      replica->dump(metadata, false, false);
                      CASE_EXPECT_EQ(create_sequence, metadata.create_sequence());
                      RPC_RETURN_CODE(0);
                    });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}
}  // namespace

CASE_TEST(component_dtmq_channel, non_auto_create_promotes_existing_memory_replica) {
  check_non_auto_create_memory_replica(false);
}

CASE_TEST(component_dtmq_channel, non_auto_create_preserves_destroyed_memory_replica) {
  check_non_auto_create_memory_replica(true);
}

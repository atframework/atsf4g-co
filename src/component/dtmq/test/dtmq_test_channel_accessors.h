// Copyright 2026 atframework

// Unit-test accessors for mq_channel_manager. This friend class centralizes test-only manager state
// setup so the production header only needs one declaration gated by unit-test hooks.
//
// Usage:
//   MqChannelManagerUnitTest::clear_all_channels(*mq_channel_manager::me());
//   MqChannelManagerUnitTest::set_stoping(*mq_channel_manager::me(), true);
//
// The classes are only compiled when PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS is on (matches the
// friend declaration in mq_channel_manager.h). Empty when the macro is off.

#pragma once

#include "data/mq_channel.h"
#include "logic/mq_channel_manager.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

// Friend of mq_channel_manager. Provides case-isolation and internal-flag setters used by the
// proxysvr unit tests.
class MqChannelManagerUnitTest {
 public:
  // 清空所有内部状态(频道/IO队列/停机标记)，便于 case 间隔离。
  // 定时器随频道析构自然移除，不在此主动清理。
  static void clear_all_channels(mq_channel_manager& mgr) noexcept {
    // 先清空 IO 队列引用，再清空频道表，避免析构时回调访问已失效结构
    mgr.pending_io_channels_.clear();
    mgr.running_io_channels_.clear();
    mgr.reactive_io_channels_.clear();
    mgr.channels_.clear();

    mgr.pending_transfer_ = mq_channel_manager::pending_transfer_info{};
    mgr.more_transfer_now_ = false;
    mgr.is_stoping_ = false;
    mgr.is_pre_stoping_ = false;
  }

  // 设置 etcd revision 以驱动分布重算(channel 在下次 force_refresh_distribution 时重算缓存)。
  static void set_latest_server_etcd_revision(mq_channel_manager& mgr, int64_t rev) noexcept {
    mgr.dtmq_server_distribute_etcd_revision_ = rev;
  }

  // 直接设置停机标记，用于测试 stop/pre_stoping 路径。
  static void set_stoping(mq_channel_manager& mgr, bool v) noexcept { mgr.is_stoping_ = v; }
  static void set_pre_stoping(mq_channel_manager& mgr, bool v) noexcept { mgr.is_pre_stoping_ = v; }
};

#endif  // PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

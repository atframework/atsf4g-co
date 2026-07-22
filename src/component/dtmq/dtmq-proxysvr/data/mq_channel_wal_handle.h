// Copyright 2026 atframework
// @brief Created by owent

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <distributed_system/wal_client.h>
#include <distributed_system/wal_publisher.h>

#include <data/player_key_hash_helper.h>

#include <rpc/dtmq/dtmq_client_api.h>
#include <rpc/rpc_async_invoke.h>

#include <cstdint>
#include <functional>
#include <string>

namespace rpc {
class context;
}

class mq_channel;

using mq_channel_storage_type = atfw::dtmq::DChannelSnapshot;

struct mq_channel_wal_object_context {
  std::reference_wrapper<rpc::context> context;
  std::reference_wrapper<int32_t> result_code;

  explicit mq_channel_wal_object_context(rpc::context& ctx, int32_t& output_result);
};

struct mq_wal_object_private_data_type {
  mq_channel* channel = nullptr;
  rpc::dtmq::hash_mismatch_subscribe<int64_t> hash_mismatch_data;
};

struct mq_channel_wal_publisher_log_action_getter {
  atfw::dtmq::DChannelMessageDetail::CommandCase operator()(const atfw::dtmq::DChannelMessage&) noexcept;
};

using mq_channel_wal_subscriber_private_data = atfw::dtmq::channel_subscriber;

struct mq_channel_log_action_hash_t {
  size_t operator()(const atfw::dtmq::DChannelMessageDetail::CommandCase& key) const noexcept {
    return std::hash<int>()(key);
  }
};

struct mq_channel_log_action_equal_t {
  bool operator()(const atfw::dtmq::DChannelMessageDetail::CommandCase& l,
                  const atfw::dtmq::DChannelMessageDetail::CommandCase& r) const noexcept {
    return l == r;
  }
};

struct mq_channel_wal_publisher_log_operator
    : public atfw::util::distributed_system::wal_log_operator<
          // NOLINTNEXTLINE(modernize-use-transparent-functors)
          int64_t, atfw::dtmq::DChannelMessage, mq_channel_wal_publisher_log_action_getter, std::less<int64_t>,
          mq_channel_log_action_hash_t, mq_channel_log_action_equal_t,
          atfw::memory::stl::allocator<atfw::dtmq::DChannelMessage>,
          atfw::util::distributed_system::wal_mt_mode::kSingleThread> {};

struct mq_channel_wal_subscriber_type
    : public atfw::util::distributed_system::wal_subscriber<mq_channel_wal_subscriber_private_data, std::string> {};

using mq_channel_wal_publisher_type =
    atfw::util::distributed_system::wal_publisher<mq_channel_storage_type, mq_channel_wal_publisher_log_operator,
                                                  mq_channel_wal_object_context, mq_wal_object_private_data_type,
                                                  mq_channel_wal_subscriber_type>;
using mq_channel_wal_client_type =
    atfw::util::distributed_system::wal_client<mq_channel_storage_type, mq_channel_wal_publisher_log_operator,
                                               mq_channel_wal_object_context, mq_wal_object_private_data_type,
                                               mq_channel_storage_type>;

using mq_channel_wal_object_type = mq_channel_wal_client_type::object_type;

std::string make_subscriber_key(const mq_channel_wal_subscriber_private_data& subscriber_data);

// 读取配置的订阅者超时时间。
atfw::util::distributed_system::wal_duration get_mq_channel_subscriber_timeout(
    const atfw::dtmq::DChannelConfigure& configure);

atfw::util::memory::strong_rc_ptr<mq_channel_wal_object_type> create_mq_channel_object(
    mq_channel& mq_channel, const atfw::dtmq::DChannelConfigure& configure);

atfw::util::memory::strong_rc_ptr<mq_channel_wal_publisher_type> create_mq_channel_publisher(
    mq_channel&, const atfw::dtmq::DChannelConfigure& configure);

atfw::util::memory::strong_rc_ptr<mq_channel_wal_client_type> create_mq_channel_client(
    mq_channel& channel, const atfw::dtmq::DChannelConfigure& configure);

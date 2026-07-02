// Copyright 2026 atframework
// @brief Created by owent

#include "data/mq_channel.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

void mq_channel::mq_channel_accessor::update_timer(mq_channel& mq_channel, rpc::context& ctx, bool force) {
  mq_channel.update_timer(ctx, force);
}

void mq_channel::mq_channel_accessor::remove_timer(mq_channel& mq_channel) { mq_channel.remove_timer(); }

mq_channel::mq_channel(mq_channel_manager& /*manager*/, const atfw::dtmq::DChannelIdKey& channel_key,
                       const atfw::dtmq::DChannelConfigure& configure)
    :  // owner_(&manager),
      sequence_allocator_(0),
      compact_stateful_sequence_(0),
      status_(channel_status::kNone),
      remove_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_save_timepoint_{std::chrono::system_clock::from_time_t(0)},
      lost_last_subscriber_timepoint_{std::chrono::system_clock::from_time_t(0)},
      next_notify_readonly_subscribe_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_writable_notify_readonly_timepoint_{std::chrono::system_clock::from_time_t(0)},
      next_init_subscribe_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_result_code_(PROJECT_NAMESPACE_ID::err::EN_SUCCESS),
      custom_data_sequence_(0),
      private_data_sequence_(0),
      is_loading_snapshot_(false),
      is_dirty_(false),
      dtmq_proxysvr_etcd_revision_(0),
      writable_dtmq_proxysvr_id_(0) {
  protobuf_copy_message(channel_key_, channel_key);
  protobuf_copy_message(configure_, configure);

  shared_wal_object_ = create_mq_channel_object(*this, configure);
  wal_publisher_ = create_mq_channel_publisher(*this, configure);

  next_send_oss_time_ = atfw::util::time::time_utility::now();
}

mq_channel::~mq_channel() {
  // Remove timer
  remove_timer();
}

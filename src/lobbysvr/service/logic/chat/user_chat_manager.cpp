// Copyright 2026 atframework

#include "logic/chat/user_chat_manager.h"

#include <string/string_format.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.dtmq.common.pb.h>
#include <protocol/pbdesc/com.struct.chat.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include <rpc/dtmq/dtmq_algorithm.h>
#include <rpc/dtmq/dtmq_client_subscriber.h>

#include <string>

#include "data/player.h"

user_chat_manager::user_chat_manager(player& owner) : owner_(&owner) {}

user_chat_manager::~user_chat_manager() {}

rpc::result_code_type user_chat_manager::login_init(rpc::context& /*ctx*/) {
  std::string subscriber_key = atfw::util::string::format("user:{}:{}", owner_->get_zone_id(), owner_->get_user_id());
  // 创建聊天频道
  if (!world_chat_channel_) {
    // TODO(ANY): 如果以后世界频道要分片，这里添加分片逻辑
    uint64_t partition_id = 0;
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC));
    channel_key.set_channel_id(rpc::dtmq::make_world_partition_channel_id(
        channel_key.channel_type(), logic_config::me()->get_local_world_id(), partition_id));
    world_chat_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    setup_subscriber_callback(world_chat_channel_);
  }

  if (!private_chat_channel_) {
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE));
    channel_key.set_channel_id(
        rpc::dtmq::make_unicast_channel_id(channel_key.channel_type(), owner_->get_zone_id(), owner_->get_user_id()));
    private_chat_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    setup_subscriber_callback(private_chat_channel_);
  }

  // 创建系统通知 Channel(生命周期短)
  if (!sys_notification_channel_) {
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_NOTIFICATION));
    channel_key.set_channel_id(rpc::dtmq::make_world_broadcast_channel_id(channel_key.channel_type(),
                                                                          logic_config::me()->get_local_world_id()));
    sys_notification_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    setup_subscriber_callback(sys_notification_channel_);
  }

  // 创建系统公告 Channel(生命周期长)
  if (!sys_announcement_channel_) {
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_ANNOUNCEMENT));
    channel_key.set_channel_id(rpc::dtmq::make_world_broadcast_channel_id(channel_key.channel_type(),
                                                                          logic_config::me()->get_local_world_id()));
    sys_announcement_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    setup_subscriber_callback(sys_announcement_channel_);
  }
  RPC_RETURN_CODE(0);
}

void user_chat_manager::foreach_channel(
    atfw::util::nostd::function_ref<bool(const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&)>
        callback) const {
  const rpc::dtmq::client_subscriber::ptr_t* channels[] = {&world_chat_channel_, &private_chat_channel_,
                                                           &sys_notification_channel_, &sys_announcement_channel_};
  for (const auto* channel_ptr : channels) {
    if (*channel_ptr) {
      if (!callback(*channel_ptr)) {
        break;
      }
    }
  }
}

void user_chat_manager::get_snapshot(rpc::context& /*ctx*/, gsl::string_view channel_id,
                                     atfw::chat::DChatChannelData& data) {
  foreach_channel(
      [&channel_id, &data, this](const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>& channel) {
        if (channel_id != channel->get_channel_key().channel_id()) {
          return true;
        }

        dump_dtmq_to_chat_channel_snapshot(*channel, *data.mutable_metadata(), *data.mutable_snapshot());

        // 我们要求客户端拉取一次之后才会主动推送频道通知
        // 如果没设置回调函数组，说明之前没拉过，设置回调函数组也能触发后续的主动通知
        if (!channel->get_shared_event_callback_set()) {
          setup_subscriber_callback(channel);
        }
        return false;
      });
}

void user_chat_manager::dump_dtmq_to_chat_channel_metadata(const rpc::dtmq::client_subscriber& channel,
                                                           atfw::chat::DChatChannelMeta& metadata,
                                                           bool with_configure) {
  protobuf_copy_message(*metadata.mutable_channel_key(), channel.get_channel_key());

  if (with_configure) {
    protobuf_copy_message(*metadata.mutable_channel_configure(), channel.get_configure());
  }

  if (channel.is_ready()) {
    metadata.set_create_sequence(channel.get_create_sequence());
    *metadata.mutable_create_timepoint() = protobuf_from_system_clock(channel.get_create_timepoint());
  }
  if (channel.is_destroyed()) {
    metadata.set_destroy_sequence(channel.get_destroy_sequence());
    *metadata.mutable_destroy_timepoint() = protobuf_from_system_clock(channel.get_destroy_timepoint());
  }

  metadata.set_last_removed_sequence(channel.get_last_removed_sequence());
}

void user_chat_manager::dump_dtmq_to_chat_channel_snapshot(const rpc::dtmq::client_subscriber& channel,
                                                           atfw::chat::DChatChannelMeta& metadata,
                                                           atfw::chat::DChatChannelSnapshot& /*snapshot*/) {
  dump_dtmq_to_chat_channel_metadata(channel, metadata, true);
}

void user_chat_manager::setup_subscriber_callback(const rpc::dtmq::client_subscriber::ptr_t& /*channel*/) {}

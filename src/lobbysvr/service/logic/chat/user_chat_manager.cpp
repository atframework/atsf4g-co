// Copyright 2026 atframework

#include "logic/chat/user_chat_manager.h"

#include <string/string_format.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.chat.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/dtmq/dtmq_algorithm.h>


user_chat_manager::user_chat_manager(player& owner): owner_(&owner) {}

user_chat_manager::~user_chat_manager() {}

rpc::result_code_type user_chat_manager::login_init(rpc::context& ctx) {

  std::string subscriber_key = atfw::util::string::string_format("user:{}:{}", owner_->get_zone_id(), owner_->get_user_id());
  // 创建聊天频道


  // 创建通知频道
  if (!sys_notification_channel_) {
    rpc::dtmq::dtmq_client_subscriber subscribe_options{subscriber_key};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::dtmq::EN_CHAT_CHANNEL_TYPE_SYS_NOTIFICATION));
    channel_key = atfw::dtmq::DChannelIdKey::make_world_broadcast_channel_id(owner_->get_zone_id(), owner_->get_user_id());
    sys_notification_channel_ = rpc::dtmq::dtmq_client_subscriber::create(ctx, subscribe_options);
  }
  RPC_RETURN_CODE(0);
}

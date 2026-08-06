// Copyright 2026 atframework

#pragma once

#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <rpc/rpc_common_types.h>
#include <rpc/dtmq/dtmq_client_subscriber.h>

namespace rpc {
class context;
}

class player;

class user_chat_manager {
 public:
  explicit user_chat_manager(player& owner);
  ~user_chat_manager();

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type login_init(rpc::context&);

  player& get_owner() { return *owner_; }
  const player& get_owner() const { return *owner_; }

 private:
  atfw::util::nostd::nonnull<player*> owner_;

  rpc::dtmq::client_subscriber::ptr_t world_chat_channel_;
  rpc::dtmq::client_subscriber::ptr_t private_chat_channel_;

  rpc::dtmq::client_subscriber::ptr_t sys_notification_channel_;
  rpc::dtmq::client_subscriber::ptr_t sys_announcement_channel_;
};

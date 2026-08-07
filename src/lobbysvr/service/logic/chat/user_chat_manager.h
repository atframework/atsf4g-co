// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>

#include <nostd/function_ref.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_common_types.h>

namespace rpc {
class context;
}

namespace atframework {
namespace chat {
class DChatChannelMeta;
class DChatChannelData;
class DChatChannelSnapshot;
}  // namespace chat
}  // namespace atframework

class player;

class user_chat_manager {
 public:
  explicit user_chat_manager(player& owner);
  ~user_chat_manager();

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type login_init(rpc::context&);

  player& get_owner() { return *owner_; }
  const player& get_owner() const { return *owner_; }

  void foreach_channel(
      atfw::util::nostd::function_ref<bool(const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&)>
          callback) const;

  void get_snapshot(rpc::context& ctx, gsl::string_view channel_id, atfw::chat::DChatChannelData& data);

  static void dump_dtmq_to_chat_channel_metadata(const rpc::dtmq::client_subscriber& channel,
                                                 atfw::chat::DChatChannelMeta& metadata, bool with_configure);

  static void dump_dtmq_to_chat_channel_snapshot(const rpc::dtmq::client_subscriber& channel,
                                                 atfw::chat::DChatChannelMeta& metadata,
                                                 atfw::chat::DChatChannelSnapshot& snapshot);

 private:
  void setup_subscriber_callback(const rpc::dtmq::client_subscriber::ptr_t& channel);

 private:
  player* ATFW_UTIL_MACRO_NONNULL owner_;

  rpc::dtmq::client_subscriber::ptr_t world_chat_channel_;
  rpc::dtmq::client_subscriber::ptr_t private_chat_channel_;

  rpc::dtmq::client_subscriber::ptr_t sys_notification_channel_;
  rpc::dtmq::client_subscriber::ptr_t sys_announcement_channel_;
};

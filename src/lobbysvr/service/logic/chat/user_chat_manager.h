// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>

#include <nostd/function_ref.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_common_types.h>

#include <string>

namespace rpc {
class context;
}

namespace google {
namespace protobuf {
class Any;
}
}  // namespace google

namespace atframework {
namespace dtmq {
class DChannelIdKey;
class DChannelSyncPoint;
}  // namespace dtmq
namespace chat {
class DChatChannelKey;
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

  /**
   * @brief 检查是否有写权限
   *
   * @param ctx RPC上下文
   * @param channel_key 频道Key
   * @return 如果有写权限返回0，否则返回错误码
   */
  int32_t check_writable(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key) const;

  rpc::result_code_type send_text_message(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key,
                                          gsl::string_view text);

  rpc::result_code_type send_event_message(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key,
                                           google::protobuf::Any&& event_data);

  int32_t get_snapshot(rpc::context& ctx, gsl::string_view channel_id, atfw::chat::DChatChannelData& data);

  int32_t receive_heartbeat(rpc::context& ctx, const atfw::dtmq::DChannelSyncPoint& sync_point,
                            atfw::chat::SCChatChannelSync& sync_msg);

  static int32_t build_dtmq_channel_key_from_chat_channel_key(const atfw::chat::DChatChannelKey& chat_channel_key,
                                                              atfw::dtmq::DChannelIdKey& dtmq_channel_key);

  static void dump_dtmq_to_chat_channel_metadata(rpc::context& ctx, const rpc::dtmq::client_subscriber& channel,
                                                 atfw::chat::DChatChannelMeta& metadata, bool with_configure);

  static void dump_dtmq_to_chat_channel_snapshot(rpc::context& ctx, const rpc::dtmq::client_subscriber& channel,
                                                 atfw::chat::DChatChannelMeta& metadata,
                                                 atfw::chat::DChatChannelSnapshot& snapshot);

  static uint32_t parse_channel_type_from_channel_id(const atfw::dtmq::DChannelIdKey& channel_key);

 private:
  rpc::dtmq::client_subscriber::ptr_t get_channel_by_key(const atfw::dtmq::DChannelIdKey& channel_key) const;

  void setup_subscriber_callback(const rpc::dtmq::client_subscriber::ptr_t& channel);

 private:
  player* ATFW_UTIL_MACRO_NONNULL owner_;
  time_t last_send_to_world_channel_timepoint_unix_sec_;
  std::string subscriber_key_;

  rpc::dtmq::client_subscriber::ptr_t world_chat_channel_;
  rpc::dtmq::client_subscriber::ptr_t private_chat_channel_;

  rpc::dtmq::client_subscriber::ptr_t sys_notification_channel_;
  rpc::dtmq::client_subscriber::ptr_t sys_announcement_channel_;
};

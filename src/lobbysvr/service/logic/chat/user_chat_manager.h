// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>

#include <design_pattern/noncopyable.h>
#include <nostd/function_ref.h>
#include <nostd/nullability.h>
#include <nostd/type_traits.h>
#include <std/explicit_declare.h>

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_common_types.h>

#include <string>
#include <utility>

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

class user;

class user_chat_manager : public atfw::util::design_pattern::noncopyable {
 public:
  using chat_channel_event_callback_t = void (*)(rpc::context& ctx, user&, const ::atfw::dtmq::DChannelMessage& data);

 public:
  static int32_t global_tick(rpc::context& ctx);

  explicit user_chat_manager(user& owner);
  ~user_chat_manager();

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t login_init(rpc::context&);

  inline user& get_owner() { return *owner_; }
  inline const user& get_owner() const { return *owner_; }

  inline const std::string& get_subscriber_key() const noexcept { return subscriber_key_; }

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

  /**
   * @brief 设置私有频道的事件回调函数
   *
   * @param unique_guard 唯一性检测标识（每个标识值只会注册一次）
   * @param type_url 事件类型的URL
   * @param fn 事件回调函数
   */
  template <class T, class = atfw::util::nostd::enable_if_t<std::is_base_of<::google::protobuf::Message, T>::value>>
  static void global_setup_private_channel_event_callback(uintptr_t unique_guard,
                                                          chat_channel_event_callback_t ATFW_UTIL_MACRO_NONNULL fn) {
    if (global_setup_private_channel_event_is_existed(unique_guard)) {
      return;
    }

    rpc::dtmq::client_subscriber::set_event_callback_on_receive_event_by_message_type<T>(
        global_setup_private_channel_event_get_callback_set(),
        global_setup_private_channel_event_set_handle(unique_guard, fn));
  }

 private:
  rpc::dtmq::client_subscriber::ptr_t get_channel_by_key(const atfw::dtmq::DChannelIdKey& channel_key) const;

  static void setup_subscriber_callback(const rpc::dtmq::client_subscriber::ptr_t& channel);

  static rpc::dtmq::client_subscriber::event_callback_set_t& global_setup_private_channel_event_get_callback_set();
  static bool global_setup_private_channel_event_is_existed(uintptr_t unique_guard);
  static rpc::dtmq::client_subscriber::event_callback_on_receive_event_t global_setup_private_channel_event_set_handle(
      uintptr_t unique_guard, chat_channel_event_callback_t ATFW_UTIL_MACRO_NONNULL fn);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  time_t last_send_to_world_channel_timepoint_unix_sec_;
  std::string subscriber_key_;

  rpc::dtmq::client_subscriber::ptr_t world_chat_channel_;
  rpc::dtmq::client_subscriber::ptr_t private_chat_channel_;

  rpc::dtmq::client_subscriber::ptr_t sys_notification_channel_;
  rpc::dtmq::client_subscriber::ptr_t sys_announcement_channel_;
};

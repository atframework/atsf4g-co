// Copyright 2026 atframework
// Created by owent on 2026-09-22.
//

#pragma once

#include <memory/rc_ptr.h>
#include <nostd/nullability.h>

#include <data/friend_cache.h>

#include <rpc/rpc_common_types.h>

#include <memory/lru_map.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "data/friend_transaction_participator_handle.h"
#include "data/friend_wal_handle.h"

namespace rpc {
class context;
}

PROJECT_NAMESPACE_BEGIN
class table_friend;
class friend_transaction_data;
class DUserIDKey;
PROJECT_NAMESPACE_END

namespace atframework {
namespace friend_api {

class table_friend_blob_data;
class DFriendInfo;
class DFriendEvent;
class DFriendInvitationInfo;
class DFriendSubscribeKey;
class DFriendManagementNotificationEvent;

// NOLINTNEXTLINE(misc-multiple-inheritance)
class friend_object : public friend_cache, public std::enable_shared_from_this<friend_object> {
 public:
  using base_type = friend_cache;
  using ptr_t = std::shared_ptr<friend_object>;

 private:
  using friend_cache::ctor_guard_t;

 public:
  explicit friend_object(
      ctor_guard_t&,
      atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<friend_wal_publisher_type>>&& wal_publisher,
      atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle>>&&
          transaction_handle);
  ~friend_object() override;

  void init(rpc::context& ctx) override;

  static ptr_t create(rpc::context& ctx, uint32_t zone_id, uint64_t user_id);

  void on_loaded(rpc::context& ctx) override;

  void on_saved(rpc::context& ctx, uint64_t svr_id) override;

  bool is_writable() const noexcept override;

  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_friend& db_data) override;

  void dump(rpc::context& ctx, table_friend_blob_data& blob_data, bool with_transaction_data);

  bool add_friend(rpc::context& ctx, int64_t event_id, DFriendInfo& friend_data);

  bool remove_friend(rpc::context& ctx, int64_t event_id, DFriendInfo& friend_data);

  bool add_inviter(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src);

  bool remove_inviter(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src);

  bool remove_all_inviters(rpc::context& ctx, int64_t event_id);

  bool add_invitee(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src);

  bool remove_invitee(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src);

  bool add_gift(rpc::context& ctx, int64_t event_id, DFriendGift& src);

  bool remove_gift(rpc::context& ctx, int64_t event_id, DFriendGift& src);

  void refresh_feature_limit(rpc::context& ctx);

  rpc::result_code_type send_notification(rpc::context& ctx);

  int32_t check_prepare_transcation(rpc::context& ctx, const std::string& transaction_uuid,
                                    const ::google::protobuf::RepeatedPtrField<DFriendEvent>& events);

  int64_t allocate_event_id();

  void gm_reset_limit();

  void subscribe(rpc::context& ctx, const DFriendSubscribeKey& subscribe_key, uint64_t update_server_node_id);

  void unsubscribe(rpc::context& ctx, const DFriendSubscribeKey& subscribe_key);

  bool clear_all_data(rpc::context& ctx, int64_t event_id);

  bool is_empty() const;
  bool has_relation() const;

  friend_wal_publisher_type& get_wal_publisher() noexcept;
  const friend_wal_publisher_type& get_wal_publisher() const noexcept;

  friend_transaction_participator_handle& get_transaction_handle() noexcept;
  const friend_transaction_participator_handle& get_transaction_handle() const noexcept;

  atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::friend_transaction_data>
  mutable_transaction_participator_data(rpc::context& ctx, const std::string& transaction_uuid, friend_key_type key,
                                        const google::protobuf::Any& data);
  void remove_transaction_data(rpc::context& ctx, const std::string& transaction_uuid);

  void cleanup_invalid_friends(rpc::context& ctx, std::chrono::system_clock::time_point now);
  void cleanup_invalid_inviters(rpc::context& ctx, std::chrono::system_clock::time_point now);
  void cleanup_invalid_invitees(rpc::context& ctx, std::chrono::system_clock::time_point now);
  void cleanup_invalid_gifts(rpc::context& ctx, std::chrono::system_clock::time_point now);

  size_t get_current_friend_count() const noexcept;
  size_t get_current_inviter_count() const noexcept;

  const std::unordered_map<friend_key_type, DFriendInvitationInfo, friend_key_hash_type>& get_all_inviters() const {
    return inviters_;
  }

  bool append_notification_snapshot(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& subscriber_key);

  bool append_notification_event(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& subscriber_key,
                                 const DFriendEvent& event_data);

 private:
  void check_inviter_count_exceed(rpc::context& ctx, int64_t event_id);
  void set_quick_save() const;

 private:
  mutable bool already_setup_quick_save_;
  int64_t event_id_allocator_;
  atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<friend_wal_publisher_type>> wal_publisher_;
  atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle>>
      transaction_handle_;

  std::unordered_map<int64_t, DFriendGift> gifts_;
  std::unordered_map<friend_key_type, std::unordered_set<int64_t>, friend_key_hash_type> gift_sender_index_;
  std::unordered_map<friend_key_type, DFriendInvitationInfo, friend_key_hash_type> inviters_;
  std::unordered_map<friend_key_type, DFriendInvitationInfo, friend_key_hash_type> invitees_;
  std::unordered_map<friend_key_type, DFriendInfo, friend_key_hash_type> friends_;

  struct transaction_participator_data_cache_t {
    std::chrono::system_clock::time_point timeout;
    std::unordered_map<friend_key_type,  // participator key
                       atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::friend_transaction_data>,
                       friend_key_hash_type>
        participator_data;
  };

  atfw::util::memory::lru_map<std::string, transaction_participator_data_cache_t, std::hash<std::string>,
                              std::equal_to<>,
                              atfw::util::memory::lru_map_option<atfw::util::memory::compat_strong_ptr_mode::kStrongRc>>
      transaction_participator_data_cache_;

  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey,
                     atfw::util::memory::strong_rc_ptr<DFriendManagementNotificationEvent>, user_key_hash_t,
                     user_key_equal_t>
      pending_notification_event_log_;
  std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t>
      pending_notification_snapshot_;
};

}  // namespace friend_api
}  // namespace atframework

ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_BEGIN
template <class CharT>
struct formatter<atfw::friend_api::friend_object, CharT> : formatter<atfw::friend_api::friend_cache, CharT> {};
ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_END

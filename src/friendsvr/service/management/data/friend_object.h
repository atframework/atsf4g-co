// Copyright 2022 atframework
// Created by owent on 2022-03-01.
//

#pragma once

#include <memory/rc_ptr.h>

#include <data/friend_cache.h>

#include <rpc/rpc_common_types.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

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

class DFriendInfo;
class DFriendInvitationInfo;

class friend_object : public friend_cache {
 public:
  using base_type = friend_cache;
  using ptr_t = std::shared_ptr<friend_object>;

 private:
  using friend_cache::ctor_guard_t;

 public:
  explicit friend_object(ctor_guard_t&);
  ~friend_object() override;

  void init(rpc::context& ctx) override;

  static ptr_t create(rpc::context& ctx, uint32_t zone_id, uint64_t user_id);

  void on_loaded(rpc::context& ctx) override;

  void on_saved(rpc::context& ctx, uint64_t svr_id) override;

  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_friend& db_data) override;

  bool add_friend(rpc::context& ctx, int64_t event_id, DFriendInfo& friend_data);

  bool remove_friend(rpc::context& ctx, int64_t event_id, DFriendInfo& friend_data);

  bool add_inviter(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src);

  bool remove_inviter(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src);

  bool add_invitee(int64_t event_id, DFriendInvitationInfo& src);

  bool remove_invitee(int64_t event_id, DFriendInvitationInfo& src);

  void refresh_feature_limit(rpc::context& ctx);

  int32_t check_prepare_transcation(const ::google::protobuf::RepeatedPtrField<DFriendEvent>& events);

  int64_t allocate_event_id();

  void gm_reset_limit();

  bool clear_all_data(int64_t event_id);

  bool is_empty() const;
  bool has_relation() const;

  friend_wal_publisher_type& get_wal_publisher() noexcept;
  const friend_wal_publisher_type& get_wal_publisher() const noexcept;

  friend_transaction_participator_handle& get_transaction_handle() noexcept;
  const friend_transaction_participator_handle& get_transaction_handle() const noexcept;

  atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::friend_transaction_data>
  mutable_transaction_participator_data(const std::string& transaction_uuid, friend_key_type key,
                                        const google::protobuf::Any& data);
  void remove_transaction_data(const std::string& transaction_uuid);

  void cleanup_invalid_friends(time_t now, time_t max_expire_timepoint, int32_t& friend_number_cache);
  void cleanup_invalid_invitees(time_t now, time_t max_expire_timepoint, int32_t& invitee_number_cache);
  int32_t get_current_friend_num() const;
  int32_t get_current_inviter_num() const;

  const std::unordered_map<friend_key_type, DFriendInvitationInfo, friend_key_hash_type>& get_all_inviters() const {
    return inviters_;
  }

 private:
  void check_inviter_num_exceed(rpc::context& ctx, int64_t event_id);
  void set_quick_save() const;

 private:
  int64_t event_id_allocator_;
  atfw::util::memory::strong_rc_ptr<friend_wal_publisher_type> wal_publisher_;
  atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle> transaction_handle_;

  std::unordered_map<friend_key_type, DFriendInvitationInfo, friend_key_hash_type> inviters_;
  // std::unordered_map<friend_key_type, DFriendInvitationInfo, friend_key_hash_type> invitees_;
  std::unordered_map<friend_key_type, DFriendInfo, friend_key_hash_type> friends_;

  std::unordered_map<
      std::string,                         // transaction_uuid
      std::unordered_map<friend_key_type,  // participator key
                         atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::friend_transaction_data>,
                         friend_key_hash_type>>
      transaction_participator_data_cache_;
  std::list<std::pair<std::string, time_t>> transaction_participator_data_cache_expire_timepoint_;
};

}  // namespace friend_api
}  // namespace atframework

ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_BEGIN
template <class CharT>
struct formatter<atfw::friend_api::friend_object, CharT> : formatter<atfw::friend_api::friend_cache, CharT> {};
ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_END

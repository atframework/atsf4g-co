// Copyright 2026 atframework
// Created by owent on 2026-09-28

#pragma once

#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.friend_api.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_type_traits.h>

#include <data/user_key_hash_helper.h>

#include <chrono>

#include "logic/friend_api/friend_api_defs.h"
#include "logic/friend_api/friend_api_transaction_client_handle.h"
#include "logic/friend_api/friend_api_wal_client.h"

class user;

class user_friend_api_manager {
 public:
  using friends_cache_t = atfw::memory::stl::unordered_map<atfw::shared::DUserIDKey, atfw::friend_api::DFriendInfo,
                                                           user_key_hash_t, user_key_equal_t>;
  using invites_cache_t =
      atfw::memory::stl::unordered_map<atfw::shared::DUserIDKey, atfw::friend_api::DFriendInvitationInfo,
                                       user_key_hash_t, user_key_equal_t>;
  using gift_history_cache_t =
      atfw::memory::stl::unordered_map<atfw::shared::DUserIDKey, atfw::friend_api::DFriendGiftHistory, user_key_hash_t,
                                       user_key_equal_t>;
  using gifts_cache_t = atfw::memory::stl::unordered_map<int64_t, atfw::friend_api::DFriendGift>;
  using sns_friends_cache_t = atfw::memory::stl::unordered_map<uint64_t, atfw::friend_api::DFriendSNSInfo>;

 public:
  explicit user_friend_api_manager(user& owner);
  ~user_friend_api_manager();

  // 创建默认角色数据
  void create_init(rpc::context& ctx, uint32_t version_type);

  // 登入读取用户数据
  void login_init(rpc::context& ctx);

  // 触发订阅续期(心跳)
  void refresh_feature_limit_second(rpc::context& ctx);
  // 刷新功能限制次数
  void refresh_feature_limit_minute(rpc::context& ctx);

  static std::pair<bool, bool> refresh_feature_limit(rpc::context& ctx, std::chrono::system_clock::time_point now,
                                                     atfw::friend_api::DFriendStatistics& stats);

  void dump_stats(atfw::friend_api::DFriendStatistics& out) const;

  // 从table数据初始化
  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);

  void load_storage(rpc::context& ctx, const PROJECT_NAMESPACE_ID::user_friend_data& friend_data);

  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& user);

  void dump_storage(rpc::context& ctx, PROJECT_NAMESPACE_ID::user_friend_data& friend_data);

  bool is_dirty() const;

  void clear_dirty();

  bool is_async_task_running() const;

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type wait_for_async_task(rpc::context& ctx);

  int32_t invoke_async_task(rpc::context& ctx);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type pull_friend_data(rpc::context& ctx);

  int32_t load_logs(rpc::context& ctx, const google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendEvent>& logs);

  void load_snapshot(rpc::context& ctx, const atfw::friend_api::table_friend_blob_data& snapshot_data);

  void cleanup_friend_data(rpc::context& ctx);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type gm_reset_limits(rpc::context& ctx);

  // 和get_friend_cache的区别是这里过期返回nullptr
  const atfw::friend_api::DFriendInfo* get_friend(const atfw::shared::DUserIDKey& user_key) const;
  // 和get_inviter_cache的区别是这里过期返回nullptr
  const atfw::friend_api::DFriendInvitationInfo* get_inviter(const atfw::shared::DUserIDKey& user_key) const;
  // 和get_invitee_cache的区别是这里过期返回nullptr
  const atfw::friend_api::DFriendInvitationInfo* get_invitee(const atfw::shared::DUserIDKey& user_key) const;
  // 和get_gift_cache的区别是这里过期返回nullptr
  const atfw::friend_api::DFriendGift* get_gift(int64_t gift_id) const;

  /**
   * @brief 发送好友邀请的分布式事务接口
   * @param user_id 对方用户ID
   * @param user_id 对方大区
   * @return 0或CS错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_invite(rpc::context& ctx,
                                                                 const atfw::shared::DUserIDKey& user_key);

  /**
   * @brief 接受好友邀请的分布式事务接口
   * @param user_id 对方用户ID
   * @param zone_id 对方大区
   * @return 0或CS错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type accept_invite(rpc::context& ctx,
                                                                   const atfw::shared::DUserIDKey& user_key);

  /**
   * @brief 拒绝好友邀请的分布式事务接口
   * @param user_id 对方用户ID
   * @param zone_id 对方大区
   * @return 0或CS错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_invite(rpc::context& ctx,
                                                                   const atfw::shared::DUserIDKey& user_key);

  /**
   * @brief 移除好友的分布式事务接口
   * @param user_id 对方用户ID
   * @param zone_id 对方大区
   * @return 0或CS错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_friend(rpc::context& ctx,
                                                                   const atfw::shared::DUserIDKey& user_key);

  /**
   * @brief 发送礼物
   * @param user_id 对方用户ID
   * @param zone_id 对方大区
   * @param gift_type_id 礼物类型ID
   * @return 0或CS错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_gift(rpc::context& ctx,
                                                               const atfw::shared::DUserIDKey& user_key,
                                                               int32_t gift_type_id);

  /**
   * @brief 接收礼物
   * @param gift_ids 礼物ID列表
   * @return 0或CS错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type receive_gifts(
      rpc::context& ctx, std::vector<int64_t>& gift_ids,
      ::google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendGift>* out = nullptr);

  int patch_stats(const atfw::friend_api::DFriendStatistics& stats);
  bool add_event_dirty(atfw::friend_api::DFriendEvent&& evt_data);

  void add_send_gift_times(int32_t times);
  void add_receive_gift_times(int32_t times);

  bool add_gift_send_list(const atfw::friend_api::DFriendGiftHistory& history);
  bool remove_gift_send_list(const atfw::friend_api::DFriendGiftHistory& history);

  bool add_gift_receive_list(const atfw::friend_api::DFriendGiftHistory& history);

  // 获取本地统计信息（主要是礼物发送和接收的次数）
  inline const atfw::friend_api::DFriendStatistics& get_local_stats() const { return local_friend_statistics_; }

  // 获取远程统计信息（主要是发送邀请和被邀请的次数）
  inline const atfw::friend_api::DFriendStatistics& get_remote_stats() const { return remote_friend_statistics_; }

  inline const gift_history_cache_t& get_today_send_gift() const { return daily_send_list_; }
  inline const gift_history_cache_t& get_today_receive_gift() const { return daily_receive_list_; }

  bool add_friend_cache(rpc::context& ctx, const atfw::friend_api::DFriendInfo& friend_data, bool need_notify = true);
  void remove_friend_cache(rpc::context& ctx, const atfw::shared::DUserIDKey& friend_key, bool need_notify = true);
  const atfw::friend_api::DFriendInfo* get_friend_cache(const atfw::shared::DUserIDKey& friend_key) const;
  inline const friends_cache_t& get_all_friend_cache() const { return friend_cache_set_; }

  bool add_inviter_cache(rpc::context& ctx, const atfw::friend_api::DFriendInvitationInfo& invite_data,
                         bool need_notify = true);
  void remove_inviter_cache(rpc::context& ctx, const atfw::shared::DUserIDKey& friend_key, bool need_notify = true);
  const atfw::friend_api::DFriendInvitationInfo* get_inviter_cache(const atfw::shared::DUserIDKey& friend_key) const;
  inline const invites_cache_t& get_all_inviter_cache() const { return inviter_cache_set_; }
  atfw::shared::DUserIDKey get_key_from_inviter_cache(const atfw::friend_api::DFriendInvitationInfo& invite_data) const;

  bool add_invitee_cache(rpc::context& ctx, const atfw::friend_api::DFriendInvitationInfo& invite_data,
                         bool need_notify = true);
  void remove_invitee_cache(rpc::context& ctx, const atfw::shared::DUserIDKey& friend_key, bool need_notify = true);
  const atfw::friend_api::DFriendInvitationInfo* get_invitee_cache(const atfw::shared::DUserIDKey& friend_key) const;
  inline const invites_cache_t& get_all_invitee_cache() const { return invitee_cache_set_; }
  atfw::shared::DUserIDKey get_key_from_invitee_cache(const atfw::friend_api::DFriendInvitationInfo& invite_data) const;

  bool add_gift_cache(rpc::context& ctx, const atfw::friend_api::DFriendGift& gift_data, bool need_notify = true);
  void remove_gift_cache(rpc::context& ctx, int64_t gift_id, bool need_notify = true);
  const atfw::friend_api::DFriendGift* get_gift_cache(int64_t gift_id) const;
  inline const gifts_cache_t& get_all_gift_cache() const { return gift_cache_set_; }

  bool add_sns_friend_cache(rpc::context& ctx, const atfw::friend_api::DFriendSNSInfo& friend_data);
  void remove_sns_friend_cache(rpc::context& ctx, uint64_t user_id);
  const atfw::friend_api::DFriendSNSInfo* get_sns_friend_cache(uint64_t user_id) const;
  inline const sns_friends_cache_t& get_all_sns_friend_cache() const { return sns_friend_cache_set_; }

  // ================ 社交分享类接口 ================
#if 0
  void update_sns_share(bool daily_reset, bool weekly_reset);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_sns_share(rpc::context& ctx, int32_t share_type, int32_t sub_type, atfw::friend_api::SNSShareRecord*& out,
                    ::google::protobuf::RepeatedPtrField<atfw::friend_api::DItemOffset>* out_reward_items = nullptr);
  atfw::friend_api::SNSShareRecord* mutable_sns_share(int32_t share_type);
  void clear_sns_share(int32_t share_type = 0);
  void dump_sns_share(atfw::friend_api::UserSNSData& out, int32_t share_type = 0);
#endif

  inline user& get_owner() noexcept { return *owner_; }
  inline const user& get_owner() const noexcept { return *owner_; }

  bool is_friend_full() const noexcept;

 private:
  bool is_sns_friend_available() const noexcept;
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type pull_sns_friend_data(rpc::context& ctx);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  bool is_dirty_;
  bool is_remote_data_ready_;
  bool need_send_wal_heartbeat_;
  bool need_pull_sns_friend_;
  std::chrono::system_clock::time_point next_pull_data_timepoint_;

  friends_cache_t friend_cache_set_;
  invites_cache_t inviter_cache_set_;
  invites_cache_t invitee_cache_set_;
  gifts_cache_t gift_cache_set_;
  sns_friends_cache_t sns_friend_cache_set_;
  atfw::friend_api::DFriendStatistics remote_friend_statistics_;
  atfw::friend_api::DFriendStatistics local_friend_statistics_;
  gift_history_cache_t daily_send_list_;
  gift_history_cache_t daily_receive_list_;
  std::unordered_set<int64_t> confirm_remove_gifts_;

  google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendEvent> dirty_cache_;

  mutable task_type_trait::task_type friend_async_task_;

  atfw::util::memory::strong_rc_ptr<user_friend_api_wal_client_type> wal_client_;
  atfw::util::memory::strong_rc_ptr<friend_api_transaction_client_handle> transaction_client_;

  // atfw::friend_api::UserSNSData sns_data_;
};

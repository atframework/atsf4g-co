// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

#include <rpc/team/team_key_hash_helper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.team.shared.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstddef>
#include <list>
#include <unordered_map>

#include "logic/team/user_team.h"

class user;

PROJECT_NAMESPACE_BEGIN
class table_user;
PROJECT_NAMESPACE_END

class user_team_manager {
 public:
  using team_join_request_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamJoinRequest>;
  using team_invitation_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamInvitation>;

  struct team_group {
    user_team::ptr_t current;

    // 同组队伍只能存在一个
    std::unordered_map<atfw::team::DTeamKey, user_team::ptr_t, rpc::team::team_api::team_key_hash_t,
                       rpc::team::team_api::team_key_equal_t>
        pending_to_exit;
  };

 public:
  explicit user_team_manager(user& owner);
  ~user_team_manager();

  // 创建默认角色数据
  void create_init(rpc::context& ctx);

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t login_init(rpc::context&);

  void refresh_feature_limit_second(rpc::context&);

  // 从table数据初始化
  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);

  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& table) const;

  bool is_dirty() const;

  void clear_dirty();

  inline user& get_owner() { return *owner_; }
  inline const user& get_owner() const { return *owner_; }

 private:
  void set_processed_private_chat_channel_sequence(int64_t sequence);

  void add_team(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType team_type, const atfw::team::DTeamKey& team_key,
                const atfw::dtmq::DChannelIdKey& channel_key);

  void remove_team(rpc::context& ctx, const atfw::team::DTeamKey& team_key, bool send_exit,
                   atfw::team::EnTeamExitReason exit_reason);

  size_t cleanup_expired_join_request(rpc::context& ctx);
  bool add_pending_join_request(rpc::context& ctx, const team_join_request_ptr_t& join_request);
  bool remove_pending_join_request(rpc::context& ctx, const atfw::team::DTeamKey& team_key);

  size_t cleanup_expired_invitation(rpc::context& ctx);
  bool add_pending_invitation(rpc::context& ctx, const team_invitation_ptr_t& invitation);
  bool remove_pending_invitation(rpc::context& ctx, const atfw::team::DTeamKey& team_key);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;

  bool is_dirty_;

  int64_t processed_private_chat_channel_sequence_;

  friend class user_team_manager_utility;

  // 可以同时在多个队伍（分组）中，但同组队伍只能存在一个
  std::unordered_map<uint32_t, team_group> team_group_;
  std::unordered_map<atfw::team::DTeamKey, user_team::ptr_t, rpc::team::team_api::team_key_hash_t,
                     rpc::team::team_api::team_key_equal_t>
      team_index_;

  std::list<team_join_request_ptr_t> pending_join_request_by_expired_time_;
  std::unordered_map<atfw::team::DTeamKey, std::list<team_join_request_ptr_t>::iterator,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      pending_join_request_by_team_id_;

  std::list<team_invitation_ptr_t> pending_invitation_by_expired_time_;
  std::unordered_map<atfw::team::DTeamKey, std::list<team_invitation_ptr_t>::iterator,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      pending_invitation_by_team_id_;
};

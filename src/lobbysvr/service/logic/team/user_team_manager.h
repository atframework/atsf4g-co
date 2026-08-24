// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

#include <rpc/team/team_key_hash_helper.h>

#include <cstddef>
#include <list>
#include <unordered_map>

class user;

class user_team_manager {
 public:
  using team_join_request_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamJoinRequest>;
  using team_invitation_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamInvitation>;

 public:
  explicit user_team_manager(user& owner);
  ~user_team_manager();

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t login_init(rpc::context&);

  void refresh_feature_limit_second(rpc::context&);

  inline user& get_owner() { return *owner_; }
  inline const user& get_owner() const { return *owner_; }

 private:
  size_t cleanup_expired_join_request(rpc::context& ctx);
  bool add_pending_join_request(rpc::context& ctx, const team_join_request_ptr_t& join_request);
  bool remove_pending_join_request(rpc::context& ctx, const atfw::team::DTeamKey& team_key);

  size_t cleanup_expired_invitation(rpc::context& ctx);
  bool add_pending_invitation(rpc::context& ctx, const team_invitation_ptr_t& invitation);
  bool remove_pending_invitation(rpc::context& ctx, const atfw::team::DTeamKey& team_key);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;

  friend class user_team_manager_utility;

  std::list<team_join_request_ptr_t> pending_join_request_by_expired_time_;
  std::unordered_map<atfw::team::DTeamKey, std::list<team_join_request_ptr_t>::iterator,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      pending_join_request_by_team_id_;

  std::list<team_invitation_ptr_t> pending_invitation_by_expired_time_;
  std::unordered_map<atfw::team::DTeamKey, std::list<team_invitation_ptr_t>::iterator,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      pending_invitation_by_team_id_;
};

// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>
#include <memory/rc_ptr.h>
#include <nostd/nullability.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/com.struct.team.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <rpc/dtmq/dtmq_client_subscriber.h>

#include <chrono>

class user;
class user_team_manager;

class user_team : public atfw::util::memory::enable_shared_rc_from_this<user_team> {
 public:
  using ptr_t = atfw::util::memory::strong_rc_ptr<user_team>;

 private:
  struct ctor_guard_t;

 public:
  user_team(ctor_guard_t&, rpc::context& ctx, user_team_manager& owner,
            atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&& channel_subscriber, uint32_t team_type,
            const atfw::team::DTeamKey& team_key);

  ~user_team();

  static ptr_t create(rpc::context& ctx, user_team_manager& owner, uint32_t team_type,
                      const atfw::team::DTeamKey& team_key, const atfw::dtmq::DChannelIdKey& channel_key);

  void init_cached_data(const PROJECT_NAMESPACE_ID::DUserIDKey& captain_user_key,
                        atfw::team::EnTeamPermissionRole permission_role);

  void dump(atfw::team::DTeamMemberJoinData& join_data) const;

  bool can_be_removed(rpc::context& ctx) const noexcept;

  bool wait_to_be_member_but_timeout(rpc::context& ctx) const noexcept;

  bool is_exiting() const noexcept;

  inline uint32_t get_team_type() const noexcept { return team_type_; }

  inline const atfw::team::DTeamKey& get_team_key() const noexcept { return team_key_; }

  const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept;

  inline const PROJECT_NAMESPACE_ID::DUserIDKey& get_cached_captain_user_key() const noexcept {
    return cached_captain_user_key_;
  }

  inline atfw::team::EnTeamPermissionRole get_cached_permission_role() const noexcept {
    return cached_permission_role_;
  }

  void make_current_actived(rpc::context& ctx);

  void send_exit_team_request(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason);

  void set_exit_team(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason);

  void retry_send_exit_team_request(rpc::context& ctx);

  void try_load_snapshot(rpc::context& ctx);

 private:
  void load_snapshot(rpc::context& ctx);

  void on_receive_raw_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& data);

 private:
  friend class user_team_utility;

  user_team_manager* ATFW_UTIL_MACRO_NONNULL owner_;
  uint32_t team_type_;
  atfw::team::DTeamKey team_key_;
  atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t> channel_subscriber_;
  bool is_member_;
  int64_t channel_create_sequence_;

  std::chrono::system_clock::time_point actived_timepoint_;
  std::chrono::system_clock::time_point last_exit_team_request_timepoint_;
  atfw::team::EnTeamExitReason last_exit_team_reason_;

  PROJECT_NAMESPACE_ID::DUserIDKey cached_captain_user_key_;
  atfw::team::EnTeamPermissionRole cached_permission_role_;
};

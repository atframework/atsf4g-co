// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>
#include <memory/rc_ptr.h>

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
  user_team(ctor_guard_t&, user_team_manager& owner, uint32_t team_type, const atfw::team::DTeamKey& team_key,
            const atfw::dtmq::DChannelIdKey& channel_key);

  ~user_team();

  static ptr_t create(user_team_manager& owner, uint32_t team_type, const atfw::team::DTeamKey& team_key,
                      const atfw::dtmq::DChannelIdKey& channel_key);

  inline uint32_t get_team_type() const noexcept { return team_type_; }

  inline const atfw::team::DTeamKey& get_team_key() const noexcept { return team_key_; }

  void make_current_actived(rpc::context& ctx);

  void send_exit_team_request(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason);

  void retry_send_exit_team_request(rpc::context& ctx);

 private:
  friend class user_team_utility;

  user_team_manager* ATFW_UTIL_MACRO_NONNULL owner_;
  uint32_t team_type_;
  atfw::team::DTeamKey team_key_;
  rpc::dtmq::client_subscriber::ptr_t channel_subscriber_;

  std::chrono::system_clock::time_point last_exit_team_request_timepoint_;
  atfw::team::EnTeamExitReason last_exit_team_reason_;
};

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

class user;

class user_team {
 public:
  using ptr_t = atfw::util::memory::strong_rc_ptr<user_team>;

 private:
  struct ctor_guard_t;

 public:
  user_team(ctor_guard_t&, user& owner, const atfw::team::DTeamKey& team_key,
            const atfw::dtmq::DChannelIdKey& channel_key);

  ~user_team();

 private:
  atfw::team::DTeamKey team_key_;
  rpc::dtmq::client_subscriber::ptr_t channel_subscriber_;
};

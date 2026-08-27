// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/com.struct.team.shared.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstdint>

class user_team_algorithm {
 public:
  static PROJECT_NAMESPACE_ID::EnTeamType get_team_type(const atfw::team::DTeamMemberJoinData&) noexcept;
  static PROJECT_NAMESPACE_ID::EnTeamType get_team_type(const atfw::team::DTeamJoinRequest&) noexcept;
  static PROJECT_NAMESPACE_ID::EnTeamType get_team_type(const atfw::team::DTeamInvitation&) noexcept;

  static int64_t make_team_shared_data_key(const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data);

  static int64_t make_team_member_shared_data_key(const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data);

  static bool allow_client_update_team_shared_data(const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data);

  static bool allow_client_update_team_member_shared_data(
      const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data);
};

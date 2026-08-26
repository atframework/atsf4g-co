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

  static int64_t make_team_shared_data_key(PROJECT_NAMESPACE_ID::EnTeamSharedModuleType module_type,
                                           int32_t module_data_id);

  static PROJECT_NAMESPACE_ID::EnTeamSharedModuleType parse_team_shared_data_module_type(int64_t key);
  static int32_t parse_team_shared_data_module_data_id(int64_t key);

  static bool allow_client_update_team_shared_data(PROJECT_NAMESPACE_ID::EnTeamSharedModuleType module_type,
                                                   int32_t module_data_id);

  static bool allow_client_update_team_member_shared_data(PROJECT_NAMESPACE_ID::EnTeamSharedModuleType module_type,
                                                          int32_t module_data_id);
};

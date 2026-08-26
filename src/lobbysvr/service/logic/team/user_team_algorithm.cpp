// Copyright 2026 atframework

#include "logic/team/user_team_algorithm.h"

PROJECT_NAMESPACE_ID::EnTeamType user_team_algorithm::get_team_type(const atfw::team::DTeamMemberJoinData&) noexcept {
  return PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL;
}
PROJECT_NAMESPACE_ID::EnTeamType user_team_algorithm::get_team_type(const atfw::team::DTeamJoinRequest&) noexcept {
  return PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL;
}
PROJECT_NAMESPACE_ID::EnTeamType user_team_algorithm::get_team_type(const atfw::team::DTeamInvitation&) noexcept {
  return PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL;
}

int64_t user_team_algorithm::make_team_shared_data_key(PROJECT_NAMESPACE_ID::EnTeamSharedModuleType module_type,
                                                       int32_t module_data_id) {
  return (static_cast<int64_t>(module_data_id) & 0xFFFF) | (static_cast<int64_t>(module_type) << 16);
}

PROJECT_NAMESPACE_ID::EnTeamSharedModuleType user_team_algorithm::parse_team_shared_data_module_type(int64_t key) {
  return static_cast<PROJECT_NAMESPACE_ID::EnTeamSharedModuleType>((key >> 16) & 0xFFFF);
}

int32_t user_team_algorithm::parse_team_shared_data_module_data_id(int64_t key) {
  return static_cast<int32_t>(key & 0xFFFF);
}

bool user_team_algorithm::allow_client_update_team_shared_data(PROJECT_NAMESPACE_ID::EnTeamSharedModuleType module_type,
                                                               int32_t module_data_id) {
  switch (module_type) {
    case PROJECT_NAMESPACE_ID::EN_TEAM_SHARED_MODULE_TYPE_BATTLE: {
      return module_data_id == PROJECT_NAMESPACE_ID::EN_TEAM_SHARED_DATA_BATTLE_MATCHING;
    }
    default:
      return false;
  }
}

bool user_team_algorithm::allow_client_update_team_member_shared_data(
    PROJECT_NAMESPACE_ID::EnTeamSharedModuleType module_type, int32_t module_data_id) {
  switch (module_type) {
    case PROJECT_NAMESPACE_ID::EN_TEAM_SHARED_MODULE_TYPE_BATTLE: {
      return module_data_id == PROJECT_NAMESPACE_ID::EN_TEAM_MEMBER_SHARED_DATA_CHARACTER_READY;
    }
    default:
      return false;
  }
}

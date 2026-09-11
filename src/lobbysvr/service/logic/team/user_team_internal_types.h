// Copyright 2026 atframework

#pragma once

#ifndef __PROJECT_INTERNAL_INCLUDE_GUARD_USER_TEAM_INTERNAL_TYPES_H__
#  error "This file is a internal types definition for team module and should not be included by other files."
#endif

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/com.struct.team.shared.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

// (DUserTeamSnapshot.unpacked_member_data)，不下发内部路由字段
struct user_team_member_cache {
  // 共享成员数据由下方的 key-value 索引维护
  atfw::team::DTeamMember member_data;

  // 成员共享数据(解包后的模块数据，key 算法见 user_team_algorithm::make_team_member_shared_data_key)
  std::unordered_map<int64_t, PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> shared_member_data;
};

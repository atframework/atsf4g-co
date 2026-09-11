// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>
#include <memory/rc_ptr.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/com.struct.team.shared.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace rpc {
class context;
}

class user;
class user_team;
struct user_team_member_cache;

class user_team_battle_library_function {
 public:
  static void pack_default_team_shared_data(
      rpc::context& ctx, user& user_inst,
      ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output);

  static bool is_matching(const user_team& team) noexcept;
  static const PROJECT_NAMESPACE_ID::DMatchingTeamSyncView& get_matching_team_sync_view(const user_team& team) noexcept;

  static void pack_default_member_shared_data(
      rpc::context& ctx, user& user_inst,
      ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output);

  static bool foreach_member(
      rpc::context& ctx, const user_team& team,
      atfw::util::nostd::function_ref<bool(rpc::context&, const user_team_member_cache&)> fn) noexcept;

  static atfw::util::memory::strong_rc_ptr<user_team_member_cache> find_member(
      const user_team& team, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) noexcept;

  static bool is_ready(const user_team_member_cache& team) noexcept;
  static const PROJECT_NAMESPACE_ID::DMatchingTeamParameter& get_matching_team_parameter(
      const user_team_member_cache& team) noexcept;
};

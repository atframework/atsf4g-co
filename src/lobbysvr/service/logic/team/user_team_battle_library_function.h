// Copyright 2026 atframework

#pragma once

#include <nostd/function_ref.h>

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

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
  static void register_allow_client_update_team_shared_data(const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data);
  static void register_allow_client_update_member_shared_data(
      const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data);

  static bool allow_client_update_team_shared_data(const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data);

  static bool allow_client_update_team_member_shared_data(
      const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data);

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

  // ============================ 跨模块胶水层 ============================
  static void auto_check_and_correct_team_data(rpc::context& ctx, user_team& team);

  static void glue_layer_normalize_team_action_update_matching(
      rpc::context& ctx, user_team& team, bool matching,
      atfw::util::nostd::function_ref<PROJECT_NAMESPACE_ID::DTeamSharedDataModule*(int64_t key)>);

  static void glue_layer_event_on_team_action_update_matching(rpc::context& ctx, user_team& team, bool matching);

  static void glue_layer_event_on_matching_action_start_matching_finished(rpc::context& ctx, user_team& team);

  static void glue_layer_event_on_team_action_update_matching_team_view(
      rpc::context& ctx, user_team& team, const PROJECT_NAMESPACE_ID::DMatchingTeamSyncView& team_view);

  static void glue_layer_normalize_member_action_update_ready(
      rpc::context& ctx, user_team& team, bool ready,
      atfw::util::nostd::function_ref<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule*(int64_t key)>);
};

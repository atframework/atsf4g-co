// Copyright 2026 atframework

#include "logic/team/user_team_battle_library_function.h"

#include <rpc/rpc_context.h>

#include <cstdint>
#include <unordered_set>

#include "data/user.h"

#include "logic/matching/user_matching_manager.h"
#include "logic/team/user_team.h"
#include "logic/team/user_team_algorithm.h"
#include "logic/team/user_team_manager.h"

#define __PROJECT_INTERNAL_INCLUDE_GUARD_USER_TEAM_INTERNAL_TYPES_H__
#include "logic/team/user_team_internal_types.h"

namespace {
static std::unordered_set<int64_t> build_allow_client_update_team_shared_data_set();
static std::unordered_set<int64_t> build_allow_client_update_member_shared_data_set();

static std::unordered_set<int64_t>& get_allow_client_update_team_shared_data_set() {
  static std::unordered_set<int64_t> res = build_allow_client_update_team_shared_data_set();
  return res;
}
static std::unordered_set<int64_t>& get_allow_client_update_member_shared_data_set() {
  static std::unordered_set<int64_t> res = build_allow_client_update_member_shared_data_set();
  return res;
}

static std::unordered_set<int64_t> build_allow_client_update_team_shared_data_set() {
  // Build and return the set of team shared data keys that are allowed to be updated by the client.
  std::unordered_set<int64_t> ret;

  // 匹配只能由内部流程发起，不允许用户发起
  return ret;
}

static std::unordered_set<int64_t> build_allow_client_update_member_shared_data_set() {
  // Build and return the set of member shared data keys that are allowed to be updated by the client.
  std::unordered_set<int64_t> ret;

  {
    PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule data;
    data.mutable_battle()->set_ready(true);
    ret.emplace(user_team_algorithm::make_team_member_shared_data_key(data));
  }
  return ret;
}
}  // namespace

void user_team_battle_library_function::register_allow_client_update_team_shared_data(
    const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data) {
  int64_t key = user_team_algorithm::make_team_shared_data_key(data);
  if (key == 0) {
    FWLOGERROR("Failed to generate team shared data key from {}", data.DebugString());
    return;
  }
  get_allow_client_update_team_shared_data_set().emplace(key);
}

void user_team_battle_library_function::register_allow_client_update_member_shared_data(
    const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data) {
  int64_t key = user_team_algorithm::make_team_member_shared_data_key(data);
  if (key == 0) {
    FWLOGERROR("Failed to generate team member shared data key from {}", data.DebugString());
    return;
  }
  get_allow_client_update_member_shared_data_set().emplace(key);
}

bool user_team_battle_library_function::allow_client_update_team_shared_data(
    const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data) {
  int64_t key = user_team_algorithm::make_team_shared_data_key(data);
  return get_allow_client_update_team_shared_data_set().count(key) > 0;
}

bool user_team_battle_library_function::allow_client_update_team_member_shared_data(
    const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data) {
  int64_t key = user_team_algorithm::make_team_member_shared_data_key(data);
  return get_allow_client_update_member_shared_data_set().count(key) > 0;
}

void user_team_battle_library_function::pack_default_team_shared_data(
    rpc::context& ctx, user& user_inst,
    ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output) {
  {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamSharedDataModule> wrapper{ctx};
    wrapper->mutable_battle()->set_matching(false);

    auto* output_field = output.Add();
    output_field->set_key(user_team_algorithm::make_team_shared_data_key(*wrapper));
    output_field->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!output_field->mutable_value()->mutable_data()->PackFrom(*wrapper)) {
      FCTXLOGERROR(ctx, "{} pack_team_shared_data: failed to pack team shared data", user_inst);
      output.RemoveLast();
    }
  }

  {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamSharedDataModule> wrapper{ctx};
    wrapper->mutable_battle()->mutable_matching_team_view();

    auto* output_field = output.Add();
    output_field->set_key(user_team_algorithm::make_team_shared_data_key(*wrapper));
    output_field->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!output_field->mutable_value()->mutable_data()->PackFrom(*wrapper)) {
      FCTXLOGERROR(ctx, "{} pack_team_shared_data: failed to pack team shared data", user_inst);
      output.RemoveLast();
    }
  }
}

bool user_team_battle_library_function::is_matching(const user_team& team) noexcept { return team.is_matching(); }

void user_team_battle_library_function::pack_default_member_shared_data(
    rpc::context& ctx, user& user_inst,
    ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output) {
  {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> wrapper{ctx};
    wrapper->mutable_battle()->set_ready(false);

    auto* output_field = output.Add();
    output_field->set_key(user_team_algorithm::make_team_member_shared_data_key(*wrapper));
    output_field->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!output_field->mutable_value()->mutable_data()->PackFrom(*wrapper)) {
      FCTXLOGERROR(ctx, "{} pack_team_member_shared_data: failed to pack team member shared data", user_inst);
      output.RemoveLast();
    }
  }

  {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> wrapper{ctx};
    wrapper->mutable_battle()->mutable_matching_parameter();

    auto* output_field = output.Add();
    output_field->set_key(user_team_algorithm::make_team_member_shared_data_key(*wrapper));
    output_field->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!output_field->mutable_value()->mutable_data()->PackFrom(*wrapper)) {
      FCTXLOGERROR(ctx, "{} pack_team_member_shared_data: failed to pack team member shared data", user_inst);
      output.RemoveLast();
    }
  }
}

bool user_team_battle_library_function::foreach_member(
    rpc::context& ctx, const user_team& team,
    atfw::util::nostd::function_ref<bool(rpc::context&, const user_team_member_cache&)> fn) noexcept {
  return team.foreach_member(ctx, fn);
}

atfw::util::memory::strong_rc_ptr<user_team_member_cache> user_team_battle_library_function::find_member(
    const user_team& team, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) noexcept {
  return team.find_member(user_key);
}

void user_team_battle_library_function::auto_check_and_correct_team_data(rpc::context& ctx, user_team& team) {
  bool team_data_is_matching = team.is_matching();
  auto& matching_mgr = team.get_owner().get_owner().get_user_matching_manager();

  do {
    if (!team.is_member()) {
      break;
    }
    bool team_is_captain = team.is_captain();

    if (team_data_is_matching) {
      if (team_is_captain && !matching_mgr.is_in_matching()) {
        // 状态已失效,取消队伍的匹配状态
        atframework::shared::DTeamSharedDataModule team_data;
        team_data.mutable_battle()->set_matching(false);
        team.async_send_team_shared_data(ctx, std::move(team_data));
        break;
      }

      if (matching_mgr.is_in_matching_start()) {
        // 组队匹配状态正确，但未发起过 start_matching 重试发起callback_start_matching
        matching_mgr.callback_start_matching(ctx, true, 0);
        break;
      }
    } else {
      // 重试补充组队匹配状态
      if (team_is_captain && matching_mgr.is_in_matching()) {
        atframework::shared::DTeamSharedDataModule team_data;
        team_data.mutable_battle()->set_matching(true);
        team.async_send_team_shared_data(ctx, std::move(team_data));
        break;
      }

      if (matching_mgr.is_in_matching_start()) {
        // 组队匹配状态正确，但未发起过 start_matching 重试发起callback_start_matching
        matching_mgr.callback_start_matching(ctx, false, 0);
        break;
      }
    }
  } while (false);

  // 正在匹配时，且已经收到TeamView之后，所有的成员都要通知更新TeamView
  if (team_data_is_matching && team.is_member()) {
    const PROJECT_NAMESPACE_ID::DMatchingTeamSyncView& team_view = get_matching_team_sync_view(team);
    if (team_view.unit_id() != 0) {
      matching_mgr.subscribe_matching_unit(ctx, team_view);
    }
  }
}

void user_team_battle_library_function::glue_layer_normalize_team_action_update_matching(
    rpc::context& ctx, user_team& team, bool matching,
    atfw::util::nostd::function_ref<PROJECT_NAMESPACE_ID::DTeamSharedDataModule*(int64_t key)> fn) {
  // 设置匹配时需要补充DMatchingTeamSyncView数据
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamSharedDataModule> key_maker{ctx};
  key_maker->mutable_battle()->mutable_matching_team_view();
  auto key = user_team_algorithm::make_team_shared_data_key(*key_maker);

  PROJECT_NAMESPACE_ID::DTeamSharedDataModule* autocomplete_data = fn(key);

  if (matching) {
    auto& matching_mgr = team.get_owner().get_owner().get_user_matching_manager();
    matching_mgr.fetch_team_sync_matching_view(ctx, *autocomplete_data->mutable_battle()->mutable_matching_team_view());
  } else {
    // 不在匹配则清空数据
    autocomplete_data->mutable_battle()->mutable_matching_team_view();
  }
}

void user_team_battle_library_function::glue_layer_event_on_matching_action_start_matching_check_function_passed(
    rpc::context& ctx, user_team& team) {
  // 只有队长允许改变状态
  if (!team.is_captain()) {
    FCTXLOGERROR(
        ctx, "{} is not the captain of team {}:{}. Only the team captain is allowed to change the matching status.",
        team.get_owner().get_owner().get_user_id(), team.get_team_key().zone_id(), team.get_team_key().team_id());
    return;
  }

  atframework::shared::DTeamSharedDataModule team_data;
  team_data.mutable_battle()->set_matching(true);
  team.async_send_team_shared_data(ctx, std::move(team_data));
}

void user_team_battle_library_function::glue_layer_event_on_matching_action_matching_finish_final(rpc::context& ctx,
                                                                                                  user_team& team) {
  // 只有队长允许改变状态
  if (!team.is_captain()) {
    FCTXLOGERROR(
        ctx, "{} is not the captain of team {}:{}. Only the team captain is allowed to change the matching status.",
        team.get_owner().get_owner().get_user_id(), team.get_team_key().zone_id(), team.get_team_key().team_id());
    return;
  }

  atframework::shared::DTeamSharedDataModule team_data;
  team_data.mutable_battle()->set_matching(false);
  team.async_send_team_shared_data(ctx, std::move(team_data));
}

void user_team_battle_library_function::glue_layer_event_on_team_action_update_matching(rpc::context& ctx,
                                                                                        user_team& team,
                                                                                        bool matching) {
  auto& matching_mgr = team.get_owner().get_owner().get_user_matching_manager();

  do {
    if (!team.is_member()) {
      break;
    }

    // 恢复发起匹配和队伍状态更新的流程仅队长能发起
    if (matching && team.is_captain() && !matching_mgr.is_in_matching()) {
      // 状态已失效,取消队伍的匹配状态
      atframework::shared::DTeamSharedDataModule team_data;
      team_data.mutable_battle()->set_matching(false);
      team.async_send_team_shared_data(ctx, std::move(team_data));
      break;
    }

    // 取消匹配
    if (!matching) {
      if (matching_mgr.is_in_matching_start()) {
        matching_mgr.callback_start_matching(ctx, false, 0);
      }
    } else {
      // 继续匹配流程
      if (matching_mgr.is_in_matching_start()) {
        matching_mgr.callback_start_matching(ctx, true, 0);
      }
    }
  } while (false);
}

void user_team_battle_library_function::glue_layer_event_on_matching_action_start_matching_finished(rpc::context& ctx,
                                                                                                    user_team& team) {
  if (!team.is_member()) {
    return;
  }

  // 收到匹配完成的通知，要通过队伍频道广播给所有队员
  auto& matching_mgr = team.get_owner().get_owner().get_user_matching_manager();

  atframework::shared::DTeamSharedDataModule team_data;
  matching_mgr.fetch_team_sync_matching_view(ctx, *team_data.mutable_battle()->mutable_matching_team_view());

  team.async_send_team_shared_data(ctx, std::move(team_data));
}

void user_team_battle_library_function::glue_layer_event_on_team_action_update_matching_team_view(
    rpc::context& ctx, user_team& team, const PROJECT_NAMESPACE_ID::DMatchingTeamSyncView& team_view) {
  if (!team.is_member()) {
    return;
  }

  auto& matching_mgr = team.get_owner().get_owner().get_user_matching_manager();

  // 所有的队员要订阅匹配单元的更新
  matching_mgr.subscribe_matching_unit(ctx, team_view);
}

void user_team_battle_library_function::glue_layer_normalize_member_action_update_ready(
    rpc::context& ctx, user_team& team, bool ready,
    atfw::util::nostd::function_ref<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule*(int64_t key)> fn) {
  // 设置Ready时需要补充DMatchingTeamSyncView数据
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> key_maker{ctx};
  key_maker->mutable_battle()->mutable_matching_parameter();
  auto key = user_team_algorithm::make_team_member_shared_data_key(*key_maker);

  PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule* autocomplete_data = fn(key);

  if (ready) {
    auto& matching_mgr = team.get_owner().get_owner().get_user_matching_manager();
    matching_mgr.fetch_team_matching_parameter(ctx, *autocomplete_data->mutable_battle()->mutable_matching_parameter());
  } else {
    // 不在匹配则清空数据
    autocomplete_data->mutable_battle()->mutable_matching_parameter();
  }
}

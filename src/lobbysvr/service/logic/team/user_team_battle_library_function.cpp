// Copyright 2026 atframework

#include "logic/team/user_team_battle_library_function.h"

#include <rpc/rpc_context.h>

#include "logic/team/user_team.h"
#include "logic/team/user_team_algorithm.h"

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

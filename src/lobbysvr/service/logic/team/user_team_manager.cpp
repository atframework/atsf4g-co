// Copyright 2026 atframework

#include "logic/team/user_team_manager.h"

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.team.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <rpc/rpc_context.h>

#include <data/user_key_hash_helper.h>

#include <utility/protobuf_mini_dumper.h>

#include <unordered_set>

#include "data/user.h"
#include "logic/chat/user_chat_manager.h"

class user_team_manager_utility {
 public:
  static PROJECT_NAMESPACE_ID::EnTeamType get_team_type(const atfw::team::DTeamMemberJoinData&) noexcept {
    return PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL;
  }

  static void dispatch_team_member_event(rpc::context& ctx, user& user_inst,
                                         const ::atfw::dtmq::DChannelMessage& data) {
    auto& team_mgr = user_inst.get_user_team_manager();
    if (team_mgr.processed_private_chat_channel_sequence_ >= data.sequence()) {
      return;
    }
    team_mgr.processed_private_chat_channel_sequence_ = data.sequence();

    // 处理队伍成员事件
    rpc::context::message_holder<atfw::team::DTeamMemberAction> action(ctx);
    if (!data.detail().event().Is<atfw::team::DTeamMemberAction>()) {
      FCTXLOGERROR(ctx, "dispatch_team_member_event: event type mismatch, expect DTeamMemberAction, but got {}",
                   data.detail().event().type_url());
      return;
    }

    if (!data.detail().event().UnpackTo(&(*action))) {
      FCTXLOGERROR(ctx, "dispatch_team_member_event: unpack event failed, type_url={}",
                   data.detail().event().type_url());
      return;
    }

    switch (action->action_case()) {
      case atfw::team::DTeamMemberAction::kInvited: {
        const auto& invited = action->invited();
        if (user_inst.get_user_id() != invited.invitee().user_id() ||
            user_inst.get_zone_id() != invited.invitee().zone_id()) {
          FCTXLOGERROR(ctx, "{} receive an invitation to team {}:{} for {}:{}, but the invitee is not me", user_inst,
                       invited.team_key().zone_id(), invited.team_key().team_id(), invited.invitee().zone_id(),
                       invited.invitee().user_id());

        } else {
          FCTXLOGINFO(ctx, "{} receive an invitation to team {}:{} from {}:{}", user_inst, invited.team_key().zone_id(),
                      invited.team_key().team_id(), invited.inviter().zone_id(), invited.inviter().user_id());
          team_mgr.add_pending_invitation(
              ctx, atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamInvitation>(invited));
        }
        break;
      }
      case atfw::team::DTeamMemberAction::kRejectInvitation: {
        const auto& reject_invitation = action->reject_invitation();
        if (user_inst.get_user_id() != reject_invitation.invitee().user_id() ||
            user_inst.get_zone_id() != reject_invitation.invitee().zone_id()) {
          FCTXLOGERROR(ctx, "{} receive a rejection for invitation to team {}:{} for {}:{}, but the invitee is not me",
                       user_inst, reject_invitation.team_key().zone_id(), reject_invitation.team_key().team_id(),
                       reject_invitation.invitee().zone_id(), reject_invitation.invitee().user_id());
        } else {
          FCTXLOGINFO(ctx, "{} receive a rejection for invitation to team {}:{} from {}:{}", user_inst,
                      reject_invitation.team_key().zone_id(), reject_invitation.team_key().team_id(),
                      reject_invitation.inviter().zone_id(), reject_invitation.inviter().user_id());
          team_mgr.remove_pending_invitation(ctx, reject_invitation.team_key());
        }
        break;
      }
      case atfw::team::DTeamMemberAction::kApplyJoinRequest: {
        const auto& apply_join_request = action->apply_join_request();
        if (user_inst.get_user_id() != apply_join_request.requester().user_id() ||
            user_inst.get_zone_id() != apply_join_request.requester().zone_id()) {
          FCTXLOGERROR(ctx, "{} receive an application to join team {}:{} from {}:{}, but the requester is not me",
                       user_inst, apply_join_request.team_key().zone_id(), apply_join_request.team_key().team_id(),
                       apply_join_request.requester().zone_id(), apply_join_request.requester().user_id());
        } else {
          FCTXLOGINFO(ctx, "{} receive an application to join team {}:{} from {}:{}", user_inst,
                      apply_join_request.team_key().zone_id(), apply_join_request.team_key().team_id(),
                      apply_join_request.requester().zone_id(), apply_join_request.requester().user_id());
          team_mgr.add_pending_join_request(
              ctx, atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamJoinRequest>(apply_join_request));
        }
        break;
      }
      case atfw::team::DTeamMemberAction::kRejectJoinRequest: {
        const auto& reject_join_request = action->reject_join_request();
        if (user_inst.get_user_id() != reject_join_request.requester().user_id() ||
            user_inst.get_zone_id() != reject_join_request.requester().zone_id()) {
          FCTXLOGERROR(ctx,
                       "{} receive a rejection for join request to team {}:{} from {}:{}, but the requester is not me",
                       user_inst, reject_join_request.team_key().zone_id(), reject_join_request.team_key().team_id(),
                       reject_join_request.requester().zone_id(), reject_join_request.requester().user_id());
        } else {
          FCTXLOGINFO(ctx, "{} receive a rejection for join request to team {}:{} from {}:{}", user_inst,
                      reject_join_request.team_key().zone_id(), reject_join_request.team_key().team_id(),
                      reject_join_request.requester().zone_id(), reject_join_request.requester().user_id());
          team_mgr.remove_pending_join_request(ctx, reject_join_request.team_key());
        }
        break;
      }
      case atfw::team::DTeamMemberAction::kJoinedTeam: {
        const auto& joined_team = action->joined_team();
        if (user_inst.get_user_id() != joined_team.user_key().user_id() ||
            user_inst.get_zone_id() != joined_team.user_key().zone_id()) {
          FCTXLOGERROR(ctx, "{} has joined team {}:{} from {}:{}, but the user is not me", user_inst,
                       joined_team.team_key().zone_id(), joined_team.team_key().team_id(),
                       joined_team.user_key().zone_id(), joined_team.user_key().user_id());
        } else {
          FCTXLOGINFO(ctx, "{} has joined team {}:{} from {}:{}", user_inst, joined_team.team_key().zone_id(),
                      joined_team.team_key().team_id(), joined_team.user_key().zone_id(),
                      joined_team.user_key().user_id());
          team_mgr.add_team(ctx, get_team_type(joined_team), joined_team.team_key(), joined_team.team_channel());
        }
        break;
      }
      case atfw::team::DTeamMemberAction::kRemoveMember: {
        const auto& remove_member = action->remove_member();
        if (user_inst.get_user_id() != remove_member.user_key().user_id() ||
            user_inst.get_zone_id() != remove_member.user_key().zone_id()) {
          FCTXLOGERROR(ctx, "{} has been removed from team {}:{} by {}:{}, but the user is not me", user_inst,
                       remove_member.team_key().zone_id(), remove_member.team_key().team_id(),
                       remove_member.user_key().zone_id(), remove_member.user_key().user_id());
        } else {
          FCTXLOGINFO(ctx, "{} has been removed from team {}:{} by {}:{}", user_inst,
                      remove_member.team_key().zone_id(), remove_member.team_key().team_id(),
                      remove_member.user_key().zone_id(), remove_member.user_key().user_id());
          team_mgr.remove_team(ctx, remove_member.team_key(), false, atfw::team::EN_TEAM_EXIT_REASON_DEFAULT);
        }
        break;
      }
      default:
        FCTXLOGINFO(ctx, "dispatch_team_member_event: unknown action type {}",
                    static_cast<int32_t>(action->action_case()));
        break;
    }
  }
};

user_team_manager::user_team_manager(user& owner)
    : owner_(&owner), is_dirty_(false), processed_private_chat_channel_sequence_(0) {}

user_team_manager::~user_team_manager() {}

void user_team_manager::create_init(rpc::context& /*ctx*/) { processed_private_chat_channel_sequence_ = 0; }

int32_t user_team_manager::login_init(rpc::context&) {
  user_chat_manager::global_setup_private_channel_event_callback<::atfw::team::DTeamMemberAction>(
      reinterpret_cast<uintptr_t>(user_team_manager_utility::dispatch_team_member_event),
      user_team_manager_utility::dispatch_team_member_event);

  is_dirty_ = false;
  return 0;
}

void user_team_manager::refresh_feature_limit_second(rpc::context& ctx) {
  cleanup_expired_join_request(ctx);
  cleanup_expired_invitation(ctx);

  // 触发退出重试
  for (const auto& group : team_group_) {
    for (const auto& exiting_team : group.second.pending_to_exit) {
      exiting_team.second->retry_send_exit_team_request(ctx);
    }
  }
}

void user_team_manager::init_from_table_data(rpc::context& /*ctx*/,
                                             const PROJECT_NAMESPACE_ID::table_user& user_table) {
  const auto& team_data = user_table.team_data();
  processed_private_chat_channel_sequence_ = team_data.processed_private_chat_channel_sequence();
}

int user_team_manager::dump(rpc::context& /*ctx*/, PROJECT_NAMESPACE_ID::table_user& table) const {
  auto* team_data = table.mutable_team_data();

  team_data->set_processed_private_chat_channel_sequence(processed_private_chat_channel_sequence_);
  return 0;
}

bool user_team_manager::is_dirty() const { return is_dirty_; }

void user_team_manager::clear_dirty() { is_dirty_ = false; }

void user_team_manager::set_processed_private_chat_channel_sequence(int64_t sequence) {
  if (processed_private_chat_channel_sequence_ == sequence) {
    return;
  }

  processed_private_chat_channel_sequence_ = sequence;
  is_dirty_ = true;
}

void user_team_manager::add_team(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType team_type,
                                 const atfw::team::DTeamKey& team_key, const atfw::dtmq::DChannelIdKey& channel_key) {
  // Implementation here
  if (team_key.team_id() == 0 || channel_key.channel_id().empty()) {
    return;
  }

  switch (team_type) {
    case PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL:
      break;
    default: {
      FCTXLOGERROR(ctx, "{} add_team: unknown team type {}", *owner_, static_cast<int32_t>(team_type));
      return;
    }
  }

  auto iter = team_index_.find(team_key);
  if (team_index_.end() != iter && iter->second) {
    FCTXLOGINFO(ctx, "{} add_team: team {}:{} already exists, skip to add a new one", *owner_, team_key.zone_id(),
                team_key.team_id());
    iter->second->make_current_actived(ctx);
    return;
  }

  auto team_ptr = user_team::create(*this, static_cast<uint32_t>(team_type), team_key, channel_key);
  if (!team_ptr) {
    FCTXLOGERROR(ctx, "{} add_team: create user_team failed for team {}:{}", *owner_, team_key.zone_id(),
                 team_key.team_id());
    return;
  }

  team_group& group = team_group_[team_ptr->get_team_type()];
  if (group.current) {
    group.pending_to_exit.emplace(group.current->get_team_key(), group.current);
    group.current->send_exit_team_request(ctx, atfw::team::EN_TEAM_EXIT_REASON_IN_ANOTHER_TEAM);
  }
  group.current = team_ptr;
  team_index_.emplace(team_key, team_ptr);

  team_ptr->make_current_actived(ctx);
}

void user_team_manager::remove_team(rpc::context& ctx, const atfw::team::DTeamKey& team_key, bool send_exit,
                                    atfw::team::EnTeamExitReason exit_reason) {
  auto iter = team_index_.find(team_key);
  if (iter == team_index_.end()) {
    return;
  }

  if (!iter->second) {
    team_index_.erase(iter);
    return;
  }
  auto team_ptr = iter->second;

  auto iter_group = team_group_.find(team_ptr->get_team_type());
  if (iter_group != team_group_.end()) {
    iter_group->second.pending_to_exit.erase(team_key);
    if (iter_group->second.current == team_ptr) {
      iter_group->second.current.reset();
    }

    if (iter_group->second.pending_to_exit.empty() && !iter_group->second.current) {
      team_group_.erase(iter_group);
    }
  }

  team_index_.erase(iter);

  if (send_exit) {
    team_ptr->send_exit_team_request(ctx, exit_reason);
  }
}

size_t user_team_manager::cleanup_expired_join_request(rpc::context& ctx) {
  std::unordered_set<atfw::team::DTeamKey, rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      expired_team_keys;

  auto now = ctx.logical_now();
  for (auto iter = pending_join_request_by_expired_time_.begin(); iter != pending_join_request_by_expired_time_.end();
       ++iter) {
    if (!*iter) {
      iter = pending_join_request_by_expired_time_.erase(iter);
      continue;
    }

    if (protobuf_to_system_clock((*iter)->expired_timepoint()) > now) {
      break;
    }

    expired_team_keys.insert((*iter)->team_key());
  }

  size_t ret = 0;
  for (const auto& team_key : expired_team_keys) {
    if (remove_pending_join_request(ctx, team_key)) {
      ++ret;
    }
  }
  return ret;
}

bool user_team_manager::add_pending_join_request(rpc::context& ctx, const team_join_request_ptr_t& join_request) {
  if (!join_request) {
    return false;
  }

  if (join_request->team_key().team_id() == 0) {
    return false;
  }

  auto expired_timepoint = protobuf_to_system_clock(join_request->expired_timepoint());
  if (expired_timepoint <= ctx.logical_now()) {
    return false;
  }

  // 覆盖检查
  do {
    auto iter = pending_join_request_by_team_id_.find(join_request->team_key());
    if (iter == pending_join_request_by_team_id_.end()) {
      break;
    }
    if (protobuf_to_system_clock((*iter->second)->expired_timepoint()) == expired_timepoint) {
      protobuf_copy_message(*(*iter->second), *join_request);
      return true;
    }

    remove_pending_join_request(ctx, join_request->team_key());
  } while (false);

  // 快速查找插入点，大部分情况下应该是 append 到末尾
  if (pending_join_request_by_expired_time_.empty()) {
    auto iter = pending_join_request_by_expired_time_.insert(pending_join_request_by_expired_time_.end(), join_request);
    pending_join_request_by_team_id_.emplace(join_request->team_key(), iter);
    return true;
  }

  if (expired_timepoint >=
      protobuf_to_system_clock((*pending_join_request_by_expired_time_.rbegin())->expired_timepoint())) {
    auto iter = pending_join_request_by_expired_time_.insert(pending_join_request_by_expired_time_.end(), join_request);
    pending_join_request_by_team_id_.emplace(join_request->team_key(), iter);
    return true;
  }

  if (expired_timepoint <=
      protobuf_to_system_clock((*pending_join_request_by_expired_time_.begin())->expired_timepoint())) {
    auto iter =
        pending_join_request_by_expired_time_.insert(pending_join_request_by_expired_time_.begin(), join_request);
    pending_join_request_by_team_id_.emplace(join_request->team_key(), iter);
    return true;
  }

  // 慢速查找插入点
  for (auto iter = pending_join_request_by_expired_time_.begin(); iter != pending_join_request_by_expired_time_.end();
       ++iter) {
    if (expired_timepoint < protobuf_to_system_clock((*iter)->expired_timepoint())) {
      auto new_iter = pending_join_request_by_expired_time_.insert(iter, join_request);
      pending_join_request_by_team_id_.emplace(join_request->team_key(), new_iter);
      break;
    }
  }

  return true;
}

bool user_team_manager::remove_pending_join_request(rpc::context& /*ctx*/, const atfw::team::DTeamKey& team_key) {
  auto iter = pending_join_request_by_team_id_.find(team_key);
  if (iter == pending_join_request_by_team_id_.end()) {
    return false;
  }

  pending_join_request_by_expired_time_.erase(iter->second);
  pending_join_request_by_team_id_.erase(iter);
  return true;
}

size_t user_team_manager::cleanup_expired_invitation(rpc::context& ctx) {
  std::unordered_set<atfw::team::DTeamKey, rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      expired_team_keys;

  auto now = ctx.logical_now();
  for (auto iter = pending_invitation_by_expired_time_.begin(); iter != pending_invitation_by_expired_time_.end();
       ++iter) {
    if (!*iter) {
      iter = pending_invitation_by_expired_time_.erase(iter);
      continue;
    }

    if (protobuf_to_system_clock((*iter)->expired_timepoint()) > now) {
      break;
    }

    expired_team_keys.insert((*iter)->team_key());
  }

  size_t ret = 0;
  for (const auto& team_key : expired_team_keys) {
    if (remove_pending_invitation(ctx, team_key)) {
      ++ret;
    }
  }

  return ret;
}

bool user_team_manager::add_pending_invitation(rpc::context& ctx, const team_invitation_ptr_t& invitation) {
  if (!invitation) {
    return false;
  }

  auto expired_timepoint = protobuf_to_system_clock(invitation->expired_timepoint());
  if (expired_timepoint <= ctx.logical_now()) {
    return false;
  }

  // 覆盖检查
  do {
    auto iter = pending_invitation_by_team_id_.find(invitation->team_key());
    if (iter == pending_invitation_by_team_id_.end()) {
      break;
    }
    if (protobuf_to_system_clock((*iter->second)->expired_timepoint()) == expired_timepoint) {
      protobuf_copy_message(*(*iter->second), *invitation);
      return true;
    }

    remove_pending_invitation(ctx, invitation->team_key());
  } while (false);

  // 快速查找插入点，大部分情况下应该是 append 到末尾
  if (pending_invitation_by_expired_time_.empty()) {
    auto iter = pending_invitation_by_expired_time_.insert(pending_invitation_by_expired_time_.end(), invitation);
    pending_invitation_by_team_id_.emplace(invitation->team_key(), iter);
    return true;
  }

  if (expired_timepoint >=
      protobuf_to_system_clock((*pending_invitation_by_expired_time_.rbegin())->expired_timepoint())) {
    auto iter = pending_invitation_by_expired_time_.insert(pending_invitation_by_expired_time_.end(), invitation);
    pending_invitation_by_team_id_.emplace(invitation->team_key(), iter);
    return true;
  }

  if (expired_timepoint <=
      protobuf_to_system_clock((*pending_invitation_by_expired_time_.begin())->expired_timepoint())) {
    auto iter = pending_invitation_by_expired_time_.insert(pending_invitation_by_expired_time_.begin(), invitation);
    pending_invitation_by_team_id_.emplace(invitation->team_key(), iter);
    return true;
  }

  // 慢速查找插入点
  for (auto iter = pending_invitation_by_expired_time_.begin(); iter != pending_invitation_by_expired_time_.end();
       ++iter) {
    if (expired_timepoint < protobuf_to_system_clock((*iter)->expired_timepoint())) {
      auto new_iter = pending_invitation_by_expired_time_.insert(iter, invitation);
      pending_invitation_by_team_id_.emplace(invitation->team_key(), new_iter);
      break;
    }
  }

  return true;
}

bool user_team_manager::remove_pending_invitation(rpc::context& /*ctx*/, const atfw::team::DTeamKey& team_key) {
  auto iter = pending_invitation_by_team_id_.find(team_key);
  if (iter == pending_invitation_by_team_id_.end()) {
    return false;
  }

  pending_invitation_by_expired_time_.erase(iter->second);
  pending_invitation_by_team_id_.erase(iter);
  return true;
}

// Copyright 2026 atframework

#include "logic/team/user_team_manager.h"

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.chat.pb.h>
#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include <memory/object_allocator.h>

#include <rpc/dtmq/dtmq_algorithm.h>
#include <rpc/rpc_context.h>
#include <rpc/team/team_room_client_api.h>

#include <data/user_key_hash_helper.h>

#include <utility/protobuf_mini_dumper.h>

#include <unordered_set>

#include "data/user.h"
#include "logic/chat/user_chat_manager.h"
#include "logic/team/user_team_algorithm.h"

class user_team_manager_utility {
 public:
  static void dispatch_team_member_event(rpc::context& ctx, user& user_inst,
                                         const ::atfw::dtmq::DChannelMessage& data) {
    auto& team_mgr = user_inst.get_user_team_manager();
    if (team_mgr.processed_private_chat_channel_sequence_ >= data.sequence()) {
      return;
    }
    // 走 setter 保持脏标记语义(与落地字段 processed_private_chat_channel_sequence 的写入契约一致)
    team_mgr.set_processed_private_chat_channel_sequence(data.sequence());

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
          team_mgr.add_team(ctx, user_team_algorithm::get_team_type(joined_team), joined_team);
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
    : owner_(&owner), is_dirty_(false), processed_private_chat_channel_sequence_(0) {
  ATFW_EXPLICIT_UNUSED_ATTR static auto _init_get_info_handle = user::init_get_info_handle(
      PROJECT_NAMESPACE_ID::CSUserGetInfoReq::descriptor()->FindFieldByNumber(
          PROJECT_NAMESPACE_ID::CSUserGetInfoReq::kNeedUserTeamFieldNumber),
      [](rpc::context& ctx, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp& rsp, user& user_inst) {
        auto& team_mgr = user_inst.get_user_team_manager();
        team_mgr.foreach_running_team(
            [&ctx, &rsp](uint32_t /*group_type*/, const atfw::util::nostd::nonnull<user_team::ptr_t>& team) {
              team->dump(ctx, *rsp.add_user_team());
            });
      });
}

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

  // 队伍级缓存的过期准入数据清理(teamsvr-room 清理时不下发事件，各端自行清理)与成员心跳
  for (auto& group : team_group_) {
    if (!group.second.current) {
      continue;
    }
    group.second.current->cleanup_expired_admissions(ctx);
    group.second.current->maybe_send_heartbeat(ctx);
  }
}

void user_team_manager::refresh_feature_limit_minute(rpc::context& ctx) {
  // 每分钟清理，大多数情况都会由事件触发清理。这里仅仅是一个防泄露的补充
  using team_key_set_t = std::unordered_set<atfw::team::DTeamKey, rpc::team::team_api::team_key_hash_t,
                                            rpc::team::team_api::team_key_equal_t>;
  team_key_set_t can_be_removed_keys;
  team_key_set_t wait_member_timeout_team_keys;
  std::unordered_set<uint32_t> empty_group_types;
  for (auto& group : team_group_) {
    can_be_removed_keys.clear();

    // 清理移除队列
    for (const auto& exiting_team : group.second.pending_to_exit) {
      exiting_team.second->retry_send_exit_team_request(ctx);
      if (exiting_team.second->can_be_removed(ctx)) {
        can_be_removed_keys.emplace(exiting_team.first);
      }
    }

    for (const auto& team_key : can_be_removed_keys) {
      group.second.pending_to_exit.erase(team_key);
      team_index_.erase(team_key);
    }

    // 如果当前队伍长时间都检测不到在队伍中，则是数据链路出现问题，直接准备退出
    if (group.second.current && group.second.current->wait_to_be_member_but_timeout(ctx)) {
      FCTXLOGINFO(ctx, "{} current team {}:{} wait to be member but timeout, prepare to exit", *owner_,
                  group.second.current->get_team_key().zone_id(), group.second.current->get_team_key().team_id());
      // remove_team 可能会移除当前正在迭代的分组，必须等遍历结束后再执行
      wait_member_timeout_team_keys.emplace(group.second.current->get_team_key());
    }

    if (group.second.pending_to_exit.empty() && !group.second.current) {
      empty_group_types.emplace(group.first);
    }
  }

  for (const auto& team_key : wait_member_timeout_team_keys) {
    remove_team(ctx, team_key, true, atfw::team::EN_TEAM_EXIT_REASON_EXPIRED);
  }

  for (const auto& group_type : empty_group_types) {
    team_group_.erase(group_type);
  }
}

void user_team_manager::init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table) {
  const auto& team_data = user_table.team_data();
  processed_private_chat_channel_sequence_ = team_data.processed_private_chat_channel_sequence();

  for (const auto& group_data : team_data.group()) {
    if (group_data.has_current()) {
      const auto& current_team_data = group_data.current();
      add_team(ctx, static_cast<PROJECT_NAMESPACE_ID::EnTeamType>(group_data.team_type()), current_team_data);
    }
  }
}

int user_team_manager::dump(rpc::context& /*ctx*/, PROJECT_NAMESPACE_ID::table_user& table) const {
  auto* team_data = table.mutable_team_data();

  team_data->set_processed_private_chat_channel_sequence(processed_private_chat_channel_sequence_);

  for (const auto& group : team_group_) {
    auto* group_data = team_data->add_group();
    if (nullptr == group_data) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
    }
    // init_from_table_data 依赖 team_type 恢复分组(非法类型会被 add_team 丢弃)，必须落地
    group_data->set_team_type(group.first);

    if (group.second.current) {
      auto* current_team_data = group_data->mutable_current();
      if (nullptr == current_team_data) {
        return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
      }
      group.second.current->dump(*current_team_data);
    }

    // pending_to_exit 里的队伍理论上都尝试过发送exit消息了，如果发送失败，也可以等超时自动清理
    // 不用再落地数据库再恢复
  }
  return 0;
}

bool user_team_manager::is_dirty() const { return is_dirty_; }

void user_team_manager::clear_dirty() { is_dirty_ = false; }

void user_team_manager::foreach_running_team(
    atfw::util::nostd::function_ref<void(uint32_t, const atfw::util::nostd::nonnull<user_team::ptr_t>&)> fn) const {
  for (const auto& group : team_group_) {
    if (group.second.current && !group.second.current->is_exiting() && !group.second.current->is_destroyed()) {
      fn(group.first, group.second.current);
    }
  }
}

user_team_manager::team_join_request_ptr_t user_team_manager::get_pending_join_request(
    const atfw::team::DTeamKey& team_key) const noexcept {
  auto iter = pending_join_request_by_team_id_.find(team_key);
  if (iter != pending_join_request_by_team_id_.end() && *iter->second) {
    return *iter->second;
  }
  return nullptr;
}

user_team_manager::team_invitation_ptr_t user_team_manager::get_pending_invitation(
    const atfw::team::DTeamKey& team_key) const noexcept {
  auto iter = pending_invitation_by_team_id_.find(team_key);
  if (iter != pending_invitation_by_team_id_.end() && *iter->second) {
    return *iter->second;
  }
  return nullptr;
}

rpc::result_code_type user_team_manager::approve_invitation(rpc::context& ctx,
                                                            const team_invitation_ptr_t& invitation) {
  if (!invitation) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }

  // 被邀请人本人接受邀请: 版本/路由等成员数据由被邀请人在同意时上报
  rpc::context::message_holder<atfw::team::SSTeamRoomApproveInvitationReq> ss_req{ctx};
  rpc::context::message_holder<atfw::team::SSTeamRoomApproveInvitationRsp> ss_rsp{ctx};
  protobuf_copy_message(*ss_req->mutable_team_key(), invitation->team_key());
  ss_req->mutable_sender_user_key()->set_zone_id(owner_->get_zone_id());
  ss_req->mutable_sender_user_key()->set_user_id(owner_->get_user_id());
  protobuf_copy_message(*ss_req->mutable_invitee(), invitation->invitee());
  ss_req->set_client_version(owner_->get_client_info().client_version());
  // 成员通知路由到当前持有会话的 lobbysvr 节点
  ss_req->set_user_router_server_id(logic_config::me()->get_local_server_id());

  // 填充 shared_member_data
  pack_team_member_shared_data(ctx, user_team_algorithm::get_team_type(*invitation),
                               *ss_req->mutable_shared_member_data());

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::approve_invitation(ctx, *ss_req, *ss_rsp));
  if (0 == ret) {
    ret = ss_rsp->client_result();
  }

  if (0 == ret) {
    // 接受后房间只向被邀请人发 joined_team 通知，这里直接移除本地待处理邀请
    remove_pending_invitation(ctx, invitation->team_key());
  } else if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND == ret) {
    // 目标频道已不存在(队伍已解散或数据链路失效)，对客户端表现为邀请不存在
    ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND;
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_team_manager::reject_invitation(rpc::context& ctx, const team_invitation_ptr_t& invitation) {
  if (!invitation) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }

  // 被邀请人本人拒绝邀请
  rpc::context::message_holder<atfw::team::SSTeamRoomRejectInvitationReq> ss_req{ctx};
  rpc::context::message_holder<atfw::team::SSTeamRoomRejectInvitationRsp> ss_rsp{ctx};
  protobuf_copy_message(*ss_req->mutable_team_key(), invitation->team_key());
  ss_req->mutable_sender_user_key()->set_zone_id(owner_->get_zone_id());
  ss_req->mutable_sender_user_key()->set_user_id(owner_->get_user_id());
  protobuf_copy_message(*ss_req->mutable_invitee(), invitation->invitee());

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::reject_invitation(ctx, *ss_req, *ss_rsp));
  if (0 == ret) {
    ret = ss_rsp->client_result();
  }

  if (0 == ret) {
    // 拒绝成功后即使房间的回执事件丢失，也不再需要保留本地记录
    remove_pending_invitation(ctx, invitation->team_key());
  } else if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND == ret) {
    // 目标频道已不存在(队伍已解散或数据链路失效)，对客户端表现为邀请不存在
    ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND;
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_team_manager::send_join_request(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                                           atfw::team::EnTeamSourceType team_source_type,
                                                           const ::google::protobuf::Any& team_source_data) {
  // 申请人的版本/路由/私有频道由本人上报
  rpc::context::message_holder<atfw::team::SSTeamRoomAddJoinRequestReq> ss_req{ctx};
  rpc::context::message_holder<atfw::team::SSTeamRoomAddJoinRequestRsp> ss_rsp{ctx};
  auto* join_request = ss_req->mutable_join_request();
  protobuf_copy_message(*join_request->mutable_team_key(), team_key);
  join_request->mutable_requester()->set_zone_id(owner_->get_zone_id());
  join_request->mutable_requester()->set_user_id(owner_->get_user_id());
  auto* requester_channel = join_request->mutable_requester_private_channel();
  protobuf_copy_message(*requester_channel, owner_->get_user_chat_manager().get_private_chat_channel_key());
  join_request->set_client_version(owner_->get_client_info().client_version());
  // 成员通知路由到当前持有会话的 lobbysvr 节点
  join_request->set_user_router_server_id(logic_config::me()->get_local_server_id());

  join_request->set_team_source_type(team_source_type);
  protobuf_copy_message(*join_request->mutable_team_source_data(), team_source_data);

  // 填充 member_admission_data
  pack_team_member_shared_data(ctx, user_team_algorithm::get_team_type(*join_request),
                               *join_request->mutable_member_admission_data());

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::add_join_request(ctx, *ss_req, *ss_rsp));
  if (0 == ret) {
    ret = ss_rsp->client_result();
  }

  if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND == ret) {
    // 目标频道已不存在(队伍从未创建或已解散)，对客户端表现为队伍不存在
    ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ROOM_NOT_FOUND;
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_team_manager::create_team(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType type,
                                                     atfw::team::DTeamKey& output_team_key) {
  rpc::context::message_holder<atfw::team::SSTeamRoomCreateReq> ss_req{ctx};
  rpc::context::message_holder<atfw::team::SSTeamRoomCreateRsp> ss_rsp{ctx};
  // team_id 置 0 由 teamsvr-room 分配，zone_id 参与按队伍一致性哈希的路由
  ss_req->mutable_team_key()->set_zone_id(owner_->get_zone_id());
  owner_->dump_user_key(*ss_req->mutable_sender_user_key());
  protobuf_copy_message(*ss_req->mutable_sender_user_channel(),
                        owner_->get_user_chat_manager().get_private_chat_channel_key());

  // 填充初始的共享数据(队伍的和成员的)，各模块按 key 区分自己的数据
  pack_team_shared_data(ctx, type, *ss_req->mutable_shared_team_data());
  pack_team_member_shared_data(ctx, type, *ss_req->mutable_shared_member_data());

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::create(ctx, *ss_req, *ss_rsp));
  if (0 == ret) {
    ret = ss_rsp->client_result();
  }
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  // create 不会回发 joined_team 通知，这里直接按响应注册本地队伍(创建者即队长)
  atfw::team::DTeamMemberJoinData join_data;
  protobuf_copy_message(*join_data.mutable_team_key(), ss_rsp->team_key());
  protobuf_copy_message(*join_data.mutable_user_key(), ss_req->sender_user_key());
  protobuf_copy_message(*join_data.mutable_team_channel(), ss_rsp->room_channel());
  join_data.set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  protobuf_copy_message(*join_data.mutable_captain_user_key(), ss_req->sender_user_key());
  add_team(ctx, type, join_data);

  protobuf_copy_message(output_team_key, ss_rsp->team_key());
  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_team_manager::send_invitation(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                                         const PROJECT_NAMESPACE_ID::DUserIDKey& invitee,
                                                         atfw::team::EnTeamSourceType team_source_type,
                                                         const ::google::protobuf::Any& team_source_data) {
  rpc::context::message_holder<atfw::team::SSTeamRoomAddInvitationReq> ss_req{ctx};
  rpc::context::message_holder<atfw::team::SSTeamRoomAddInvitationRsp> ss_rsp{ctx};
  auto* invitation = ss_req->mutable_invitation();
  protobuf_copy_message(*invitation->mutable_team_key(), team_key);
  owner_->dump_user_key(*invitation->mutable_inviter());
  protobuf_copy_message(*invitation->mutable_invitee(), invitee);
  // 被邀请人的私有通知频道由其 user_key 按标准单播格式派生(与 user_chat_manager 的私有频道一致)
  auto* invitee_channel = invitation->mutable_invitee_private_channel();
  invitee_channel->set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE));
  invitee_channel->set_channel_id(
      rpc::dtmq::make_unicast_channel_id(invitee_channel->channel_type(), invitee.zone_id(), invitee.user_id()));
  // 开始/过期时间由 teamsvr-room 填充
  owner_->dump_user_key(*ss_req->mutable_sender_user_key());

  invitation->set_team_source_type(team_source_type);
  protobuf_copy_message(*invitation->mutable_team_source_data(), team_source_data);

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::add_invitation(ctx, *ss_req, *ss_rsp));
  if (0 == ret) {
    ret = ss_rsp->client_result();
  }

  if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND == ret) {
    // 目标频道已不存在(队伍已解散或数据链路失效)，对客户端表现为已不在队伍中
    ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM;
  }
  RPC_RETURN_CODE(ret);
}

user_team::ptr_t user_team_manager::get_team_by_team_key(const atfw::team::DTeamKey& team_key) const noexcept {
  auto iter = team_index_.find(team_key);
  if (iter != team_index_.end() && iter->second) {
    return iter->second;
  }

  return nullptr;
}

user_team::ptr_t user_team_manager::get_team_by_team_type(PROJECT_NAMESPACE_ID::EnTeamType type) const noexcept {
  auto iter = team_group_.find(static_cast<uint32_t>(type));
  if (iter != team_group_.end() && iter->second.current) {
    return iter->second.current;
  }
  return nullptr;
}

void user_team_manager::remove_team(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                    atfw::team::EnTeamExitReason exit_reason) {
  remove_team(ctx, team_key, true, exit_reason);
}

void user_team_manager::pack_team_shared_data(
    rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType /*type*/,
    ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output) {
  // 战斗模块
  {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamSharedDataModule> wrapper{ctx};
    wrapper->mutable_battle()->set_matching(false);

    auto* output_field = output.Add();
    output_field->set_key(user_team_algorithm::make_team_shared_data_key(*wrapper));
    output_field->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!output_field->mutable_value()->mutable_data()->PackFrom(*wrapper)) {
      FCTXLOGERROR(ctx, "{} pack_team_shared_data: failed to pack team shared data", *owner_);
      output.RemoveLast();
    }
  }
}

void user_team_manager::pack_team_member_shared_data(
    rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType /*type*/,
    ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output) {
  // 战斗模块
  {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> wrapper{ctx};
    wrapper->mutable_battle()->set_ready(false);

    auto* output_field = output.Add();
    output_field->set_key(user_team_algorithm::make_team_member_shared_data_key(*wrapper));
    output_field->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!output_field->mutable_value()->mutable_data()->PackFrom(*wrapper)) {
      FCTXLOGERROR(ctx, "{} pack_team_member_shared_data: failed to pack team member shared data", *owner_);
      output.RemoveLast();
    }
  }
}

void user_team_manager::set_processed_private_chat_channel_sequence(int64_t sequence) {
  if (processed_private_chat_channel_sequence_ == sequence) {
    return;
  }

  processed_private_chat_channel_sequence_ = sequence;
  is_dirty_ = true;
}

void user_team_manager::add_team(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType team_type,
                                 const atfw::team::DTeamMemberJoinData& join_data) {
  // Implementation here
  if (join_data.team_key().team_id() == 0 || join_data.team_channel().channel_id().empty()) {
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

  const auto& team_key = join_data.team_key();
  const auto& channel_key = join_data.team_channel();
  auto iter = team_index_.find(team_key);
  if (team_index_.end() != iter && iter->second) {
    auto exists_team = iter->second;
    auto iter_group = team_group_.find(exists_team->get_team_type());
    if (iter_group != team_group_.end() && iter_group->second.current != exists_team) {
      // 之前还处于退出流程中的队伍又重新加入了：从退出队列移出并恢复为当前队伍，原当前队伍转入退出队列
      iter_group->second.pending_to_exit.erase(team_key);
      if (iter_group->second.current) {
        iter_group->second.pending_to_exit.emplace(iter_group->second.current->get_team_key(),
                                                   iter_group->second.current);
        iter_group->second.current->send_exit_team_request(ctx, atfw::team::EN_TEAM_EXIT_REASON_IN_ANOTHER_TEAM);
      }
      iter_group->second.current = exists_team;
      exists_team->init_cached_data(join_data.captain_user_key(), join_data.user_role());
      FCTXLOGINFO(ctx, "{} add_team: team {}:{} is still in exit queue, restore it to current", *owner_,
                  team_key.zone_id(), team_key.team_id());
    } else {
      FCTXLOGINFO(ctx, "{} add_team: team {}:{} already exists, skip to add a new one", *owner_, team_key.zone_id(),
                  team_key.team_id());
    }
    exists_team->make_current_actived(ctx);
    return;
  }

  auto team_ptr = user_team::create(ctx, *this, static_cast<uint32_t>(team_type), team_key, channel_key);
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

  team_ptr->init_cached_data(join_data.captain_user_key(), join_data.user_role());
  team_ptr->make_current_actived(ctx);

  team_ptr->try_load_snapshot(ctx);
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
  if (send_exit) {
    team_ptr->send_exit_team_request(ctx, exit_reason);
  } else if (!team_ptr->is_exiting()) {
    team_ptr->set_exit_team(ctx, exit_reason);
  }

  auto iter_group = team_group_.find(team_ptr->get_team_type());
  if (iter_group != team_group_.end()) {
    if (iter_group->second.current == team_ptr) {
      // 如果是当前队伍，且已经不是成员了或退出超时，则直接移除
      if (team_ptr->can_be_removed(ctx)) {
        iter_group->second.pending_to_exit.erase(team_key);
        team_index_.erase(iter);
      } else {
        // 如果是当前队伍，仍然是成员，移入到退出列表中，以便后续重试发送移除成员的消息
        iter_group->second.pending_to_exit.emplace(iter_group->second.current->get_team_key(),
                                                   iter_group->second.current);
      }
      iter_group->second.current.reset();
    } else {
      // 如果已经在退出列表中，检查是否可以直接移除
      auto iter_pending = iter_group->second.pending_to_exit.find(team_key);
      if (iter_pending != iter_group->second.pending_to_exit.end()) {
        if (iter_pending->second->can_be_removed(ctx)) {
          iter_group->second.pending_to_exit.erase(iter_pending);
          team_index_.erase(iter);
        }
      }
    }

    if (iter_group->second.pending_to_exit.empty() && !iter_group->second.current) {
      team_group_.erase(iter_group);
    }
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

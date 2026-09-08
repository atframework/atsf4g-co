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

#include <config/excel/config_easy_api.h>
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
        if (!user_inst.is(invited.invitee())) {
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
        if (!user_inst.is(reject_invitation.invitee())) {
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
        if (!user_inst.is(apply_join_request.requester())) {
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
        if (!user_inst.is(reject_join_request.requester())) {
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
        if (!user_inst.is(joined_team.user_key())) {
          FCTXLOGERROR(ctx, "{} has joined team {}:{} from {}:{}, but the user is not me", user_inst,
                       joined_team.team_key().zone_id(), joined_team.team_key().team_id(),
                       joined_team.user_key().zone_id(), joined_team.user_key().user_id());
        } else {
          FCTXLOGINFO(ctx, "{} has joined team {}:{} from {}:{}", user_inst, joined_team.team_key().zone_id(),
                      joined_team.team_key().team_id(), joined_team.user_key().zone_id(),
                      joined_team.user_key().user_id());
          team_mgr.add_team(ctx, joined_team);
        }
        break;
      }
      case atfw::team::DTeamMemberAction::kRemoveMember: {
        const auto& remove_member = action->remove_member();
        if (!user_inst.is(remove_member.user_key())) {
          FCTXLOGERROR(ctx, "{} has been removed from team {}:{} by {}:{}, but the user is not me", user_inst,
                       remove_member.team_key().zone_id(), remove_member.team_key().team_id(),
                       remove_member.user_key().zone_id(), remove_member.user_key().user_id());
        } else {
          FCTXLOGINFO(ctx, "{} has been removed from team {}:{} by {}:{}", user_inst,
                      remove_member.team_key().zone_id(), remove_member.team_key().team_id(),
                      remove_member.user_key().zone_id(), remove_member.user_key().user_id());
          // 透传服务端下发的移除原因(退出重试复用该 reason, 队伍解散时还要驱动客户端解散通知)
          team_mgr.remove_team(ctx, remove_member.team_key(), false, remove_member.remove_member_reason());
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
    : owner_(&owner), is_dirty_(false), is_pulled_(false), processed_private_chat_channel_sequence_(0) {
  ATFW_EXPLICIT_UNUSED_ATTR static auto _init_get_info_handle = user::init_get_info_handle(
      PROJECT_NAMESPACE_ID::CSUserGetInfoReq::descriptor()->FindFieldByNumber(
          PROJECT_NAMESPACE_ID::CSUserGetInfoReq::kNeedUserTeamFieldNumber),
      [](rpc::context& ctx, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp& rsp, user& user_inst) {
        auto& team_mgr = user_inst.get_user_team_manager();
        // 即使当前没有队伍或 pending，也显式返回完整空状态，供客户端覆盖旧数据。
        rsp.mutable_user_team()->Clear();
        team_mgr.cleanup_expired_invitation(ctx);
        team_mgr.cleanup_expired_join_request(ctx);
        team_mgr.foreach_running_team(
            [&ctx, &rsp](uint32_t /*group_type*/, const atfw::util::nostd::nonnull<user_team::ptr_t>& team) {
              team->dump(ctx, *rsp.mutable_user_team()->add_team());
              // get-info 响应导出了完整队伍数据，客户端已知晓该队伍
              user_team::manager_accessor::set_client_announced(*team, true);
              user_team::manager_accessor::set_dirty_remove_sent(*team, false);
            });
        for (const auto& invitation : team_mgr.pending_invitation_by_expired_time_) {
          auto* output = rsp.mutable_user_team()->add_pending_invitation();
          protobuf_copy_message(*output, *invitation);
          output->clear_invitee_private_channel();
        }
        for (const auto& request : team_mgr.pending_join_request_by_expired_time_) {
          auto* output = rsp.mutable_user_team()->add_pending_join_request();
          protobuf_copy_message(*output, *request);
          output->clear_requester_private_channel();
          output->set_user_router_server_id(0);
        }
        // 该响应包含完整当前状态，之前积累的增量已被覆盖。
        // 登记时的对象可能已被收编/替换, 与 flush 收尾一致, 索引中的当前代际也要一并清理
        for (auto& dirty : team_mgr.dirty_team_) {
          auto current_team = team_mgr.get_team_by_team_key(dirty.first);
          if (current_team && current_team != dirty.second) {
            current_team->clear_dirty_data(ctx);
          }
          if (dirty.second) {
            // 本次完整响应也覆盖移除状态，迟到确认和生命周期清理无需再次下发 remove。
            if (dirty.second->is_removed_for_client()) {
              user_team::manager_accessor::set_client_announced(*dirty.second, false);
              user_team::manager_accessor::set_dirty_remove_sent(*dirty.second, true);
            }
            dirty.second->clear_dirty_data(ctx);
          }
        }
        team_mgr.dirty_team_.clear();
        team_mgr.dirty_invitation_.clear();
        team_mgr.dirty_join_request_.clear();
        team_mgr.is_pulled_ = true;
      });
}

user_team_manager::~user_team_manager() {}

void user_team_manager::create_init(rpc::context& /*ctx*/) { processed_private_chat_channel_sequence_ = 0; }

int32_t user_team_manager::login_init(rpc::context&) {
  user_chat_manager::global_setup_private_channel_event_callback<::atfw::team::DTeamMemberAction>(
      reinterpret_cast<uintptr_t>(user_team_manager_utility::dispatch_team_member_event),
      user_team_manager_utility::dispatch_team_member_event);

  is_dirty_ = false;
  is_pulled_ = false;
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
      insert_dirty_handle_for_team(team_key);
      group.second.pending_to_exit.erase(team_key);
      // 从索引移除时同步清代际标志, 对象的迟到回调经 is_active_generation 短路
      auto iter_removed = team_index_.find(team_key);
      if (iter_removed != team_index_.end()) {
        if (iter_removed->second) {
          user_team::manager_accessor::set_index_active(*iter_removed->second, false);
        }
        team_index_.erase(iter_removed);
      }
      is_dirty_ = true;
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
  pending_join_request_by_team_id_.clear();
  pending_join_request_by_expired_time_.clear();
  pending_invitation_by_team_id_.clear();
  pending_invitation_by_expired_time_.clear();

  // 先恢复待处理的邀请/加入请求(已过期或无效 team_id 的条目会被 add_pending_* 丢弃)，
  // 再由 add_team 清理当前队伍同 team_key 的 pending(入队后同队伍的邀请/加入请求视为已有结论)
  for (const auto& invitation : team_data.pending_invitation()) {
    add_pending_invitation(ctx, atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamInvitation>(invitation));
  }
  for (const auto& join_request : team_data.pending_join_request()) {
    add_pending_join_request(ctx,
                             atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamJoinRequest>(join_request));
  }

  for (const auto& group_data : team_data.group()) {
    if (group_data.has_current()) {
      const auto& current_team_data = group_data.current();
      add_team(ctx, current_team_data);
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

  // 待处理的邀请/加入请求也要落地，否则重启/迁移后本地 pending 丢失，且相关历史事件的 sequence 低于个人频道
  // 已处理序号，重放会被跳过。
  // 恢复时由 add_pending_invitation/add_pending_join_request 过滤已过期或无效 team_id 的条目。
  for (const auto& invitation : pending_invitation_by_expired_time_) {
    protobuf_copy_message(*team_data->add_pending_invitation(), *invitation);
  }
  for (const auto& join_request : pending_join_request_by_expired_time_) {
    protobuf_copy_message(*team_data->add_pending_join_request(), *join_request);
  }
  return 0;
}

bool user_team_manager::is_dirty() const { return is_dirty_; }

void user_team_manager::clear_dirty() { is_dirty_ = false; }

void user_team_manager::send_dirty_data(rpc::context& ctx) {
  if (!dirty_team_.empty() || !dirty_invitation_.empty() || !dirty_join_request_.empty()) {
    owner_->send_all_syn_msg(ctx);
  }
}

void user_team_manager::foreach_running_team(
    atfw::util::nostd::function_ref<void(uint32_t, const atfw::util::nostd::nonnull<user_team::ptr_t>&)> fn) const {
  for (const auto& group : team_group_) {
    if (group.second.current && group.second.current->is_member() && !group.second.current->is_exiting() &&
        !group.second.current->is_destroyed()) {
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

  if (protobuf_to_system_clock(invitation->expired_timepoint()) <= ctx.logical_now()) {
    remove_pending_invitation(ctx, invitation->team_key());
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
  pack_team_member_shared_data(ctx, *ss_req->mutable_shared_member_data());

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::approve_invitation(ctx, *ss_req, *ss_rsp));
  if (0 == ret) {
    ret = ss_rsp->client_result();
  }

  if (0 == ret) {
    // 接受后房间只向被邀请人发 joined_team 通知，这里直接移除本地待处理邀请
    remove_pending_invitation(ctx, invitation->team_key());
  } else if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND == ret ||
             PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ROOM_NOT_FOUND == ret ||
             PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED == ret ||
             PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND == ret) {
    // room 上的记录已不存在(从未存在/已过期被清理/频道已销毁), 本地 pending 确定失效, 一并删除;
    // 对客户端表现为邀请不存在
    remove_pending_invitation(ctx, invitation->team_key());
    ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND;
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_team_manager::reject_invitation(rpc::context& ctx, const team_invitation_ptr_t& invitation) {
  if (!invitation) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }

  if (protobuf_to_system_clock(invitation->expired_timepoint()) <= ctx.logical_now()) {
    remove_pending_invitation(ctx, invitation->team_key());
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
  } else if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND == ret ||
             PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ROOM_NOT_FOUND == ret ||
             PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED == ret ||
             PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND == ret) {
    // room 上的记录已不存在(从未存在/已过期被清理/频道已销毁), 本地 pending 确定失效, 一并删除;
    // 对客户端表现为邀请不存在
    remove_pending_invitation(ctx, invitation->team_key());
    ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND;
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_team_manager::send_join_request(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                                           atfw::team::EnTeamSourceType team_source_type,
                                                           const ::google::protobuf::Any& team_source_data) {
  // 过期申请不阻止重新申请，但必须登记删除，即使本次 RPC 随后失败也不能保留失效记录。
  auto pending = get_pending_join_request(team_key);
  if (pending && protobuf_to_system_clock(pending->expired_timepoint()) <= ctx.logical_now()) {
    remove_pending_join_request(ctx, team_key);
  }

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
  pack_team_member_shared_data(ctx, *join_request->mutable_member_admission_data());

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
  // 非法队伍类型直接拒绝，避免房间创建成功后本地 add_team 校验失败导致两端状态不一致
  if (!excel::get_ExcelTeamType_by_team_type(static_cast<uint32_t>(type))) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVALID_TEAM_TYPE);
  }

  rpc::context::message_holder<atfw::team::SSTeamRoomCreateReq> ss_req{ctx};
  rpc::context::message_holder<atfw::team::SSTeamRoomCreateRsp> ss_rsp{ctx};
  // team_id 置 0 由 teamsvr-room 分配，zone_id 参与按队伍一致性哈希的路由
  ss_req->mutable_team_key()->set_zone_id(owner_->get_zone_id());
  ss_req->set_team_type(static_cast<uint32_t>(type));
  owner_->dump_user_key(*ss_req->mutable_sender_user_key());
  protobuf_copy_message(*ss_req->mutable_sender_user_channel(),
                        owner_->get_user_chat_manager().get_private_chat_channel_key());
  // 创建者(首任队长)上报客户端版本与成员通知路由，入队后其他成员才能在快照里看到队长的版本信息
  ss_req->set_client_version(owner_->get_client_info().client_version());
  ss_req->set_user_router_server_id(logic_config::me()->get_local_server_id());

  // 填充初始的共享数据(队伍的和成员的)，各模块按 key 区分自己的数据
  pack_team_shared_data(ctx, *ss_req->mutable_shared_team_data());
  pack_team_member_shared_data(ctx, *ss_req->mutable_shared_member_data());

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
  join_data.set_team_type(static_cast<uint32_t>(type));
  protobuf_copy_message(*join_data.mutable_user_key(), ss_req->sender_user_key());
  protobuf_copy_message(*join_data.mutable_team_channel(), ss_rsp->room_channel());
  join_data.set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  protobuf_copy_message(*join_data.mutable_captain_user_key(), ss_req->sender_user_key());
  add_team(ctx, join_data);

  protobuf_copy_message(output_team_key, ss_rsp->team_key());
  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_team_manager::send_invitation(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                                         const PROJECT_NAMESPACE_ID::DUserIDKey& invitee,
                                                         atfw::team::EnTeamSourceType team_source_type,
                                                         const ::google::protobuf::Any& team_source_data) {
  // 持有本次请求对应的对象；异步响应不得清理同 key 后来建立的新对象。
  auto team = get_team_by_team_key(team_key);
  if (team && (team->is_exiting() || team->is_destroyed())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM);
  }
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

  if (team) {
    user_team::manager_accessor::repair_from_room_error(*team, ctx, ret);
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
    rpc::context& ctx, ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output) {
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
    rpc::context& ctx, ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output) {
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

void user_team_manager::add_team(rpc::context& ctx, const atfw::team::DTeamMemberJoinData& join_data) {
  if (join_data.team_key().team_id() == 0 || join_data.team_channel().channel_id().empty()) {
    return;
  }

  // 旧版本房间事件/存档未携带 team_type, 按普通组队处理; 显式传入但未配置的类型仍然丢弃
  // (init_from_table_data 依赖丢弃非法类型来恢复分组)
  uint32_t team_type = join_data.team_type();
  if (static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_INVALID) == team_type) {
    team_type = static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
  }
  if (!excel::get_ExcelTeamType_by_team_type(team_type)) {
    FCTXLOGERROR(ctx, "{} add_team: unknown team type {}", *owner_, static_cast<int32_t>(team_type));
    return;
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

        // 准备退出的队伍也需要标记为脏，以便触发下发移除
        insert_dirty_handle_for_team(iter_group->second.current->get_team_key());
      }
      iter_group->second.current = exists_team;
      // 已存在的新队伍要重新下发快照,因为之前可能已经下发过移除通知
      exists_team->init_cached_data(join_data.captain_user_key(), join_data.user_role());
      exists_team->make_current_actived(ctx);
      exists_team->insert_dirty_snapshot_handle();
      is_dirty_ = true;
      FCTXLOGINFO(ctx, "{} add_team: team {}:{} is still in exit queue, restore it to current", *owner_,
                  team_key.zone_id(), team_key.team_id());
    } else {
      FCTXLOGINFO(ctx, "{} add_team: team {}:{} already exists, skip to add a new one", *owner_, team_key.zone_id(),
                  team_key.team_id());
      // 重复入队(含退出流程中重新成为当前队伍前的通知)重置激活时间，避免被 wait_to_be_member 超时误踢
      exists_team->make_current_actived(ctx);
    }
    // 入队事件到达说明同队的邀请/加入请求已有结论(无论是否经过 approve), 清理自己的 pending
    remove_pending_invitation(ctx, team_key);
    remove_pending_join_request(ctx, team_key);

    return;
  }

  auto team_ptr = user_team::create(ctx, *this, team_type, team_key, channel_key);
  if (!team_ptr) {
    FCTXLOGERROR(ctx, "{} add_team: create user_team failed for team {}:{}", *owner_, team_key.zone_id(),
                 team_key.team_id());
    return;
  }

  team_group& group = team_group_[team_ptr->get_team_type()];
  if (group.current) {
    group.pending_to_exit.emplace(group.current->get_team_key(), group.current);
    group.current->send_exit_team_request(ctx, atfw::team::EN_TEAM_EXIT_REASON_IN_ANOTHER_TEAM);

    // 准备退出的队伍也需要标记为脏，以便触发下发移除
    insert_dirty_handle_for_team(group.current->get_team_key());
  }

  group.current = team_ptr;
  // 新的team会在load_snapshot时下发脏数据快照

  team_index_.emplace(team_key, team_ptr);
  user_team::manager_accessor::set_index_active(*team_ptr, true);
  is_dirty_ = true;
  // 同队的邀请/加入请求已有结论, 清理自己的 pending
  remove_pending_invitation(ctx, team_key);
  remove_pending_join_request(ctx, team_key);

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
    insert_dirty_handle_for_team(team_key);
    return;
  }
  auto team_ptr = iter->second;
  if (send_exit) {
    team_ptr->send_exit_team_request(ctx, exit_reason);
  } else if (!team_ptr->is_exiting()) {
    team_ptr->set_exit_team(ctx, exit_reason);
  }
  insert_dirty_handle_for_team(team_key);

  auto iter_group = team_group_.find(team_ptr->get_team_type());
  if (iter_group != team_group_.end()) {
    if (iter_group->second.current == team_ptr) {
      // 如果是当前队伍，且已经不是成员了或退出超时，则直接移除
      if (team_ptr->can_be_removed(ctx)) {
        iter_group->second.pending_to_exit.erase(team_key);
        user_team::manager_accessor::set_index_active(*team_ptr, false);
        team_index_.erase(iter);
      } else {
        // 如果是当前队伍，仍然是成员，移入到退出列表中，以便后续重试发送移除成员的消息
        iter_group->second.pending_to_exit.emplace(iter_group->second.current->get_team_key(),
                                                   iter_group->second.current);
      }
      iter_group->second.current.reset();
      is_dirty_ = true;
    } else {
      // 如果已经在退出列表中，检查是否可以直接移除
      auto iter_pending = iter_group->second.pending_to_exit.find(team_key);
      if (iter_pending != iter_group->second.pending_to_exit.end()) {
        if (iter_pending->second->can_be_removed(ctx)) {
          iter_group->second.pending_to_exit.erase(iter_pending);
          user_team::manager_accessor::set_index_active(*team_ptr, false);
          team_index_.erase(iter);
          is_dirty_ = true;
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
  for (auto iter = pending_join_request_by_expired_time_.begin();
       iter != pending_join_request_by_expired_time_.end();) {
    if (!*iter) {
      iter = pending_join_request_by_expired_time_.erase(iter);
      continue;
    }

    if (protobuf_to_system_clock((*iter)->expired_timepoint()) > now) {
      break;
    }

    expired_team_keys.insert((*iter)->team_key());
    ++iter;
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
  is_dirty_ = true;

  // 覆盖检查
  do {
    auto iter = pending_join_request_by_team_id_.find(join_request->team_key());
    if (iter == pending_join_request_by_team_id_.end()) {
      break;
    }
    if (protobuf_to_system_clock((*iter->second)->expired_timepoint()) == expired_timepoint) {
      protobuf_copy_message(*(*iter->second), *join_request);

      insert_dirty_handle_for_join_request(join_request->team_key());
      return true;
    }

    remove_pending_join_request(ctx, join_request->team_key());
  } while (false);

  // 快速查找插入点，大部分情况下应该是 append 到末尾
  if (pending_join_request_by_expired_time_.empty()) {
    auto iter = pending_join_request_by_expired_time_.insert(pending_join_request_by_expired_time_.end(), join_request);
    pending_join_request_by_team_id_.emplace(join_request->team_key(), iter);
    insert_dirty_handle_for_join_request(join_request->team_key());
    return true;
  }

  if (expired_timepoint >=
      protobuf_to_system_clock((*pending_join_request_by_expired_time_.rbegin())->expired_timepoint())) {
    auto iter = pending_join_request_by_expired_time_.insert(pending_join_request_by_expired_time_.end(), join_request);
    pending_join_request_by_team_id_.emplace(join_request->team_key(), iter);
    insert_dirty_handle_for_join_request(join_request->team_key());
    return true;
  }

  if (expired_timepoint <=
      protobuf_to_system_clock((*pending_join_request_by_expired_time_.begin())->expired_timepoint())) {
    auto iter =
        pending_join_request_by_expired_time_.insert(pending_join_request_by_expired_time_.begin(), join_request);
    pending_join_request_by_team_id_.emplace(join_request->team_key(), iter);
    insert_dirty_handle_for_join_request(join_request->team_key());
    return true;
  }

  // 慢速查找插入点
  for (auto iter = pending_join_request_by_expired_time_.begin(); iter != pending_join_request_by_expired_time_.end();
       ++iter) {
    if (expired_timepoint < protobuf_to_system_clock((*iter)->expired_timepoint())) {
      auto new_iter = pending_join_request_by_expired_time_.insert(iter, join_request);
      pending_join_request_by_team_id_.emplace(join_request->team_key(), new_iter);
      insert_dirty_handle_for_join_request(join_request->team_key());
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
  is_dirty_ = true;

  insert_dirty_handle_for_join_request(team_key);
  return true;
}

size_t user_team_manager::cleanup_expired_invitation(rpc::context& ctx) {
  std::unordered_set<atfw::team::DTeamKey, rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      expired_team_keys;

  auto now = ctx.logical_now();
  for (auto iter = pending_invitation_by_expired_time_.begin(); iter != pending_invitation_by_expired_time_.end();) {
    if (!*iter) {
      iter = pending_invitation_by_expired_time_.erase(iter);
      continue;
    }

    if (protobuf_to_system_clock((*iter)->expired_timepoint()) > now) {
      break;
    }

    expired_team_keys.insert((*iter)->team_key());
    ++iter;
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
  if (invitation->team_key().team_id() == 0) {
    return false;
  }

  auto expired_timepoint = protobuf_to_system_clock(invitation->expired_timepoint());
  if (expired_timepoint <= ctx.logical_now()) {
    return false;
  }
  is_dirty_ = true;

  // 覆盖检查
  do {
    auto iter = pending_invitation_by_team_id_.find(invitation->team_key());
    if (iter == pending_invitation_by_team_id_.end()) {
      break;
    }

    if (protobuf_to_system_clock((*iter->second)->expired_timepoint()) == expired_timepoint) {
      protobuf_copy_message(*(*iter->second), *invitation);

      insert_dirty_handle_for_invitation(invitation->team_key());
      return true;
    }

    remove_pending_invitation(ctx, invitation->team_key());
  } while (false);

  // 快速查找插入点，大部分情况下应该是 append 到末尾
  if (pending_invitation_by_expired_time_.empty()) {
    auto iter = pending_invitation_by_expired_time_.insert(pending_invitation_by_expired_time_.end(), invitation);
    pending_invitation_by_team_id_.emplace(invitation->team_key(), iter);
    insert_dirty_handle_for_invitation(invitation->team_key());
    return true;
  }

  if (expired_timepoint >=
      protobuf_to_system_clock((*pending_invitation_by_expired_time_.rbegin())->expired_timepoint())) {
    auto iter = pending_invitation_by_expired_time_.insert(pending_invitation_by_expired_time_.end(), invitation);
    pending_invitation_by_team_id_.emplace(invitation->team_key(), iter);
    insert_dirty_handle_for_invitation(invitation->team_key());
    return true;
  }

  if (expired_timepoint <=
      protobuf_to_system_clock((*pending_invitation_by_expired_time_.begin())->expired_timepoint())) {
    auto iter = pending_invitation_by_expired_time_.insert(pending_invitation_by_expired_time_.begin(), invitation);
    pending_invitation_by_team_id_.emplace(invitation->team_key(), iter);
    insert_dirty_handle_for_invitation(invitation->team_key());
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

  insert_dirty_handle_for_invitation(invitation->team_key());
  return true;
}

bool user_team_manager::remove_pending_invitation(rpc::context& /*ctx*/, const atfw::team::DTeamKey& team_key) {
  auto iter = pending_invitation_by_team_id_.find(team_key);
  if (iter == pending_invitation_by_team_id_.end()) {
    return false;
  }

  pending_invitation_by_expired_time_.erase(iter->second);
  pending_invitation_by_team_id_.erase(iter);
  is_dirty_ = true;

  insert_dirty_handle_for_invitation(team_key);
  return true;
}

bool user_team_manager::insert_dirty_handle() {
  // 未拉取过数据，直接返回，不用设置数据推送
  if (!is_pulled_) {
    return false;
  }

  owner_->insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(this), "user_team_manager.insert_dirty_handle",
      [](rpc::context& ctx, user& user_inst, user::dirty_message_container& output) {
        auto& team_mgr = user_inst.get_user_team_manager();

        if (team_mgr.dirty_team_.empty() && team_mgr.dirty_invitation_.empty() &&
            team_mgr.dirty_join_request_.empty()) {
          return;
        }

        // 直接写入输出消息, 记录追加前的基数: 脏集合非空不代表有内容要发(全部去重/已移除未发布过时为空),
        // 末尾按基数判定, 仍为空则回收本次创建的 SCUserDirtyChgSync, 避免向客户端下发空消息
        const bool created_here = !output.user_dirty;
        if (created_here) {
          output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
        }
        auto* output_teams = output.user_dirty->mutable_dirty_team();
        const int output_base_size = output_teams->size();

        // team的脏数据dump在team里执行，如果找不到说明是移除
        for (const auto& dirty : team_mgr.dirty_team_) {
          const auto& key = dirty.first;
          auto team = team_mgr.get_team_by_team_key(key);
          if (!team) {
            // 索引中找不到说明队伍已移除; 只有客户端曾经收到过这个队伍的数据才需要下发 remove
            // (对象缺失时无法确认是否下发过，保守下发, 客户端对未知队伍的 remove 是幂等的)
            if (!dirty.second || user_team::manager_accessor::is_client_announced(*dirty.second)) {
              protobuf_copy_message(*output_teams->Add()->mutable_team_remove(), key);
            }
            if (dirty.second) {
              user_team::manager_accessor::set_client_announced(*dirty.second, false);
              user_team::manager_accessor::set_dirty_remove_sent(*dirty.second, true);
            }
            continue;
          }

          // remove 已下发且之后未重新激活(add_team 经 make_current_actived 复位该标记),
          // 直接跳过, 无须再走 is_removed_for_client 的索引查找
          if (user_team::manager_accessor::is_dirty_remove_sent(*team)) {
            continue;
          }

          // 离开成员状态/正在退出/频道已销毁/已让出当前队伍或被新对象替换，对客户端来说都当成移除处理
          // (与 user_team::insert_dirty_action_handle 的增量准入共用同一判定)
          if (team->is_removed_for_client()) {
            // 即使本次因未发布过而无需下发，也标记为已处理，防止退出重试/迟到事件/清理流程重复标记
            user_team::manager_accessor::set_dirty_remove_sent(*team, true);
            if (user_team::manager_accessor::is_client_announced(*team)) {
              user_team::manager_accessor::set_client_announced(*team, false);
              protobuf_copy_message(*output_teams->Add()->mutable_team_remove(), key);
            }

            continue;
          }

          // 直接 dump 到输出 entry: dump 无内容时保持 dirty_type 未设置, 回滚该 entry, 空 entry 不下发
          team->dump_dirty_data(ctx, *output_teams->Add());
          if (PROJECT_NAMESPACE_ID::DUserTeamDirty::DIRTY_TYPE_NOT_SET ==
              output_teams->at(output_teams->size() - 1).dirty_type_case()) {
            output_teams->RemoveLast();
          } else {
            user_team::manager_accessor::set_client_announced(*team, true);
          }
        }

        // 更新数据和新增都是add，找不到则是移除
        for (const auto& key : team_mgr.dirty_invitation_) {
          auto invitation = team_mgr.get_pending_invitation(key);
          if (invitation) {
            auto* added = output_teams->Add()->mutable_add_pending_invitation();
            protobuf_copy_message(*added, *invitation);
            added->clear_invitee_private_channel();
          } else {
            protobuf_copy_message(*output_teams->Add()->mutable_remove_pending_invitation(), key);
          }
        }

        for (const auto& key : team_mgr.dirty_join_request_) {
          auto join_request = team_mgr.get_pending_join_request(key);
          if (join_request) {
            auto* added = output_teams->Add()->mutable_add_pending_join_request();
            protobuf_copy_message(*added, *join_request);
            added->clear_requester_private_channel();
            added->set_user_router_server_id(0);
          } else {
            protobuf_copy_message(*output_teams->Add()->mutable_remove_pending_join_request(), key);
          }
        }

        if (output_teams->size() == output_base_size && created_here) {
          // 本次没有追加任何内容且消息由本回调创建, 回收以避免空推送; 已有消息则原样保留其他模块的内容
          output.user_dirty.reset();
        }
      },
      [](rpc::context& ctx, user& user_inst) {
        auto& team_mgr = user_inst.get_user_team_manager();

        for (const auto& dirty : team_mgr.dirty_team_) {
          // 登记时的对象可能已被收编/替换(条目未随 flush 及时清理), 此时要清理的是索引里的当前代际,
          // 否则当前代际的 pending 快照/增量标志残留, 下一次 flush 会重复下发
          auto current_team = team_mgr.get_team_by_team_key(dirty.first);
          if (current_team && current_team != dirty.second) {
            current_team->clear_dirty_data(ctx);
          }
          if (dirty.second) {
            dirty.second->clear_dirty_data(ctx);
          }
        }

        team_mgr.dirty_team_.clear();
        team_mgr.dirty_invitation_.clear();
        team_mgr.dirty_join_request_.clear();
      });

  return true;
}

bool user_team_manager::insert_dirty_handle_for_team(const atfw::team::DTeamKey& key) {
  auto team = get_team_by_team_key(key);
  // remove 已下发(或无需下发)后不重复标记; 重新激活(add_team 经 make_current_actived)会复位该标记,
  // 因此这里无须再调用 is_removed_for_client 做索引查找
  if (team && user_team::manager_accessor::is_dirty_remove_sent(*team)) {
    return false;
  }
  if (!insert_dirty_handle()) {
    return false;
  }
  dirty_team_.emplace(key, team);
  return true;
}

bool user_team_manager::insert_dirty_handle_for_invitation(const atfw::team::DTeamKey& key) {
  if (!insert_dirty_handle()) {
    return false;
  }
  dirty_invitation_.insert(key);
  return true;
}

bool user_team_manager::insert_dirty_handle_for_join_request(const atfw::team::DTeamKey& key) {
  if (!insert_dirty_handle()) {
    return false;
  }
  dirty_join_request_.insert(key);
  return true;
}

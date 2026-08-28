// Copyright 2026 atframework

#include "logic/team/user_team.h"

#include <memory/object_allocator.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/lobbysvr_config.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/team/team_room_client_api.h>

#include <utility/protobuf_mini_dumper.h>

#include <data/user_key_hash_helper.h>

#include <chrono>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h"

#include "data/user.h"

#include "logic/chat/user_chat_manager.h"
#include "logic/team/user_team_algorithm.h"
#include "logic/team/user_team_manager.h"

namespace {
std::chrono::system_clock::duration get_exit_team_request_retry_interval() noexcept {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().exit_retry_interval().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().exit_retry_interval());
  }
  return std::chrono::seconds(5);
}

std::chrono::system_clock::duration get_exit_team_timeout() noexcept {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().exit_timeout().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().exit_timeout());
  }
  return std::chrono::seconds(30);
}

std::chrono::system_clock::duration get_wait_add_member_timeout() noexcept {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().wait_add_member_timeout().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().wait_add_member_timeout());
  }
  return std::chrono::seconds(30);
}

// 成员心跳间隔。需要明显低于 teamsvr-room 的 member_offline_expire(默认 300s)，否则成员会被离线过期踢出
std::chrono::system_clock::duration get_team_heartbeat_interval() noexcept {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  if (server_cfg.team().heartbeat_interval().seconds() > 0) {
    return protobuf_to_system_clock(server_cfg.team().heartbeat_interval());
  }
  return std::chrono::seconds(120);
}

// 与 user_team_manager 的 pending_join_request/pending_invitation 相同的双重索引维护:
// 列表按过期时间升序 + key 到 list 迭代器的索引，保证单点 O(1) 删除。
// 邀请/加入请求的有效期长度大多一致，新条目通常直接追加到尾部，先走尾部/头部快速路径
template <class TLIST, class TINDEX, class TKEY>
void insert_admission_by_expired_time(TLIST& list, TINDEX& index, const TKEY& key,
                                      const typename TLIST::value_type& value) {
  auto expired_timepoint = protobuf_to_system_clock(value.expired_timepoint());

  auto insert_point = list.end();
  if (!list.empty() && expired_timepoint < protobuf_to_system_clock(list.back().expired_timepoint())) {
    if (expired_timepoint <= protobuf_to_system_clock(list.front().expired_timepoint())) {
      insert_point = list.begin();
    } else {
      for (auto iter = list.begin(); iter != list.end(); ++iter) {
        if (expired_timepoint < protobuf_to_system_clock(iter->expired_timepoint())) {
          insert_point = iter;
          break;
        }
      }
    }
  }

  index.emplace(key, list.insert(insert_point, value));
}

template <class TLIST, class TINDEX, class TKEY>
void upsert_admission_by_expired_time(TLIST& list, TINDEX& index, const TKEY& key,
                                      const typename TLIST::value_type& value) {
  auto old_iter = index.find(key);
  if (old_iter != index.end()) {
    // 同 key 且过期时间不变则原位覆盖，保持列表顺序不变
    if (protobuf_to_system_clock(old_iter->second->expired_timepoint()) ==
        protobuf_to_system_clock(value.expired_timepoint())) {
      protobuf_copy_message(*old_iter->second, value);
      return;
    }
    list.erase(old_iter->second);
    index.erase(old_iter);
  }
  insert_admission_by_expired_time(list, index, key, value);
}

static user_team* get_user_team(const rpc::dtmq::client_subscriber::ptr_t& subscriber, gsl::string_view callback_name) {
  if (!subscriber) {
    return nullptr;
  }
  auto local_private_data = subscriber->get_local_private_data();
  if (local_private_data.empty()) {
    FWLOGERROR("user_team {} callback missing local_private_data", callback_name);
    return nullptr;
  }

  static_assert(sizeof(user_team*) == sizeof(*local_private_data.data()), "user_team* size mismatch");
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return reinterpret_cast<user_team*>(*local_private_data.data());
}

}  // namespace

class user_team_utility {
 public:
  using append_condition_checker = void (*)(rpc::context&, user_team&,
                                            ::google::protobuf::RepeatedPtrField<atfw::team::DTeamConditionChecker>&);

  using build_team_shared_data_condition_checker =
      append_condition_checker (*)(user_team&, const PROJECT_NAMESPACE_ID::DTeamSharedDataModule&);
  using allow_update_team_shared_data = int32_t (*)(rpc::context&, user_team&,
                                                    const PROJECT_NAMESPACE_ID::DTeamSharedDataModule&);
  using normalize_update_team_shared_data = void (*)(rpc::context&, user_team&,
                                                     PROJECT_NAMESPACE_ID::DTeamSharedDataModule&);
  using do_update_team_shared_data = void (*)(rpc::context&, user_team&,
                                              const PROJECT_NAMESPACE_ID::DTeamSharedDataModule&);

  using build_member_shared_data_condition_checker =
      append_condition_checker (*)(user_team&, const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule&);
  using allow_update_member_shared_data = int32_t (*)(rpc::context&, user_team&,
                                                      const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule&);
  using normalize_update_member_shared_data = void (*)(rpc::context&, user_team&,
                                                       PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule&);
  using do_update_member_shared_data = void (*)(rpc::context&, user_team&, const PROJECT_NAMESPACE_ID::DUserIDKey&,
                                                const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule&);

  struct team_shared_data_update_handlers {
    build_team_shared_data_condition_checker build_append_condition = nullptr;
    allow_update_team_shared_data allow_update = nullptr;
    normalize_update_team_shared_data normalize_update = nullptr;
    do_update_team_shared_data do_update = nullptr;
  };

  struct member_shared_data_update_handlers {
    build_member_shared_data_condition_checker build_append_condition = nullptr;
    allow_update_member_shared_data allow_update = nullptr;
    normalize_update_member_shared_data normalize_update = nullptr;
    do_update_member_shared_data do_update = nullptr;
  };

 private:
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t build_event_callback_set() {
    rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
        rpc::dtmq::client_subscriber::create_event_callback_set();

    rpc::dtmq::client_subscriber::set_event_callback_on_receive_snapshot_finished(
        *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                 const ::atfw::dtmq::DChannelSnapshot& /*snapshot*/, int32_t result_code) {
          user_team* team_ptr = get_user_team(subscriber, "on_receive_snapshot_finished");
          if (team_ptr != nullptr && result_code >= 0) {
            auto hold_lifetime = team_ptr->shared_from_this();
            hold_lifetime->load_snapshot(ctx);
            // 快照脏数据在回调任务内不会经过 CS 任务收尾，这里主动触发一次脏数据下发
            hold_lifetime->owner_->get_owner().send_all_syn_msg(ctx);
          }
        });

    rpc::dtmq::client_subscriber::set_event_callback_on_receive_raw_message(
        *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                 const ::atfw::dtmq::DChannelMessage& data) {
          user_team* team_ptr = get_user_team(subscriber, "on_receive_raw_message");
          if (team_ptr != nullptr) {
            auto hold_lifetime = team_ptr->shared_from_this();
            hold_lifetime->on_receive_raw_message(ctx, data);
          }
        });

    rpc::dtmq::client_subscriber::set_event_callback_on_receive_batch_message_finished(
        *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                 int64_t /*first_log_sequence*/, int64_t /*last_log_sequence*/) {
          user_team* team_ptr = get_user_team(subscriber, "on_receive_batch_message_finished");
          if (team_ptr != nullptr) {
            auto hold_lifetime = team_ptr->shared_from_this();
            // 实时事件(非快照回放)处理后主动触发一次脏数据下发，快照回放路径由
            // on_receive_snapshot_finished 统一触发，不会逐条消息下发
            hold_lifetime->owner_->get_owner().send_all_syn_msg(ctx);
          }
        });

    rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
        *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber, int64_t log_sequence,
                 std::chrono::system_clock::time_point /*destroy_time*/) {
          user_team* team_ptr = get_user_team(subscriber, "on_destroyed");
          // 可能先删除频道，而后重新创建的流程。所以要忽略之前的频道销毁通知
          if (team_ptr != nullptr && log_sequence >= team_ptr->channel_create_sequence_) {
            auto hold_lifetime = team_ptr->shared_from_this();
            hold_lifetime->is_member_ = false;
            FCTXLOGDEBUG(ctx, "{} channel for team {}:{} destroyed, sequence:{}", hold_lifetime->owner_->get_owner(),
                         hold_lifetime->team_key_.zone_id(), hold_lifetime->team_key_.team_id(), log_sequence);
            hold_lifetime->owner_->remove_team(ctx, hold_lifetime->get_team_key(),
                                               atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
          }
        });

    return ret;
  }

  static void append_condition_team_not_matching(
      rpc::context& ctx, user_team&,
      ::google::protobuf::RepeatedPtrField<atfw::team::DTeamConditionChecker>& conditions) {
    auto* rule = conditions.empty() ? conditions.Add() : conditions.Mutable(0);

    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamSharedDataModule> checked_value{ctx};
    checked_value->mutable_battle()->set_matching(false);
    auto* checked_item = rule->add_shared_team_data();
    checked_item->set_key(user_team_algorithm::make_team_shared_data_key(*checked_value));
    if (!checked_item->mutable_value()->PackFrom(*checked_value)) {
      FCTXLOGERROR(ctx, "Failed to pack checked_value into checked_item");
      rule->mutable_shared_team_data()->RemoveLast();
    }
  }

  static void append_condition_all_member_ready(
      rpc::context& ctx, user_team&,
      ::google::protobuf::RepeatedPtrField<atfw::team::DTeamConditionChecker>& conditions) {
    auto* rule = conditions.empty() ? conditions.Add() : conditions.Mutable(0);

    ::atfw::team::DTeamConditionChecker_DMemberConditionGroup* group = nullptr;
    for (int i = 0; i < rule->member_condition_group_size(); ++i) {
      if (rule->mutable_member_condition_group(i)->scope_type_case() ==
          ::atfw::team::DTeamConditionChecker_DMemberConditionGroup::kAllMembers) {
        group = rule->mutable_member_condition_group(i);
        break;
      }
    }
    if (group == nullptr) {
      group = rule->add_member_condition_group();
    }
    group->set_all_members(true);

    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> checked_value{ctx};
    checked_value->mutable_battle()->set_ready(true);
    auto* checked_item = group->mutable_member_condition()->add_shared_member_data();
    checked_item->set_key(user_team_algorithm::make_team_member_shared_data_key(*checked_value));
    if (!checked_item->mutable_value()->PackFrom(*checked_value)) {
      FCTXLOGERROR(ctx, "Failed to pack checked_value into checked_item");
      group->mutable_member_condition()->mutable_shared_member_data()->RemoveLast();
    }
  }

 public:
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t get_event_callback_set() {
    static rpc::dtmq::client_subscriber::event_callback_set_ptr_t callback_set = build_event_callback_set();
    return callback_set;
  }

  static std::unordered_map<int64_t, team_shared_data_update_handlers> build_team_shared_data_update_handlers_map() {
    std::unordered_map<int64_t, team_shared_data_update_handlers> handlers_map;

    {
      PROJECT_NAMESPACE_ID::DTeamSharedDataModule event;
      event.mutable_battle()->set_matching(true);
      int64_t key = user_team_algorithm::make_team_shared_data_key(event);

      auto& handles = handlers_map[key];
      handles.build_append_condition =
          [](user_team&, const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data) -> append_condition_checker {
        // 开始匹配的条件是所有用户都ready
        if (data.battle().matching()) {
          return append_condition_all_member_ready;
        }
        return nullptr;
      };

      handles.do_update = [](rpc::context& ctx, user_team& team,
                             const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data) {
        team.set_matching(ctx, data.battle().matching());
      };
    }
    return handlers_map;
  }

  static std::unordered_map<int64_t, team_shared_data_update_handlers> get_team_shared_data_update_handlers_map() {
    static std::unordered_map<int64_t, team_shared_data_update_handlers> handlers_map =
        build_team_shared_data_update_handlers_map();
    return handlers_map;
  }

  static std::unordered_map<int64_t, member_shared_data_update_handlers>
  build_member_shared_data_update_handlers_map() {
    std::unordered_map<int64_t, member_shared_data_update_handlers> handlers_map;
    {
      PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule event;
      event.mutable_battle()->set_ready(true);
      int64_t key = user_team_algorithm::make_team_member_shared_data_key(event);

      auto& handles = handlers_map[key];
      handles.build_append_condition =
          [](user_team&, const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule&) -> append_condition_checker {
        // 设置ready的条件是团队不在匹配状态
        return append_condition_team_not_matching;
      };
    }
    return handlers_map;
  }

  static std::unordered_map<int64_t, member_shared_data_update_handlers> get_member_shared_data_update_handlers_map() {
    static std::unordered_map<int64_t, member_shared_data_update_handlers> handlers_map =
        build_member_shared_data_update_handlers_map();
    return handlers_map;
  }
};

struct user_team::ctor_guard_t {};

// NOLINTNEXTLINE(modernize-pass-by-value)
user_team::user_team(ctor_guard_t&, rpc::context& /*ctx*/, user_team_manager& owner,
                     atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&& channel_subscriber,
                     // NOLINTNEXTLINE(modernize-pass-by-value)
                     uint32_t team_type, const atfw::team::DTeamKey& team_key)
    : owner_(&owner),
      pending_dirty_snapshot_(false),
      team_type_(team_type),
      team_key_(team_key),
      channel_subscriber_(channel_subscriber),
      is_member_(false),
      is_matching_(false),
      channel_create_sequence_(0),
      channel_saved_sequence_(0),
      last_exit_team_request_timepoint_(std::chrono::system_clock::from_time_t(0)),
      last_exit_team_reason_(atfw::team::EN_TEAM_EXIT_REASON_DEFAULT),
      cached_permission_role_(atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(this)};
  channel_subscriber_->set_local_private_data(local_private_data);
}

user_team::~user_team() {
  // 脏数据句柄以成员地址为 key 挂在 user 上，对象析构后不会自动移除。
  // 若不注销，新的 user_team 复用到相同地址时 insert_dirty_handle_if_not_exists 会因 key 冲突而跳过注册，
  // 导致新队伍的脏数据(快照/增量)永远无法下发。
  auto& dirty_handles = owner_->get_owner().get_cache_data().dirty_handles;
  dirty_handles.erase(reinterpret_cast<uintptr_t>(&pending_dirty_snapshot_));
  dirty_handles.erase(reinterpret_cast<uintptr_t>(&pending_dirty_actions_));
}

user_team::ptr_t user_team::create(rpc::context& ctx, user_team_manager& owner, uint32_t team_type,
                                   const atfw::team::DTeamKey& team_key, const atfw::dtmq::DChannelIdKey& channel_key) {
  ctor_guard_t guard;
  rpc::dtmq::client_subscriber::subscriber_options options{
      owner.get_owner().get_user_chat_manager().get_subscriber_key()};

  // auto_create_channel 设为false，如果是恢复已经失效的频道，后续会通过 on_destroyed 回调移除
  options.auto_create_channel = false;
  options.event_callback_set = user_team_utility::get_event_callback_set();
  auto channel_subscriber = rpc::dtmq::client_subscriber::create(channel_key, options);
  if (!channel_subscriber) {
    return nullptr;
  }

  return atfw::component::memory::stl::make_strong_rc<user_team>(guard, ctx, owner, std::move(channel_subscriber),
                                                                 team_type, team_key);
}

void user_team::init_cached_data(const PROJECT_NAMESPACE_ID::DUserIDKey& captain_user_key,
                                 atfw::team::EnTeamPermissionRole permission_role) {
  cached_captain_user_key_ = captain_user_key;
  cached_permission_role_ = permission_role;
}

void user_team::dump(atfw::team::DTeamMemberJoinData& join_data) const {
  protobuf_copy_message(*join_data.mutable_team_key(), get_team_key());
  protobuf_copy_message(*join_data.mutable_team_channel(), get_channel_key());
  owner_->get_owner().dump_user_key(*join_data.mutable_user_key());

  join_data.mutable_captain_user_key()->CopyFrom(cached_captain_user_key_);
  join_data.set_user_role(cached_permission_role_);
}

void user_team::dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::DUserTeamSnapshot& output) const {
  auto* storage = output.mutable_snapshot();
  protobuf_copy_message(*storage->mutable_team_key(), team_key_);
  protobuf_copy_message(*storage->mutable_captain_user_key(), cached_captain_user_key_);
  protobuf_copy_message(*storage->mutable_configure(), cached_configure_);

  // 成员数据(快照无需有序，直接按哈希表序导出)。
  // 不下发 user_channel/user_router_server_id/acknowledge_* 等内部路由与确认字段
  for (const auto& member : cached_members_) {
    auto* output_member = storage->add_member();
    protobuf_copy_message(*output_member, member.second.member_data);
    output_member->clear_user_channel();
    output_member->set_user_router_server_id(0);
    output_member->set_acknowledge_action_sequence(0);
    output_member->set_acknowledge_action_hash_code(0);

    // 回填共享成员数据(内存中 member_data.shared_member_data 恒为空)
    for (const auto& kv : member.second.shared_member_data) {
      auto* output_data = output_member->add_shared_member_data();
      output_data->set_key(kv.first);
      protobuf_copy_message(*output_data->mutable_value(), kv.second);
    }
  }

  // 待处理的邀请和加入请求(列表按过期时间升序，过期条目只可能出现在头部且通常已被清理)，
  // 不下发私有通知频道等内部路由字段
  auto now = ctx.logical_now();
  for (const auto& invitation : pending_invitation_by_expired_time_) {
    if (protobuf_to_system_clock(invitation.expired_timepoint()) <= now) {
      continue;
    }
    auto* output_invitation = storage->add_pending_invitation();
    protobuf_copy_message(*output_invitation, invitation);
    output_invitation->clear_invitee_private_channel();
  }
  for (const auto& join_request : pending_join_request_by_expired_time_) {
    if (protobuf_to_system_clock(join_request.expired_timepoint()) <= now) {
      continue;
    }
    auto* output_join_request = storage->add_pending_join_request();
    protobuf_copy_message(*output_join_request, join_request);
    output_join_request->clear_requester_private_channel();
    output_join_request->set_user_router_server_id(0);
  }

  // 队伍共享数据不导出 snapshot.shared_team_data 的原始打包数据(本地也不存)，直接转出成解包后的模块数据
  for (const auto& kv : cached_team_shared_data_) {
    protobuf_copy_message(*output.add_shared_team_data(), kv.second);
  }
}

bool user_team::can_be_removed(rpc::context& ctx) const noexcept {
  // 频道已销毁，可以直接移除
  if (channel_subscriber_->is_destroyed()) {
    return true;
  }

  if (last_exit_team_request_timepoint_ <= std::chrono::system_clock::from_time_t(0)) {
    return false;
  }

  if (ctx.logical_now() >= last_exit_team_request_timepoint_ + get_exit_team_timeout()) {
    return true;
  }

  if (!channel_subscriber_->is_ready()) {
    return false;
  }

  return !is_member_;
}

bool user_team::wait_to_be_member_but_timeout(rpc::context& ctx) const noexcept {
  // 频道已销毁，可以直接视为member添加超时
  if (channel_subscriber_->is_destroyed()) {
    return true;
  }

  if (is_member_) {
    return false;
  }

  return ctx.logical_now() >= actived_timepoint_ + get_wait_add_member_timeout();
}

bool user_team::is_exiting() const noexcept {
  return last_exit_team_request_timepoint_ > std::chrono::system_clock::from_time_t(0);
}

bool user_team::is_destroyed() const noexcept { return channel_subscriber_->is_destroyed(); }

const atfw::dtmq::DChannelIdKey& user_team::get_channel_key() const noexcept {
  return channel_subscriber_->get_channel_key();
}

bool user_team::check_permission(atfw::team::EnTeamPermissionRole checked) const noexcept {
  return cached_permission_role_ >= checked;
}

void user_team::make_current_actived(rpc::context& ctx) {
  last_exit_team_request_timepoint_ = std::chrono::system_clock::from_time_t(0);
  actived_timepoint_ = ctx.logical_now();
}

void user_team::send_exit_team_request(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason) {
  set_exit_team(ctx, exit_reason);

  // 如果频道已销毁，则不用再发送退出
  if (channel_subscriber_->is_destroyed()) {
    return;
  }

  auto self = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "user_team.send_exit_team_request", [self, exit_reason](rpc::context& child_ctx) -> rpc::result_code_type {
        rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req_body{child_ctx};
        rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp_body{child_ctx};
        protobuf_copy_message(*req_body->mutable_team_key(), self->team_key_);
        self->owner_->get_owner().dump_user_key(*req_body->mutable_sender_user_key());

        auto* remove_member_action = req_body->mutable_action()->mutable_remove_member();
        remove_member_action->mutable_team_key()->CopyFrom(self->team_key_);
        protobuf_copy_message(*remove_member_action->mutable_user_key(), req_body->sender_user_key());
        remove_member_action->set_remove_member_reason(exit_reason);

        RPC_AWAIT_IGNORE_RESULT(rpc::team::team_api::send_message(child_ctx, *req_body, *rsp_body, true));

        RPC_RETURN_CODE(0);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "team {}:{} send_exit_team_request: async_invoke failed, error={}({})", team_key_.zone_id(),
                 team_key_.team_id(), *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

rpc::result_code_type user_team::send_action(rpc::context& ctx, atfw::team::DTeamAction&& action) {
  rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> req_body{ctx};
  rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> rsp_body{ctx};
  protobuf_copy_message(*req_body->mutable_team_key(), team_key_);
  owner_->get_owner().dump_user_key(*req_body->mutable_sender_user_key());
  *req_body->mutable_action() = std::move(action);

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::team::team_api::send_message(ctx, *req_body, *rsp_body));
  if (0 == ret) {
    ret = rsp_body->client_result();
  }
  if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND == ret) {
    // 目标频道已不存在(队伍已解散或数据链路失效)，对客户端表现为已不在队伍中
    ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM;
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_team::accept_join_request(rpc::context& ctx,
                                                     const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  rpc::context::message_holder<atfw::team::DTeamAction> action{ctx};
  auto* join_request = action->mutable_approve_join_request();
  protobuf_copy_message(*join_request->mutable_team_key(), team_key_);
  protobuf_copy_message(*join_request->mutable_requester(), user_key);

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, std::move(*action))));
}

rpc::result_code_type user_team::reject_join_request(rpc::context& ctx,
                                                     const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  rpc::context::message_holder<atfw::team::DTeamAction> action{ctx};
  auto* join_request = action->mutable_reject_join_request();
  protobuf_copy_message(*join_request->mutable_team_key(), team_key_);
  protobuf_copy_message(*join_request->mutable_requester(), user_key);

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, std::move(*action))));
}

rpc::result_code_type user_team::remove_member(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  rpc::context::message_holder<atfw::team::DTeamAction> action{ctx};
  auto* remove_data = action->mutable_remove_member();
  protobuf_copy_message(*remove_data->mutable_team_key(), team_key_);
  protobuf_copy_message(*remove_data->mutable_user_key(), user_key);
  remove_data->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_REMOVE_MEMBER);

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, std::move(*action))));
}

rpc::result_code_type user_team::transfer_captain(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  rpc::context::message_holder<atfw::team::DTeamAction> action{ctx};
  protobuf_copy_message(*action->mutable_election_captain()->mutable_user_key(), user_key);

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, std::move(*action))));
}

rpc::result_code_type user_team::update_member_role(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                                    atfw::team::EnTeamPermissionRole role) {
  rpc::context::message_holder<atfw::team::DTeamAction> action{ctx};
  auto* set_role = action->mutable_member_set_role();
  protobuf_copy_message(*set_role->mutable_user_key(), user_key);
  set_role->set_role(role);

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, std::move(*action))));
}

rpc::result_code_type user_team::update_team_shared_data(
    rpc::context& ctx, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DTeamSharedDataModule>& data) {
  std::unordered_set<int64_t> existed_keys;
  std::unordered_set<user_team_utility::append_condition_checker> condition_appenders;
  existed_keys.reserve(static_cast<size_t>(data.size()));

  rpc::context::message_holder<atfw::team::DTeamAction> action{ctx};
  auto* team_update = action->mutable_team_update();

  const auto& handle_map = user_team_utility::get_team_shared_data_update_handlers_map();
  for (auto& team_data : data) {
    int64_t key = user_team_algorithm::make_team_shared_data_key(team_data);
    if (existed_keys.find(key) != existed_keys.end()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    }
    existed_keys.insert(key);

    // 未注册处理器的模块数据也要正常透传(处理器只负责附加条件检查与本地行为)，
    // 客户端可写的模块由任务层的 user_team_algorithm::allow_client_update_team_shared_data 把关
    auto iter = handle_map.find(key);
    if (iter != handle_map.end()) {
      if (iter->second.allow_update != nullptr) {
        int32_t res = iter->second.allow_update(ctx, *this, team_data);
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }
      }
      if (iter->second.normalize_update != nullptr) {
        iter->second.normalize_update(ctx, *this, team_data);
      }

      if (iter->second.build_append_condition != nullptr) {
        auto handle = iter->second.build_append_condition(*this, team_data);
        if (handle != nullptr) {
          condition_appenders.insert(std::move(handle));
        }
      }
    }

    auto* data_item = team_update->add_shared_team_data();
    data_item->set_key(key);
    // FIXME: 其他可见性
    data_item->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!data_item->mutable_value()->mutable_data()->PackFrom(team_data)) {
      FCTXLOGERROR(ctx, "{} failed to parse shared team data for key {}, error message: {}", owner_->get_owner(), key,
                   team_data.InitializationErrorString());
      team_update->mutable_shared_team_data()->RemoveLast();
    }
  }

  // 填充更新条件
  // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
  for (const auto& condition_appender : condition_appenders) {
    condition_appender(ctx, *this, *team_update->mutable_condition());
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, std::move(*action))));
}

rpc::result_code_type user_team::update_member_shared_data(
    rpc::context& ctx, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule>& data) {
  std::unordered_set<int64_t> existed_keys;
  std::unordered_set<user_team_utility::append_condition_checker> condition_appenders;
  existed_keys.reserve(static_cast<size_t>(data.size()));

  rpc::context::message_holder<atfw::team::DTeamAction> action{ctx};
  auto* member_update = action->mutable_member_update();
  owner_->get_owner().dump_user_key(*member_update->mutable_user_key());
  protobuf_copy_message(*member_update->mutable_user_channel(),
                        owner_->get_owner().get_user_chat_manager().get_private_chat_channel_key());
  member_update->set_client_version(owner_->get_owner().get_client_info().client_version());
  member_update->set_user_router_server_id(logic_config::me()->get_local_server_id());

  const auto& handle_map = user_team_utility::get_member_shared_data_update_handlers_map();
  for (auto& member_data : data) {
    int64_t key = user_team_algorithm::make_team_member_shared_data_key(member_data);
    if (existed_keys.find(key) != existed_keys.end()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    }
    existed_keys.insert(key);

    // 未注册处理器的模块数据也要正常透传(处理器只负责附加条件检查与本地行为)，
    // 客户端可写的模块由任务层的 user_team_algorithm::allow_client_update_team_member_shared_data 把关
    auto iter = handle_map.find(key);
    if (iter != handle_map.end()) {
      if (iter->second.allow_update != nullptr) {
        int32_t res = iter->second.allow_update(ctx, *this, member_data);
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }
      }
      if (iter->second.normalize_update != nullptr) {
        iter->second.normalize_update(ctx, *this, member_data);
      }

      if (iter->second.build_append_condition != nullptr) {
        auto handle = iter->second.build_append_condition(*this, member_data);
        if (handle != nullptr) {
          condition_appenders.insert(std::move(handle));
        }
      }
    }

    auto* data_item = member_update->add_shared_member_data();
    data_item->set_key(key);
    // FIXME: 其他可见性
    data_item->mutable_value()->set_permission(::atfw::team::EN_TEAM_PERMISSION_TYPE_MEMBER);
    if (!data_item->mutable_value()->mutable_data()->PackFrom(member_data)) {
      FCTXLOGERROR(ctx, "{} failed to parse shared member data for key {}, error message: {}", owner_->get_owner(), key,
                   member_data.InitializationErrorString());
      member_update->mutable_shared_member_data()->RemoveLast();
    }
  }

  // 填充更新条件
  // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
  for (const auto& condition_appender : condition_appenders) {
    condition_appender(ctx, *this, *member_update->mutable_condition());
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, std::move(*action))));
}

void user_team::set_exit_team(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason) {
  last_exit_team_reason_ = exit_reason;
  last_exit_team_request_timepoint_ = ctx.logical_now();
}

void user_team::retry_send_exit_team_request(rpc::context& ctx) {
  if (last_exit_team_request_timepoint_ <= std::chrono::system_clock::from_time_t(0)) {
    return;
  }

  if (last_exit_team_request_timepoint_ + get_exit_team_request_retry_interval() >= ctx.logical_now()) {
    return;
  }

  send_exit_team_request(ctx, last_exit_team_reason_);
}

void user_team::try_load_snapshot(rpc::context& ctx) {
  if (!channel_subscriber_->is_ready()) {
    return;
  }

  load_snapshot(ctx);
}

void user_team::async_flush_all_member_shared_data(rpc::context& ctx) {
  auto self = weak_from_this();
  auto user_inst = owner_->get_owner().shared_from_this();

  auto result = rpc::async_invoke(
      ctx, "user_team.async_flush_all_member_shared_data",
      // ====================================================================================================
      [self, user_inst](rpc::context& child_ctx) -> rpc::result_code_type {
        auto team = self.lock();
        if (!team) {
          RPC_RETURN_CODE(0);
        }

        rpc::context::message_holder<atfw::team::DTeamAction> action{child_ctx};
        auto* member_update = action->mutable_member_update();
        user_inst->dump_user_key(*member_update->mutable_user_key());
        protobuf_copy_message(*member_update->mutable_user_channel(),
                              user_inst->get_user_chat_manager().get_private_chat_channel_key());
        member_update->set_client_version(user_inst->get_client_info().client_version());
        member_update->set_user_router_server_id(logic_config::me()->get_local_server_id());

        user_inst->get_user_team_manager().pack_team_member_shared_data(
            child_ctx, static_cast<atfw::shared::EnTeamType>(team->team_type_),
            *member_update->mutable_shared_member_data());

        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(team->send_action(child_ctx, std::move(*action))));
      });

  if (result.is_error()) {
    FCTXLOGERROR(ctx, "async_flush_all_member_shared_data failed, error code: {}({})", *result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*result.get_error()));
  }
}

bool user_team::load_dtmq_custom_data(rpc::context& ctx, const ::google::protobuf::Any& custom_data) {
  rpc::context::message_holder<atfw::team::DTeamStorage> team_snapshot{ctx};
  if (!custom_data.UnpackTo(&(*team_snapshot))) {
    FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, unpack snapshot failed, type_url: {}, error message: {}",
                 owner_->get_owner(), team_key_.zone_id(), team_key_.team_id(), custom_data.type_url(),
                 team_snapshot->InitializationErrorString());
    return false;
  }

  channel_saved_sequence_ = team_snapshot->saved_action_sequence();
  cached_captain_user_key_ = team_snapshot->captain_user_key();
  cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
  cached_configure_ = team_snapshot->configure();

  is_member_ = false;

  // 快照是权威状态，重建全部本地缓存
  cached_members_.clear();
  cached_team_shared_data_.clear();
  pending_invitation_by_expired_time_.clear();
  pending_invitation_by_invitee_.clear();
  pending_join_request_by_expired_time_.clear();
  pending_join_request_by_requester_.clear();

  do_team_shared_data(ctx, team_snapshot->shared_team_data());

  for (const auto& member : team_snapshot->member()) {
    upsert_member_cache(member);

    if (owner_->get_owner().is(member.user_key())) {
      is_member_ = true;
      cached_permission_role_ = member.role();

      // 自己的共享成员数据需要触发本地行为(其他成员的数据仅入缓存供快照导出)
      do_member_shared_data(ctx, member.user_key(), member.shared_member_data());
    }
  }

  // 待处理的邀请和加入请求(key 无效或已过期的条目由 upsert 内部忽略)
  for (const auto& invitation : team_snapshot->pending_invitation()) {
    upsert_pending_invitation(ctx, invitation);
  }
  for (const auto& join_request : team_snapshot->pending_join_request()) {
    upsert_pending_join_request(ctx, join_request);
  }

  insert_dirty_snapshot_handle();
  return true;
}

bool user_team::load_team_action(rpc::context& ctx, const ::atfw::team::DTeamAction& action) {
  append_pending_dirty_action(ctx, action);

  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam: {
      is_member_ = false;
      cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
      reset_cached_state();
      owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
      break;
    }
    case atfw::team::DTeamAction::kAddMember: {
      const auto& member_data = action.add_member();
      upsert_member_cache(member_data);

      // 与 teamsvr-room apply_add_member 一致: 队长缺位时首位成员成为队长且角色为 OWNER
      auto member_iter = cached_members_.find(member_data.user_key());
      if (member_iter != cached_members_.end() && cached_captain_user_key_.user_id() == 0) {
        protobuf_copy_message(cached_captain_user_key_, member_data.user_key());
        member_iter->second.member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      }

      if (owner_->get_owner().is(member_data.user_key())) {
        if (is_member_ == false) {
          is_member_ = true;

          // 进入队伍后要刷新一次当前成员的数据，以防发起邀请时使用的数据后续又发生变化（比如角色更新装备）
          async_flush_all_member_shared_data(ctx);
        }
        cached_permission_role_ =
            member_iter != cached_members_.end() ? member_iter->second.member_data.role() : member_data.role();
      }
      // 入队后不再保留其待处理的加入请求/邀请(与 teamsvr-room apply_add_member 的清理保持一致)
      remove_pending_join_request(member_data.user_key());
      remove_pending_invitation(member_data.user_key());
      break;
    }
    case atfw::team::DTeamAction::kRemoveMember: {
      const auto& member_data = action.remove_member();
      cached_members_.erase(member_data.user_key());
      if (owner_->get_owner().is(member_data.user_key())) {
        is_member_ = false;
        cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
      }
      break;
    }
    case atfw::team::DTeamAction::kMemberUpdate: {
      const auto& member_update = action.member_update();
      auto member_iter = cached_members_.find(member_update.user_key());
      if (member_iter != cached_members_.end()) {
        // 与 teamsvr-room apply_member_update 一致: 仅更新非空版本与共享数据，
        // user_router_server_id 由心跳维护且属于内部路由字段，本地缓存不跟踪
        if (!member_update.client_version().empty()) {
          member_iter->second.member_data.set_client_version(member_update.client_version());
        }
      }
      if (member_update.shared_member_data_size() > 0) {
        do_member_shared_data(ctx, member_update.user_key(), member_update.shared_member_data());
      }
      break;
    }
    case atfw::team::DTeamAction::kMemberSetRole: {
      const auto& set_role = action.member_set_role();
      auto member_iter = cached_members_.find(set_role.user_key());
      if (member_iter != cached_members_.end()) {
        member_iter->second.member_data.set_role(set_role.role());
      }
      if (owner_->get_owner().is(set_role.user_key())) {
        cached_permission_role_ = set_role.role();
      }
      break;
    }
    case atfw::team::DTeamAction::kElectionCaptain: {
      const auto& election_captain = action.election_captain();
      // 与 teamsvr-room change_captain 一致: 新队长必须是成员(否则忽略该事件)，
      // 新队长重置角色(缺省为 OWNER)，老队长降为 NORMAL
      auto old_captain_iter = cached_members_.find(cached_captain_user_key_);
      auto new_captain_iter = cached_members_.find(election_captain.user_key());
      if (new_captain_iter == cached_members_.end()) {
        break;
      }
      // 自己是老队长且不再是新队长时，同步降低本地缓存的角色
      if (owner_->get_owner().is(cached_captain_user_key_) && !owner_->get_owner().is(election_captain.user_key())) {
        cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL;
      }
      if (new_captain_iter != old_captain_iter) {
        new_captain_iter->second.member_data.set_role(election_captain.role() > atfw::team::EN_TEAM_MEMBER_ROLE_GUEST
                                                          ? election_captain.role()
                                                          : atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
        if (old_captain_iter != cached_members_.end()) {
          old_captain_iter->second.member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
        }
      }
      cached_captain_user_key_ = election_captain.user_key();
      if (owner_->get_owner().is(cached_captain_user_key_)) {
        cached_permission_role_ = election_captain.role() > atfw::team::EN_TEAM_MEMBER_ROLE_GUEST
                                      ? election_captain.role()
                                      : atfw::team::EN_TEAM_MEMBER_ROLE_OWNER;
      }
      break;
    }
    case atfw::team::DTeamAction::kTeamUpdate: {
      if (action.team_update().has_configure()) {
        cached_configure_ = action.team_update().configure();
      }

      if (action.team_update().shared_team_data_size() > 0) {
        do_team_shared_data(ctx, action.team_update().shared_team_data());
      }
      break;
    }
    case atfw::team::DTeamAction::kAddInvitation: {
      // 维护队伍的待处理邀请缓存(被邀请人已在队伍中则忽略，与 teamsvr-room apply_add_invitation 一致)
      const auto& invitation = action.add_invitation();
      if (cached_members_.find(invitation.invitee()) == cached_members_.end()) {
        upsert_pending_invitation(ctx, invitation);
      }
      break;
    }
    case atfw::team::DTeamAction::kApproveInvitation:
    case atfw::team::DTeamAction::kRejectInvitation: {
      const auto& invitation = action.action_case() == atfw::team::DTeamAction::kApproveInvitation
                                   ? action.approve_invitation()
                                   : action.reject_invitation();
      remove_pending_invitation(invitation.invitee());
      break;
    }
    case atfw::team::DTeamAction::kAddJoinRequest: {
      // 维护队伍的待处理加入请求缓存(申请人已在队伍中则忽略，与 teamsvr-room apply_add_join_request 一致)
      const auto& join_request = action.add_join_request();
      if (cached_members_.find(join_request.requester()) == cached_members_.end()) {
        upsert_pending_join_request(ctx, join_request);
      }
      break;
    }
    case atfw::team::DTeamAction::kApproveJoinRequest:
    case atfw::team::DTeamAction::kRejectJoinRequest: {
      const auto& join_request = action.action_case() == atfw::team::DTeamAction::kApproveJoinRequest
                                     ? action.approve_join_request()
                                     : action.reject_join_request();
      remove_pending_join_request(join_request.requester());
      break;
    }
    default:
      break;
  }

  return true;
}

void user_team::append_pending_dirty_action(rpc::context& ctx, const ::atfw::team::DTeamAction& action) {
  // 快照脏数据待下发时，重放期间的变更已由快照覆盖，无需再追加增量
  if (pending_dirty_snapshot_) {
    return;
  }
  insert_dirty_action_handle();

  pending_dirty_actions_.emplace_back();
  auto& one_action = pending_dirty_actions_.back();
  protobuf_copy_message(*one_action.mutable_action(), action);

  // 不下发内部路由与确认字段。condition 已由 teamsvr-room 在写入频道日志前裁剪，这里无需再处理
  const ::google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* member_shared_data = nullptr;
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kAddMember: {
      auto* mutable_action = one_action.mutable_action()->mutable_add_member();
      mutable_action->clear_user_channel();
      mutable_action->set_user_router_server_id(0);
      mutable_action->set_acknowledge_action_sequence(0);
      mutable_action->set_acknowledge_action_hash_code(0);
      member_shared_data = &mutable_action->shared_member_data();
      break;
    }
    case atfw::team::DTeamAction::kMemberUpdate: {
      auto* mutable_action = one_action.mutable_action()->mutable_member_update();
      mutable_action->clear_user_channel();
      mutable_action->set_user_router_server_id(0);
      member_shared_data = &mutable_action->shared_member_data();
      break;
    }
    case atfw::team::DTeamAction::kAddInvitation:
    case atfw::team::DTeamAction::kApproveInvitation:
    case atfw::team::DTeamAction::kRejectInvitation: {
      auto* mutable_action = one_action.mutable_action();
      if (action.action_case() == atfw::team::DTeamAction::kAddInvitation) {
        mutable_action->mutable_add_invitation()->clear_invitee_private_channel();
      } else if (action.action_case() == atfw::team::DTeamAction::kApproveInvitation) {
        mutable_action->mutable_approve_invitation()->clear_invitee_private_channel();
      } else {
        mutable_action->mutable_reject_invitation()->clear_invitee_private_channel();
      }
      break;
    }
    case atfw::team::DTeamAction::kAddJoinRequest:
    case atfw::team::DTeamAction::kApproveJoinRequest:
    case atfw::team::DTeamAction::kRejectJoinRequest: {
      auto* mutable_action = one_action.mutable_action();
      atfw::team::DTeamJoinRequest* join_request = nullptr;
      if (action.action_case() == atfw::team::DTeamAction::kAddJoinRequest) {
        join_request = mutable_action->mutable_add_join_request();
      } else if (action.action_case() == atfw::team::DTeamAction::kApproveJoinRequest) {
        join_request = mutable_action->mutable_approve_join_request();
      } else {
        join_request = mutable_action->mutable_reject_join_request();
      }
      join_request->clear_requester_private_channel();
      join_request->set_user_router_server_id(0);
      break;
    }
    default:
      break;
  }

  // 动作里携带的成员共享数据解包后随动作下发，客户端无需再关心 Any 的打包类型
  if (nullptr != member_shared_data) {
    for (const auto& item : *member_shared_data) {
      if (item.value().data().type_url().empty()) {
        continue;
      }
      if (!item.value().data().Is<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule>()) {
        continue;
      }
      auto* unpacked = one_action.add_shared_member_data();
      if (!item.value().data().UnpackTo(unpacked)) {
        FCTXLOGERROR(ctx,
                     "{} channel for team {}:{}, unpack dirty member shared data failed, type_url: {}, error "
                     "message: {}",
                     owner_->get_owner(), team_key_.zone_id(), team_key_.team_id(), item.value().data().type_url(),
                     unpacked->InitializationErrorString());
        one_action.mutable_shared_member_data()->RemoveLast();
      }
    }
  }
}

void user_team::upsert_member_cache(const ::atfw::team::DTeamMember& member_data) {
  if (member_data.user_key().zone_id() == 0 || member_data.user_key().user_id() == 0) {
    return;
  }

  auto iter = cached_members_.find(member_data.user_key());
  bool is_new_member = (iter == cached_members_.end());
  if (is_new_member) {
    iter = cached_members_.emplace(member_data.user_key(), member_cache_data{}).first;
  }

  auto& cache = iter->second;
  // 重复 add_member 只保留更早的入队时间，不能改变成员加入顺序(与 teamsvr-room apply_add_member 一致)
  auto previous_joined_timepoint = cache.member_data.joined_timepoint();
  protobuf_copy_message(cache.member_data, member_data);
  if (!is_new_member && previous_joined_timepoint.seconds() > 0 &&
      (previous_joined_timepoint.seconds() < member_data.joined_timepoint().seconds() ||
       (previous_joined_timepoint.seconds() == member_data.joined_timepoint().seconds() &&
        previous_joined_timepoint.nanos() < member_data.joined_timepoint().nanos()))) {
    protobuf_copy_message(*cache.member_data.mutable_joined_timepoint(), previous_joined_timepoint);
  }

  // 成员共享数据移入 key-value 索引，proto 字段在内存中保持为空
  cache.member_data.clear_shared_member_data();
  cache.shared_member_data.clear();
  for (const auto& kv : member_data.shared_member_data()) {
    protobuf_copy_message(cache.shared_member_data[kv.key()], kv.value());
  }
}

void user_team::upsert_pending_invitation(rpc::context& ctx, const ::atfw::team::DTeamInvitation& invitation) {
  if (invitation.invitee().zone_id() == 0 || invitation.invitee().user_id() == 0) {
    return;
  }
  if (protobuf_to_system_clock(invitation.expired_timepoint()) <= ctx.logical_now()) {
    return;
  }
  upsert_admission_by_expired_time(pending_invitation_by_expired_time_, pending_invitation_by_invitee_,
                                   invitation.invitee(), invitation);
}

void user_team::remove_pending_invitation(const PROJECT_NAMESPACE_ID::DUserIDKey& invitee) {
  auto iter = pending_invitation_by_invitee_.find(invitee);
  if (iter == pending_invitation_by_invitee_.end()) {
    return;
  }
  pending_invitation_by_expired_time_.erase(iter->second);
  pending_invitation_by_invitee_.erase(iter);
}

void user_team::upsert_pending_join_request(rpc::context& ctx, const ::atfw::team::DTeamJoinRequest& join_request) {
  if (join_request.requester().zone_id() == 0 || join_request.requester().user_id() == 0) {
    return;
  }
  if (protobuf_to_system_clock(join_request.expired_timepoint()) <= ctx.logical_now()) {
    return;
  }
  upsert_admission_by_expired_time(pending_join_request_by_expired_time_, pending_join_request_by_requester_,
                                   join_request.requester(), join_request);
}

void user_team::remove_pending_join_request(const PROJECT_NAMESPACE_ID::DUserIDKey& requester) {
  auto iter = pending_join_request_by_requester_.find(requester);
  if (iter == pending_join_request_by_requester_.end()) {
    return;
  }
  pending_join_request_by_expired_time_.erase(iter->second);
  pending_join_request_by_requester_.erase(iter);
}

void user_team::reset_cached_state() {
  cached_members_.clear();
  cached_team_shared_data_.clear();
  pending_invitation_by_expired_time_.clear();
  pending_invitation_by_invitee_.clear();
  pending_join_request_by_expired_time_.clear();
  pending_join_request_by_requester_.clear();
}

size_t user_team::cleanup_expired_admissions(rpc::context& ctx) {
  auto now = ctx.logical_now();
  size_t ret = 0;

  // teamsvr-room 清理过期准入数据时不下发事件(对端有自身的超时失效机制)，这里按 expired_timepoint 自行清理。
  // 列表按过期时间升序，遇到第一个未过期条目即停止，无需遍历全部数据
  while (!pending_invitation_by_expired_time_.empty()) {
    const auto& front = pending_invitation_by_expired_time_.front();
    if (protobuf_to_system_clock(front.expired_timepoint()) > now) {
      break;
    }
    pending_invitation_by_invitee_.erase(front.invitee());
    pending_invitation_by_expired_time_.pop_front();
    ++ret;
  }
  while (!pending_join_request_by_expired_time_.empty()) {
    const auto& front = pending_join_request_by_expired_time_.front();
    if (protobuf_to_system_clock(front.expired_timepoint()) > now) {
      break;
    }
    pending_join_request_by_requester_.erase(front.requester());
    pending_join_request_by_expired_time_.pop_front();
    ++ret;
  }
  return ret;
}

void user_team::maybe_send_heartbeat(rpc::context& ctx) {
  // 只在确认是成员且频道可用时心跳，退出中/未入队/频道销毁时无需心跳
  if (!is_member_ || is_exiting() || channel_subscriber_->is_destroyed()) {
    return;
  }
  if (!channel_subscriber_->is_ready()) {
    return;
  }

  auto now = ctx.logical_now();
  if (last_heartbeat_timepoint_ > std::chrono::system_clock::from_time_t(0) &&
      now < last_heartbeat_timepoint_ + get_team_heartbeat_interval()) {
    return;
  }
  last_heartbeat_timepoint_ = now;

  auto self = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "user_team.maybe_send_heartbeat", [self](rpc::context& child_ctx) -> rpc::result_code_type {
        rpc::context::message_holder<atfw::team::SSTeamRoomHeartbeatReq> req_body{child_ctx};
        rpc::context::message_holder<atfw::team::SSTeamRoomHeartbeatRsp> rsp_body{child_ctx};
        protobuf_copy_message(*req_body->mutable_team_key(), self->team_key_);
        self->owner_->get_owner().dump_user_key(*req_body->mutable_user_key());
        req_body->set_user_router_server_id(logic_config::me()->get_local_server_id());
        // 上报本端已确认的日志序号(只增不减，teamsvr-room 侧按较大值更新成员 ack)
        req_body->set_sequence(self->last_applied_action_sequence_);
        req_body->set_hash_code(self->last_applied_action_hash_code_);

        RPC_AWAIT_IGNORE_RESULT(rpc::team::team_api::heartbeat(child_ctx, *req_body, *rsp_body, true));
        RPC_RETURN_CODE(0);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "team {}:{} maybe_send_heartbeat: async_invoke failed, error={}({})", team_key_.zone_id(),
                 team_key_.team_id(), *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

void user_team::load_snapshot(rpc::context& ctx) {
  FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, load a snapshot, last sequence:{}", owner_->get_owner(),
               team_key_.zone_id(), team_key_.team_id(), channel_subscriber_->get_last_message_sequence());

  channel_create_sequence_ = channel_subscriber_->get_create_sequence();

  // 加载快照
  if (!load_dtmq_custom_data(ctx, channel_subscriber_->get_custom_data_content())) {
    return;
  }

  // 回放压缩点之后的增量日志
  rpc::dtmq::client_subscriber::query_options options;
  options.start_sequence = channel_subscriber_->get_last_removed_sequence() + 1;
  channel_subscriber_->query_cached_message(
      ctx,
      [this, &ctx](const ::atfw::dtmq::DChannelMessage& message) {
        on_receive_raw_message(ctx, message);
        return true;
      },
      options);

  // 如果已经可以被删除，则直接通知 manager 移除
  if (can_be_removed(ctx)) {
    FCTXLOGINFO(ctx, "{} can_be_removed for team {}:{} after loadsnapshot", owner_->get_owner(), team_key_.zone_id(),
                team_key_.team_id());
    owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
  } else if (wait_to_be_member_but_timeout(ctx)) {
    FCTXLOGINFO(ctx, "{} wait_to_be_member_but_timeout for team {}:{} after loadsnapshot", owner_->get_owner(),
                team_key_.zone_id(), team_key_.team_id());
    owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_EXPIRED);
  }
}

void user_team::on_receive_raw_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& data) {
  if (data.sequence() <= channel_saved_sequence_) {
    // 已经处理过，直接忽略
    return;
  }

  FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, receive a raw message, sequence:{}", owner_->get_owner(),
               team_key_.zone_id(), team_key_.team_id(), data.sequence());

  // 记录本端已确认的最新日志序号(随心跳上报给 teamsvr-room 更新成员 ack)
  if (data.sequence() > last_applied_action_sequence_) {
    last_applied_action_sequence_ = data.sequence();
    last_applied_action_hash_code_ = data.hash_code();
  }

  switch (data.detail().command_case()) {
    case atfw::dtmq::DChannelMessageDetail::kCreate: {
      if (data.sequence() > channel_create_sequence_) {
        channel_create_sequence_ = data.sequence();
      }
      break;
    }
    case atfw::dtmq::DChannelMessageDetail::kDestroy: {
      is_member_ = false;
      cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
      reset_cached_state();
      owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
      break;
    }
    case atfw::dtmq::DChannelMessageDetail::kEvent: {
      rpc::context::message_holder<atfw::team::DTeamAction> team_action{ctx};
      if (!data.detail().event().UnpackTo(&(*team_action))) {
        FCTXLOGDEBUG(ctx, "{} channel for team {}:{}, unpack event failed, type_url: {}, error message: {}",
                     owner_->get_owner(), team_key_.zone_id(), team_key_.team_id(), data.detail().event().type_url(),
                     team_action->InitializationErrorString());
        break;
      }

      load_team_action(ctx, *team_action);
      break;
    }
    case atfw::dtmq::DChannelMessageDetail::kUpdateCustomData: {
      load_dtmq_custom_data(ctx, channel_subscriber_->get_custom_data_content());
      break;
    }
    default:
      break;
  }
}

void user_team::set_matching(rpc::context& /*ctx*/, bool value) {
  if (is_matching_ == value) {
    return;
  }

  is_matching_ = value;

  // TODO(owent): 发起匹配流程
}

void user_team::do_team_shared_data(rpc::context& ctx,
                                    const ::google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& data) {
  if (data.empty()) {
    return;
  }

  const auto& handle_map = user_team_utility::get_team_shared_data_update_handlers_map();
  for (const auto& item : data) {
    if (item.value().data().type_url().empty()) {
      continue;
    }
    if (!item.value().data().Is<PROJECT_NAMESPACE_ID::DTeamSharedDataModule>()) {
      continue;
    }

    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamSharedDataModule> unpacked{ctx};
    if (!item.value().data().UnpackTo(&(*unpacked))) {
      FCTXLOGERROR(ctx, "{} channel for team {}:{}, unpack team shared data failed, type_url: {}, error message: {}",
                   owner_->get_owner(), team_key_.zone_id(), team_key_.team_id(), item.value().data().type_url(),
                   unpacked->InitializationErrorString());
      continue;
    }

    // 所有成功解包的模块数据都进缓存(dump 快照时转出成 output.shared_team_data)，
    // 不限制于注册了处理器的模块
    int64_t key = user_team_algorithm::make_team_shared_data_key(*unpacked);
    protobuf_copy_message(cached_team_shared_data_[key], *unpacked);

    auto iter = handle_map.find(key);
    if (iter == handle_map.end()) {
      continue;
    }

    if (iter->second.do_update == nullptr) {
      continue;
    }
    iter->second.do_update(ctx, *this, *unpacked);
  }
}

void user_team::do_member_shared_data(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
    const ::google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& data) {
  if (data.empty()) {
    return;
  }

  // 所有成员的共享数据都合并进缓存(dump 快照时回填)，合并语义为同 key 覆盖(与 teamsvr-room apply_member_update 一致)
  auto member_iter = cached_members_.find(user_key);
  if (member_iter != cached_members_.end()) {
    for (const auto& kv : data) {
      protobuf_copy_message(member_iter->second.shared_member_data[kv.key()], kv.value());
    }
  }

  // 本地行为(处理器)暂时只需要处理自己的数据
  if (!owner_->get_owner().is(user_key)) {
    return;
  }

  const auto& handle_map = user_team_utility::get_member_shared_data_update_handlers_map();
  for (const auto& item : data) {
    if (item.value().data().type_url().empty()) {
      continue;
    }
    if (!item.value().data().Is<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule>()) {
      continue;
    }

    rpc::context::message_holder<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> unpacked{ctx};
    if (!item.value().data().UnpackTo(&(*unpacked))) {
      FCTXLOGERROR(ctx,
                   "{} channel for team {}:{}, unpack team member shared data failed, type_url: {}, error message: {}",
                   owner_->get_owner(), team_key_.zone_id(), team_key_.team_id(), item.value().data().type_url(),
                   unpacked->InitializationErrorString());
      continue;
    }

    int64_t key = user_team_algorithm::make_team_member_shared_data_key(*unpacked);
    auto iter = handle_map.find(key);
    if (iter == handle_map.end()) {
      continue;
    }

    if (iter->second.do_update == nullptr) {
      continue;
    }
    iter->second.do_update(ctx, *this, user_key, *unpacked);
  }
}

void user_team::insert_dirty_snapshot_handle() {
  if (pending_dirty_snapshot_) {
    return;
  }
  pending_dirty_snapshot_ = true;
  pending_dirty_actions_.clear();

  auto self_weak = weak_from_this();
  owner_->get_owner().insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(&pending_dirty_snapshot_), "user.user_team.insert_dirty_snapshot_handle",
      [self_weak](rpc::context& ctx, user&, user::dirty_message_container& output) {
        auto self = self_weak.lock();
        if (!self) {
          return;
        }
        self->pending_dirty_snapshot_ = false;

        if (!output.user_dirty) {
          output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
        }

        self->dump(ctx, *output.user_dirty->add_dirty_team()->mutable_snapshot());
      },
      [self_weak](rpc::context&, user&) {
        auto self = self_weak.lock();
        if (!self) {
          return;
        }
        self->pending_dirty_snapshot_ = false;
      });
}

void user_team::insert_dirty_action_handle() {
  if (pending_dirty_snapshot_) {
    return;
  }

  auto self_weak = weak_from_this();
  owner_->get_owner().insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(&pending_dirty_actions_), "user.user_team.insert_dirty_action_handle",
      [self_weak](rpc::context&, user&, user::dirty_message_container& output) {
        auto self = self_weak.lock();
        if (!self) {
          return;
        }

        if (self->pending_dirty_actions_.empty()) {
          return;
        }

        if (!output.user_dirty) {
          output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
        }
        auto* dump_team_actions = output.user_dirty->add_dirty_team()->mutable_increase();
        protobuf_copy_message(*dump_team_actions->mutable_team_key(), self->get_team_key());

        std::list<PROJECT_NAMESPACE_ID::DUserTeamDirty::OneAction> actions;
        actions.swap(self->pending_dirty_actions_);
        for (auto& action : actions) {
          auto* one_action = dump_team_actions->add_actions();
          protobuf_move_message(*one_action, std::move(action));
        }
      },
      [self_weak](rpc::context&, user&) {
        auto self = self_weak.lock();
        if (!self) {
          return;
        }
        self->pending_dirty_actions_.clear();
      });
}

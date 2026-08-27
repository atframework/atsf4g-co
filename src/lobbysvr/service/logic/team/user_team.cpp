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
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
      PROJECT_NAMESPACE_ID::DTeamSharedDataModule event;
      event.mutable_battle()->set_matching(true);
      int64_t key = user_team_algorithm::make_team_shared_data_key(event);

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

user_team::~user_team() {}

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

    auto iter = handle_map.find(key);
    if (iter == handle_map.end()) {
      continue;
    }
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

    auto iter = handle_map.find(key);
    if (iter == handle_map.end()) {
      continue;
    }
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

  do_team_shared_data(ctx, team_snapshot->shared_team_data());

  for (const auto& member : team_snapshot->member()) {
    if (member.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
        member.user_key().user_id() == owner_->get_owner().get_user_id()) {
      is_member_ = true;
      cached_permission_role_ = member.role();

      do_member_shared_data(ctx, member.user_key(), member.shared_member_data());
      break;
    }

    do_member_shared_data(ctx, member.user_key(), member.shared_member_data());
  }

  return true;
}

bool user_team::load_team_action(rpc::context& ctx, const ::atfw::team::DTeamAction& action) {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam: {
      is_member_ = false;
      cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
      owner_->remove_team(ctx, team_key_, atfw::team::EN_TEAM_EXIT_REASON_DESTROY_TEAM);
      break;
    }
    case atfw::team::DTeamAction::kAddMember: {
      const auto& member_data = action.add_member();
      if (member_data.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
          member_data.user_key().user_id() == owner_->get_owner().get_user_id()) {
        if (is_member_ == false) {
          is_member_ = true;

          // 进入队伍后要刷新一次当前成员的数据，以防发起邀请时使用的数据后续又发生变化（比如角色更新装备）
          async_flush_all_member_shared_data(ctx);
        }
        cached_permission_role_ = member_data.role();
      }
      break;
    }
    case atfw::team::DTeamAction::kRemoveMember: {
      const auto& member_data = action.remove_member();
      if (member_data.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
          member_data.user_key().user_id() == owner_->get_owner().get_user_id()) {
        is_member_ = false;
        cached_permission_role_ = atfw::team::EN_TEAM_MEMBER_ROLE_GUEST;
      }
      break;
    }
    case atfw::team::DTeamAction::kMemberUpdate: {
      const auto& member_update = action.member_update();
      if (member_update.shared_member_data_size() > 0) {
        do_member_shared_data(ctx, member_update.user_key(), member_update.shared_member_data());
      }
      break;
    }
    case atfw::team::DTeamAction::kMemberSetRole: {
      const auto& set_role = action.member_set_role();
      if (set_role.user_key().zone_id() == owner_->get_owner().get_zone_id() &&
          set_role.user_key().user_id() == owner_->get_owner().get_user_id()) {
        cached_permission_role_ = set_role.role();
      }
      break;
    }
    case atfw::team::DTeamAction::kElectionCaptain: {
      cached_captain_user_key_ = action.election_captain().user_key();
      if (owner_->get_owner().is(cached_captain_user_key_) &&
          action.election_captain().role() > atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
        cached_permission_role_ = action.election_captain().role();
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
    case atfw::team::DTeamAction::kAddInvitation:
    case atfw::team::DTeamAction::kApproveInvitation:
    case atfw::team::DTeamAction::kRejectInvitation:
    case atfw::team::DTeamAction::kAddJoinRequest:
    case atfw::team::DTeamAction::kApproveJoinRequest:
    case atfw::team::DTeamAction::kRejectJoinRequest: {
      // 队伍内的邀请和加入请求本地暂不用记录
      break;
    }
    default:
      break;
  }
  return true;
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

  switch (data.detail().command_case()) {
    case atfw::dtmq::DChannelMessageDetail::kCreate: {
      if (data.sequence() > channel_create_sequence_) {
        channel_create_sequence_ = data.sequence();
      }
      break;
    }
    case atfw::dtmq::DChannelMessageDetail::kDestroy: {
      is_member_ = false;
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

    int64_t key = user_team_algorithm::make_team_shared_data_key(*unpacked);
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

  // 暂时不需要处理别人的数据
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

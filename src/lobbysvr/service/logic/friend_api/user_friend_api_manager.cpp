// Copyright 2026 atframework
// Created by owent on 2026-09-28

#include "logic/friend_api/user_friend_api_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel_config_const_index.h>
#include <config/logic_config.h>

#include <logic/misc/logic_datetime_cache.h>

#include <utility/protobuf_mini_dumper.h>

#include <router/router_friend_manager.h>

#include <rpc/db/uuid.h>
#include <rpc/friend_api/friend_algorithm.h>
#include <rpc/friend_api/friendmanagementservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>

#include <unordered_set>
#include <utility>
#include <vector>

#include "data/user.h"

#include "logic/friend_api/friend_api_transaction_client_handle.h"
#include "logic/friend_api/friend_api_wal_client.h"

user_friend_api_manager::user_friend_api_manager(user &owner)
    : owner_(&owner),
      is_dirty_(false),
      is_remote_data_ready_(true),
      need_send_wal_heartbeat_(false),
      need_pull_sns_friend_(false),
      next_pull_data_timepoint_(std::chrono::system_clock::from_time_t(0)),
      wal_client_{user_friend_api_create_wal_client(*this)},
      transaction_client_{user_friend_api_create_transaction_client(*this)} {}

user_friend_api_manager::~user_friend_api_manager() {}

void user_friend_api_manager::create_init(rpc::context &, uint32_t) {}

void user_friend_api_manager::login_init(rpc::context &) {}

void user_friend_api_manager::refresh_feature_limit_second(rpc::context &ctx) {
  if (wal_client_) {
    int32_t result_code = 0;
    wal_client_->tick(ctx.logical_now(), friend_api_wal_client_context{ctx, result_code});
  }
}

void user_friend_api_manager::refresh_feature_limit_minute(rpc::context &ctx) {
  auto now = ctx.logical_now();

  // remote
  refresh_feature_limit(ctx, now, remote_friend_statistics_);

  // local
  std::pair<bool, bool> update_type = refresh_feature_limit(ctx, now, local_friend_statistics_);
  if (update_type.first) {
    daily_send_list_.clear();
    daily_receive_list_.clear();
  }

  // TODO(owentou): 社交分享类接口
  // if (update_type.first || update_type.second) {
  //   update_sns_share(update_type.first, update_type.second);
  // }
}

std::pair<bool, bool> user_friend_api_manager::refresh_feature_limit(rpc::context &,
                                                                     std::chrono::system_clock::time_point now,
                                                                     atfw::friend_api::DFriendStatistics &stats) {
  auto offset = logic_datetime_cache_get_default_daily_refresh_offset();
  std::pair<bool, bool> ret(false, false);
  auto next_daily_reset_time = protobuf_to_system_clock(stats.next_daily_reset_time());
  if (now >= next_daily_reset_time || now + std::chrono::hours(24) < next_daily_reset_time) {
    protobuf_from_system_clock(*stats.mutable_next_daily_reset_time(),
                               logic_datetime_cache_get_next_day_start_timepoint(offset));
    stats.set_daily_inviter(0);
    stats.set_daily_invitee(0);
    stats.set_daily_send_gift_times(0);
    stats.set_daily_receive_gift_times(0);

    ret.first = true;
  }

  auto next_weekly_reset_time = protobuf_to_system_clock(stats.next_weekly_reset_time());
  if (now >= next_weekly_reset_time || now + std::chrono::hours(24 * 7) < next_weekly_reset_time) {
    protobuf_from_system_clock(*stats.mutable_next_weekly_reset_time(),
                               logic_datetime_cache_get_next_week_start_timepoint(offset));
    stats.set_weekly_inviter(0);
    stats.set_weekly_invitee(0);
    stats.set_weekly_send_gift_times(0);
    stats.set_weekly_receive_gift_times(0);

    ret.second = true;
  }

  return ret;
}

void user_friend_api_manager::dump_stats(atfw::friend_api::DFriendStatistics &out) const {
  protobuf_copy_message(out, remote_friend_statistics_);

  out.set_daily_send_gift_times(local_friend_statistics_.daily_send_gift_times());
  out.set_weekly_send_gift_times(local_friend_statistics_.weekly_send_gift_times());
  out.set_sum_send_gift_times(local_friend_statistics_.sum_send_gift_times());

  out.set_daily_receive_gift_times(local_friend_statistics_.daily_receive_gift_times());
  out.set_weekly_receive_gift_times(local_friend_statistics_.weekly_receive_gift_times());
  out.set_sum_receive_gift_times(local_friend_statistics_.sum_receive_gift_times());
}

void user_friend_api_manager::init_from_table_data(rpc::context &ctx,
                                                   const PROJECT_NAMESPACE_ID::table_user &user_table) {
  if (user_table.has_friend_data()) {
    const PROJECT_NAMESPACE_ID::user_friend_data &friend_data = user_table.friend_data();
    if (wal_client_) {
      int32_t result_code = 0;
      wal_client_->load(friend_data, friend_api_wal_client_context{ctx, result_code});
    } else {
      load_storage(ctx, friend_data);
    }
  }
}

void user_friend_api_manager::load_storage(rpc::context &, const PROJECT_NAMESPACE_ID::user_friend_data &friend_data) {
  // nothing now
  protobuf_copy_message(local_friend_statistics_, friend_data.local_statistics());
  daily_send_list_.clear();
  daily_send_list_.reserve(static_cast<size_t>(friend_data.daily_send_list_size()));
  for (int i = 0; i < friend_data.daily_send_list_size(); ++i) {
    const atfw::friend_api::DFriendGiftHistory &gift_target = friend_data.daily_send_list(i);
    protobuf_copy_message(daily_send_list_[gift_target.user_key()], friend_data.daily_send_list(i));
  }

  daily_receive_list_.clear();
  daily_receive_list_.reserve(static_cast<size_t>(friend_data.daily_receive_list_size()));
  for (int i = 0; i < friend_data.daily_receive_list_size(); ++i) {
    const atfw::friend_api::DFriendGiftHistory &gift_target = friend_data.daily_receive_list(i);
    protobuf_copy_message(daily_receive_list_[gift_target.user_key()], friend_data.daily_receive_list(i));
  }

  confirm_remove_gifts_.clear();
  confirm_remove_gifts_.reserve(static_cast<size_t>(friend_data.confirm_remove_gift_ids_size()));
  for (int i = 0; i < friend_data.confirm_remove_gift_ids_size(); ++i) {
    confirm_remove_gifts_.insert(friend_data.confirm_remove_gift_ids(i));
  }

  // TODO(owentou): sns data
  // protobuf_copy_message(sns_data_, friend_data.sns_data());
}

int user_friend_api_manager::dump(rpc::context &ctx, PROJECT_NAMESPACE_ID::table_user &user) {
  PROJECT_NAMESPACE_ID::user_friend_data *friend_data = user.mutable_friend_data();
  if (NULL == friend_data) {
    FCTXLOGERROR(ctx, "{} malloc player_friend failed", *owner_);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  if (wal_client_) {
    int32_t result_code = 0;
    wal_client_->dump(*friend_data, friend_api_wal_client_context{ctx, result_code});
  } else {
    dump_storage(ctx, *friend_data);
  }

  return 0;
}

void user_friend_api_manager::dump_storage(rpc::context &ctx, PROJECT_NAMESPACE_ID::user_friend_data &friend_data) {
  cleanup_friend_data(ctx);

  protobuf_copy_message(*friend_data.mutable_local_statistics(), local_friend_statistics_);
  friend_data.mutable_daily_send_list()->Reserve(static_cast<int>(daily_send_list_.size()));
  for (auto &gift : daily_send_list_) {
    protobuf_copy_message(*friend_data.add_daily_send_list(), gift.second);
  }

  protobuf_copy_message(*friend_data.mutable_local_statistics(), local_friend_statistics_);
  friend_data.mutable_daily_receive_list()->Reserve(static_cast<int>(daily_receive_list_.size()));
  for (auto &gift : daily_receive_list_) {
    protobuf_copy_message(*friend_data.add_daily_receive_list(), gift.second);
  }

  friend_data.mutable_confirm_remove_gift_ids()->Reserve(static_cast<int>(confirm_remove_gifts_.size()));
  for (const auto &gift_id : confirm_remove_gifts_) {
    friend_data.add_confirm_remove_gift_ids(gift_id);
  }

  // TODO(owentou): sns data
  // protobuf_copy_message(*friend_data.mutable_sns_data(), sns_data_);
}

bool user_friend_api_manager::is_dirty() const { return is_dirty_; }

void user_friend_api_manager::clear_dirty() { is_dirty_ = false; }

bool user_friend_api_manager::is_async_task_running() const {
  if (task_type_trait::empty(friend_async_task_)) {
    return false;
  }

  if (task_type_trait::is_exiting(friend_async_task_)) {
    task_type_trait::reset_task(friend_async_task_);
    return false;
  }

  return true;
}

rpc::result_code_type user_friend_api_manager::wait_for_async_task(rpc::context &ctx) {
  if (!is_async_task_running()) {
    RPC_RETURN_CODE(0);
  }

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, friend_async_task_));
  if (task_type_trait::is_exiting(friend_async_task_)) {
    task_type_trait::reset_task(friend_async_task_);
  }
  RPC_RETURN_CODE(ret);
}

int32_t user_friend_api_manager::invoke_async_task(rpc::context &ctx) {
  if (!need_send_wal_heartbeat_ && !need_pull_sns_friend_) {
    return 0;
  }

  // 正在运行，不用重复创建
  if (!task_type_trait::empty(friend_async_task_) && !task_type_trait::is_exiting(friend_async_task_)) {
    return 0;
  }

  int ret = 0;
  auto user_ptr = owner_->shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "user_friend_api_manager.invoke_async_task", [user_ptr](rpc::context &child_ctx) -> rpc::result_code_type {
        user_friend_api_manager &self = user_ptr->get_user_friend_api_manager();

        int32_t child_ret = 0;
        while (self.need_pull_sns_friend_ || self.need_send_wal_heartbeat_) {
          // 拉取社交好友数据,忽略平台错误
          if (self.need_pull_sns_friend_) {
            self.need_pull_sns_friend_ = false;
            child_ret = RPC_AWAIT_CODE_RESULT(self.pull_sns_friend_data(child_ctx));
            auto now = child_ctx.logical_now();
            const auto &lobby_cfg =
                logic_config::me()->get_server_instance_config<atfw::shared::config::lobbysvr_cfg>();
            if (child_ret < 0) {
              self.next_pull_data_timepoint_ =
                  now + protobuf_to_system_clock(lobby_cfg.friend_api().friend_sns_cache_retry());
              if (self.next_pull_data_timepoint_ <= now) {
                self.next_pull_data_timepoint_ = now;
                self.next_pull_data_timepoint_ += std::chrono::minutes(5);
              }
              self.need_pull_sns_friend_ = true;
            } else {
              self.next_pull_data_timepoint_ =
                  now + protobuf_to_system_clock(lobby_cfg.friend_api().friend_sns_cache_timeout());
              if (self.next_pull_data_timepoint_ <= now) {
                self.next_pull_data_timepoint_ = now;
                self.next_pull_data_timepoint_ += std::chrono::hours(1);
              }
            }
          }

          // 发送 WAL 心跳
          if (self.need_send_wal_heartbeat_) {
            self.need_send_wal_heartbeat_ = false;

            rpc::context::message_holder<atfw::friend_api::SSFriendSubscribeReq> req_body(child_ctx);
            rpc::context::message_holder<atfw::friend_api::SSFriendSubscribeRsp> rsp_body(child_ctx);

            auto *subscriber_user_key = req_body->mutable_subscriber()->mutable_subscriber_user_key();
            subscriber_user_key->set_zone_id(user_ptr->get_zone_id());
            subscriber_user_key->set_user_id(user_ptr->get_user_id());
            auto *last_received = req_body->mutable_subscriber()->mutable_last_received();

            const auto &all_logs = self.wal_client_->get_log_manager().get_all_logs();
            if (!all_logs.empty()) {
              const auto &last_log = *all_logs.rbegin();
              last_received->set_sequence(last_log->event_id());
              last_received->set_hash_code(rpc::friend_api::get_hash_code(*last_log));
            }

            // 忽略心跳错误，下次重试会补
            RPC_AWAIT_IGNORE_RESULT(rpc::friend_api::management_subscribe(
                child_ctx, atfw::friend_api::router_friend_manager::me()->get_type_id(), user_ptr->get_zone_id(),
                user_ptr->get_user_id(), *req_body, *rsp_body));
          }

          TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
          // 如果超时或者killed,则要强制退出任务
          if (task_type_trait::is_exiting(current_task_status)) {
            break;
          }
        }

        // Reset task
        if (task_type_trait::empty(self.friend_async_task_)) {
          RPC_RETURN_CODE(child_ret);
        }

        if (task_type_trait::is_exiting(self.friend_async_task_)) {
          task_type_trait::reset_task(self.friend_async_task_);
        } else if (task_type_trait::get_task_id(self.friend_async_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self.friend_async_task_);
        }

        RPC_RETURN_CODE(child_ret);
      });

  if (invoke_result.is_error()) {
    ret = *invoke_result.get_error();
  } else {
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      friend_async_task_ = *invoke_result.get_success();
    }
  }

  return ret;
}

rpc::result_code_type user_friend_api_manager::pull_friend_data(rpc::context &ctx) {
  auto now = ctx.logical_now();

  if (next_pull_data_timepoint_ <= now) {
    need_pull_sns_friend_ = true;
    invoke_async_task(ctx);
  }

  // pull friend again
  auto ret = RPC_AWAIT_CODE_RESULT(wait_for_async_task(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(ret);
}

int32_t user_friend_api_manager::load_logs(
    rpc::context &ctx, const google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendEvent> &logs) {
  int32_t result = 0;

  if (!wal_client_) {
    return result;
  }

  friend_api_wal_client_context param{ctx, result};
  wal_client_->receive_logs(param, logs.begin(), logs.end());

  return result;
}

void user_friend_api_manager::load_snapshot(rpc::context &ctx,
                                            const atfw::friend_api::table_friend_blob_data &snapshot_data) {
  // 刷新事件数据
  protobuf_copy_message(remote_friend_statistics_, snapshot_data.statistics());

  // 好友数据
  {
    std::unordered_set<atfw::shared::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_keys;
    for (const auto &friend_data : friend_cache_set_) {
      expired_keys.insert(friend_data.first);
    }
    for (const auto &friend_data : snapshot_data.friend_list()) {
      if (friend_data.removed_time().seconds() > 0) {
        continue;
      }
      add_friend_cache(ctx, friend_data, false == is_remote_data_ready_);
      expired_keys.erase(friend_data.user_key());
    }
    for (const auto &friend_key : expired_keys) {
      remove_friend_cache(ctx, friend_key, false == is_remote_data_ready_);
    }
  }

  // 被邀请数据
  {
    std::unordered_set<atfw::shared::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_keys;
    for (const auto &invite_data : inviter_cache_set_) {
      expired_keys.insert(invite_data.first);
    }
    for (const auto &invite_data : snapshot_data.inviter_list()) {
      if (invite_data.removed_time().seconds() > 0) {
        continue;
      }
      add_inviter_cache(ctx, invite_data, false == is_remote_data_ready_);
      expired_keys.erase(get_key_from_inviter_cache(invite_data));
    }
    for (const auto &friend_key : expired_keys) {
      remove_inviter_cache(ctx, friend_key, false == is_remote_data_ready_);
    }
  }

  // 邀请数据
  {
    std::unordered_set<atfw::shared::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_keys;
    for (const auto &invite_data : invitee_cache_set_) {
      expired_keys.insert(invite_data.first);
    }
    for (const auto &invite_data : snapshot_data.invitee_list()) {
      if (invite_data.removed_time().seconds() > 0) {
        continue;
      }
      add_invitee_cache(ctx, invite_data, false == is_remote_data_ready_);
      expired_keys.erase(get_key_from_invitee_cache(invite_data));
    }
    for (const auto &friend_key : expired_keys) {
      remove_invitee_cache(ctx, friend_key, false == is_remote_data_ready_);
    }
  }

  // 礼物数据
  {
    std::unordered_set<int64_t> expired_keys;
    for (const auto &gift_data : gift_cache_set_) {
      expired_keys.insert(gift_data.first);
    }
    for (const auto &gift_data : snapshot_data.gift_list()) {
      if (gift_data.removed_time().seconds() > 0) {
        continue;
      }
      add_gift_cache(ctx, gift_data, false == is_remote_data_ready_);
      expired_keys.erase(gift_data.gift_id());
    }
    for (const auto &gift_id : expired_keys) {
      remove_gift_cache(ctx, gift_id, false == is_remote_data_ready_);
    }
  }

  is_remote_data_ready_ = true;
}

void user_friend_api_manager::cleanup_friend_data(rpc::context &ctx) {
  // 清理过期数据
  auto now = ctx.logical_now();

  // 好友数据
  {
    std::unordered_set<atfw::shared::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_keys;
    for (const auto &friend_data : friend_cache_set_) {
      if (protobuf_to_system_clock(friend_data.second.expired_time()) < now) {
        expired_keys.insert(friend_data.first);
      }
    }
    for (const auto &friend_key : expired_keys) {
      remove_friend_cache(ctx, friend_key, false == is_remote_data_ready_);
    }
  }

  // 被邀请数据
  {
    std::unordered_set<atfw::shared::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_keys;
    for (const auto &invite_data : inviter_cache_set_) {
      if (protobuf_to_system_clock(invite_data.second.expired_time()) < now) {
        expired_keys.insert(invite_data.first);
      }
    }
    for (const auto &friend_key : expired_keys) {
      remove_inviter_cache(ctx, friend_key, false == is_remote_data_ready_);
    }
  }

  // 邀请数据
  {
    std::unordered_set<atfw::shared::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_keys;
    for (const auto &invite_data : invitee_cache_set_) {
      if (protobuf_to_system_clock(invite_data.second.expired_time()) < now) {
        expired_keys.insert(invite_data.first);
      }
    }
    for (const auto &friend_key : expired_keys) {
      remove_invitee_cache(ctx, friend_key, false == is_remote_data_ready_);
    }
  }

  // 礼物数据
  {
    std::unordered_set<int64_t> expired_keys;
    for (const auto &gift_data : gift_cache_set_) {
      if (protobuf_to_system_clock(gift_data.second.expired_time()) < now) {
        expired_keys.insert(gift_data.first);
      }
    }

    for (const auto &gift_id : expired_keys) {
      remove_gift_cache(ctx, gift_id, false == is_remote_data_ready_);
    }
  }

  refresh_feature_limit_minute(ctx);
}

rpc::result_code_type user_friend_api_manager::gm_reset_limits(rpc::context &ctx) {
  local_friend_statistics_.set_daily_send_gift_times(0);
  local_friend_statistics_.set_daily_receive_gift_times(0);
  local_friend_statistics_.set_daily_invitee(0);
  local_friend_statistics_.set_daily_inviter(0);
  local_friend_statistics_.set_weekly_send_gift_times(0);
  local_friend_statistics_.set_weekly_receive_gift_times(0);
  local_friend_statistics_.set_weekly_invitee(0);
  local_friend_statistics_.set_weekly_inviter(0);

  daily_send_list_.clear();
  daily_receive_list_.clear();

// TODO(any): 社交分享类接口
#if 0
  // clear_sns_share(0);
#endif

  rpc::context::message_holder<atfw::friend_api::SSFriendGMResetLimitReq> req_body(ctx);
  rpc::context::message_holder<atfw::friend_api::SSFriendGMResetLimitRsp> rsp_body(ctx);

  auto ret = RPC_AWAIT_CODE_RESULT(
      rpc::friend_api::management_gm_reset_limit(ctx, atfw::friend_api::router_friend_manager::me()->get_type_id(),
                                                 owner_->get_zone_id(), owner_->get_user_id(), *req_body, *rsp_body));
  RPC_RETURN_CODE(ret);
}

const atfw::friend_api::DFriendInfo *user_friend_api_manager::get_friend(
    const atfw::shared::DUserIDKey &user_key) const {
  auto iter = friend_cache_set_.find(user_key);
  if (iter == friend_cache_set_.end()) {
    return nullptr;
  }

  // 1秒容忍值,好友允许无期限限制
  if (iter->second.expired_time().seconds() > 0 &&
      protobuf_to_system_clock(iter->second.expired_time()) + std::chrono::seconds(1) <
          atfw::util::time::time_utility::now()) {
    return nullptr;
  }

  return &iter->second;
}

const atfw::friend_api::DFriendInvitationInfo *user_friend_api_manager::get_inviter(
    const atfw::shared::DUserIDKey &user_key) const {
  auto iter = inviter_cache_set_.find(user_key);
  if (iter == inviter_cache_set_.end()) {
    return nullptr;
  }

  // 1秒容忍值
  if (protobuf_to_system_clock(iter->second.expired_time()) + std::chrono::seconds(1) <
      atfw::util::time::time_utility::now()) {
    return nullptr;
  }

  return &iter->second;
}

const atfw::friend_api::DFriendInvitationInfo *user_friend_api_manager::get_invitee(
    const atfw::shared::DUserIDKey &user_key) const {
  auto iter = invitee_cache_set_.find(user_key);
  if (iter == invitee_cache_set_.end()) {
    return nullptr;
  }

  // 1秒容忍值
  if (protobuf_to_system_clock(iter->second.expired_time()) + std::chrono::seconds(1) <
      atfw::util::time::time_utility::now()) {
    return nullptr;
  }

  return &iter->second;
}

const atfw::friend_api::DFriendGift *user_friend_api_manager::get_gift(int64_t gift_id) const {
  auto iter = gift_cache_set_.find(gift_id);
  if (iter == gift_cache_set_.end()) {
    return nullptr;
  }

  // 1秒容忍值
  if (protobuf_to_system_clock(iter->second.expired_time()) + std::chrono::seconds(1) <
      atfw::util::time::time_utility::now()) {
    return nullptr;
  }

  return &iter->second;
}

namespace {
static int32_t transaction_add_friend_event(rpc::context &ctx, user &user, bool is_add_event, uint64_t from_user_id,
                                            uint32_t from_zone_id, uint64_t to_user_id, uint32_t to_zone_id,
                                            PROJECT_NAMESPACE_ID::friend_transaction_data &from_event_datas,
                                            PROJECT_NAMESPACE_ID::friend_transaction_data &to_event_datas) {
  // TODO(any): 如果有需要可以设置好友超时
  // auto timeout = logic_datetime_cache_get_max_timepoint();
  atfw::friend_api::DFriendEvent *to_evt_data = to_event_datas.add_event_data();
  if (nullptr == to_evt_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }
  atfw::friend_api::DFriendInfo *to_friend_data = nullptr;
  if (is_add_event) {
    to_friend_data = to_evt_data->mutable_add_friend_data();
  } else {
    to_friend_data = to_evt_data->mutable_remove_friend_data();
  }

  if (nullptr == to_friend_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }
  to_friend_data->mutable_user_key()->set_user_id(from_user_id);
  to_friend_data->mutable_user_key()->set_zone_id(from_zone_id);
  // to_friend_data->set_expired_time(timeout);

  atfw::friend_api::DFriendEvent *from_evt_data = from_event_datas.add_event_data();
  if (nullptr == from_evt_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }
  atfw::friend_api::DFriendInfo *from_friend_data = nullptr;
  if (is_add_event) {
    from_friend_data = from_evt_data->mutable_add_friend_data();
  } else {
    from_friend_data = from_evt_data->mutable_remove_friend_data();
  }
  if (nullptr == from_friend_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }
  from_friend_data->mutable_user_key()->set_user_id(to_user_id);
  from_friend_data->mutable_user_key()->set_zone_id(to_zone_id);
  // from_friend_data->set_expired_time(timeout);

  return 0;
}

static int transaction_add_invite_event(rpc::context &ctx, user &user, bool is_add_event,
                                        std::chrono::system_clock::time_point timeout, uint64_t from_user_id,
                                        uint32_t from_zone_id, uint64_t to_user_id, uint32_t to_zone_id,
                                        PROJECT_NAMESPACE_ID::friend_transaction_data &from_event_datas,
                                        PROJECT_NAMESPACE_ID::friend_transaction_data &to_event_datas) {
  atfw::friend_api::DFriendEvent *to_evt_data = to_event_datas.add_event_data();
  if (nullptr == to_evt_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }
  atfw::friend_api::DFriendInvitationInfo *to_invite_data = nullptr;
  if (is_add_event) {
    to_invite_data = to_evt_data->mutable_add_inviter();
  } else {
    to_invite_data = to_evt_data->mutable_remove_inviter();
  }
  if (nullptr == to_invite_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }

  atfw::friend_api::DFriendEvent *from_evt_data = from_event_datas.add_event_data();
  if (nullptr == from_evt_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }
  atfw::friend_api::DFriendInvitationInfo *from_invite_data = nullptr;
  if (is_add_event) {
    from_invite_data = from_evt_data->mutable_add_invitee();
  } else {
    from_invite_data = from_evt_data->mutable_remove_invitee();
  }
  if (nullptr == from_invite_data) {
    FCTXLOGERROR(ctx, "{} create transaction event data failed", user);
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
  }

  to_invite_data->mutable_from_user()->set_user_id(from_user_id);
  to_invite_data->mutable_from_user()->set_zone_id(from_zone_id);
  to_invite_data->mutable_to_user()->set_user_id(to_user_id);
  to_invite_data->mutable_to_user()->set_zone_id(to_zone_id);
  if (timeout > std::chrono::system_clock::from_time_t(0)) {
    protobuf_from_system_clock(*to_invite_data->mutable_expired_time(), timeout);
  }
  protobuf_copy_message(*from_invite_data, *to_invite_data);

  return 0;
}
}  // namespace

rpc::result_code_type user_friend_api_manager::send_invite(rpc::context &ctx,
                                                           const atfw::shared::DUserIDKey &user_key) {
  if (0 == user_key.user_id() || 0 == user_key.zone_id()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  if (owner_->get_user_id() == user_key.user_id()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_CAN_NOT_INVITE_SELF);
  }

  if (owner_->get_zone_id() != user_key.zone_id() && !excel::get_const_config().friend_allow_cross_zone()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_CAN_NOT_INVITE_CORSS_ZONE);
  }

  // 检查是否已经是好友
  if (nullptr != get_friend(user_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_ALREADY_FRIEND);
  }

  // 检查是否已经被邀请过, 当前版本被邀请过也允许邀请
  // if (nullptr != get_inviter(user_key)) {
  //     RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_ALREADY_BE_INVITED);
  // }

  // 检查是否已经邀请过
  if (nullptr != get_invitee(user_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_ALREADY_SEND_INVITE);
  }

  // 好友数量上限预检查
  if (static_cast<int32_t>(friend_cache_set_.size()) >= excel::get_const_config().friend_max_number()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_MAX_NUMBER_LIMIT);
  }

  // 每日邀请上限预检查
  int32_t friend_daily_invite_limit = excel::get_const_config().friend_daily_invite_limit();
  if (friend_daily_invite_limit <= 0) {
    friend_daily_invite_limit = 200;
  }
  if (remote_friend_statistics_.daily_invitee() >= static_cast<uint32_t>(friend_daily_invite_limit)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITE_DAILY_LIMIT);
  }

  // 总邀请上限预检查
  int32_t friend_total_invitee_limit = excel::get_const_config().friend_total_invitee_limit();
  if (friend_total_invitee_limit <= 0) {
    friend_total_invitee_limit = 1600;
  }
  if (invitee_cache_set_.size() >= static_cast<size_t>(friend_total_invitee_limit)) {
    // 只计数未超时的
    int32_t valid_count = 0;
    auto now = ctx.logical_now();
    for (auto &invitee_cache : invitee_cache_set_) {
      if (protobuf_to_system_clock(invitee_cache.second.expired_time()) >= now) {
        valid_count++;
      }
    }
    if (valid_count >= friend_total_invitee_limit) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITEE_TOTAL_LIMIT);
    }
  }

  // 创建事务
  friend_api_transaction_client_handle::storage_ptr_type transacation_ptr;
  auto ret = RPC_AWAIT_CODE_RESULT(transaction_client_->create_transaction(
      ctx, transacation_ptr, rpc::friend_api::get_normal_transaction_options()));
  if (ret < 0 || !transacation_ptr) {
    RPC_RETURN_CODE(ret);
  }
  auto now = ctx.logical_now();
  auto expired_time = now + protobuf_to_system_clock(excel::get_const_config().friend_invite_expire());
  if (expired_time <= now) {
    expired_time = now;
    expired_time += std::chrono::hours(24);
  }

  // 事务事件 - 对方加被邀请，自己加邀请
  PROJECT_NAMESPACE_ID::friend_transaction_data other_event_datas;
  PROJECT_NAMESPACE_ID::friend_transaction_data self_event_datas;
  ret = transaction_add_invite_event(ctx, *owner_, true, expired_time, owner_->get_user_id(), owner_->get_zone_id(),
                                     user_key.user_id(), user_key.zone_id(), self_event_datas, other_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(owner_->get_zone_id(), owner_->get_user_id()),
      self_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(user_key.zone_id(), user_key.user_id()),
      other_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(transaction_client_->submit_transaction(ctx, transacation_ptr));
  // if (ret >= 0) {
  // TODO(any): OSS log
  // }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_friend_api_manager::accept_invite(rpc::context &ctx,
                                                             const atfw::shared::DUserIDKey &user_key) {
  if (0 == user_key.user_id() || 0 == user_key.zone_id()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // 检查被邀请记录
  const atfw::friend_api::DFriendInvitationInfo *invite_info = get_inviter(user_key);
  if (nullptr == invite_info) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITE_NOT_FOUND);
  }

  // 好友数量上限预检查
  if (static_cast<int>(friend_cache_set_.size()) >= excel::get_const_config().friend_max_number()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_MAX_NUMBER_LIMIT);
  }

  // 创建事务
  friend_api_transaction_client_handle::storage_ptr_type transacation_ptr;
  auto ret = RPC_AWAIT_CODE_RESULT(transaction_client_->create_transaction(
      ctx, transacation_ptr, rpc::friend_api::get_normal_transaction_options()));
  if (ret < 0 || !transacation_ptr) {
    RPC_RETURN_CODE(ret);
  }

  auto now = ctx.logical_now();
  auto expired_time = now + protobuf_to_system_clock(excel::get_const_config().friend_invite_expire());
  if (expired_time <= now) {
    expired_time = now;
    expired_time += std::chrono::hours(24);
  }

  PROJECT_NAMESPACE_ID::friend_transaction_data other_event_datas;
  PROJECT_NAMESPACE_ID::friend_transaction_data self_event_datas;
  // 事务事件 - 添加好友
  ret = transaction_add_friend_event(ctx, *owner_, true, user_key.user_id(), user_key.zone_id(), owner_->get_user_id(),
                                     owner_->get_zone_id(), other_event_datas, self_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  // 事务事件 - 移除被邀请
  ret = transaction_add_invite_event(ctx, *owner_, false, expired_time, user_key.user_id(), user_key.zone_id(),
                                     owner_->get_user_id(), owner_->get_zone_id(), other_event_datas, self_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  // 事务事件 - 移除邀请
  ret = transaction_add_invite_event(ctx, *owner_, false, expired_time, owner_->get_user_id(), owner_->get_zone_id(),
                                     user_key.user_id(), user_key.zone_id(), self_event_datas, other_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(owner_->get_zone_id(), owner_->get_user_id()),
      self_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(user_key.zone_id(), user_key.user_id()),
      other_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(transaction_client_->submit_transaction(ctx, transacation_ptr));
  // if (ret >= 0) {
  // TODO(any): OSS log
  // }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_friend_api_manager::reject_invite(rpc::context &ctx,
                                                             const atfw::shared::DUserIDKey &user_key) {
  if (0 == user_key.user_id() || 0 == user_key.zone_id()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // 检查被邀请记录
  const atfw::friend_api::DFriendInvitationInfo *invite_info = get_inviter(user_key);
  if (nullptr == invite_info) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITE_NOT_FOUND);
  }

  // 创建事务
  friend_api_transaction_client_handle::storage_ptr_type transacation_ptr;
  auto ret = RPC_AWAIT_CODE_RESULT(transaction_client_->create_transaction(
      ctx, transacation_ptr, rpc::friend_api::get_normal_transaction_options()));
  if (ret < 0 || !transacation_ptr) {
    RPC_RETURN_CODE(ret);
  }

  auto now = ctx.logical_now();
  auto expired_time = now + protobuf_to_system_clock(excel::get_const_config().friend_invite_expire());
  if (expired_time <= now) {
    expired_time = now;
    expired_time += std::chrono::hours(24);
  }

  PROJECT_NAMESPACE_ID::friend_transaction_data other_event_datas;
  PROJECT_NAMESPACE_ID::friend_transaction_data self_event_datas;
  // 事务事件 - 移除被邀请
  ret = transaction_add_invite_event(ctx, *owner_, false, expired_time, user_key.user_id(), user_key.zone_id(),
                                     owner_->get_user_id(), owner_->get_zone_id(), other_event_datas, self_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(owner_->get_zone_id(), owner_->get_user_id()),
      self_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(user_key.zone_id(), user_key.user_id()),
      other_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(transaction_client_->submit_transaction(ctx, transacation_ptr));
  // if (ret >= 0) {
  // TODO(any): OSS log
  // }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_friend_api_manager::remove_friend(rpc::context &ctx,
                                                             const atfw::shared::DUserIDKey &user_key) {
  if (0 == user_key.user_id() || 0 == user_key.zone_id()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // 检查是否已经是好友
  if (nullptr == get_friend(user_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_NOT_FOUND);
  }

  // 创建事务
  friend_api_transaction_client_handle::storage_ptr_type transacation_ptr;
  auto ret = RPC_AWAIT_CODE_RESULT(transaction_client_->create_transaction(
      ctx, transacation_ptr, rpc::friend_api::get_normal_transaction_options()));
  if (ret < 0 || !transacation_ptr) {
    RPC_RETURN_CODE(ret);
  }

  PROJECT_NAMESPACE_ID::friend_transaction_data other_event_datas;
  PROJECT_NAMESPACE_ID::friend_transaction_data self_event_datas;
  // 事务事件 - 移除好友
  ret = transaction_add_friend_event(ctx, *owner_, false, owner_->get_user_id(), owner_->get_zone_id(),
                                     user_key.user_id(), user_key.zone_id(), self_event_datas, other_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  // 事务事件 - 移除邀请
  ret = transaction_add_invite_event(ctx, *owner_, false, std::chrono::system_clock::from_time_t(0),
                                     owner_->get_user_id(), owner_->get_zone_id(), user_key.user_id(),
                                     user_key.zone_id(), self_event_datas, other_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  // 事务事件 - 移除被邀请
  ret = transaction_add_invite_event(ctx, *owner_, false, std::chrono::system_clock::from_time_t(0), user_key.user_id(),
                                     user_key.zone_id(), owner_->get_user_id(), owner_->get_zone_id(),
                                     other_event_datas, self_event_datas);
  if (0 != ret) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(owner_->get_zone_id(), owner_->get_user_id()),
      self_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(user_key.zone_id(), user_key.user_id()),
      other_event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(transaction_client_->submit_transaction(ctx, transacation_ptr));
  // if (ret >= 0) {
  // TODO(any): OSS log
  // }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_friend_api_manager::send_gift(rpc::context &ctx, const atfw::shared::DUserIDKey &user_key,
                                                         int32_t gift_type_id) {
  // 检查发送礼物次数限制
  int32_t friend_daily_send_gift_limit = excel::get_const_config().friend_daily_send_gift_limit();
  if (friend_daily_send_gift_limit > 0 &&
      get_local_stats().daily_send_gift_times() > static_cast<uint32_t>(friend_daily_send_gift_limit)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_DAILY_SEND_LIMIT);
  }

  // 检查是否是好友
  if (nullptr == get_friend(user_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_NOT_FOUND);
  }

  // 检查是否今日已经发过
  if (daily_send_list_.end() != daily_send_list_.find(user_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_ALREAD_SEND);
  }

  // 检查礼物ID和发送条件
  if (0 == gift_type_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_CONFIG_NOT_FOUND);
  }

  // TODO(owentou): Excel Configure
#if 0
  auto gift_cfg = excel::get_ResFriendGiftType_by_id(gift_type_id);
  if (!gift_cfg) {
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_CONFIG_NOT_FOUND;
  }
#endif

  int64_t gift_id = RPC_AWAIT_TYPE_RESULT(
      rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_DEFAULT));
  if (gift_id <= 0) {
    FCTXLOGERROR(ctx, "{} failed to generate uuid, res: {}({})", *owner_, gift_id,
                 protobuf_mini_dumper_get_error_msg(static_cast<int>(gift_id)));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_NOT_FOUND_UUID);
  }

  // 创建事务
  friend_api_transaction_client_handle::storage_ptr_type transacation_ptr;
  auto ret = RPC_AWAIT_CODE_RESULT(transaction_client_->create_transaction(
      ctx, transacation_ptr, rpc::friend_api::get_force_commit_transaction_options()));
  if (ret < 0 || !transacation_ptr) {
    RPC_RETURN_CODE(ret);
  }

  // 这是一个非严格一致性请求
  PROJECT_NAMESPACE_ID::friend_transaction_data event_datas;
  atfw::friend_api::DFriendEvent *evt_data = event_datas.add_event_data();
  if (nullptr == evt_data) {
    FCTXLOGERROR(ctx, "{} malloc event", *owner_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }

  atfw::friend_api::DFriendGift *gift_data = evt_data->mutable_add_gift();
  if (nullptr == gift_data) {
    FCTXLOGERROR(ctx, "{} malloc gift", *owner_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }

  gift_data->set_gift_id(gift_id);
  gift_data->mutable_from_user()->set_user_id(owner_->get_user_id());
  gift_data->mutable_from_user()->set_zone_id(owner_->get_zone_id());
  gift_data->set_gift_type_id(gift_type_id);

  protobuf_from_system_clock(
      *gift_data->mutable_expired_time(),
      ctx.logical_now() + protobuf_to_system_clock(excel::get_const_config().friend_gift_expire()));

  // TODO(any): extract random pool if need

  // 计数发送次数，发起增加礼物的friendsvr请求
  atfw::friend_api::DFriendGiftHistory history;
  history.set_gift_id(gift_data->gift_id());
  protobuf_copy_message(*history.mutable_user_key(), user_key);
  history.set_gift_type_id(gift_data->gift_type_id());

  uint32_t send_count = 0;
  if (add_gift_send_list(history)) {
    ++send_count;
  }
  add_send_gift_times(send_count);

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(user_key.zone_id(), user_key.user_id()), event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(transaction_client_->submit_transaction(ctx, transacation_ptr));
  if (ret < 0) {
    remove_gift_send_list(history);

    // 返回客户端错误码时，服务器流程是正确的，所以不用打错误日志
    if (ret >= PROJECT_NAMESPACE_ID::EnErrorCode_MIN) {
      FCTXLOGINFO(ctx, "{} submit transaction {} to send gift for user {}:{} failed, res: {}({})", *owner_,
                  transacation_ptr->data.metadata().transaction_uuid(), user_key.zone_id(), user_key.user_id(), ret,
                  protobuf_mini_dumper_get_error_msg(ret));
      RPC_RETURN_CODE(ret);
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED);
  }

  // TODO(any): OSS log
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_friend_api_manager::receive_gifts(
    rpc::context &ctx, std::vector<int64_t> &gift_ids,
    ::google::protobuf::RepeatedPtrField<atfw::friend_api::DFriendGift> *out) {
  rpc::result_code_type::value_type ret = 0;

  // 检查是否已经在待清理列表
  std::vector<const atfw::friend_api::DFriendGift *> gifts;
  gifts.reserve(gift_ids.size());
  if (nullptr != out) {
    out->Reserve(static_cast<int>(gift_ids.size()));
  }
  for (size_t i = 0; i < gift_ids.size(); ++i) {
    if (confirm_remove_gifts_.end() != confirm_remove_gifts_.find(gift_ids[i])) {
      gift_ids[i] = gift_ids[gift_ids.size() - 1];
      gift_ids.pop_back();
      continue;
    }

    // 检查礼物有效
    const atfw::friend_api::DFriendGift *gift_data = get_gift(gift_ids[i]);
    if (nullptr == gift_data) {
      ret = PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_NOT_FOUND;
      gift_ids[i] = gift_ids[gift_ids.size() - 1];
      gift_ids.pop_back();
      continue;
    }

    gifts.push_back(gift_data);

    if (nullptr != out) {
      protobuf_copy_message(*out->Add(), *gift_data);
    }
  }

  // 所有礼物都已领取
  if (gift_ids.empty()) {
    RPC_RETURN_CODE(ret);
  }

  // 检查接收礼物次数限制
  int32_t friend_daily_receive_gift_limit = excel::get_const_config().friend_daily_receive_gift_limit();
  if (friend_daily_receive_gift_limit > 0 &&
      get_local_stats().daily_receive_gift_times() + static_cast<uint32_t>(gift_ids.size()) >
          static_cast<uint32_t>(friend_daily_receive_gift_limit)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_GIFT_DAILY_RECEIVE_LIMIT);
  }

  std::vector<atfw::friend_api::DFriendGiftHistory> histories;
  histories.reserve(gifts.size());

  // 这是一个非严格一致性请求
  PROJECT_NAMESPACE_ID::friend_transaction_data event_datas;
  auto now = ctx.logical_now();

  // 补上尚未确认的礼物（故障恢复流程）
  for (const auto &gift_id : confirm_remove_gifts_) {
    atfw::friend_api::DFriendEvent *evt_data = event_datas.add_event_data();
    if (nullptr == evt_data) {
      FCTXLOGERROR(ctx, "{} malloc event", *owner_);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }
    atfw::friend_api::DFriendGift *gift_data = evt_data->mutable_remove_gift();
    if (nullptr == gift_data) {
      FCTXLOGERROR(ctx, "{} malloc gift", *owner_);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }

    gift_data->set_gift_id(gift_id);
    protobuf_from_system_clock(*gift_data->mutable_expired_time(), now);
  }

  for (auto &origin_gift_data : gifts) {
    atfw::friend_api::DFriendEvent *evt_data = event_datas.add_event_data();
    if (nullptr == evt_data) {
      FCTXLOGERROR(ctx, "{} malloc event", *owner_);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }
    atfw::friend_api::DFriendGift *gift_data = evt_data->mutable_remove_gift();
    if (nullptr == gift_data) {
      FCTXLOGERROR(ctx, "{} malloc gift", *owner_);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }

    gift_data->set_gift_id(origin_gift_data->gift_id());
    protobuf_from_system_clock(*gift_data->mutable_expired_time(), now);

    // history
    histories.emplace_back();
    atfw::friend_api::DFriendGiftHistory &history = histories.back();
    history.set_gift_id(origin_gift_data->gift_id());
    protobuf_copy_message(*history.mutable_user_key(), origin_gift_data->from_user());
    history.set_gift_type_id(origin_gift_data->gift_type_id());

    confirm_remove_gifts_.insert(origin_gift_data->gift_id());

    // TODO(any): 下发道具
#if 0
    ret = owner_->add_all_items(origin_gift_data->gift_item(), PROJECT_NAMESPACE_ID::EN_ICMT_FRIEND_GIFT,
                                origin_gift_data->gift_type_id(), origin_gift_data->gift_id());
    if (ret < 0) {
      return ret;
    }
#endif
    // TODO(any): OSS log
  }

  // 计数接收次数
  uint32_t receive_count = 0;
  for (auto &history : histories) {
    if (add_gift_receive_list(history)) {
      ++receive_count;
    }
  }
  add_receive_gift_times(receive_count);

  // 创建事务
  friend_api_transaction_client_handle::storage_ptr_type transacation_ptr;
  ret = RPC_AWAIT_CODE_RESULT(transaction_client_->create_transaction(
      ctx, transacation_ptr, rpc::friend_api::get_force_commit_transaction_options()));
  if (ret < 0 || !transacation_ptr) {
    RPC_RETURN_CODE(ret);
  }

  ret = transaction_client_->add_participator(
      ctx, transacation_ptr,
      rpc::friend_api::friend_key_to_transaction_participator_key(owner_->get_zone_id(), owner_->get_user_id()),
      event_datas);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(transaction_client_->submit_transaction(ctx, transacation_ptr));

  if (ret < 0) {
    // 返回客户端错误码时，服务器流程时正确的，所以不用打错误日志
    if (ret >= PROJECT_NAMESPACE_ID::EnErrorCode_MIN) {
      FCTXLOGINFO(ctx, "{} submit transaction {} to receive gifts failed, res: {}({})", *owner_,
                  transacation_ptr->data.metadata().transaction_uuid(), ret, protobuf_mini_dumper_get_error_msg(ret));
    } else if (ret >= 0) {
      ret = PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_TRANSACTION_FAILED;
    }
  } else {
    // 如果成功确认就可以清理删除确认列表
    for (auto gift_id : gift_ids) {
      confirm_remove_gifts_.erase(gift_id);
      // 保底删除，错误数据修复
      remove_gift_cache(ctx, gift_id);
    }
  }
  RPC_RETURN_CODE(ret);
}

int user_friend_api_manager::patch_stats(const atfw::friend_api::DFriendStatistics &stats) {
  // patch 数据记录
  protobuf_copy_message(remote_friend_statistics_, stats);
  return 0;
}

bool user_friend_api_manager::add_event_dirty(atfw::friend_api::DFriendEvent &&evt_data) {
  if (atfw::friend_api::DFriendEvent::EVENT_NOT_SET == evt_data.event_case()) {
    return false;
  }

  atfw::friend_api::DFriendEvent *dirty_data = dirty_cache_.Add();
  if (nullptr != dirty_data) {
    protobuf_move_message(*dirty_data, std::move(evt_data));

    // 注册脏数据dump的handle
    owner_->insert_dirty_handle_if_not_exists(
        reinterpret_cast<uintptr_t>(&dirty_cache_), "user_friend_api_manager.add_event_dirty",
        [](rpc::context & /*ctx*/, user &user_inst, user::dirty_message_container &dirty_message) {
          if (user_inst.get_user_friend_api_manager().dirty_cache_.empty()) {
            return;
          }

          if (!dirty_message.user_dirty) {
            dirty_message.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
          }

          user_inst.get_user_friend_api_manager().dirty_cache_.Swap(
              dirty_message.user_dirty->mutable_dirty_friend_data()->mutable_friend_event_data());
        },
        nullptr);
  }

  return true;
}

void user_friend_api_manager::add_send_gift_times(uint32_t times) {
  if (0 == times) {
    return;
  }

  local_friend_statistics_.set_daily_send_gift_times(local_friend_statistics_.daily_send_gift_times() + times);
  local_friend_statistics_.set_weekly_send_gift_times(local_friend_statistics_.weekly_send_gift_times() + times);
  local_friend_statistics_.set_sum_send_gift_times(local_friend_statistics_.sum_send_gift_times() + times);

  if (times > 0) {
    return;
  }

  if (local_friend_statistics_.daily_send_gift_times() < 0) {
    local_friend_statistics_.set_daily_send_gift_times(0);
  }

  if (local_friend_statistics_.weekly_send_gift_times() < 0) {
    local_friend_statistics_.set_weekly_send_gift_times(0);
  }

  if (local_friend_statistics_.sum_send_gift_times() < 0) {
    local_friend_statistics_.set_sum_send_gift_times(0);
  }
}

void user_friend_api_manager::add_receive_gift_times(uint32_t times) {
  if (0 == times) {
    return;
  }

  local_friend_statistics_.set_daily_receive_gift_times(local_friend_statistics_.daily_receive_gift_times() + times);
  local_friend_statistics_.set_weekly_receive_gift_times(local_friend_statistics_.weekly_receive_gift_times() + times);
  local_friend_statistics_.set_sum_receive_gift_times(local_friend_statistics_.sum_receive_gift_times() + times);

  if (times > 0) {
    return;
  }

  if (local_friend_statistics_.daily_receive_gift_times() < 0) {
    local_friend_statistics_.set_daily_receive_gift_times(0);
  }

  if (local_friend_statistics_.weekly_receive_gift_times() < 0) {
    local_friend_statistics_.set_weekly_receive_gift_times(0);
  }

  if (local_friend_statistics_.sum_receive_gift_times() < 0) {
    local_friend_statistics_.set_sum_receive_gift_times(0);
  }
}

bool user_friend_api_manager::add_gift_send_list(const atfw::friend_api::DFriendGiftHistory &history) {
  if (daily_send_list_.end() != daily_send_list_.find(history.user_key())) {
    return false;
  }

  protobuf_copy_message(daily_send_list_[history.user_key()], history);

  atfw::friend_api::DFriendEvent evt_data;
  protobuf_copy_message(*evt_data.mutable_daily_send_list(), history);

  add_event_dirty(std::move(evt_data));
  return true;
}

bool user_friend_api_manager::remove_gift_send_list(const atfw::friend_api::DFriendGiftHistory &history) {
  auto iter = daily_send_list_.find(history.user_key());
  if (iter == daily_send_list_.end()) {
    return false;
  }

  user_key_equal_t eq;

  protobuf_remove_repeated_if(dirty_cache_, [&history, &eq](const atfw::friend_api::DFriendEvent &evt_data) {
    return evt_data.has_daily_send_list() && eq(evt_data.daily_send_list().user_key(), history.user_key());
  });

  daily_send_list_.erase(iter);
  return true;
}

bool user_friend_api_manager::add_gift_receive_list(const atfw::friend_api::DFriendGiftHistory &history) {
  if (daily_receive_list_.end() != daily_receive_list_.find(history.user_key())) {
    return false;
  }

  protobuf_copy_message(daily_receive_list_[history.user_key()], history);

  atfw::friend_api::DFriendEvent evt_data;
  protobuf_copy_message(*evt_data.mutable_daily_receive_list(), history);

  add_event_dirty(std::move(evt_data));

  return true;
}

bool user_friend_api_manager::add_friend_cache(rpc::context & /*ctx*/, const atfw::friend_api::DFriendInfo &friend_data,
                                               bool need_notify) {
  if (0 == friend_data.user_key().user_id() || 0 == friend_data.user_key().zone_id()) {
    return false;
  }

  auto iter = friend_cache_set_.find(friend_data.user_key());
  if (iter != friend_cache_set_.end()) {
    protobuf_copy_message(iter->second, friend_data);
    iter->second.mutable_removed_time()->Clear();
    return true;
  }

  auto &new_cache_data = friend_cache_set_[friend_data.user_key()];
  protobuf_copy_message(new_cache_data, friend_data);
  new_cache_data.mutable_removed_time()->Clear();

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_add_friend_data(), friend_data);
    add_event_dirty(std::move(event_data));
  }

  // TODO(any): 本地推荐: 如果这个玩家好友达到上限了,那么就需要停止自己被推荐给被人
  // if (is_friend_full()) {
  //   user_level_local_index_manager::me()->remove_user_index(owner_->get_user_id());
  // }

  // TODO(any): 如果需要的话，增加社交信息缓存
  // owner_->get_social_information().set_friend_count_cache(static_cast<int32_t>(get_all_friend_cache().size()));
  return true;
}

void user_friend_api_manager::remove_friend_cache(rpc::context &ctx, const atfw::shared::DUserIDKey &friend_key,
                                                  bool need_notify) {
  auto iter = friend_cache_set_.find(friend_key);
  if (iter == friend_cache_set_.end()) {
    return;
  }

  protobuf_from_system_clock(*iter->second.mutable_expired_time(), ctx.logical_now());

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_remove_friend_data(), iter->second);
    add_event_dirty(std::move(event_data));
  }

  friend_cache_set_.erase(iter);

  // TODO(any): 本地推荐: 将这个玩家添加到推荐列表中，允许被推荐
  // user_level_local_index_manager::me()->add_user_index(owner_->get_player_data().player_level(),
  // owner_->get_user_id(), owner_->get_zone_id());

  // TODO(any): 如果需要的话，增加社交信息缓存
  // owner_->get_social_information().set_friend_count_cache(static_cast<int32_t>(get_all_friend_cache().size()));
}

const atfw::friend_api::DFriendInfo *user_friend_api_manager::get_friend_cache(
    const atfw::shared::DUserIDKey &friend_key) const {
  auto iter = friend_cache_set_.find(friend_key);
  if (iter == friend_cache_set_.end()) {
    return nullptr;
  }

  return &iter->second;
}

bool user_friend_api_manager::add_inviter_cache(rpc::context & /*ctx*/,
                                                const atfw::friend_api::DFriendInvitationInfo &invite_data,
                                                bool need_notify) {
  if (0 == invite_data.from_user().user_id() || 0 == invite_data.from_user().zone_id()) {
    return false;
  }

  auto iter = inviter_cache_set_.find(invite_data.from_user());
  if (iter != inviter_cache_set_.end()) {
    protobuf_copy_message(iter->second, invite_data);
    iter->second.mutable_removed_time()->Clear();
    return true;
  }

  auto &new_cache_data = inviter_cache_set_[invite_data.from_user()];
  protobuf_copy_message(inviter_cache_set_[invite_data.from_user()], invite_data);
  new_cache_data.mutable_removed_time()->Clear();

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_add_inviter(), invite_data);
    add_event_dirty(std::move(event_data));
  }

  // TODO(any): 如果需要的话，增加社交信息缓存
  // owner_->get_social_information().set_friend_inviter_count_cache(
  //     static_cast<int32_t>(get_all_inviter_cache().size()));

  return true;
}

void user_friend_api_manager::remove_inviter_cache(rpc::context & /*ctx*/, const atfw::shared::DUserIDKey &friend_key,
                                                   bool need_notify) {
  auto iter = inviter_cache_set_.find(friend_key);
  if (iter == inviter_cache_set_.end()) {
    return;
  }

  iter->second.mutable_removed_time()->Clear();

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_remove_inviter(), iter->second);
    add_event_dirty(std::move(event_data));
  }

  inviter_cache_set_.erase(iter);

  // TODO(any): 如果需要的话，增加社交信息缓存
  // owner_->get_social_information().set_friend_inviter_count_cache(
  //     static_cast<int32_t>(get_all_inviter_cache().size()));
}

const atfw::friend_api::DFriendInvitationInfo *user_friend_api_manager::get_inviter_cache(
    const atfw::shared::DUserIDKey &friend_key) const {
  auto iter = inviter_cache_set_.find(friend_key);
  if (iter == inviter_cache_set_.end()) {
    return nullptr;
  }

  return &iter->second;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
atfw::shared::DUserIDKey user_friend_api_manager::get_key_from_inviter_cache(
    const atfw::friend_api::DFriendInvitationInfo &invite_data) const {
  return invite_data.from_user();
}

bool user_friend_api_manager::add_invitee_cache(rpc::context & /*ctx*/,
                                                const atfw::friend_api::DFriendInvitationInfo &invite_data,
                                                bool need_notify) {
  if (0 == invite_data.to_user().user_id() || 0 == invite_data.to_user().zone_id()) {
    return false;
  }

  atfw::shared::DUserIDKey key = get_key_from_invitee_cache(invite_data);
  auto iter = invitee_cache_set_.find(key);
  if (iter != invitee_cache_set_.end()) {
    protobuf_copy_message(iter->second, invite_data);
    iter->second.mutable_removed_time()->Clear();
    return true;
  }

  auto &new_cache_data = invitee_cache_set_[key];
  protobuf_copy_message(invitee_cache_set_[key], invite_data);
  new_cache_data.mutable_removed_time()->Clear();

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_add_invitee(), invite_data);
    add_event_dirty(std::move(event_data));
  }

  // TODO(any): 如果需要的话，增加社交信息缓存
  // owner_->get_social_information().set_friend_invitee_count_cache(
  //     static_cast<int32_t>(get_all_invitee_cache().size()));
  return true;
}

void user_friend_api_manager::remove_invitee_cache(rpc::context &ctx, const atfw::shared::DUserIDKey &friend_key,
                                                   bool need_notify) {
  auto iter = invitee_cache_set_.find(friend_key);
  if (iter == invitee_cache_set_.end()) {
    return;
  }

  protobuf_from_system_clock(*iter->second.mutable_removed_time(), ctx.logical_now());

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_remove_invitee(), iter->second);
    add_event_dirty(std::move(event_data));
  }

  invitee_cache_set_.erase(iter);

  // TODO(any): 如果需要的话，增加社交信息缓存
  // owner_->get_social_information().set_friend_invitee_count_cache(
  //     static_cast<int32_t>(get_all_invitee_cache().size()));
}

const atfw::friend_api::DFriendInvitationInfo *user_friend_api_manager::get_invitee_cache(
    const atfw::shared::DUserIDKey &friend_key) const {
  auto iter = invitee_cache_set_.find(friend_key);
  if (iter == invitee_cache_set_.end()) {
    return nullptr;
  }

  return &iter->second;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
atfw::shared::DUserIDKey user_friend_api_manager::get_key_from_invitee_cache(
    const atfw::friend_api::DFriendInvitationInfo &invite_data) const {
  return invite_data.to_user();
}

bool user_friend_api_manager::add_gift_cache(rpc::context & /*ctx*/, const atfw::friend_api::DFriendGift &gift_data,
                                             bool need_notify) {
  if (0 == gift_data.gift_id()) {
    return false;
  }

  auto iter = gift_cache_set_.find(gift_data.gift_id());
  if (iter != gift_cache_set_.end()) {
    protobuf_copy_message(iter->second, gift_data);
    iter->second.mutable_removed_time()->Clear();
    return true;
  }

  auto &new_cache_data = gift_cache_set_[gift_data.gift_id()];
  protobuf_copy_message(gift_cache_set_[gift_data.gift_id()], gift_data);
  new_cache_data.mutable_removed_time()->Clear();

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_add_gift(), gift_data);
    add_event_dirty(std::move(event_data));
  }

  return true;
}

void user_friend_api_manager::remove_gift_cache(rpc::context &ctx, int64_t gift_id, bool need_notify) {
  auto iter = gift_cache_set_.find(gift_id);
  if (iter == gift_cache_set_.end()) {
    return;
  }

  protobuf_from_system_clock(*iter->second.mutable_removed_time(), ctx.logical_now());

  if (need_notify) {
    atfw::friend_api::DFriendEvent event_data;
    protobuf_copy_message(*event_data.mutable_remove_gift(), iter->second);
    add_event_dirty(std::move(event_data));
  }

  gift_cache_set_.erase(iter);
}

const atfw::friend_api::DFriendGift *user_friend_api_manager::get_gift_cache(int64_t gift_id) const {
  auto iter = gift_cache_set_.find(gift_id);
  if (iter == gift_cache_set_.end()) {
    return nullptr;
  }

  return &iter->second;
}

bool user_friend_api_manager::add_sns_friend_cache(rpc::context & /*ctx*/,
                                                   const atfw::friend_api::DFriendSNSInfo &friend_data) {
  if (0 == friend_data.user_key().user_id()) {
    return false;
  }

  protobuf_copy_message(sns_friend_cache_set_[friend_data.user_key().user_id()], friend_data);

  // SNS 好友只会由客户端主动拉取，所以不需要通知
  return true;
}

void user_friend_api_manager::remove_sns_friend_cache(rpc::context & /*ctx*/, uint64_t user_id) {
  auto iter = sns_friend_cache_set_.find(user_id);
  if (iter == sns_friend_cache_set_.end()) {
    return;
  }

  // SNS 好友只会由客户端主动拉取，所以不需要通知

  sns_friend_cache_set_.erase(iter);
}

const atfw::friend_api::DFriendSNSInfo *user_friend_api_manager::get_sns_friend_cache(uint64_t user_id) const {
  auto iter = sns_friend_cache_set_.find(user_id);
  if (iter == sns_friend_cache_set_.end()) {
    return nullptr;
  }

  return &iter->second;
}

bool user_friend_api_manager::is_sns_friend_available() const noexcept {
  const auto &lobby_cfg = logic_config::me()->get_server_instance_config<atfw::shared::config::lobbysvr_cfg>();
  const auto &block_list = lobby_cfg.friend_api().friend_sns_channel_blacklist();
  if (block_list.end() !=
      std::find(block_list.begin(), block_list.end(), static_cast<int32_t>(owner_->get_account_info().channel_id()))) {
    return false;
  }

  return true;
}

rpc::result_code_type user_friend_api_manager::pull_sns_friend_data(rpc::context & /*ctx*/) {
  // TODO(any): 接入开放平台，获取平台好友
  // 注意删除过期数据

  RPC_RETURN_CODE(0);
}
#if 0  // 社交分享类接口
void user_friend_api_manager::update_sns_share(bool daily_reset, bool weekly_reset) {
  if (daily_reset || weekly_reset) {
    for (int i = 0; i < sns_data_.shares_size(); ++i) {
      auto share_item = sns_data_.mutable_shares(i);
      if (nullptr == share_item) {
        continue;
      }

      if (daily_reset) {
        share_item->set_daily_share_times(0);
      }

      if (weekly_reset) {
        share_item->set_weekly_share_times(0);
      }

      // dirty
      protobuf_copy_message(owner_->mutable_dirty_sns_share(share_item->share_reward_type()), *share_item);
    }
  }
}

rpc::result_code_type user_friend_api_manager::add_sns_share(rpc::context& ctx, int32_t reward_type, int32_t sub_type,
                                        PROJECT_NAMESPACE_ID::SNSShareRecord *&out,
                                       ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemOffset> *out_reward_items) {
  refresh_feature_limit_minute(ctx);
  out = nullptr;

  if (reward_type <= 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // TODO(any): 社交分享、限频和奖励

  // TODO(any): OSS log

  RPC_RETURN_CODE(0);
}

PROJECT_NAMESPACE_ID::SNSShareRecord *user_friend_api_manager::mutable_sns_share(int32_t reward_type) {
  if (reward_type == PROJECT_NAMESPACE_ID::EN_SSRT_WITHOUT_REWARD) {
    return nullptr;
  }

  for (int i = 0; i < sns_data_.shares_size(); ++i) {
    if (sns_data_.shares(i).share_reward_type() == reward_type) {
      return sns_data_.mutable_shares(i);
    }
  }

  PROJECT_NAMESPACE_ID::SNSShareRecord *record = sns_data_.add_shares();
  if (nullptr == record) {
    return record;
  }

  record->set_share_reward_type(reward_type);

  is_dirty_ = true;
  return record;
}

void user_friend_api_manager::clear_sns_share(int32_t reward_type) {
  if (0 == reward_type) {
    for (auto &share : sns_data_.shares()) {
      owner_->mutable_dirty_sns_share(share.share_reward_type());
    }
    sns_data_.clear_shares();

  } else {
    PROJECT_NAMESPACE_ID::SNSShareRecord *share = mutable_sns_share(reward_type);
    if (nullptr != share) {
      share->Clear();
      share->set_share_reward_type(reward_type);
    }

    // dirty
    owner_->mutable_dirty_sns_share(reward_type);
  }

  is_dirty_ = true;
}

void user_friend_api_manager::dump_sns_share(PROJECT_NAMESPACE_ID::UserSNSData &out, int32_t reward_type) {
  if (0 == reward_type) {
    protobuf_copy_message(out, sns_data_);
    return;
  }

  for (int i = 0; i < sns_data_.shares_size(); ++i) {
    if (sns_data_.shares(i).share_reward_type() == reward_type) {
      protobuf_copy_message(*out.add_shares(), sns_data_.shares(i));
      break;
    }
  }
}
#endif

bool user_friend_api_manager::is_friend_full() const noexcept {
  if (static_cast<int32_t>(friend_cache_set_.size()) >= excel::get_const_config().friend_max_number()) {
    return true;
  }
  return false;
}

void user_friend_api_manager::set_need_send_wal_heartbeat(rpc::context &ctx) {
  need_send_wal_heartbeat_ = true;

  invoke_async_task(ctx);
}

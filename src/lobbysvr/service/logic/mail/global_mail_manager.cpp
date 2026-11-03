// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#include "logic/mail/global_mail_manager.h"

#include <log/log_wrapper.h>
#include <random/random_generator.h>
#include <time/time_utility.h>

#include <memory/object_allocator.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.mail.common.pb.h>
#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.mail.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel/config_easy_api.h>
#include <config/excel_config_const_index.h>

#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <logic/mail/mail_utils.h>
#include <logic/mail/user_mail_manager.h>

#include <router/router_user_manager.h>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "data/user.h"
#include "logic/mail/task_action_global_mail_async_jobs.h"

struct global_mail_manager::global_mail_box_type_index_cleanup_guard {
  inline explicit global_mail_box_type_index_cleanup_guard(global_mail_manager &mgr) : lock(&mgr) {}

  ~global_mail_box_type_index_cleanup_guard() {
    if (lock == nullptr) {
      return;
    }

    for (auto &zone_pair : check_map) {
      uint32_t zone_id = zone_pair.first;
      auto iter_zone = lock->mail_box_type_index_.find(zone_id);
      if (iter_zone == lock->mail_box_type_index_.end()) {
        continue;
      }

      for (const auto &major_type : zone_pair.second) {
        auto iter_major_type = iter_zone->second.find(major_type);
        if (iter_major_type == iter_zone->second.end()) {
          continue;
        }

        if (!iter_major_type->second) {
          iter_zone->second.erase(iter_major_type);
          continue;
        }

        if (iter_major_type->second->mails.empty()) {
          iter_zone->second.erase(iter_major_type);
          continue;
        }
      }

      if (iter_zone->second.empty()) {
        lock->mail_box_type_index_.erase(iter_zone);
      }
    }
  }

  void insert(uint32_t zone_id, int32_t major_type) { check_map[zone_id].insert(major_type); }

  global_mail_manager *lock;

  std::unordered_map<uint32_t, std::unordered_set<int32_t>> check_map;
};

global_mail_manager::global_mail_manager()
    : task_next_timepoint_(std::chrono::system_clock::from_time_t(0)),
      last_success_fetch_timepoint_(std::chrono::system_clock::from_time_t(0)),
      force_users_reload_(false) {
  atfw::util::random::xoshiro256_plus rnd;
  rnd.init_seed(static_cast<uint64_t>(atfw::util::time::time_utility::get_now()) ^
                logic_config::me()->get_local_server_id());
  // 启动随机20秒起始拉取时间，以便错峰负载
  task_next_timepoint_ = atfw::util::time::time_utility::now();
  task_next_timepoint_ += std::chrono::seconds(rnd.random_between<time_t>(0, 20));
}

int global_mail_manager::tick(rpc::context &ctx) {
  if (ss_msg_dispatcher::is_instance_destroyed() || ss_msg_dispatcher::me()->is_closing()) {
    return 0;
  }

  if (db_msg_dispatcher::is_instance_destroyed() ||
      (!db_msg_dispatcher::me()->is_available(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT) &&
       !db_msg_dispatcher::me()->is_available(db_msg_dispatcher::channel_t::RAW_DEFAULT))) {
    return 0;
  }

  try_to_start_async_jobs(ctx);

  if (force_users_reload_) {
    // 重置所有玩家对象的全服邮件缓存为已过期
    router_user_manager::me()->foreach_object([](const router_user_manager::ptr_t &user_cache) {
      user::ptr_t user_inst = std::static_pointer_cast<user>(user_cache->get_object());
      if (user_inst && user_inst->is_writable()) {
        user_inst->get_user_mail_manager().reset_global_mails_cache();
      }
      return true;
    });
    force_users_reload_ = false;
  }
  return 0;
}

bool global_mail_manager::is_async_task_running() {
  if (task_type_trait::empty(mail_async_task_)) {
    return false;
  }

  if (task_type_trait::is_exiting(mail_async_task_)) {
    task_type_trait::reset_task(mail_async_task_);
    return false;
  }

  return true;
}

void global_mail_manager::try_to_start_async_jobs(rpc::context &ctx) {
  if (ctx.logical_now() < task_next_timepoint_) {
    return;
  }

  if (is_async_task_running()) {
    return;
  }

  // 启动邮件异步任务
  task_type_trait::task_type task_inst;
  task_action_global_mail_async_jobs::ctor_param_t params;
  task_manager::me()->create_task<task_action_global_mail_async_jobs>(task_inst, std::move(params));

  if (task_type_trait::empty(task_inst)) {
    FWLOGERROR("[MAIL] create task_action_global_mail_async_jobs failed");
  } else {
    mail_async_task_ = task_inst;

    dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

    int res = task_manager::me()->start_task(task_inst, start_data);
    if (res < 0) {
      FWLOGERROR("[MAIL] start task_action_global_mail_async_jobs failed, res: {}({})", res,
                 protobuf_mini_dumper_get_error_msg(res));
      task_type_trait::reset_task(mail_async_task_);
      return;
    }

    atfw::util::random::xoshiro256_plus rnd;
    rnd.init_seed(static_cast<uint64_t>(atfw::util::time::time_utility::get_now()));
    // 启动随机一下下一轮时间，以便错峰负载，类似raft的策略
    task_next_timepoint_ = ctx.logical_now();
    task_next_timepoint_ += std::chrono::seconds(
        PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_JOBS_TASK_INTERVAL -
        (PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_JOBS_TASK_INTERVAL >> 2) +
        rnd.random_between<time_t>(0, PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_JOBS_TASK_INTERVAL >> 1));
  }
}

void global_mail_manager::reset_async_jobs_protect(rpc::context & /*ctx*/) {
  task_next_timepoint_ = std::chrono::system_clock::from_time_t(0);
}

bool global_mail_manager::update_from_db(rpc::context &ctx, uint32_t zone_id, int32_t major_type,
                                         atfw::mail::table_global_mail_blob_data &blob_data, bool rewrite_db_data) {
  auto type_index_cleanup_guard = make_mail_box_type_index_cleanup_guard();
  bool ret = false;
  mail_box_ptr_t old_mail_box = global_mail_manager::me()->internal_get_mail_box_by_type(zone_id, major_type);
  std::unordered_set<int64_t> invalid_ids;
  if (old_mail_box) {
    invalid_ids.reserve(old_mail_box->mails.size());
    for (auto &mail : old_mail_box->mails) {
      invalid_ids.insert(mail.first);
    }
  }

  {
    std::vector<int> append_mail_ids;
    append_mail_ids.reserve(static_cast<size_t>(blob_data.mail_records_size()));

    // 先更新，再追加。否则添加邮件的时候可能会触发删除老邮件，然后又被后面的记录刷掉状态
    for (int i = 0; i < blob_data.mail_records_size(); ++i) {
      const PROJECT_NAMESPACE_ID::DMailRecord &record = blob_data.mail_records(i);
      // 如果有要移除的邮件，需要重刷DB
      if (rewrite_db_data && atfw::mail::mail_is_history_removable(ctx, record)) {
        ret = true;
        // 不能continue，要走后面的流程触发删除content
      }

      if (false == global_mail_manager::me()->update_global_mail(ctx, zone_id, record)) {
        append_mail_ids.push_back(i);
      }
      invalid_ids.erase(record.mail_id());
    }

    for (auto i : append_mail_ids) {
      global_mail_manager::me()->add_global_mail(ctx, zone_id, blob_data.mail_records(i));
    }
  }

  // 不在库里的邮件要全部删除
  for (auto invalid_mail_id : invalid_ids) {
    FWLOGINFO("[MAIL] global_mail_manager remove mail record ({}, {}) because not found int DB", major_type,
              invalid_mail_id);
    global_mail_manager::me()->remove_global_mail(ctx, invalid_mail_id);
  }

  old_mail_box = global_mail_manager::me()->internal_get_mail_box_by_type(zone_id, major_type);
  if (nullptr != old_mail_box) {
    size_t old_count = old_mail_box->mails.size();
    compact_mails(ctx, *old_mail_box);
    if (rewrite_db_data && old_mail_box->mails.size() != old_count) {
      ret = true;
    }
  }
  // 重写刷回数据库数据
  if (ret) {
    blob_data.clear_mail_records();
    if (nullptr != old_mail_box) {
      for (auto &mail : old_mail_box->mails) {
        if (mail.second && mail.second->record) {
          protobuf_copy_message(*blob_data.add_mail_records(), *mail.second->record);
        }
      }
    }
  }

  std::unordered_map<int64_t, const PROJECT_NAMESPACE_ID::DMailRecord *> check_additional_remove_list;
  check_additional_remove_list.reserve(pending_to_remove_.size());
  for (auto &mail : pending_to_remove_) {
    if (mail.second.first == zone_id) {
      check_additional_remove_list[mail.first] = &mail.second.second;
    }
  }

  for (int i = 0; i < blob_data.pending_to_remove_size(); ++i) {
    PROJECT_NAMESPACE_ID::DMailRecord &record = *blob_data.mutable_pending_to_remove(i);
    auto check_iter = check_additional_remove_list.find(record.mail_id());
    if (check_additional_remove_list.end() != check_iter) {
      // update expired_time if removed
      if (rewrite_db_data) {
        protobuf_copy_message(record, *check_iter->second);
      }
      if (atfw::mail::mail_is_history_removable(ctx, *check_iter->second)) {
        pending_to_remove_.erase(check_iter->first);
        if (rewrite_db_data) {
          pending_to_remove_contents_.insert(check_iter->first);
        }
        FWLOGINFO("[MAIL] global_mail_manager remove mail record ({}, {}) from pending_to_remove set",
                  record.major_type(), check_iter->first);
      }

      check_additional_remove_list.erase(check_iter);
    } else if (atfw::mail::mail_is_history_removable(ctx, record)) {
      pending_to_remove_contents_.insert(record.mail_id());

      FWLOGINFO(
          "[MAIL] global_mail_manager remove mail record ({}, {}) from pending_to_remove set of DB and will remove  "
          "content directly",
          record.major_type(), record.mail_id());
    } else {
      // 直接数据库来的数据，不需要追加误差容忍值
      global_mail_record_map_t::mapped_type &new_pending_record = pending_to_remove_[record.mail_id()];
      new_pending_record.first = zone_id;
      protobuf_copy_message(new_pending_record.second, record);
      FWLOGINFO("[MAIL] global_mail_manager add mail_id ({}, {}) to pending_to_remove set and reset expired time to {}",
                record.major_type(), record.mail_id(), record.expired_time().seconds());
    }
  }

  // 移除过期的移除队列
  if (rewrite_db_data) {
    int old_size = blob_data.pending_to_remove_size();
    protobuf_remove_repeated_if(*blob_data.mutable_pending_to_remove(),
                                [&ctx](const PROJECT_NAMESPACE_ID::DMailRecord &record) {
                                  return atfw::mail::mail_is_history_removable(ctx, record);
                                });

    if (old_size != blob_data.pending_to_remove_size()) {
      ret = true;
    }
  }

  for (auto &mail : check_additional_remove_list) {
    if (atfw::mail::mail_is_history_removable(ctx, *mail.second)) {
      FWLOGINFO(
          "[MAIL] global_mail_manager remove mail record ({}, {}) from pending_to_remove set and will remove content",
          mail.second->major_type(), mail.first);
      pending_to_remove_.erase(mail.first);
      if (rewrite_db_data) {
        pending_to_remove_contents_.insert(mail.first);
      }
    } else if (rewrite_db_data) {
      // 补全本地的过期队列
      protobuf_copy_message(*blob_data.add_pending_to_remove(), *mail.second);
      ret = true;
    }
  }

  return ret;
}

bool global_mail_manager::is_history_removeable(rpc::context & /*ctx*/,
                                                const PROJECT_NAMESPACE_ID::DMailRecord &record) {
  if (global_mail_manager::is_instance_destroyed()) {
    return false;
  }

  if (!record.is_global_mail()) {
    return true;
  }

  if (record.mail_id() == 0 || record.major_type() == 0) {
    return true;
  }

  // 如果处于待删除队列或者已存在在邮件列表中，历史记录都不能移除，要考虑误差容忍值
  if (global_mail_manager::me()->pending_to_remove_.end() !=
      global_mail_manager::me()->pending_to_remove_.find(record.mail_id())) {
    return false;
  }

  if (global_mail_manager::me()->mail_box_id_index_.end() !=
      global_mail_manager::me()->mail_box_id_index_.find(record.mail_id())) {
    return false;
  }

  // 没有记录的话可能是服务器缓存过老，也可能是邮件失效了。
  // 如果是缓存过老那么里上一次完全成功拉取的时间肯定低于邮件发送时间，判定的时候需要再计入误差值
  // 如果最后拉取成功时间-发件时间大于特定值 PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT
  // ,且邮件库中找不到则可以删除
  if (global_mail_manager::me()->last_success_fetch_timepoint_ >
      protobuf_to_system_clock(record.delivery_time()) +
          std::chrono::seconds(PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT)) {
    return true;
  }

  // 可能由于缓存时间差，延期判定
  return false;
}

bool global_mail_manager::is_record_removeable(rpc::context &ctx, const PROJECT_NAMESPACE_ID::DMailRecord &record) {
  if (global_mail_manager::is_instance_destroyed()) {
    return false;
  }

  if (!record.is_global_mail()) {
    return false;
  }

  if (record.mail_id() == 0 || record.major_type() == 0) {
    return true;
  }

  // 检查是否已存在在邮件列表中，直接检查邮件有效性即可
  // 邮件状态要以全服邮件缓存为准，不能以玩家数据为准
  global_mail_box_t::const_iterator iter = global_mail_manager::me()->mail_box_id_index_.find(record.mail_id());
  if (iter != global_mail_manager::me()->mail_box_id_index_.end()) {
    if (!iter->second.second || !iter->second.second->record) {
      FWLOGERROR("[MAIL] mail.record should not be null");
      return true;
    }

    return atfw::mail::mail_is_history_removable(ctx, *iter->second.second->record);
  }

  // 如果处于待删除列表则可以移除
  if (global_mail_manager::me()->pending_to_remove_.end() !=
      global_mail_manager::me()->pending_to_remove_.find(record.mail_id())) {
    return true;
  }

  // 如果已超时 + logic_config::me()->get_const_settings().time_tolerate() 秒则可以删除
  if (ctx.logical_now() >
      (std::max)(protobuf_to_system_clock(record.expired_time()), protobuf_to_system_clock(record.remove_time())) +
          std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate())) {
    return true;
  }

  // 没有记录的话可能是服务器缓存过老，也可能是邮件失效了。
  // 如果是缓存过老那么里上一次完全成功拉取的时间肯定低于邮件发送时间，判定的时候需要再计入误差值
  // 如果最后拉取成功时间-发件时间大于特定值 PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT
  // ,且邮件库中找不到则可以删除
  if (global_mail_manager::me()->last_success_fetch_timepoint_ >
      protobuf_to_system_clock(record.delivery_time()) +
          std::chrono::seconds(PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT)) {
    return true;
  }

  // 可能由于缓存时间差，要后面才能判定是否能删除该邮件
  return false;
}

void global_mail_manager::update_global_mail_record(rpc::context &ctx, PROJECT_NAMESPACE_ID::DMailRecord &dst,
                                                    const PROJECT_NAMESPACE_ID::DMailRecord &src) {
  dst.set_mail_id(src.mail_id());
  dst.set_major_type(src.major_type());
  dst.set_minor_type(src.minor_type());
  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  dst.set_status(dst.status() | src.status());
  dst.set_is_global_mail(true);

  // 结算首次刷新
  if (dst.delivery_time().seconds() <= 0) {
    if (src.resolve_expired_time().seconds() <= 0) {
      protobuf_copy_message(*dst.mutable_delivery_time(), src.delivery_time());
      protobuf_copy_message(*dst.mutable_expired_time(), src.expired_time());
      protobuf_copy_message(*dst.mutable_remove_time(), src.remove_time());
    } else {
      *dst.mutable_delivery_time() = protobuf_from_system_clock(ctx.logical_now());
      *dst.mutable_expired_time() = protobuf_from_system_clock(atfw::mail::mail_padding_day_time(
          protobuf_to_system_clock(dst.delivery_time()) + protobuf_to_system_clock(src.resolve_expired_time())));
      *dst.mutable_remove_time() = protobuf_from_system_clock(
          (std::max)(protobuf_to_system_clock(src.remove_time()), protobuf_to_system_clock(dst.expired_time())));
    }
  }
  protobuf_copy_message(*dst.mutable_start_time(), src.start_time());
  protobuf_copy_message(*dst.mutable_show_time(), src.show_time());

  // 重设过期时间的配置
  protobuf_copy_message(*dst.mutable_resolve_expired_time(), src.resolve_expired_time());
}

int global_mail_manager::add_global_mail(rpc::context &ctx, uint32_t zone_id,
                                         const PROJECT_NAMESPACE_ID::DMailRecord &mail) {
  if (mail.mail_id() == 0 || mail.major_type() == 0) {
    FWLOGERROR("[MAIL] global_mail_manager can not add mail, because mail_id=0 or major_type = 0");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  // 如果在移除队列中则忽略
  if (pending_to_remove_.end() != pending_to_remove_.find(mail.mail_id())) {
    return 0;
  }

  auto type_index_cleanup_guard = make_mail_box_type_index_cleanup_guard();

  global_mail_box_t::iterator mail_box_iter = mail_box_id_index_.find(mail.mail_id());
  auto now = ctx.logical_now();
  if (mail_box_iter != mail_box_id_index_.end()) {
    bool old_is_future = protobuf_to_system_clock(mail_box_iter->second.second->record->start_time()) > now;
    bool new_is_future = protobuf_to_system_clock(mail.start_time()) > now;

    // 已存在直接刷新
    if (mail_box_iter->second.second && mail_box_iter->second.second->record) {
      if (mail_box_iter->second.second->record->status() != mail.status()) {
        force_users_reload_ = true;
      }

      // 保留失败状态
      uint32_t keep_fetch_error_count = mail_box_iter->second.second->record->fetch_error_count();
      protobuf_copy_message(*mail_box_iter->second.second->record, mail);
      mail_box_iter->second.second->record->set_fetch_error_count(keep_fetch_error_count);
    }

    // 如果添加过期全服邮件则直接进删除队列
    if (atfw::mail::mail_is_history_removable(ctx, mail)) {
      remove_global_mail(ctx, mail.mail_id());
    } else if (old_is_future != new_is_future) {
      auto box_iter = mail_box_type_index_[zone_id].find(mail.major_type());
      if (box_iter != mail_box_type_index_[zone_id].end()) {
        if (!box_iter->second) {
          type_index_cleanup_guard->insert(zone_id, mail.major_type());
        } else {
          box_iter->second->future_cache_expire = std::chrono::system_clock::from_time_t(0);
        }
      }
    }

    return 0;
  }

  mail_data_ptr_t mail_data = atfw::memory::stl::make_strong_rc<mail_data_t>();
  if (!mail_data) {
    FWLOGERROR("[MAIL] global_mail_manager can not add mail ({}, {}), because malloc mail_data_ptr_t failed",
               mail.major_type(), mail.mail_id());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  mail_data->record = gsl::make_unique<PROJECT_NAMESPACE_ID::DMailRecord>();
  if (!mail_data->record) {
    FWLOGERROR("[MAIL] global_mail_manager can not add mail ({}, {}), because malloc DMailRecord failed",
               mail.major_type(), mail.mail_id());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  protobuf_copy_message(*mail_data->record, mail);
  global_mail_box_t::mapped_type &idx_index = mail_box_id_index_[mail_data->record->mail_id()];
  idx_index.first = zone_id;
  idx_index.second = mail_data;
  if (!mail_data->content) {
    mail_unloaded_index_[mail_data->record->mail_id()] = mail_data;
  }
  mail_box_ptr_t &mail_box_type_index = mail_box_type_index_[zone_id][mail_data->record->major_type()];
  if (!mail_box_type_index) {
    mail_box_type_index = atfw::memory::stl::make_strong_rc<mail_box_t>();
  }
  mail_box_type_index->mails[mail_data->record->mail_id()] = mail_data;

  FWLOGINFO("[MAIL] global_mail_manager add mail_id ({}, {}) success", mail.major_type(), mail.mail_id());

  if (protobuf_to_system_clock(mail_data->record->start_time()) > now) {
    ++mail_box_type_index->future_mail_count;
    if (protobuf_to_system_clock(mail_data->record->start_time()) < mail_box_type_index->future_cache_expire) {
      mail_box_type_index->future_cache_expire = protobuf_to_system_clock(mail_data->record->start_time());
    }
  }

  // 如果本来就是已过期则加入到移除列表
  if (atfw::mail::mail_is_history_removable(ctx, mail)) {
    remove_global_mail(ctx, mail.mail_id());
  }

  refresh_mail_box_future_cache(ctx, *mail_box_type_index);

  int32_t future_reserve_count = excel::get_const_config().global_future_reserve_mail_max_count_per_major_type();
  if (future_reserve_count > mail_box_type_index->future_mail_count) {
    future_reserve_count = mail_box_type_index->future_mail_count;
  }
  if (future_reserve_count < 0) {
    future_reserve_count = 0;
  }

  int32_t max_mail_count = excel::get_const_config().global_mail_max_count_per_major_type();
  if (max_mail_count > 0 && mail_box_type_index->mails.size() >
                                static_cast<size_t>(max_mail_count) + static_cast<size_t>(future_reserve_count)) {
    compact_mails(ctx, *mail_box_type_index);
  }
  return 0;
}

bool global_mail_manager::update_global_mail(rpc::context &ctx, uint32_t zone_id,
                                             const PROJECT_NAMESPACE_ID::DMailRecord &mail) {
  if (mail.mail_id() == 0 || mail.major_type() == 0) {
    FWLOGERROR("[MAIL] global_mail_manager can not add mail, because mail_id={}, major_type={}", mail.mail_id(),
               mail.major_type());
    return false;
  }

  // 如果在移除队列中则忽略
  if (pending_to_remove_.end() != pending_to_remove_.find(mail.mail_id())) {
    return false;
  }

  global_mail_box_t::iterator mail_box_iter = mail_box_id_index_.find(mail.mail_id());
  if (mail_box_iter == mail_box_id_index_.end()) {
    return false;
  }

  auto now = ctx.logical_now();
  bool old_is_future = protobuf_to_system_clock(mail_box_iter->second.second->record->start_time()) > now;
  bool new_is_future = protobuf_to_system_clock(mail.start_time()) > now;
  // 直接刷新
  if (mail_box_iter->second.second && mail_box_iter->second.second->record) {
    if (mail_box_iter->second.second->record->status() != mail.status()) {
      force_users_reload_ = true;
    }

    // 保留失败状态
    uint32_t keep_fetch_error_count = mail_box_iter->second.second->record->fetch_error_count();
    protobuf_copy_message(*mail_box_iter->second.second->record, mail);
    mail_box_iter->second.second->record->set_fetch_error_count(keep_fetch_error_count);
  }

  auto type_index_cleanup_guard = make_mail_box_type_index_cleanup_guard();

  // 如果过期全服邮件则直接进删除队列
  if (atfw::mail::mail_is_history_removable(ctx, mail)) {
    remove_global_mail(ctx, mail.mail_id());
  } else if (old_is_future != new_is_future) {
    auto box_iter = mail_box_type_index_[zone_id].find(mail.major_type());
    if (box_iter != mail_box_type_index_[zone_id].end()) {
      if (!box_iter->second) {
        type_index_cleanup_guard->insert(zone_id, mail.major_type());
      } else {
        box_iter->second->future_cache_expire = std::chrono::system_clock::from_time_t(0);
      }
    }
  }

  return true;
}

void global_mail_manager::resolve_limits() {}

void global_mail_manager::refresh_mail_box_future_cache(rpc::context &ctx, mail_box_t &mail_box) {
  auto now = ctx.logical_now();
  if (now < mail_box.future_cache_expire) {
    return;
  }

  // NOLINTNEXTLINE(readability-redundant-parentheses)
  mail_box.future_cache_expire = (std::chrono::system_clock::time_point::max)();
  mail_box.future_mail_count = 0;
  for (auto &mail : mail_box.mails) {
    if (!mail.second) {
      continue;
    }
    if (!mail.second->record) {
      continue;
    }

    auto start_time = protobuf_to_system_clock(mail.second->record->start_time());
    if (start_time <= now) {
      continue;
    }

    if (start_time < mail_box.future_cache_expire) {
      mail_box.future_cache_expire = start_time;
    }
    ++mail_box.future_mail_count;
  }
}

void global_mail_manager::compact_mails(rpc::context &ctx, mail_box_t &mail_box) {
  refresh_mail_box_future_cache(ctx, mail_box);

  if (mail_box.mails.empty() || excel::get_const_config().global_mail_max_count_per_major_type() <= 0) {
    return;
  }

  int32_t future_reserve_count = excel::get_const_config().global_future_reserve_mail_max_count_per_major_type();
  if (future_reserve_count > mail_box.future_mail_count) {
    future_reserve_count = mail_box.future_mail_count;
  }
  if (future_reserve_count < 0) {
    future_reserve_count = 0;
  }

  size_t max_mail_count = static_cast<size_t>(excel::get_const_config().global_mail_max_count_per_major_type());
  max_mail_count += static_cast<size_t>(future_reserve_count);
  // 按类型只保留一定数量的邮件
  if (mail_box.mails.size() <= max_mail_count) {
    return;
  }

  // 用于检查未来生效的邮件，防止起始生效时间异常过高而无法删除
  auto delivery_time_max_offset =
      protobuf_to_system_clock(logic_config::me()->get_logic_cfg().mail().compact_delivery_time_max_offset());
  if (delivery_time_max_offset <= std::chrono::seconds(0)) {
    delivery_time_max_offset = std::chrono::seconds(30 * atfw::util::time::time_utility::DAY_SECONDS);
  }

  auto now = ctx.logical_now();
  // 全服邮件都是一个一个加进来的，没有全量load流程，所以都是走简单算法，反正数量也不会很多
  std::vector<int64_t> pending_to_remove;
  pending_to_remove.reserve(mail_box.mails.size() - max_mail_count);
  size_t rmcnt = mail_box.mails.size() - max_mail_count;

  FWLOGINFO(
      "[MAIL] global_mail_manager start to compact {} mails, sum mail count: {}, future mail count: {}, limit: {}, "
      "future limit: {}",
      rmcnt, mail_box.mails.size(), mail_box.future_mail_count,
      excel::get_const_config().global_mail_max_count_per_major_type(),
      excel::get_const_config().global_future_reserve_mail_max_count_per_major_type());

  for (size_t i = 0; i < rmcnt && mail_box.mails.size() > max_mail_count; ++i) {
    int64_t select_mail_id = 0;
    std::chrono::system_clock::time_point select_mail_compact_time = std::chrono::system_clock::from_time_t(0);

    for (auto &mail : mail_box.mails) {
      if (!mail.second || !mail.second->record) {
        pending_to_remove.push_back(mail.first);
        continue;
      }

      if ((std::max)(protobuf_to_system_clock(mail.second->record->expired_time()),
                     protobuf_to_system_clock(mail.second->record->remove_time())) < now) {
        pending_to_remove.push_back(mail.first);
        continue;
      }

      auto compact_time = protobuf_to_system_clock(mail.second->record->start_time());
      if (protobuf_to_system_clock(mail.second->record->delivery_time()) > compact_time) {
        compact_time = protobuf_to_system_clock(mail.second->record->delivery_time());
      } else if (compact_time >
                 protobuf_to_system_clock(mail.second->record->delivery_time()) + delivery_time_max_offset) {
        compact_time = protobuf_to_system_clock(mail.second->record->delivery_time()) + delivery_time_max_offset;
      }

      if (select_mail_id == 0 || compact_time < select_mail_compact_time) {
        select_mail_compact_time = compact_time;
        select_mail_id = mail.first;
      }
    }

    if (select_mail_id != 0 && mail_box.mails.size() > max_mail_count + pending_to_remove.size()) {
      pending_to_remove.push_back(select_mail_id);
    }

    for (int64_t mail_id : pending_to_remove) {
      remove_global_mail(ctx, mail_id);
    }
    pending_to_remove.clear();
  }
}

mail_data_ptr_t global_mail_manager::get_mail_raw(int64_t mail_id) const {
  auto iter = mail_box_id_index_.find(mail_id);
  if (iter == mail_box_id_index_.end()) {
    return nullptr;
  }

  return iter->second.second;
}

mail_box_const_ptr_t global_mail_manager::get_mail_box_by_type(uint32_t zone_id, int32_t major_type) const {
  return atfw::util::memory::const_pointer_cast<const mail_box_t>(
      const_cast<global_mail_manager *>(this)->internal_get_mail_box_by_type(zone_id, major_type));
}

mail_box_ptr_t global_mail_manager::internal_get_mail_box_by_type(uint32_t zone_id, int32_t major_type) {
  auto type_zone_iter = mail_box_type_index_.find(zone_id);
  if (type_zone_iter == mail_box_type_index_.end()) {
    return nullptr;
  }

  auto type_iter = type_zone_iter->second.find(major_type);
  if (type_iter == type_zone_iter->second.end()) {
    return nullptr;
  }

  return type_iter->second;
}

void global_mail_manager::remove_global_mail(rpc::context &ctx, int64_t mail_id) {
  auto id_iter = mail_box_id_index_.find(mail_id);
  if (id_iter == mail_box_id_index_.end()) {
    return;
  }

  uint32_t zone_id = id_iter->second.first;
  if (!id_iter->second.second || !id_iter->second.second->record) {
    FWLOGERROR("[MAIL] mail.record should not be null");
    mail_box_id_index_.erase(id_iter);
    mail_unloaded_index_.erase(mail_id);
    return;
  }

  auto type_index_cleanup_guard = make_mail_box_type_index_cleanup_guard();

  force_users_reload_ = true;
  {
    auto type_zone_iter = mail_box_type_index_.find(zone_id);
    if (type_zone_iter == mail_box_type_index_.end()) {
      FWLOGERROR("[MAIL] global_mail_manager can not find type index of zone {} for mail {}", zone_id, mail_id);
    } else {
      auto major_type = id_iter->second.second->record->major_type();
      auto type_iter = type_zone_iter->second.find(major_type);
      if (type_iter == type_zone_iter->second.end()) {
        FWLOGERROR("[MAIL] global_mail_manager can not find type index of type {} for mail {}", major_type, mail_id);
      } else if (!type_iter->second) {
        type_index_cleanup_guard->insert(zone_id, major_type);
      } else {
        // 重置未来邮件缓存
        if (type_iter->second->future_cache_expire <=
            protobuf_to_system_clock(id_iter->second.second->record->start_time())) {
          type_iter->second->future_cache_expire = std::chrono::system_clock::from_time_t(0);
        }

        type_iter->second->mails.erase(mail_id);
        if (type_iter->second->mails.empty()) {
          type_index_cleanup_guard->insert(zone_id, major_type);
        }
      }
    }
  }

  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  id_iter->second.second->record->set_status(id_iter->second.second->record->status() |
                                             PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED);
  // 重设失效时间为当前时间+容忍误差时间,之后可以删除该记录
  auto remove_time = ctx.logical_now();
  remove_time += std::chrono::seconds(PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT);
  if (protobuf_to_system_clock(id_iter->second.second->record->expired_time()) > remove_time) {
    *id_iter->second.second->record->mutable_expired_time() = protobuf_from_system_clock(remove_time);
  }
  if (protobuf_to_system_clock(id_iter->second.second->record->remove_time()) > remove_time) {
    *id_iter->second.second->record->mutable_remove_time() = protobuf_from_system_clock(remove_time);
  }

  FWLOGINFO("[MAIL] global_mail_manager remove mail_id ({}, {}) and reset expired time to {} success",
            id_iter->second.second->record->major_type(), id_iter->second.second->record->mail_id(),
            id_iter->second.second->record->expired_time().seconds());

  global_mail_record_map_t::mapped_type &mail_to_remove = pending_to_remove_[mail_id];
  mail_to_remove.first = zone_id;
  protobuf_copy_message(mail_to_remove.second, *id_iter->second.second->record);
  // 首次加入待移除队列时增加误差容忍值
  if (mail_to_remove.second.remove_time().seconds() > 0) {
    *mail_to_remove.second.mutable_remove_time() =
        protobuf_from_system_clock(protobuf_to_system_clock(mail_to_remove.second.remove_time()) +
                                   std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate()));
  } else if (mail_to_remove.second.expired_time().seconds() > 0) {
    *mail_to_remove.second.mutable_expired_time() =
        protobuf_from_system_clock(protobuf_to_system_clock(mail_to_remove.second.expired_time()) +
                                   std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate()));
  }

  mail_unloaded_index_.erase(id_iter->first);
  mail_box_id_index_.erase(id_iter);
}

void global_mail_manager::fetch_all_unloaded_mails(rpc::context &ctx, std::vector<int64_t> &out) {
  std::vector<int64_t> invalid_mail_ids;
  std::vector<int64_t> pending_to_remove;
  out.reserve(mail_unloaded_index_.size());

  // 这里面可能保护全服邮件，如果用户切换gamesvr节点，然后新的gamesvr的全服邮件缓存晚于老的，那么可能刷全服邮件cache的时候没刷进来
  // 这时候就fallback成直接从数据库拉取
  for (auto &checked : mail_unloaded_index_) {
    if (checked.second && checked.second->record && !checked.second->content) {
      // 如果是全服邮件的话检查一下是否已经失效
      if (global_mail_manager::is_record_removeable(ctx, *checked.second->record)) {
        invalid_mail_ids.push_back(checked.first);
        pending_to_remove.push_back(checked.first);
      } else {
        out.push_back(checked.first);
      }
    } else {
      invalid_mail_ids.push_back(checked.first);
    }
  }

  for (auto &invalid_mail_id : invalid_mail_ids) {
    mail_unloaded_index_.erase(invalid_mail_id);
  }

  for (auto &mail_id : pending_to_remove) {
    remove_global_mail(ctx, mail_id);
  }
}

void global_mail_manager::set_mail_content_loaded(int64_t mail_id) {
  auto iter = mail_unloaded_index_.find(mail_id);
  if (iter != mail_unloaded_index_.end()) {
    mail_unloaded_index_.erase(iter);
    force_users_reload_ = true;
  }
}

atfw::util::memory::strong_rc_ptr<global_mail_manager::global_mail_box_type_index_cleanup_guard>
global_mail_manager::make_mail_box_type_index_cleanup_guard() {
  atfw::util::memory::strong_rc_ptr<global_mail_manager::global_mail_box_type_index_cleanup_guard> ret;
  if (!mail_box_type_index_ref_.expired()) {
    ret = mail_box_type_index_ref_.lock();
  }

  if (ret) {
    return ret;
  }

  ret = atfw::memory::stl::make_strong_rc<global_mail_box_type_index_cleanup_guard>(*this);
  return ret;
}

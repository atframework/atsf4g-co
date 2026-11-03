// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#include "logic/mail/user_mail_manager.h"

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <string/utf8_char_t.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.mail.common.pb.h>
#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <utility/protobuf_mini_dumper.h>

#include <config/excel/config_easy_api.h>
#include <config/excel_config_const_index.h>
#include <config/excel_config_wrapper.h>
#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <logic/mail/mail_utils.h>

#include <rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h>
#include <rpc/mail/global_mail.h>
#include <rpc/mail/user_mail.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>

#include <logic/mail/task_action_mail_async_jobs.h>

#include <algorithm>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/user.h"
// #include "logic/condition/user_condition_manager.h"
#include "logic/async_jobs/task_action_user_remote_patch_jobs.h"
#include "logic/item/user_item_manager.h"
#include "logic/mail/global_mail_manager.h"
#include "logic/user/task_action_user_gm_cmd_nomsg.h"

namespace {

static void global_mail_gm_add_global_mail(const std::shared_ptr<rpc::context> &ctx, const user_ptr_t &user_inst,
                                           const std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> &rsp_body,
                                           atfw::util::cli::cmd_option_list &params) {
  if (!user_inst) {
    rsp_body->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    return;
  }
  if (!task_action_user_gm_cmd_nomsg::check_params_number(params, 1)) {
    return;
  }

  uint32_t zone_id = user_inst->get_zone_id();
  if (params.get_params_number() > 1) {
    zone_id = params[1]->to_uint32();
  }

  bool render_by_client = true;

  excel_config_block_report_t block_cfg_error;
  int32_t mail_template_id = params[0]->to_int32();
  auto async_get_ret = rpc::async_invoke(
      *ctx, "global_mail_gm.add_global_mail",
      [zone_id, mail_template_id, render_by_client](rpc::context &child_ctx) -> rpc::result_code_type {
        int32_t ret = RPC_AWAIT_CODE_RESULT(::rpc::mail::add_global_mail_with_template(
            child_ctx, mail_template_id, nullptr, zone_id, render_by_client));
        if (ret != 0) {
          FCTXLOGERROR(child_ctx, "on_gm_cmd_add_global_mail failed res {}, mail_template_id {}, zone_id{} ", ret,
                       mail_template_id, zone_id);
        }
        RPC_RETURN_CODE(ret);
      });
  if (async_get_ret.is_error()) {
    FCTXLOGERROR(*ctx, "{} async_invoke a task to add global mail failed.res: {}({})", *user_inst,
                 *async_get_ret.get_error(), protobuf_mini_dumper_get_error_msg(*async_get_ret.get_error()));
    rsp_body->set_result_code(::PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }
}

static void global_mail_gm_remove_global_mail(const std::shared_ptr<rpc::context> &ctx, const user_ptr_t &user_inst,
                                              const std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> &rsp_body,
                                              atfw::util::cli::cmd_option_list &params) {
  if (!user_inst) {
    rsp_body->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    return;
  }
  if (!task_action_user_gm_cmd_nomsg::check_params_number(params, 2)) {
    return;
  }

  uint32_t zone_id = params[0]->to_uint32();
  int64_t mail_id = params[1]->to_int64();

  auto async_get_ret = rpc::async_invoke(
      *ctx, "global_mail_gm.remove_global_mail", [zone_id, mail_id](rpc::context &child_ctx) -> rpc::result_code_type {
        int32_t ret = RPC_AWAIT_CODE_RESULT(::rpc::mail::remove_global_mail(child_ctx, zone_id, mail_id));
        RPC_RETURN_CODE(ret);
      });
  if (async_get_ret.is_error()) {
    FCTXLOGERROR(*ctx, "{} async_invoke a task to remove global mail failed.res: {}({})", *user_inst,
                 *async_get_ret.get_error(), protobuf_mini_dumper_get_error_msg(*async_get_ret.get_error()));
    rsp_body->set_result_code(::PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }
}

static void user_mail_gm_add_user_mail(const std::shared_ptr<rpc::context> &ctx, const user_ptr_t &user_inst,
                                       const std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> &rsp_body,
                                       atfw::util::cli::cmd_option_list &params) {
  if (!user_inst) {
    rsp_body->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    return;
  }
  if (!task_action_user_gm_cmd_nomsg::check_params_number(params, 1)) {
    return;
  }

  int32_t mail_template_id = params[0]->to_int32();
  auto async_get_ret = rpc::async_invoke(
      *ctx, "user_mail_gm.add_user_mail",
      [user_inst, mail_template_id](rpc::context &child_ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::DMailUserInfo *receiver = child_ctx.create<PROJECT_NAMESPACE_ID::DMailUserInfo>();
        PROJECT_NAMESPACE_ID::DMailUserInfo local_receiver;
        if (nullptr == receiver) {
          receiver = &local_receiver;
        }
        user_inst->get_user_mail_manager().pack_mail_user(*receiver);
        int32_t res = RPC_AWAIT_CODE_RESULT(::rpc::mail::add_user_mail_with_template(
            child_ctx, mail_template_id, user_inst->get_zone_id(), *receiver, nullptr, true));
        if (res != 0) {
          FCTXLOGERROR(child_ctx, "on_gm_cmd_add_user_mail failed res {}", res);
        }
        RPC_RETURN_CODE(res);
      });
  if (async_get_ret.is_error()) {
    FCTXLOGERROR(*ctx, "{} async_invoke a task to send user mail failed.res: {}({})", *user_inst,
                 *async_get_ret.get_error(), protobuf_mini_dumper_get_error_msg(*async_get_ret.get_error()));
    rsp_body->set_result_code(::PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }
}

static void init_user_mail_manager_gm_handle() {
  task_action_user_gm_cmd_nomsg::register_gm_command(
      "add-global-mail", global_mail_gm_add_global_mail,
      ATFW_UTIL_MACRO_UTF8_TO_ZSTRING(
          "add-global-mail <mail template ID> [zone ID]                      Send global mail."));
  task_action_user_gm_cmd_nomsg::register_gm_command(
      "remove-global-mail", global_mail_gm_remove_global_mail,
      ATFW_UTIL_MACRO_UTF8_TO_ZSTRING(
          "remove-global-mail <zone ID> <mail ID>                            Remove global mail."));

  task_action_user_gm_cmd_nomsg::register_gm_command(
      "add-user-mail", user_mail_gm_add_user_mail,
      ATFW_UTIL_MACRO_UTF8_TO_ZSTRING(
          "add-user-mail <mail template ID>                                  Send user mail."));
}

static bool init_user_mail_manager_handle() {
  task_action_user_remote_patch_jobs::register_sync_callbacks(
      static_cast<int32_t>(PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::kAddMail),
      [](rpc::context &ctx, user &user_inst, int32_t /*job_type*/,
         // NOLINTNEXTLINE(performance-unnecessary-value-param)
         task_action_user_remote_patch_jobs::async_job_ptr_type job_data) -> int32_t {
        const PROJECT_NAMESPACE_ID::user_async_job_add_mail &add_mail = job_data->add_mail();
        return user_inst.get_user_mail_manager().add_mail(ctx, add_mail.mail_record());
      });

  task_action_user_remote_patch_jobs::register_sync_callbacks(
      static_cast<int32_t>(PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::kRemoveMail),
      [](rpc::context &ctx, user &user_inst, int32_t /*job_type*/,
         // NOLINTNEXTLINE(performance-unnecessary-value-param)
         task_action_user_remote_patch_jobs::async_job_ptr_type job_data) -> int32_t {
        const PROJECT_NAMESPACE_ID::user_async_job_add_mail &remove_mail = job_data->remove_mail();
        if (0 == remove_mail.mail_record().mail_id()) {
          return 0;
        }

        int32_t ret = user_inst.get_user_mail_manager().remove_mail(ctx, remove_mail.mail_record().mail_id());
        if (PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND == ret) {
          ret = 0;
        }
        return ret;
      });

  init_user_mail_manager_gm_handle();
  return true;
}
}  // namespace

user_mail_manager::user_mail_manager(user &owner)
    : owner_(&owner),
      is_dirty_(false),
      is_global_mails_merged_(false),
      lazy_save_counter_(0),
      mail_async_task_protect_timepoint_(std::chrono::system_clock::from_time_t(0)) {
  ATFW_EXPLICIT_UNUSED_ATTR static bool init_handle = init_user_mail_manager_handle();
}

user_mail_manager::~user_mail_manager() {}

void user_mail_manager::create_init(rpc::context &) {}

void user_mail_manager::login_init(rpc::context &) {}

void user_mail_manager::refresh_feature_limit_second(rpc::context &ctx) {
  if (need_start_async_jobs()) {
    try_to_start_async_jobs(ctx);
  }
}

void user_mail_manager::init_from_table_data(rpc::context &ctx, const PROJECT_NAMESPACE_ID::table_user &user_table) {
  if (user_table.has_mail_data()) {
    const PROJECT_NAMESPACE_ID::user_mail_data &mail_data = user_table.mail_data();
    mail_box_id_index_.clear();
    mail_box_type_index_.clear();
    pending_to_remove_.clear();
    mail_box_unloaded_index_.clear();
    received_global_mails_.clear();

    mail_box_id_index_.reserve(static_cast<size_t>(mail_data.mail_box_size()));
    mail_box_unloaded_index_.reserve(static_cast<size_t>(mail_data.mail_box_size()));
    for (int i = 0; i < mail_data.mail_box_size(); ++i) {
      mail_data_ptr_t mail = atfw::memory::stl::make_strong_rc<mail_data_t>();
      if (!mail) {
        FWLOGERROR("malloc mail_data_t failed");
        break;
      }

      mail->record = gsl::make_unique<PROJECT_NAMESPACE_ID::DMailRecord>();
      if (!mail->record) {
        FWLOGERROR("malloc DMailRecord failed");
        break;
      }

      protobuf_copy_message(*mail->record, mail_data.mail_box(i));

      mail_box_id_index_[mail->record->mail_id()] = mail;
      auto &mail_type_box = mail_box_type_index_[mail->record->major_type()];
      if (!mail_type_box) {
        mail_type_box = atfw::memory::stl::make_strong_rc<mail_box_t>();
        mail_type_box->future_mail_count = 0;
        mail_type_box->future_cache_expire = std::chrono::system_clock::from_time_t(0);
      }
      if (mail_type_box) {
        (*mail_type_box).mails[mail->record->mail_id()] = mail;
      }

      if (!mail->content) {
        mail_box_unloaded_index_[mail->record->mail_id()] = mail;
      }
    }

    pending_to_remove_.reserve(static_cast<size_t>(mail_data.pending_to_remove_size()));
    for (int i = 0; i < mail_data.pending_to_remove_size(); ++i) {
      protobuf_copy_message(pending_to_remove_[mail_data.pending_to_remove(i).mail_id()],
                            mail_data.pending_to_remove(i));
    }

    received_global_mails_.reserve(static_cast<size_t>(mail_data.received_global_mails().size()));
    for (int i = 0; i < mail_data.received_global_mails().size(); ++i) {
      protobuf_copy_message(received_global_mails_[mail_data.received_global_mails(i).mail_id()],
                            mail_data.received_global_mails(i));
    }

    is_dirty_ = false;
    is_global_mails_merged_ = false;

    // 移除过多的邮件
    compact_mails(ctx);

    mail_async_task_protect_timepoint_ = std::chrono::system_clock::from_time_t(0);
  }
}

int user_mail_manager::dump(rpc::context &ctx, PROJECT_NAMESPACE_ID::table_user &user) {
  // 检查过期
  scan_and_remove_expired_mails(ctx);

  PROJECT_NAMESPACE_ID::user_mail_data *mail_data = user.mutable_mail_data();
  if (NULL == mail_data) {
    FWLOGERROR("{} malloc user_mail_data failed", *owner_);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  mail_data->clear_mail_box();
  mail_data->clear_pending_to_remove();
  mail_data->clear_received_global_mails();

  for (auto &mail : mail_box_id_index_) {
    if (mail.second && mail.second->record) {
      protobuf_copy_message(*mail_data->add_mail_box(), *mail.second->record);
    }
  }

  for (auto &mail : pending_to_remove_) {
    protobuf_copy_message(*mail_data->add_pending_to_remove(), mail.second);
  }

  for (auto &mail : received_global_mails_) {
    protobuf_copy_message(*mail_data->add_received_global_mails(), mail.second);
  }

  return 0;
}

bool user_mail_manager::is_dirty() const { return is_dirty_; }

void user_mail_manager::clear_dirty() { is_dirty_ = false; }

bool user_mail_manager::is_async_task_running() const {
  if (task_type_trait::empty(mail_async_task_)) {
    return false;
  }

  if (task_type_trait::is_exiting(mail_async_task_)) {
    task_type_trait::reset_task(mail_async_task_);
    return false;
  }

  return true;
}

rpc::result_code_type user_mail_manager::wait_for_async_task(rpc::context &ctx) {
  if (!is_async_task_running()) {
    RPC_RETURN_CODE(0);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, mail_async_task_)));
}

void user_mail_manager::reset_global_mails_cache() {
  is_global_mails_merged_ = false;

  // reset protect timeout and try to start the merge global task
  mail_async_task_protect_timepoint_ = std::chrono::system_clock::from_time_t(0);
}

mail_data_ptr_t user_mail_manager::get_mail_raw(int64_t mail_id) const {
  auto iter = mail_box_id_index_.find(mail_id);
  if (iter == mail_box_id_index_.end()) {
    return nullptr;
  }

  return iter->second;
}

atfw::util::memory::strong_rc_ptr<mail_box_t> user_mail_manager::get_mail_box_by_major_type(int32_t major_type) const {
  auto type_iter = mail_box_type_index_.find(major_type);
  if (mail_box_type_index_.end() == type_iter) {
    return nullptr;
  }

  return type_iter->second;
}

int user_mail_manager::add_mail(rpc::context &ctx, const PROJECT_NAMESPACE_ID::DMailRecord &mail,
                                const PROJECT_NAMESPACE_ID::DMailContent *ATFW_UTIL_MACRO_NULLABLE content) {
  if (mail.mail_id() == 0 || mail.major_type() == 0) {
    FCTXLOGERROR(ctx, "{} can not add mail, because mail_id=0 or major_type = 0", *owner_);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  auto iter = mail_box_id_index_.find(mail.mail_id());
  if (iter != mail_box_id_index_.end()) {
    FCTXLOGWARNING(ctx, "{} add mail {} twice", *owner_, mail.mail_id());
    auto now = ctx.logical_now();
    bool old_is_future = false;
    bool new_is_future = protobuf_to_system_clock(mail.start_time()) > now;
    if (iter->second && iter->second->record) {
      old_is_future = protobuf_to_system_clock(iter->second->record->start_time()) > now;

      // 保留失败状态
      uint32_t keep_fetch_error_count = iter->second->record->fetch_error_count();
      if (mail.is_global_mail()) {
        global_mail_manager::update_global_mail_record(ctx, *iter->second->record, mail);
      } else {
        protobuf_copy_message(*iter->second->record, mail);
      }
      iter->second->record->set_fetch_error_count(keep_fetch_error_count);
    }

    // 添加已过期的邮件则直接进删除队列
    if (mail.is_global_mail()) {
      if (global_mail_manager::is_record_removeable(ctx, mail) || atfw::mail::mail_is_expired_or_removed(ctx, mail)) {
        remove_user_mail(ctx, mail.mail_id());
      }
    } else if (atfw::mail::mail_is_history_removable(ctx, mail)) {
      remove_user_mail(ctx, mail.mail_id());
    } else if (old_is_future != new_is_future) {
      // 邮件起始时间变化，标记未来邮件缓存失效
      auto mail_box_ptr = get_mail_box_by_major_type(mail.major_type());
      if (mail_box_ptr) {
        mail_box_ptr->future_cache_expire = std::chrono::system_clock::from_time_t(0);
      }
    }
    return 0;
  }

  // 如果在移除队列中则忽略
  if (pending_to_remove_.end() != pending_to_remove_.find(mail.mail_id())) {
    return 0;
  }

  // 本身就是过期邮件则忽略
  if (mail.is_global_mail()) {
    if (atfw::mail::mail_is_expired_or_removed(ctx, mail) || global_mail_manager::is_record_removeable(ctx, mail)) {
      return 0;
    }
  } else if (atfw::mail::mail_is_history_removable(ctx, mail)) {
    return 0;
  }

  mail_data_ptr_t mail_data = atfw::memory::stl::make_strong_rc<mail_data_t>();
  if (!mail_data) {
    FCTXLOGERROR(ctx, "{} can not add mail ({}, {}), because malloc mail_data_ptr_t failed", *owner_, mail.major_type(),
                 mail.mail_id());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  mail_data->record = gsl::make_unique<PROJECT_NAMESPACE_ID::DMailRecord>();
  if (!mail_data->record) {
    FCTXLOGERROR(ctx, "{} can not add mail ({}, {}), because malloc DMailRecord failed", *owner_, mail.major_type(),
                 mail.mail_id());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  if (NULL != content) {
    mail_data->content = gsl::make_unique<PROJECT_NAMESPACE_ID::DMailContent>();
    if (mail_data->content) {
      protobuf_copy_message(*mail_data->content, *content);
    } else {
      FCTXLOGERROR(ctx, "{} add mail ({}, {}) but malloc DMailContent failed", *owner_, mail.major_type(),
                   mail.mail_id());
    }
  }

  if (mail.is_global_mail()) {
    global_mail_manager::update_global_mail_record(ctx, *mail_data->record, mail);
  } else {
    protobuf_copy_message(*mail_data->record, mail);
  }
  mail_box_id_index_[mail_data->record->mail_id()] = mail_data;
  if (!mail_data->content) {
    mail_box_unloaded_index_[mail_data->record->mail_id()] = mail_data;

    // reset protect timeout and try to start the pull content task
    mail_async_task_protect_timepoint_ = std::chrono::system_clock::from_time_t(0);
  }
  auto &mail_box_type_index = mail_box_type_index_[mail_data->record->major_type()];
  if (!mail_box_type_index) {
    mail_box_type_index = atfw::memory::stl::make_strong_rc<mail_box_t>();
    mail_box_type_index->future_mail_count = 0;
    mail_box_type_index->future_cache_expire = std::chrono::system_clock::from_time_t(0);
  }
  if (mail_box_type_index) {
    (*mail_box_type_index).mails[mail_data->record->mail_id()] = mail_data;

    // 未来邮件索引
    if (protobuf_to_system_clock(mail_data->record->start_time()) > ctx.logical_now()) {
      ++(*mail_box_type_index).future_mail_count;
      if (protobuf_to_system_clock(mail_data->record->start_time()) < (*mail_box_type_index).future_cache_expire) {
        (*mail_box_type_index).future_cache_expire = protobuf_to_system_clock(mail_data->record->start_time());
      }
    }
  }

  is_dirty_ = true;
  FCTXLOGDEBUG(ctx, "{} add mail ({}, {}) success", *owner_, mail.major_type(), mail.mail_id());

  mutable_dirty_mail(*mail_data->record, true);

  // 如果本来就是已过期则加入到移除列表
  if (mail.is_global_mail()) {
    if (global_mail_manager::is_record_removeable(ctx, mail) || atfw::mail::mail_is_expired_or_removed(ctx, mail)) {
      remove_user_mail(ctx, mail.mail_id());
    }
  } else if (atfw::mail::mail_is_history_removable(ctx, mail)) {
    remove_user_mail(ctx, mail.mail_id());
  }

  if (mail_box_type_index) {
    refresh_mail_box_future_cache(ctx, *mail_box_type_index);

    int32_t future_reserve_count = excel::get_const_config().user_future_reserve_mail_max_count_per_major_type();
    if (future_reserve_count > mail_box_type_index->future_mail_count) {
      future_reserve_count = mail_box_type_index->future_mail_count;
    }
    if (future_reserve_count < 0) {
      future_reserve_count = 0;
    }

    int32_t max_mail_count = excel::get_const_config().user_mail_max_count_per_major_type();
    if (max_mail_count > 0 && mail_box_type_index->mails.size() >
                                  static_cast<size_t>(max_mail_count) + static_cast<size_t>(future_reserve_count)) {
      compact_mails(ctx, *mail_box_type_index);
    }
  }

  return 0;
}

int user_mail_manager::add_global_mail(rpc::context &ctx, const PROJECT_NAMESPACE_ID::DMailRecord &mail,
                                       const PROJECT_NAMESPACE_ID::DMailContent &content) {
  if (!mail.is_global_mail()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  }

  mail_record_map_t::iterator global_iter = received_global_mails_.find(mail.mail_id());
  if (received_global_mails_.end() != global_iter) {
    // 已存在则刷新数据
    global_mail_manager::update_global_mail_record(ctx, global_iter->second, mail);

    // 没删除的话也刷新邮件record
    auto iter = mail_box_id_index_.find(mail.mail_id());
    if (iter != mail_box_id_index_.end()) {
      if (iter->second && iter->second->record) {
        global_mail_manager::update_global_mail_record(ctx, *iter->second->record, mail);
      }

      // 添加已过期的邮件则直接进删除队列
      if (global_mail_manager::is_record_removeable(ctx, mail) || atfw::mail::mail_is_expired_or_removed(ctx, mail)) {
        remove_user_mail(ctx, mail.mail_id());
      }
      return 0;
    }

    return 0;
  }

  int ret = add_mail(ctx, mail, &content);
  if (ret < 0) {
    return ret;
  }

  received_global_mails_[mail.mail_id()] = mail;

  // FIXME: OSS Log
  return ret;
}

int user_mail_manager::remove_mail(rpc::context &ctx, int64_t mail_id,
                                   PROJECT_NAMESPACE_ID::DMailOperationResult *ATFW_UTIL_MACRO_NULLABLE out) {
  mail_data_ptr_t mail = get_mail_raw(mail_id);
  if (!mail || !mail->record) {
    if (nullptr != out) {
      out->set_result(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND);
    }
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
  }

  remove_user_mail(ctx, mail_id);
  if (nullptr != out) {
    protobuf_copy_message(*out->mutable_record(), *mail->record);
  }

  if (nullptr != out) {
    out->mutable_record()->set_mail_id(mail_id);
    out->set_result(0);
  }
  return 0;
}

int user_mail_manager::read_mail(rpc::context &ctx, int64_t mail_id,
                                 PROJECT_NAMESPACE_ID::DMailOperationResult *ATFW_UTIL_MACRO_NULLABLE out,
                                 bool need_remove) {
  int ret = 0;
  mail_data_ptr_t mail = get_mail_raw(mail_id);
  do {
    if (!mail || !mail->content || !mail->record) {
      if (!need_remove) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
      }
      break;
    }

    ret = atfw::mail::mail_is_valid(ctx, *mail->content);
    if (ret < 0) {
      if (ret == PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND && need_remove) {
        ret = 0;
      }
      break;
    }

    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    if (0 == (mail->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_READ)) {
      // NOLINTNEXTLINE(bugprone-signed-bitwise)
      mail->record->set_status(mail->record->status() | PROJECT_NAMESPACE_ID::EN_MAIL_ST_READ);

      if (need_remove) {
        remove_user_mail(ctx, mail_id);
      } else {
        mutable_dirty_mail(*mail->record);
      }

      // FIXME OSS log
      // MAILUSERREADMAILFLOW tdr{};
      // tdr.senderUserId = 0;
      // tdr.isRemove = need_remove;
      // if (mail->content) {
      //   mail_dump_to_oss_tdr(tdr, *mail->content);
      //   tdr.senderUserId = mail->content->sender().profile().user_id();
      // } else if (mail->record) {
      //   mail_dump_to_oss_tdr(tdr, *mail->record);
      // }
      // game_oss_send::send(owner_->shared_from_this(), E_OSS_LOG_MAIL_USER_READ_MAIL_FLOW, tdr);
    } else if (need_remove) {
      remove_user_mail(ctx, mail_id);
    }

    if (nullptr != out) {
      protobuf_copy_message(*out->mutable_record(), *mail->record);
    }
  } while (false);

  if (nullptr != out) {
    out->mutable_record()->set_mail_id(mail_id);
    out->set_result(ret);
  }
  return ret;
}

int user_mail_manager::read_all(
    rpc::context &ctx, int32_t major_type, int32_t minor_type,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMailOperationResult> *ATFW_UTIL_MACRO_NULLABLE out,
    bool need_remove) {
  std::vector<mail_data_ptr_t> mail_box;

  auto selected_mail_box = get_mail_box_by_major_type(major_type);
  if (nullptr == selected_mail_box) {
    return 0;
  }

  mail_box.reserve(selected_mail_box->mails.size());
  for (auto &mail : selected_mail_box->mails) {
    if (!mail.second || !mail.second->record || !mail.second->content) {
      continue;
    }

    if (0 != atfw::mail::mail_is_valid(ctx, *mail.second->content)) {
      continue;
    }

    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    if (0 != (mail.second->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
      continue;
    }

    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    if (!need_remove && 0 != (mail.second->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_READ)) {
      continue;
    }

    if (0 != minor_type && minor_type != mail.second->record->minor_type()) {
      continue;
    }

    // 一键已读的删除不能覆盖有附件的邮件
    if (need_remove && mail.second->content->attachments_size() > 0) {
      continue;
    }

    mail_box.push_back(mail.second);
  }

  for (auto &mail : mail_box) {
    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    mail->record->set_status(mail->record->status() | PROJECT_NAMESPACE_ID::EN_MAIL_ST_READ);

    if (need_remove) {
      remove_user_mail(ctx, mail->record->mail_id());
    } else {
      mutable_dirty_mail(*mail->record);
    }

    // FIXME OSS log
    // MAILUSERREADMAILFLOW tdr{};
    // tdr.senderUserId = 0;
    // tdr.isRemove = need_remove;
    //
    // if (mail->content) {
    //   mail_dump_to_oss_tdr(tdr, *mail->content);
    //   tdr.senderUserId = mail->content->sender().profile().user_id();
    // } else if (mail->record) {
    //   mail_dump_to_oss_tdr(tdr, *mail->record);
    // }
    // game_oss_send::send(owner_->shared_from_this(), E_OSS_LOG_MAIL_USER_READ_MAIL_FLOW, tdr);

    if (nullptr != out) {
      PROJECT_NAMESPACE_ID::DMailOperationResult *outres = out->Add();
      if (nullptr != outres) {
        protobuf_copy_message(*outres->mutable_record(), *mail->record);
      }
    }
  }

  return 0;
}

rpc::result_code_type user_mail_manager::receive_mail_attachments(
    rpc::context &ctx, int64_t mail_id, PROJECT_NAMESPACE_ID::DMailOperationResult *ATFW_UTIL_MACRO_NULLABLE out,
    bool need_remove) {
  int ret = 0;
  mail_data_ptr_t mail = get_mail_raw(mail_id);
  do {
    if (!mail || !mail->content || !mail->record) {
      ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
      break;
    }

    ret = atfw::mail::mail_is_valid(ctx, *mail->content);
    if (ret < 0) {
      break;
    }

// FIXME 特殊判定一下付费货币最大限制
#if 0
    int64_t pay_max_pay_diamond = excel::get_const_config().market_max_pay_diamond();
    if (pay_max_pay_diamond > 0) {
      int64_t max_diamond_count = 0;
      for (int i = 0; i < mail->content->attachments_size(); ++i) {
        const PROJECT_NAMESPACE_ID::DItem &itm = mail->content->attachments(i).attachment_item();
        if (itm.id() == 0 || itm.count() <= 0) {
          continue;
        }

        auto item_id = itm.id();
        auto item_count = itm.count();
        if (PROJECT_NAMESPACE_ID::EN_VIRTUAL_ITEM_DIAMOND == item_id) {
          max_diamond_count += item_count;
          continue;
        }
        if (item_id >= PROJECT_NAMESPACE_ID::EN_ITEM_RANGE_RANDOM_POOL_START &&
          item_id < PROJECT_NAMESPACE_ID::EN_ITEM_RANGE_RANDOM_POOL_END) {
          std::list<random_pool_manager::random_result_t> rewards;
          random_pool_manager::me()->get_pool_max(item_id, rewards, item_count);
          for (auto &item : rewards) {
            if (PROJECT_NAMESPACE_ID::EN_VIRTUAL_ITEM_DIAMOND == item.id) {
              max_diamond_count += item.count;
              break;
            }
          }
        }
      }

      if (max_diamond_count > 0 && owner_->get_user_pay_manager().get_diamond() +
                                           owner_->get_user_pay_manager().get_diamond_others() + max_diamond_count >
                                       pay_max_pay_diamond) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_PAY_DIAMOND_LIMIT;
        break;
      }
    }
#endif

    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    if (0 == (mail->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_TOKEN_ATTACHMENT)) {
      ret = RPC_AWAIT_CODE_RESULT(receive_mail_attachments(ctx, *mail, need_remove));

      if (need_remove) {
        remove_user_mail(ctx, mail_id);
      } else {
        mutable_dirty_mail(*mail->record);
      }
    } else {
      if (need_remove) {
        remove_user_mail(ctx, mail_id);
      }
      ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_ALREADY_RECEIVED;
    }

    if (nullptr != out) {
      protobuf_copy_message(*out->mutable_record(), *mail->record);
    }
  } while (false);

  if (nullptr != out) {
    out->mutable_record()->set_mail_id(mail_id);
    out->set_result(ret);
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_mail_manager::receive_all(
    rpc::context &ctx, int32_t major_type, int32_t minor_type,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMailOperationResult> *ATFW_UTIL_MACRO_NULLABLE out,
    bool need_remove) {
  std::vector<mail_data_ptr_t> mail_box;

  auto selected_mail_box = get_mail_box_by_major_type(major_type);
  if (nullptr == selected_mail_box) {
    RPC_RETURN_CODE(0);
  }

  // FIXME 特殊判定一下付费货币最大限制
  // int64_t pay_max_pay_diamond = excel::get_const_config().market_max_pay_diamond();
  // int64_t max_diamond_count = 0;

  mail_box.reserve(selected_mail_box->mails.size());
  for (auto &mail : selected_mail_box->mails) {
    if (!mail.second || !mail.second->record || !mail.second->content) {
      continue;
    }

    if (0 != atfw::mail::mail_is_valid(ctx, *mail.second->content)) {
      continue;
    }

    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    if (0 != (mail.second->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
      continue;
    }

    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    if (!need_remove && 0 != (mail.second->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_TOKEN_ATTACHMENT)) {
      continue;
    }

    if (0 != minor_type && minor_type != mail.second->record->minor_type()) {
      continue;
    }

    // 一键领取且删除不能覆盖无附件的邮件
    if (need_remove && mail.second->content->attachments_size() == 0) {
      continue;
    }

    // FIXME 特殊判定一下付费货币最大限制
#if 0
    if (pay_max_pay_diamond > 0) {
      for (int i = 0; i < mail.second->content->attachments_size(); ++i) {
        const PROJECT_NAMESPACE_ID::DItem &itm = mail.second->content->attachments(i).attachment_item();
        if (itm.id() == 0 || itm.count() <= 0) {
          continue;
        }

        auto item_id = itm.type_id();
        auto item_count = itm.count();
        if (PROJECT_NAMESPACE_ID::EN_VIRTUAL_ITEM_DIAMOND == item_id) {
          max_diamond_count += item_count;
          continue;
        }
        if (item_id >= PROJECT_NAMESPACE_ID::EN_ITEM_RANGE_RANDOM_POOL_START &&
          item_id < PROJECT_NAMESPACE_ID::EN_ITEM_RANGE_RANDOM_POOL_END) {
          std::list<random_pool_manager::random_result_t> rewards;
          random_pool_manager::me()->get_pool_max(item_id, rewards, item_count);
          for (auto &item : rewards) {
            if (PROJECT_NAMESPACE_ID::EN_VIRTUAL_ITEM_DIAMOND == item.id) {
              max_diamond_count += item.count;
              break;
            }
          }
        }
      }
    }
#endif
    mail_box.push_back(mail.second);
  }

  // FIXME 最大付费货币数量限制
  // if (max_diamond_count > 0 && pay_max_pay_diamond > 0 &&
  //     owner_->get_user_pay_manager().get_diamond() + owner_->get_user_pay_manager().get_diamond_others() +
  //             max_diamond_count >
  //         pay_max_pay_diamond) {
  //   return PROJECT_NAMESPACE_ID::EN_ERR_PAY_DIAMOND_LIMIT;
  // }

  int ret = 0;
  for (auto &mail : mail_box) {
    int res = RPC_AWAIT_CODE_RESULT(receive_mail_attachments(ctx, *mail, need_remove));
    if (res < 0) {
      ret = res;
    }

    if (need_remove) {
      remove_user_mail(ctx, mail->record->mail_id());
    } else {
      mutable_dirty_mail(*mail->record);
    }

    if (nullptr != out) {
      PROJECT_NAMESPACE_ID::DMailOperationResult *outres = out->Add();
      if (nullptr != outres) {
        protobuf_copy_message(*outres->mutable_record(), *mail->record);
        outres->set_result(res);
      }
    }
  }

  RPC_RETURN_CODE(ret);
}

void user_mail_manager::pack_mail_user(PROJECT_NAMESPACE_ID::DMailUserInfo &user_info) const {
  protobuf_copy_message(*user_info.mutable_profile(), owner_->get_account_info().profile());
}

int user_mail_manager::add_mail_with_template(rpc::context &ctx, int32_t mail_template_id,
                                              const PROJECT_NAMESPACE_ID::item_algorithm::ItemOperationSource &reason) {
  auto user_ptr = owner_->shared_from_this();
  auto async_task = rpc::async_invoke(
      ctx, "user_mail_manager.add_mail_with_template",
      [user_ptr, mail_template_id, reason](rpc::context &child_ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::DMailUserInfo *receiver = child_ctx.create<PROJECT_NAMESPACE_ID::DMailUserInfo>();
        PROJECT_NAMESPACE_ID::DMailUserInfo local_receiver;
        if (nullptr == receiver) {
          receiver = &local_receiver;
        }

        user_ptr->get_user_mail_manager().pack_mail_user(*receiver);

        auto ret = RPC_AWAIT_CODE_RESULT(rpc::mail::add_user_mail_with_template(
            child_ctx, mail_template_id, user_ptr->get_zone_id(), *receiver, nullptr, true));
        if (ret < 0) {
          FCTXLOGERROR(child_ctx, "{} try to add mail of template {} reason=({}, {}, {}) failed, res: {}({})",
                       *user_ptr, mail_template_id, reason.major_reason, reason.minor_reason, reason.reason_param, ret,
                       protobuf_mini_dumper_get_error_msg(ret));
        }
        RPC_RETURN_CODE(ret);
      });
  if (async_task.is_error()) {
    FCTXLOGERROR(ctx, "{} async_invoke a task to add mail of template {} for reason=({}, {}, {}) failed.res: {}({})",
                 *user_ptr, mail_template_id, reason.major_reason, reason.minor_reason, reason.reason_param,
                 *async_task.get_error(), protobuf_mini_dumper_get_error_msg(*async_task.get_error()));

    return *async_task.get_error();
  }

  return 0;
}

bool user_mail_manager::is_mail_shown(rpc::context &ctx, const PROJECT_NAMESPACE_ID::DMailContent &content) {
  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  if (0 != (content.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return false;
  }

  auto now = ctx.logical_now();
  return now >= protobuf_to_system_clock(content.show_time()) && now < protobuf_to_system_clock(content.expired_time());
}

bool user_mail_manager::is_mail_shown(rpc::context &ctx, const mail_data_ptr_t &mail) {
  return mail && mail->record && mail->content && is_mail_shown(ctx, *mail->content);
}

void user_mail_manager::send_all_syn_msg(rpc::context &ctx) {
  if (dirty_cache_.dirty_mails.empty()) {
    return;
  }

  auto sess = owner_->get_session();
  if (!sess) {
    return;
  }

  if (is_async_task_running()) {
    return;
  }

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SCMailChangeSync> sync_msg{ctx};

  bool has_data = false;
  for (auto &dirty_mail : dirty_cache_.dirty_mails) {
    if (dirty_cache_.new_mails.find(dirty_mail.second.mail_id()) != dirty_cache_.new_mails.end()) {
      continue;
    }

    has_data = true;
    protobuf_copy_message(*sync_msg->add_update_mails(), dirty_mail.second);
  }

  // 新邮件走另一个字段
  bool need_async_job = need_start_async_jobs();
  std::vector<int64_t> mail_ids;
  mail_ids.reserve(dirty_cache_.new_mails.size());
  mail_ids.assign(dirty_cache_.new_mails.begin(), dirty_cache_.new_mails.end());
  for (auto &mail_id : mail_ids) {
    auto id_iter = mail_box_id_index_.find(mail_id);
    if (id_iter == mail_box_id_index_.end()) {
      dirty_cache_.new_mails.erase(mail_id);
      continue;
    }

    if (!id_iter->second->record) {
      dirty_cache_.new_mails.erase(mail_id);
      continue;
    }

    // 未拉取邮件内容，需要重新拉取
    if (!id_iter->second->content) {
      if (mail_box_unloaded_index_.end() != mail_box_unloaded_index_.find(mail_id)) {
        need_async_job = true;
      } else {
        FWLOGERROR("dirty mail data, mail_id={}", mail_id);
        dirty_cache_.new_mails.erase(mail_id);
      }
      continue;
    }

    PROJECT_NAMESPACE_ID::DMailContent *added_mail = sync_msg->add_add_mails();
    if (NULL != added_mail) {
      dirty_cache_.new_mails.erase(mail_id);
      atfw::mail::mail_merge_content_and_record(ctx, *added_mail, *id_iter->second->content, *id_iter->second->record);
      has_data = true;
    }
  }

  // 没有数据则放弃发送
  if (has_data) {
    // 直接清理，否则如果又要启动异步任务的话会导致这里数据被重复下发
    clear_dirty_cache();

    rpc::lobbysvrclientservice::send_mail_change_sync(ctx, *sync_msg, *sess);
  }

  // 存在未拉取的全服邮件
  if (need_async_job) {
    try_to_start_async_jobs(ctx);
  }
}

void user_mail_manager::clear_dirty_cache() {
  // 如果异步任务正在执行则不要清理，否则等执行完后会丢失邮件变更通知
  if (is_async_task_running()) {
    return;
  }

  dirty_cache_.dirty_mails.clear();
}

PROJECT_NAMESPACE_ID::DMailRecord &user_mail_manager::mutable_dirty_mail(
    const PROJECT_NAMESPACE_ID::DMailRecord &record, bool is_new) {
  PROJECT_NAMESPACE_ID::DMailRecord &ret = dirty_cache_.dirty_mails[record.mail_id()];
  protobuf_copy_message(ret, record);

  if (is_new) {
    dirty_cache_.new_mails.insert(record.mail_id());
  }

  return ret;
}

void user_mail_manager::remove_expired_mails(rpc::context &ctx) { scan_and_remove_expired_mails(ctx); }

void user_mail_manager::fetch_all_user_mail_ids(std::vector<int64_t> &out) const {
  for (const auto &mail_pair : mail_box_id_index_) {
    if (mail_pair.second && mail_pair.second->record && !mail_pair.second->record->is_global_mail()) {
      out.push_back(mail_pair.second->record->mail_id());
    }
  }
}

void user_mail_manager::refresh_mail_box_future_cache(rpc::context &ctx, mail_box_t &mail_box) {
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

    if (protobuf_to_system_clock(mail.second->record->start_time()) <= now) {
      continue;
    }

    if (protobuf_to_system_clock(mail.second->record->start_time()) < mail_box.future_cache_expire) {
      mail_box.future_cache_expire = protobuf_to_system_clock(mail.second->record->start_time());
    }
    ++mail_box.future_mail_count;
  }
}

void user_mail_manager::compact_mails(rpc::context &ctx) {
  // 复制索引，内部有删除操作不能直接迭代成员变量
  std::unordered_map<int32_t, atfw::util::memory::strong_rc_ptr<mail_box_t> > copy_index = mail_box_type_index_;
  for (auto &mail_type : copy_index) {
    if (mail_type.second) {
      compact_mails(ctx, *mail_type.second);
    }
  }
}

void user_mail_manager::compact_mails(rpc::context &ctx, mail_box_t &mail_box) {
  using std::max;

  refresh_mail_box_future_cache(ctx, mail_box);

  if (mail_box.mails.empty() || excel::get_const_config().user_mail_max_count_per_major_type() <= 0) {
    return;
  }

  int32_t future_reserve_count = excel::get_const_config().user_future_reserve_mail_max_count_per_major_type();
  if (future_reserve_count > mail_box.future_mail_count) {
    future_reserve_count = mail_box.future_mail_count;
  }
  if (future_reserve_count < 0) {
    future_reserve_count = 0;
  }

  size_t max_mail_count = static_cast<size_t>(excel::get_const_config().user_mail_max_count_per_major_type());
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

  // 一般单个邮箱的邮件上限会配成100,top k排序的复杂度是 O(nlog(k))
  // 那么超出的邮件数量在8(log2(8)=3)以内就没必要另外分配内存排序了，直接 O(n*m)
  // 的开销应该更少，因为这种情况很少出现，而且其次这样缓存命中率更高和还少一次malloc
  std::vector<int64_t> pending_to_remove;
  pending_to_remove.reserve(mail_box.mails.size() - max_mail_count);

  FCTXLOGDEBUG(ctx,
               "{} user_mail_manager start to compact {} mails, sum mail count: {}, future mail count: {}, limit: {}, "
               "future limit: {}",
               *owner_, mail_box.mails.size() - max_mail_count, mail_box.mails.size(), mail_box.future_mail_count,
               excel::get_const_config().user_mail_max_count_per_major_type(),
               excel::get_const_config().user_future_reserve_mail_max_count_per_major_type());

  if (mail_box.mails.size() - max_mail_count < 8) {
    size_t rmcnt = mail_box.mails.size() - max_mail_count;
    for (size_t i = 0; i < rmcnt && mail_box.mails.size() > max_mail_count; ++i) {
      int64_t select_mail_id = 0;
      std::chrono::system_clock::time_point select_mail_compact_time = std::chrono::system_clock::from_time_t(0);

      for (auto &mail : mail_box.mails) {
        if (!mail.second || !mail.second->record) {
          pending_to_remove.push_back(mail.first);
          continue;
        }

        // 全服邮件在失效时就可以移除邮件内容但保留received_global_mails_
        if (mail.second->record->is_global_mail()) {
          if (global_mail_manager::is_record_removeable(ctx, *mail.second->record) ||
              atfw::mail::mail_is_expired_or_removed(ctx, *mail.second->record)) {
            pending_to_remove.push_back(mail.first);
            continue;
          }
        } else if (atfw::mail::mail_is_history_removable(ctx, *mail.second->record)) {
          pending_to_remove.push_back(mail.first);
          continue;
        }

        auto compact_time = protobuf_to_system_clock(mail.second->record->start_time());
        auto delivery_time = protobuf_to_system_clock(mail.second->record->delivery_time());
        if (delivery_time > compact_time) {
          compact_time = delivery_time;
        } else if (compact_time > delivery_time + delivery_time_max_offset) {
          compact_time = delivery_time + delivery_time_max_offset;
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
        remove_user_mail(ctx, mail_id);
      }
      pending_to_remove.clear();
    }
  } else {
    std::vector<mail_data_ptr_t> select_mails;
    select_mails.reserve(mail_box.mails.size());
    for (auto &mail : mail_box.mails) {
      if (!mail.second || !mail.second->record) {
        pending_to_remove.push_back(mail.first);
        continue;
      }

      // 全服邮件在失效时就可以移除邮件内容但保留received_global_mails_
      if (mail.second->record->is_global_mail()) {
        if (global_mail_manager::is_record_removeable(ctx, *mail.second->record) ||
            atfw::mail::mail_is_expired_or_removed(ctx, *mail.second->record)) {
          pending_to_remove.push_back(mail.first);
          continue;
        }
      } else if (atfw::mail::mail_is_history_removable(ctx, *mail.second->record)) {
        pending_to_remove.push_back(mail.first);
        continue;
      }

      select_mails.push_back(mail.second);
    }

    if (select_mails.size() > max_mail_count) {
      size_t rmcnt = select_mails.size() - max_mail_count;
      std::partial_sort(
          select_mails.begin(),
          select_mails.begin() + static_cast<std::vector<mail_data_ptr_t>::difference_type>(rmcnt), select_mails.end(),
          // ==========================================================================================================
          [delivery_time_max_offset](const mail_data_ptr_t &l, const mail_data_ptr_t &r) {
            std::chrono::system_clock::time_point lv = std::chrono::system_clock::from_time_t(0);
            std::chrono::system_clock::time_point rv = std::chrono::system_clock::from_time_t(0);
            if (l && l->record) {
              lv = protobuf_to_system_clock(l->record->start_time());
              auto lv_delivery = protobuf_to_system_clock(l->record->delivery_time());
              if (lv_delivery > lv) {
                lv = lv_delivery;
              } else if (lv > lv_delivery + delivery_time_max_offset) {
                lv = lv_delivery + delivery_time_max_offset;
              }
            }
            if (r && r->record) {
              rv = protobuf_to_system_clock(r->record->start_time());
              auto rv_delivery = protobuf_to_system_clock(r->record->delivery_time());
              if (rv_delivery > rv) {
                rv = rv_delivery;
              } else if (rv > rv_delivery + delivery_time_max_offset) {
                rv = rv_delivery + delivery_time_max_offset;
              }
            }
            return lv < rv;
          });
      for (size_t i = 0; i < rmcnt; ++i) {
        pending_to_remove.push_back(select_mails[i]->record->mail_id());
      }
    }

    for (int64_t mail_id : pending_to_remove) {
      remove_user_mail(ctx, mail_id);
    }
    pending_to_remove.clear();
  }
}

void user_mail_manager::scan_and_remove_expired_mails(rpc::context &ctx) {
  // TODO(any): 这里是全量扫描，控制下调用频率

  std::vector<int64_t> remove_ids;
  for (auto &mail : mail_box_id_index_) {
    if (mail.second && mail.second->record) {
      if (mail.second->record->is_global_mail()) {
        if (global_mail_manager::is_record_removeable(ctx, *mail.second->record) ||
            atfw::mail::mail_is_expired_or_removed(ctx, *mail.second->record)) {
          remove_ids.push_back(mail.second->record->mail_id());
        }
      } else if (atfw::mail::mail_is_history_removable(ctx, *mail.second->record)) {
        remove_ids.push_back(mail.second->record->mail_id());
      }
    }
  }

  for (auto &mail_id : remove_ids) {
    remove_user_mail(ctx, mail_id);
  }

  // 移除过期的全服邮件记录 - received_global_mails_
  remove_ids.clear();
  for (auto &mail : received_global_mails_) {
    if (global_mail_manager::is_history_removeable(ctx, mail.second)) {
      remove_ids.push_back(mail.first);
    }
  }

  for (auto &mail_id : remove_ids) {
    received_global_mails_.erase(mail_id);
    remove_user_mail(ctx, mail_id);
  }

  // 邮件数量限制淘汰
  compact_mails(ctx);
}

void user_mail_manager::remove_user_mail(rpc::context &ctx, int64_t mail_id) {
  {
    // 从待拉取的新邮件集合中移除
    auto new_mail_iter = dirty_cache_.new_mails.find(mail_id);
    if (new_mail_iter != dirty_cache_.new_mails.end()) {
      dirty_cache_.new_mails.erase(new_mail_iter);
    }
  }

  auto id_iter = mail_box_id_index_.find(mail_id);
  if (id_iter == mail_box_id_index_.end()) {
    return;
  }

  if (!id_iter->second || !id_iter->second->record) {
    FCTXLOGERROR(ctx, "mail.record should not be null");
    mail_box_id_index_.erase(id_iter);
    mail_box_unloaded_index_.erase(mail_id);
    return;
  }

  is_dirty_ = true;
  {
    auto type_iter = mail_box_type_index_.find(id_iter->second->record->major_type());
    if (type_iter == mail_box_type_index_.end()) {
      FCTXLOGERROR(ctx, "{} can not find type index {} for mail {}", *owner_, id_iter->second->record->major_type(),
                   mail_id);
    } else {
      if (!type_iter->second) {
        mail_box_type_index_.erase(type_iter);
      } else {
        // 重置未来邮件缓存
        if (type_iter->second->future_cache_expire <= protobuf_to_system_clock(id_iter->second->record->start_time())) {
          type_iter->second->future_cache_expire = std::chrono::system_clock::from_time_t(0);
        }

        type_iter->second->mails.erase(mail_id);
        if (type_iter->second->mails.empty()) {
          mail_box_type_index_.erase(type_iter);
        }
      }
    }
  }

  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  id_iter->second->record->set_status(id_iter->second->record->status() | PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED);
  mutable_dirty_mail(*id_iter->second->record);

  FCTXLOGDEBUG(ctx, "{} remove mail ({}, {}) success", *owner_, id_iter->second->record->major_type(),
               id_iter->second->record->mail_id());

  if (!id_iter->second->record->is_global_mail()) {
    protobuf_copy_message(pending_to_remove_[mail_id], *id_iter->second->record);

    // reset protect timeout and try to start the remove content task
    mail_async_task_protect_timepoint_ = std::chrono::system_clock::from_time_t(0);
  }

  mail_box_unloaded_index_.erase(id_iter->first);
  mail_box_id_index_.erase(id_iter);
}

bool user_mail_manager::need_start_async_jobs() {
  if (::util::time::time_utility::now() <= mail_async_task_protect_timepoint_) {
    return false;
  }

  return !pending_to_remove_.empty() || !is_global_mails_merged_ || !mail_box_unloaded_index_.empty();
}

void user_mail_manager::try_to_start_async_jobs(rpc::context &ctx) {
  if (is_async_task_running()) {
    return;
  }

  if (!owner_->is_writable()) {
    return;
  }

  if (::util::time::time_utility::now() <= mail_async_task_protect_timepoint_) {
    return;
  }

  // 启动邮件异步任务
  task_type_trait::task_type task_inst;
  task_action_mail_async_jobs::ctor_param_t params;
  params.caller_context = &ctx;
  params.user_inst = owner_->shared_from_this();
  params.timeout_duration = task_manager::make_timeout_duration(logic_config::me()->get_cfg_task().csmsg().timeout());
  params.timeout_duration += std::chrono::seconds{2};
  params.timeout_timepoint = atfw::util::time::time_utility::now() + params.timeout_duration;

  // in case of infinity loop
  mail_async_task_protect_timepoint_ = params.timeout_timepoint;
  mail_async_task_protect_timepoint_ += std::chrono::seconds{1};

  task_manager::me()->create_task_with_timeout<task_action_mail_async_jobs>(task_inst, params.timeout_duration,
                                                                            std::move(params));

  if (task_type_trait::empty(task_inst)) {
    FWLOGERROR("create task_action_mail_async_jobs failed");
  } else {
    mail_async_task_ = task_inst;

    dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

    int res = task_manager::me()->start_task(task_inst, start_data);
    if (res < 0) {
      FCTXLOGERROR(ctx, "{} start task_action_mail_async_jobs failed, res: {}({})", *owner_, res,
                   protobuf_mini_dumper_get_error_msg(res));
      task_type_trait::reset_task(mail_async_task_);
      return;
    }
  }
}

rpc::result_code_type user_mail_manager::receive_mail_attachments(rpc::context &ctx, mail_data_t &mail,
                                                                  bool /*oss_need_remove*/) {
  if (!mail.record || !mail.content) {
    FCTXLOGERROR(ctx, "{} receive mail attachments failed, record or content not found", *owner_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // 可能走删除流程，重复领取直接跳过
  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  if (0 != (mail.record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_TOKEN_ATTACHMENT)) {
    RPC_RETURN_CODE(0);
  }

  int ret = 0;

  do {
    if (mail.content->attachments_size() <= 0) {
      break;
    }

    google::protobuf::RepeatedPtrField<atframework::shared::DItemInstance> out_instances;
    int res = RPC_AWAIT_CODE_RESULT(
        owner_->get_user_item_manager().generate_item_from_offset_cfg(ctx, mail.content->attachments(), out_instances));
    if (res < 0) {
      FCTXLOGERROR(ctx, "receive attachments of mail {} failed, res: {}({})", *owner_, mail.record->mail_id(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      ret = res;
    }

    if (out_instances.empty()) {
      break;
    }
    auto check_add_res =
        owner_->get_user_item_manager().check_add(ctx, std::move(out_instances),
                                                  {
                                                      PROJECT_NAMESPACE_ID::EN_ITEM_CHANGE_MAJOR_TYPE_MAIL_ATTACHMENT,
                                                      mail.content->minor_type(),
                                                      mail.content->mail_id(),
                                                  });
    if (check_add_res.get_check_result().error_code < 0) {
      ret = check_add_res.get_check_result().error_code;
      FCTXLOGERROR(ctx, "receive attachments of mail {} failed, res: {}({})", *owner_, mail.record->mail_id(), ret,
                   protobuf_mini_dumper_get_error_msg(ret));
    }
  } while (false);

  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  mail.record->set_status(mail.record->status() | PROJECT_NAMESPACE_ID::EN_MAIL_ST_TOKEN_ATTACHMENT);

  // FIXME OSS log
  // MAILUSERTAKEATTACHMENTFLOW tdr{};
  // tdr.senderUserId = 0;
  // tdr.isRemove = oss_need_remove;
  //
  // if (mail.content) {
  //   mail_dump_to_oss_tdr(tdr, *mail.content);
  //   tdr.senderUserId = mail.content->sender().profile().user_id();
  // } else if (mail.record) {
  //   mail_dump_to_oss_tdr(tdr, *mail.record);
  // }
  // game_oss_send::send(owner_->shared_from_this(), E_OSS_LOG_MAIL_USER_TAKE_ATTACHMENT_FLOW, tdr);

  RPC_RETURN_CODE(ret);
}

void user_mail_manager::fetch_all_unloaded_mails(rpc::context &ctx, std::vector<int64_t> &out) {
  std::vector<int64_t> invalid_mail_ids;
  std::vector<int64_t> pending_to_remove;
  out.reserve(mail_box_unloaded_index_.size());

  // 这里面可能保护全服邮件，如果用户切换gamesvr节点，然后新的gamesvr的全服邮件缓存晚于老的，那么可能刷全服邮件cache的时候没刷进来
  // 这时候就fallback成直接从数据库拉取
  for (auto &checked : mail_box_unloaded_index_) {
    if (checked.second && checked.second->record && !checked.second->content) {
      // 如果是全服邮件的话检查一下是否已经失效
      if (checked.second->record->is_global_mail() &&
          global_mail_manager::is_record_removeable(ctx, *checked.second->record)) {
        invalid_mail_ids.push_back(checked.first);
      } else {
        out.push_back(checked.first);
      }
    } else {
      invalid_mail_ids.push_back(checked.first);
    }
  }

  for (auto &invalid_mail_id : invalid_mail_ids) {
    mail_box_unloaded_index_.erase(invalid_mail_id);
  }

  for (auto &mail_id : pending_to_remove) {
    remove_user_mail(ctx, mail_id);
  }
}

void user_mail_manager::set_mail_content_loaded(int64_t mail_id) {
  mail_box_unloaded_index_.erase(mail_id);

  if (dirty_cache_.new_mails.end() != dirty_cache_.new_mails.find(mail_id)) {
    mail_data_ptr_t mail = get_mail_raw(mail_id);
    if (mail && mail->record) {
      mutable_dirty_mail(*mail->record);
    }
  }
}

int user_mail_manager::merge_global_mails(rpc::context &ctx) {
  int ret = 0;
  if (is_global_mails_merged_) {
    return ret;
  }

  mail_index_t invalid_mails;
  invalid_mails.reserve(mail_box_id_index_.size());
  for (auto &mail : mail_box_id_index_) {
    if (mail.second && mail.second->record && mail.second->record->is_global_mail()) {
      invalid_mails[mail.first] = mail.second;
    }
  }

  // 先移除无效邮件，防止无效的全服邮件顶替掉现有个人邮件
  for (const auto &mail : global_mail_manager::me()->get_all_global_mails()) {
    if (!mail.second.second || !mail.second.second->record) {
      continue;
    }
    invalid_mails.erase(mail.first);
  }

  // 移除和已删除的全服邮件
  for (auto &mail : invalid_mails) {
    // 注意全服邮件的删除要考虑因为可能出现因为切换gamesvr节点导致两边全服缓存落后的问题
    if (global_mail_manager::is_record_removeable(ctx, *mail.second->record)) {
      remove_user_mail(ctx, mail.first);
      ++ret;
    }
  }

  // 补全全局邮件内容
  for (const auto &mail : global_mail_manager::me()->get_all_global_mails()) {
    if (!mail.second.second || !mail.second.second->record) {
      continue;
    }

    // 圈复邮件尚未拉取完成，等拉完了再处理
    if (!mail.second.second->content) {
      continue;
    }

    const PROJECT_NAMESPACE_ID::DMailContent &content = *mail.second.second->content;

    // TODO(any): 通用条件检查
    // if (content.mail_limit_size() > 0) {
    //   // 检查各类限制
    //   if (0 != owner_->get_user_condition_manager().check_rules(content.mail_limit(), {})) {
    //     continue;
    //   }
    // }

    if (0 == add_global_mail(ctx, *mail.second.second->record, content)) {
      ++ret;
    }
  }

  is_global_mails_merged_ = true;
  // 返回成功合入的邮件数和删除的过期邮件数
  return ret;
}

rpc::result_code_type user_mail_manager::add_reward_by_mail(
    rpc::context &ctx, int64_t mail_id, int32_t mail_template_id, std::chrono::system_clock::time_point delivery_time,
    std::chrono::system_clock::time_point expired_time, PROJECT_NAMESPACE_ID::EnItemChgMajorType /*main_type*/,
    const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemOffset> &reward_items,
    const mail_render_extension_type *ATFW_UTIL_MACRO_NULLABLE extension) const {
  rpc::shared_message<PROJECT_NAMESPACE_ID::DMailContent> mail{ctx};

  mail->set_mail_id(mail_id);
  *mail->mutable_delivery_time() = protobuf_from_system_clock(delivery_time);
  *mail->mutable_expired_time() = protobuf_from_system_clock(expired_time);

  PROJECT_NAMESPACE_ID::DMailUserInfo *user_info = ctx.create<PROJECT_NAMESPACE_ID::DMailUserInfo>();
  PROJECT_NAMESPACE_ID::DMailUserInfo local_user_info;
  if (nullptr == user_info) {
    user_info = &local_user_info;
  }

  owner_->get_user_mail_manager().pack_mail_user(*user_info);

  for (const auto &item : reward_items) {
    if (item.type_id() <= 0 || item.count() <= 0) {
      continue;
    }

    auto *reward_item = mail->add_attachments();
    if (nullptr == reward_item) {
      FWLOGERROR("malloc DMailItem failed");
      break;
    }

    protobuf_copy_message(*reward_item, item);
  }

  int32_t ret = atfw::mail::mail_render_template(ctx, mail_template_id, *mail, *user_info, nullptr, extension, true);
  if (ret != 0) {
    FCTXLOGERROR(ctx, "{} send mail, render template({}) failed", *owner_, mail_template_id);
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(
      rpc::mail::add_user_mail(ctx, owner_->get_user_id(), owner_->get_zone_id(), std::move(*mail)));
  RPC_RETURN_CODE(ret);
}

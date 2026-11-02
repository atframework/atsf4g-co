// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#include "logic/mail/task_action_mail_async_jobs.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/protobuf_mini_dumper.h>

#include <router/router_manager_set.h>
#include <router/router_user_cache.h>
#include <router/router_user_manager.h>

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_context.h>

#include <memory>
#include <unordered_set>
#include <vector>

#include "data/user.h"
#include "logic/mail/user_mail_manager.h"

task_action_mail_async_jobs::task_action_mail_async_jobs(ctor_param_t &&param)
    : task_action_no_req_base(param),
      param_(param),
      need_restart_(false),
      is_writable_(false),
      fetch_mail_number_(0),
      remove_mail_number_(0),
      merge_global_mail_number_(0) {}

task_action_mail_async_jobs::~task_action_mail_async_jobs() {}

task_action_mail_async_jobs::result_type task_action_mail_async_jobs::operator()() {
  need_restart_ = false;
  is_writable_ = false;
  if (!param_.user_inst) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  set_user_key(param_.user_inst->get_user_id(), param_.user_inst->get_zone_id());
  router_user_cache::key_t key(router_user_manager::me()->get_type_id(), param_.user_inst->get_zone_id(),
                               param_.user_inst->get_user_id());
  router_user_cache::ptr_t cache = router_user_manager::me()->get_cache(key);

  // 缓存已被移除，当前user可能是上下文缓存，忽略patch
  if (!cache) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 缓存已被踢出，当前user是路由缓存，忽略patch
  if (!cache->is_writable()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 注意这里会续期缓存生命周期，所以要确保前面判定都过后才能到这里
  if (!cache->is_object_equal(param_.user_inst)) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  is_writable_ = true;
  need_restart_ = false;
  fetch_mail_number_ = 0;
  remove_mail_number_ = 0;
  merge_global_mail_number_ = 0;

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  if (is_writable_ && param_.user_inst->is_inited()) {
    // 补全全局邮件内容，移除过期全服邮件,这里没有RPC
    int res = param_.user_inst->get_user_mail_manager().merge_global_mails(get_shared_context());
    if (res > 0) {
      merge_global_mail_number_ += static_cast<uint32_t>(res);
    }

    // 拉取和补全邮件内容
    res = RPC_AWAIT_CODE_RESULT(fetch_mail_contents(*param_.user_inst));
    if (res < 0) {
      FCTXLOGERROR(get_shared_context(), "{} do fetch_mail_contents failed, res: {}({})", *param_.user_inst, res,
                   protobuf_mini_dumper_get_error_msg(res));
      ret = res;
    } else {
      fetch_mail_number_ += static_cast<uint32_t>(res);
    }

    // 删除过期邮件
    param_.user_inst->get_user_mail_manager().remove_expired_mails(get_shared_context());
    res = RPC_AWAIT_CODE_RESULT(cleanup_removed_mails(*param_.user_inst, *cache));
    if (res < 0) {
      FCTXLOGERROR(get_shared_context(), "{} do cleanup_removed_mails failed, res: {}({})", *param_.user_inst, res,
                   protobuf_mini_dumper_get_error_msg(res));
      ret = res;
    } else {
      remove_mail_number_ += static_cast<uint32_t>(res);
    }
  }

  // 可能是从中间中断的，需要重新计算一次是否可写
  is_writable_ = cache->is_writable();

  // 执行时间过长则中断，下一次再启动流程
  if (is_writable_ &&
      (param_.timeout_timepoint - atfw::util::time::time_utility::now()) < (param_.timeout_duration / 2)) {
    need_restart_ = true;
  }

  if (is_writable_ && (fetch_mail_number_ > 0 || remove_mail_number_ > 0 || merge_global_mail_number_ > 0) &&
      ret >= 0) {
    // 如果缓存对象仍然可写，则保存一下用户数据
    size_t lazy_save_counter = ++param_.user_inst->get_user_mail_manager().lazy_save_counter_;
    if (lazy_save_counter >= PROJECT_NAMESPACE_ID::EN_SL_LAZY_SAVE_COUNTER_L1) {
      param_.user_inst->get_user_mail_manager().lazy_save_counter_ = 0;
      ret = RPC_AWAIT_CODE_RESULT(cache->save(get_shared_context(), nullptr));
      is_writable_ = cache->is_writable();
    } else {
      router_manager_set::me()->mark_fast_save(router_user_manager::me().get(), cache);
    }
  }
  TASK_ACTION_RETURN_CODE(ret);
}

int task_action_mail_async_jobs::on_success() {
  if (param_.user_inst) {
    FCTXLOGINFO(get_shared_context(),
                "do mail async jobs success, fetch {} mail(s), remove {} mail(s), merge {} global mail(s)",
                *param_.user_inst, fetch_mail_number_, remove_mail_number_, merge_global_mail_number_);
  } else {
    FCTXLOGINFO(
        get_shared_context(),
        "user_inst UNKNOWN do mail async jobs success, fetch {} mail(s), remove {} mail(s), merge {} global mail(s)",
        fetch_mail_number_, remove_mail_number_, merge_global_mail_number_);
  }

  // 尝试再启动一次，启动排队后的任务
  if (is_writable_ && param_.user_inst) {
    if (task_type_trait::get_task_id(param_.user_inst->get_user_mail_manager().mail_async_task_) ==
        get_shared_context().get_task_context().task_id) {
      task_type_trait::reset_task(param_.user_inst->get_user_mail_manager().mail_async_task_);
    }
  }

  // 数据变更推送
  param_.user_inst->send_all_syn_msg(get_shared_context());

  // 先推送再重启，否则邮件脏数据不会下发
  if (is_writable_ && need_restart_ && param_.user_inst) {
    param_.user_inst->get_user_mail_manager().try_to_start_async_jobs(get_shared_context());
  }

  return get_result();
}

int task_action_mail_async_jobs::on_failed() {
  if (param_.user_inst) {
    FCTXLOGERROR(
        get_shared_context(),
        "{} do mail async jobs failed, res: {}({})(successed with fetch {} mail(s), remove {} mail(s), merge {} "
        "global mail(s))",
        *param_.user_inst, get_result(), protobuf_mini_dumper_get_error_msg(get_result()), fetch_mail_number_,
        remove_mail_number_, merge_global_mail_number_);
  } else {
    FCTXLOGERROR(
        get_shared_context(),
        "user_inst UNKNOWN do mail async jobs failed, res: {}({})(successed with fetch {} mail(s), remove {} mail(s), "
        "merge "
        "{} "
        "global mail(s))",
        get_result(), protobuf_mini_dumper_get_error_msg(get_result()), fetch_mail_number_, remove_mail_number_,
        merge_global_mail_number_);
  }

  // 尝试再启动一次，启动排队后的任务
  if (is_writable_ && param_.user_inst) {
    if (task_type_trait::get_task_id(param_.user_inst->get_user_mail_manager().mail_async_task_) ==
        get_shared_context().get_task_context().task_id) {
      task_type_trait::reset_task(param_.user_inst->get_user_mail_manager().mail_async_task_);
    }
  }

  // 数据变更推送
  param_.user_inst->send_all_syn_msg(get_shared_context());

  // 先推送再重启，否则邮件脏数据不会下发
  if (is_writable_ && need_restart_ && param_.user_inst) {
    param_.user_inst->get_user_mail_manager().try_to_start_async_jobs(get_shared_context());
  }

  return get_result();
}

rpc::result_code_type task_action_mail_async_jobs::fetch_mail_contents(user &user_inst) {
  std::vector<int64_t> mail_unloaded;
  user_inst.get_user_mail_manager().fetch_all_unloaded_mails(get_shared_context(), mail_unloaded);
  if (mail_unloaded.empty()) {
    RPC_RETURN_CODE(0);
  }

  std::unordered_set<int64_t> undo_mails;
  undo_mails.reserve(mail_unloaded.size());
  undo_mails.insert(mail_unloaded.begin(), mail_unloaded.end());

  // 这里面可能保护全服邮件，如果用户切换gamesvr节点，然后新的gamesvr的全服邮件缓存晚于老的，那么可能刷全服邮件cache的时候没刷进来
  // 这时候就fallback成直接从数据库拉取
  std::vector<rpc::db::mail_content::table_key_t> mail_id_keys;
  std::vector<rpc::db::mail_content::batch_get_result_t> mail_contents;
  mail_id_keys.reserve(mail_unloaded.size());
  for (auto mail_id : mail_unloaded) {
    mail_id_keys.emplace_back(mail_id);
  }

  int ret =
      RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::batch_get_all(get_shared_context(), mail_id_keys, mail_contents));

  for (auto &pulled_mail : mail_contents) {
    if (!pulled_mail.message) {
      continue;
    }

    auto mail_id = (*pulled_mail.message)->mail_id();
    undo_mails.erase(mail_id);

    mail_data_ptr_t mail = user_inst.get_user_mail_manager().get_mail_raw(mail_id);
    // 可能已经被移除,如果被移除了，忽略即可
    if (mail && mail->record) {
      if (!mail->content) {
        mail->content = gsl::make_unique<PROJECT_NAMESPACE_ID::DMailContent>();
      }
      protobuf_copy_message(*mail->content, (*pulled_mail.message)->blob_data().mail_content());
      mail->record->set_fetch_error_count(0);

      FCTXLOGDEBUG(get_shared_context(), "{} fetch mail content of mail_id {} success", user_inst, mail_id);
      if (ret >= 0) {
        ++ret;
      }
    } else {
      FCTXLOGDEBUG(get_shared_context(), "{} fetch mail content of mail_id {} success, but maybe already removed",
                   user_inst, mail_id);
    }

    user_inst.get_user_mail_manager().set_mail_content_loaded(mail_id);
  }

  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    ret = 0;
  }

  if (ret >= 0) {
    // 如果成功，则要处理一下无效的脏数据
    for (const auto &dirty_mail_id : undo_mails) {
      user_inst.get_user_mail_manager().set_mail_content_loaded(dirty_mail_id);

      mail_data_ptr_t mail = user_inst.get_user_mail_manager().get_mail_raw(dirty_mail_id);
      // 可能已经被移除,如果被移除了，忽略即可
      // 可能是邮件内容被删除了，这时候要清理一下索引，这里设置错误容忍值是为了适应万一数据库短期故障
      if (mail && mail->record) {
        mail->record->set_fetch_error_count(mail->record->fetch_error_count() + 1);
        if (mail->record->fetch_error_count() > PROJECT_NAMESPACE_ID::EN_CL_MAIL_PLAYER_TOLERANCE_ERROR_COUNT) {
          user_inst.get_user_mail_manager().remove_user_mail(get_shared_context(), dirty_mail_id);
        }
      } else {
        user_inst.get_user_mail_manager().remove_user_mail(get_shared_context(), dirty_mail_id);
      }
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type task_action_mail_async_jobs::cleanup_removed_mails(user &user_inst, router_user_cache &cache) {
  int ret = 0;

  // fetch_mail_contents 和前面的操作都有可能导致RPC回来以后缓存失效
  while (!user_inst.get_user_mail_manager().pending_to_remove_.empty() && cache.is_writable()) {
    int64_t del_mail_id = user_inst.get_user_mail_manager().pending_to_remove_.begin()->first;
    if (del_mail_id == 0) {
      user_inst.get_user_mail_manager().pending_to_remove_.erase(
          user_inst.get_user_mail_manager().pending_to_remove_.begin());
      ++ret;
      continue;
    }

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::remove_all(get_shared_context(), del_mail_id));
    // 如果之前已经删掉了，直接忽略
    if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      FCTXLOGWARNING(get_shared_context(), "delete mail content of mail_id {} failed, res: {}({})", user_inst,
                     del_mail_id, res, protobuf_mini_dumper_get_error_msg(res));
      break;
    }

    if (res == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      FCTXLOGINFO(get_shared_context(), "delete mail content of mail_id {}, but maild already removed before.",
                  user_inst, del_mail_id);
    } else {
      FCTXLOGDEBUG(get_shared_context(), "delete mail content of mail_id {} success.", user_inst, del_mail_id);
    }

    ++ret;

    // 移除待删除索引
    user_inst.get_user_mail_manager().pending_to_remove_.erase(del_mail_id);

    // 执行时间过长则中断，下一次再启动流程
    if ((param_.timeout_timepoint - atfw::util::time::time_utility::now()) < (param_.timeout_duration / 2)) {
      need_restart_ = true;
      break;
    }
  }

  RPC_RETURN_CODE(ret);
}

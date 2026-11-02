// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#include "logic/mail/task_action_global_mail_async_jobs.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atframe/etcdcli/etcd_discovery.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.mail.common.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_context.h>

#include <config/excel_config_mail_index.h>
#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <logic/hpa/logic_hpa_easy_api.h>
#include <logic/logic_server_setup.h>

#include <memory>
#include <vector>

#include "logic/mail/global_mail_manager.h"

task_action_global_mail_async_jobs::task_action_global_mail_async_jobs(ctor_param_t&& param)
    : task_action_no_req_base(param),
      param_(param),
      fetch_mail_number_(0),
      remove_mail_number_(0),
      fetch_timepoint_(std::chrono::system_clock::from_time_t(0)) {}

task_action_global_mail_async_jobs::~task_action_global_mail_async_jobs() {}

task_action_global_mail_async_jobs::result_type task_action_global_mail_async_jobs::operator()() {
  fetch_mail_number_ = 0;
  remove_mail_number_ = 0;

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;

  fetch_timepoint_ = get_shared_context().logical_now();

  bool is_zone_master = false;
  bool is_global_master = false;
  uint64_t self_node_id = logic_config::me()->get_local_server_id();
  uint32_t self_zone_id = logic_config::me()->get_local_zone_id();
  logic_server_common_module* mod = logic_server_last_common_module();
  if (mod != nullptr) {
    is_zone_master = true;
    is_global_master = true;
    auto lobbsvr_index =
        mod->get_discovery_index_by_type(static_cast<uint64_t>(::atfw::component::logic_service_type::kLobbySvr));
    if (lobbsvr_index && !lobbsvr_index->empty()) {
      for (const auto& gamesvr : lobbsvr_index->get_sorted_nodes(
               logic_hpa_discovery_select(static_cast<uint64_t>(::atfw::component::logic_service_type::kLobbySvr),
                                          logic_hpa_discovery_select_mode::kReady))) {
        if (!is_global_master && !is_zone_master) {
          break;
        }

        if (!gamesvr) {
          continue;
        }

        if (gamesvr->get_discovery_info().id() < self_node_id) {
          is_global_master = false;

          if (gamesvr->get_discovery_info().area().zone_id() == self_zone_id) {
            is_zone_master = false;
          }
        }
      }
    }
  }

  // 拉取邮件记录
  uint32_t zone_ids[] = {0, logic_config::me()->get_local_zone_id()};
  auto major_types_ref = excel::mail::get_all_global_mail_major_types();
  for (auto& zone_id : zone_ids) {
    if (ret < 0) {
      break;
    }
    for (const auto& major_type : major_types_ref.second.get()) {
      if (ret < 0) {
        break;
      }

      auto db_data = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_mail_global_index>(get_shared_context());
      uint64_t db_version = 0;
      int res = RPC_AWAIT_CODE_RESULT(
          rpc::db::mail_global_index::get_all(get_shared_context(), zone_id, major_type, *db_data, db_version));
      if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
        FCTXLOGERROR(get_shared_context(),
                     "task_action_global_mail_async_jobs get_global_mail({}, {}) failed, res: {}({})", zone_id,
                     major_type, res, protobuf_mini_dumper_get_error_msg(res));
        ret = res;
        break;
      }

      bool need_update = global_mail_manager::me()->update_from_db(
          get_shared_context(), zone_id, major_type, *db_data->mutable_blob_data(),
          (zone_id == 0 && is_global_master) || is_zone_master);
      // 主节点更新删除无效数据
      if (need_update) {
        res = RPC_AWAIT_CODE_RESULT(rpc::db::mail_global_index::replace(get_shared_context(), db_data, db_version));
        if (res < 0) {
          FCTXLOGINFO(
              get_shared_context(),
              "task_action_global_mail_async_jobs replace_global_mail({}, {}) failed, res: {}({}), will retry on next "
              "round",
              zone_id, major_type, res, protobuf_mini_dumper_get_error_msg(res));
          if (res <= PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING &&
              res >= PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED) {
            ret = res;
          }
        } else {
          FWLOGINFO("task_action_global_mail_async_jobs replace_global_mail({}, {}) success", zone_id, major_type);
        }
      }
    }
  }

  if (ret < 0) {
    TASK_ACTION_RETURN_CODE(ret);
  }

  // 拉取没有数据的邮件
  int res = RPC_AWAIT_CODE_RESULT(fetch_mail_contents());
  if (res >= 0) {
    fetch_mail_number_ += static_cast<size_t>(res);
  } else {
    ret = res;
    TASK_ACTION_RETURN_CODE(ret);
  }

  // 删除待删除队列
  res = RPC_AWAIT_CODE_RESULT(cleanup_removed_mails());
  if (res >= 0) {
    remove_mail_number_ += static_cast<size_t>(res);
  } else {
    ret = res;
  }

  global_mail_manager::me()->resolve_limits();
  TASK_ACTION_RETURN_CODE(ret);
}

rpc::result_code_type task_action_global_mail_async_jobs::fetch_mail_contents() {
  if (global_mail_manager::is_instance_destroyed()) {
    RPC_RETURN_CODE(0);
  }

  std::vector<int64_t> mail_unloaded;
  global_mail_manager::me()->fetch_all_unloaded_mails(get_shared_context(), mail_unloaded);
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

  for (const auto& pulled_mail : mail_contents) {
    if (!pulled_mail.message) {
      continue;
    }

    auto mail_id = (*pulled_mail.message)->mail_id();
    undo_mails.erase(mail_id);
    global_mail_manager::me()->set_mail_content_loaded(mail_id);

    mail_data_ptr_t mail = global_mail_manager::me()->get_mail_raw(mail_id);
    // 可能已经被移除,如果被移除了，忽略即可
    if (mail && mail->record) {
      if (!mail->content) {
        mail->content = gsl::make_unique<PROJECT_NAMESPACE_ID::DMailContent>();
      }
      protobuf_copy_message(*mail->content, (*pulled_mail.message)->blob_data().mail_content());
      mail->record->set_fetch_error_count(0);

      FCTXLOGDEBUG(get_shared_context(), "global_mail_manager fetch mail content of mail_id {} success", mail_id);
      if (ret >= 0) {
        ++ret;
      }
    } else {
      FCTXLOGDEBUG(get_shared_context(),
                   "global_mail_manager fetch mail content of mail_id {} success, but maybe already removed", mail_id);
    }
  }

  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    ret = 0;
  }

  if (ret >= 0) {
    // 如果成功，则要处理一下无效的脏数据
    for (const auto& dirty_mail_id : undo_mails) {
      mail_data_ptr_t mail = global_mail_manager::me()->get_mail_raw(dirty_mail_id);
      // 可能已经被移除,如果被移除了，忽略即可
      // 可能是邮件内容被删除了，这时候要清理一下索引，这里设置错误容忍值是为了适应万一数据库短期故障
      if (mail && mail->record) {
        mail->record->set_fetch_error_count(mail->record->fetch_error_count() + 1);
        if (mail->record->fetch_error_count() > PROJECT_NAMESPACE_ID::EN_CL_MAIL_PLAYER_TOLERANCE_ERROR_COUNT) {
          global_mail_manager::me()->remove_global_mail(get_shared_context(), dirty_mail_id);
        }
      } else {
        global_mail_manager::me()->remove_global_mail(get_shared_context(), dirty_mail_id);
      }
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type task_action_global_mail_async_jobs::cleanup_removed_mails() {
  int ret = 0;

  // 执行过程中如果关机或者宕机，会遗留一些全服邮件没删除。这里就让它泄漏吧，反正时数据库冷数据而且也没多少。
  while (!global_mail_manager::is_instance_destroyed() &&
         !global_mail_manager::me()->pending_to_remove_contents_.empty()) {
    int64_t del_mail_id = *global_mail_manager::me()->pending_to_remove_contents_.begin();
    if (del_mail_id == 0) {
      global_mail_manager::me()->pending_to_remove_contents_.erase(
          global_mail_manager::me()->pending_to_remove_contents_.begin());
      ++ret;
      continue;
    }

    int res = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::remove_all(get_shared_context(), del_mail_id));
    // 如果之前已经删掉了，直接忽略
    if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      FCTXLOGWARNING(get_shared_context(), "global_mail_manager delete mail content of mail_id {} failed, res: {}({})",
                     del_mail_id, res, protobuf_mini_dumper_get_error_msg(res));
      break;
    }

    if (res == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      FCTXLOGINFO(get_shared_context(),
                  "global_mail_manager delete mail content of mail_id {}, but maild already removed before.",
                  del_mail_id);
    } else {
      FCTXLOGDEBUG(get_shared_context(), "global_mail_manager delete mail content of mail_id {} success.", del_mail_id);
    }

    global_mail_manager::me()->pending_to_remove_contents_.erase(del_mail_id);
    ++ret;
  }

  RPC_RETURN_CODE(ret);
}

int task_action_global_mail_async_jobs::on_success() {
  FCTXLOGINFO(get_shared_context(), "global mail async jobs success, fetch {} mail(s), remove {} mail(s)",
              fetch_mail_number_, remove_mail_number_);

  if (task_type_trait::get_task_id(global_mail_manager::me()->mail_async_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(global_mail_manager::me()->mail_async_task_);
  }

  if (fetch_timepoint_ > global_mail_manager::me()->last_success_fetch_timepoint_) {
    global_mail_manager::me()->last_success_fetch_timepoint_ = fetch_timepoint_;
  }

  return get_result();
}

int task_action_global_mail_async_jobs::on_failed() {
  FCTXLOGERROR(get_shared_context(),
               "global mail async jobs failed, res: {}(successed with fetch {} mail(s), remove {} mail(s))",
               get_result(), fetch_mail_number_, remove_mail_number_);

  if (task_type_trait::get_task_id(global_mail_manager::me()->mail_async_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(global_mail_manager::me()->mail_async_task_);
  }

  return get_result();
}

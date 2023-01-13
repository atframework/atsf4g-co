// Copyright 2021 atframework
// Created by owent on 2019-06-20.
//

#include "router/action/task_action_router_close_manager_set.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <libatbus_protocol.h>

#include <dispatcher/task_manager.h>

#include <rpc/rpc_async_invoke.h>

#include <utility>

#include "router/router_manager_base.h"
#include "router/router_manager_set.h"
#include "router/router_object_base.h"

task_action_router_close_manager_set::task_action_router_close_manager_set(ctor_param_t &&param)
    : task_action_no_req_base(param), param_(param), status_data_(std::make_shared<status_data_t>()) {
  status_data_->success_count_ = 0;
  status_data_->failed_count_ = 0;
  status_data_->current_idx_ = 0;
}

task_action_router_close_manager_set::~task_action_router_close_manager_set() {}

task_action_router_close_manager_set::result_type task_action_router_close_manager_set::operator()() {
  FWLOGINFO("router close task started");
  status_data_->success_count_ = 0;
  status_data_->failed_count_ = 0;

  util::time::time_utility::update();

  size_t closing_action_batch_count = logic_config::me()->get_cfg_router().closing_action_batch_count();
  std::vector<task_type_trait::task_type> pending_action_batch_tasks;
  pending_action_batch_tasks.reserve(closing_action_batch_count);

  while (param_.pending_list && status_data_->current_idx_ < param_.pending_list->size()) {
    std::shared_ptr<status_data_t> status_data = status_data_;
    pending_list_ptr_t pending_list = param_.pending_list;
    auto invoke_task = rpc::async_invoke(
        get_shared_context(), "task_action_router_close_manager_set", [status_data, pending_list](rpc::context &ctx) {
          router_object_ptr_t obj = (*pending_list)[status_data->current_idx_];
          ++status_data->current_idx_;

          // 如果已下线并且用户缓存失效则跳过
          if (!obj) {
            RPC_RETURN_CODE(0);
          }

          // 已降级或不是实体，不需要保存
          if (!obj->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
            RPC_RETURN_CODE(0);
          }

          router_manager_base *mgr = router_manager_set::me()->get_manager(obj->get_key().type_id);
          if (nullptr == mgr) {
            FWLOGERROR("router close task save router object {}({}:{}:{}) but can not find manager", obj->name(),
                       obj->get_key().type_id, obj->get_key().zone_id, obj->get_key().object_id);
            ++status_data->failed_count_;
            RPC_RETURN_CODE(0);
          }

          // 管理器中的对象已被替换或移除则跳过
          if (mgr->get_base_cache(obj->get_key()) != obj) {
            RPC_RETURN_CODE(0);
          }

          // 降级的时候会保存
          auto res = RPC_AWAIT_CODE_RESULT(mgr->remove_object(ctx, obj->get_key(), obj, nullptr));

          if (TASK_COMPAT_CHECK_IS_TIMEOUT()) {
            FWLOGERROR("router close task save router object {}({}:{}:{}) timeout", obj->name(), obj->get_key().type_id,
                       obj->get_key().zone_id, obj->get_key().object_id);
            ++status_data->failed_count_;
            RPC_RETURN_CODE(0);
          }

          if (TASK_COMPAT_CHECK_IS_CANCEL()) {
            FWLOGERROR("router close task save router object {}({}:{}:{}) but cancelled", obj->name(),
                       obj->get_key().type_id, obj->get_key().zone_id, obj->get_key().object_id);
            ++status_data->failed_count_;
            RPC_RETURN_CODE(0);
          }

          if (TASK_COMPAT_CHECK_IS_FAULT()) {
            FWLOGERROR("router close task save router object {}({}:{}:{}) but killed", obj->name(),
                       obj->get_key().type_id, obj->get_key().zone_id, obj->get_key().object_id);
            ++status_data->failed_count_;
            RPC_RETURN_CODE(0);
          }

          if (res < 0) {
            FWLOGERROR("router close task save router object {}({}:{}:{}) failed", obj->name(), obj->get_key().type_id,
                       obj->get_key().zone_id, obj->get_key().object_id);
            ++status_data->failed_count_;
          } else {
            FWLOGINFO("router close task save router object {}({}:{}:{}) success", obj->name(), obj->get_key().type_id,
                      obj->get_key().zone_id, obj->get_key().object_id);
            ++status_data->success_count_;
          }
          RPC_RETURN_CODE(0);
        });
    bool need_wait = false;
    if (invoke_task.is_success()) {
      if (!(*invoke_task.get_success())->is_exiting()) {
        pending_action_batch_tasks.emplace_back(std::move(*invoke_task.get_success()));
      }
    } else {
      need_wait = true;
    }
    if (pending_action_batch_tasks.size() >= closing_action_batch_count) {
      need_wait = true;
    }

    if (need_wait) {
      auto wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(get_shared_context(), pending_action_batch_tasks));
      pending_action_batch_tasks.clear();
      if (wait_result < 0) {
        FWLOGERROR("Wait sub tasks to remove_object failed, result: {}({})", wait_result,
                   protobuf_mini_dumper_get_error_msg(wait_result));
      }
    }
  }

  if (!pending_action_batch_tasks.empty()) {
    auto wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(get_shared_context(), pending_action_batch_tasks));
    pending_action_batch_tasks.clear();
    if (wait_result < 0) {
      FWLOGERROR("Wait sub tasks to remove_object failed, result: {}({})", wait_result,
                 protobuf_mini_dumper_get_error_msg(wait_result));
    }
  }

  // 如果超时了可能被强杀，这时候要强制触发保存
  if (TASK_COMPAT_CHECK_IS_EXITING()) {
    RPC_AWAIT_IGNORE_RESULT(save_fallback());
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type task_action_router_close_manager_set::save_fallback() {
  while (param_.pending_list && status_data_->current_idx_ < param_.pending_list->size()) {
    router_object_ptr_t obj = (*param_.pending_list)[status_data_->current_idx_];
    ++status_data_->current_idx_;

    // 如果已下线并且用户缓存失效则跳过
    if (!obj) {
      continue;
    }

    // 已降级或不是实体，不需要保存
    if (!obj->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
      continue;
    }

    router_manager_base *mgr = router_manager_set::me()->get_manager(obj->get_key().type_id);
    if (nullptr == mgr) {
      FWLOGERROR("router close task save router object {}({}:{}:{}) but can not find manager", obj->name(),
                 obj->get_key().type_id, obj->get_key().zone_id, obj->get_key().object_id);
      ++status_data_->failed_count_;
      continue;
    }

    // 管理器中的对象已被替换或移除则跳过
    if (mgr->get_base_cache(obj->get_key()) != obj) {
      continue;
    }

    // 降级的时候会保存
    RPC_AWAIT_IGNORE_RESULT(mgr->remove_object(get_shared_context(), obj->get_key(), obj, nullptr));

    FWLOGWARNING(
        "router close task save router object {}({}:{}:{}) for fallback(task killed), we don't know if it's success "
        "to save to DB",
        obj->name(), obj->get_key().type_id, obj->get_key().zone_id, obj->get_key().object_id);
  }

  RPC_RETURN_CODE(0);
}

int task_action_router_close_manager_set::on_success() {
  if (task_type_trait::get_task_id(router_manager_set::me()->closing_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(router_manager_set::me()->closing_task_);
  }

  FWLOGINFO("router close task done.(success save: {}, failed save: {})", status_data_->success_count_,
            status_data_->failed_count_);
  return get_result();
}

int task_action_router_close_manager_set::on_failed() {
  if (task_type_trait::get_task_id(router_manager_set::me()->closing_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(router_manager_set::me()->closing_task_);
  }

  FWLOGERROR("router close task failed.(success save: {}, failed save: {}) ret: {}", status_data_->success_count_,
             status_data_->failed_count_, get_result());
  return get_result();
}

int task_action_router_close_manager_set::on_timeout() {
  if (task_type_trait::get_task_id(router_manager_set::me()->closing_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(router_manager_set::me()->closing_task_);
  }

  FWLOGWARNING("router close task timeout, we will continue on next round.");
  return 0;
}

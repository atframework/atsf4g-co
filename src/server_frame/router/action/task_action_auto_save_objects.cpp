// Copyright 2022 atframework
// Created by owent on 2018-05-01.
//

#include "router/action/task_action_auto_save_objects.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <time/time_utility.h>

#include <config/logic_config.h>

#include <libatbus_protocol.h>

#include <dispatcher/task_manager.h>

#include <logic/logic_server_setup.h>
#include <rpc/rpc_async_invoke.h>
#include <utility/protobuf_mini_dumper.h>

#include <utility>
#include <vector>

#include "router/router_manager_base.h"
#include "router/router_manager_set.h"
#include "router/router_object_base.h"

#include "rpc/telemetry/rpc_global_service.h"

bool task_action_auto_save_objects::debug_receive_stop_when_running = false;

task_action_auto_save_objects::task_action_auto_save_objects(ctor_param_t &&param)
    : task_action_no_req_base(param), status_data_(atfw::memory::stl::make_shared<status_data_t>()) {
  status_data_->success_count_ = 0;
  status_data_->failed_count_ = 0;
  status_data_->start_timepooint_ = 0;
  status_data_->action_remove_object_count = 0;
  status_data_->action_remove_cache_count = 0;
  status_data_->action_save_count = 0;
}

task_action_auto_save_objects::~task_action_auto_save_objects() {}

task_action_auto_save_objects::result_type task_action_auto_save_objects::operator()() {
  FWLOGINFO("auto save task started");
  atfw::util::time::time_utility::update();

  status_data_->success_count_ = 0;
  status_data_->failed_count_ = 0;
  status_data_->start_timepooint_ = atfw::util::time::time_utility::get_sys_now();
  status_data_->action_remove_object_count = 0;
  status_data_->action_remove_cache_count = 0;
  status_data_->action_save_count = 0;
  uint64_t left_action_count = logic_config::me()->get_cfg_router().pending_action_max_count();
  uint64_t pending_action_batch_count = logic_config::me()->get_cfg_router().pending_action_batch_count();
  std::vector<task_type_trait::task_type> pending_action_batch_tasks;
  pending_action_batch_tasks.reserve(pending_action_batch_count);

  while (left_action_count > 0) {
    --left_action_count;
    atfw::util::time::time_utility::update();
    if (router_manager_set::me()->pending_action_list_.empty()) {
      break;
    }

    std::shared_ptr<status_data_t> status_data = status_data_;
    auto invoke_task = rpc::async_invoke(
        get_shared_context(), "task_action_auto_save_objects",
        [status_data](rpc::context &ctx) -> rpc::result_code_type {
          router_manager_set::pending_action_data auto_save =
              std::move(router_manager_set::me()->pending_action_list_.front());
          router_manager_set::me()->pending_action_list_.pop_front();

          // 如果已下线并且用户缓存失效则跳过
          if (!auto_save.object) {
            RPC_RETURN_CODE(0);
          }

          if (debug_receive_stop_when_running) {
            debug_receive_stop_when_running = false;

            logic_server_common_module *mod = logic_server_last_common_module();
            if (NULL != mod) {
              mod->debug_stop_app();
            }
          }

          rpc::result_code_type::value_type res = 0;
          switch (auto_save.action) {
            case router_manager_set::EN_ASA_REMOVE_OBJECT: {
              // 有可能在一系列异步流程后又被mutable_object()了，这时候要放弃降级
              if (false == auto_save.object->check_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_OBJECT)) {
                break;
              }

              router_manager_base *mgr = router_manager_set::me()->get_manager(auto_save.type_id);
              if (nullptr != mgr) {
                ++status_data->action_remove_object_count;

                int32_t result = RPC_AWAIT_CODE_RESULT(
                    mgr->remove_object(ctx, auto_save.object->get_key(), auto_save.object, nullptr));
                // 失败且期间未升级或mutable_object()，下次重试的时候也要走降级流程
                if (result < 0 &&
                    auto_save.object->check_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_OBJECT)) {
                  auto_save.object->set_flag(router_object_base::flag_t::EN_ROFT_FORCE_REMOVE_OBJECT);
                }
              }
              break;
            }
            case router_manager_set::EN_ASA_SAVE: {
              // 有可能有可能手动触发了保存，导致多一次冗余的auto_save_data_t，就不需要再保存一次了
              if (false == auto_save.object->check_flag(router_object_base::flag_t::EN_ROFT_SCHED_SAVE_OBJECT)) {
                break;
              }

              ++status_data->action_save_count;
              res = RPC_AWAIT_CODE_RESULT(auto_save.object->save(ctx, nullptr));

              if (res >= 0) {
                auto_save.object->refresh_save_time();
              }
              break;
            }
            case router_manager_set::EN_ASA_REMOVE_CACHE: {
              // 有可能在一系列异步流程后缓存被续期了，这时候要放弃移除缓存
              if (false == auto_save.object->check_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE)) {
                break;
              }

              router_manager_base *mgr = router_manager_set::me()->get_manager(auto_save.type_id);
              if (nullptr != mgr) {
                ++status_data->action_remove_cache_count;

                RPC_AWAIT_IGNORE_RESULT(mgr->remove_cache(ctx, auto_save.object->get_key(), auto_save.object, nullptr));
              }
              break;
            }
            default: {
              break;
            }
          }

          if (PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT == res) {
            FWLOGERROR("auto do {} to router object {}({}:{}:{}) timeout", get_action_name(auto_save.action),
                       auto_save.object->name(), auto_save.object->get_key().type_id,
                       auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id);
            ++status_data->failed_count_;
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
          }

          if (PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED == res) {
            FWLOGERROR("auto do {} to router object {}({}:{}:{}) but cancelled", get_action_name(auto_save.action),
                       auto_save.object->name(), auto_save.object->get_key().type_id,
                       auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id);
            ++status_data->failed_count_;
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
          }

          if (PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED == res) {
            FWLOGERROR("auto do {} to router object {}({}:{}:{}) but killed", get_action_name(auto_save.action),
                       auto_save.object->name(), auto_save.object->get_key().type_id,
                       auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id);
            ++status_data->failed_count_;
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
          }

          if (res < 0) {
            FWLOGERROR("auto do {} to router object {}({}:{}:{}) failed, res: {}", get_action_name(auto_save.action),
                       auto_save.object->name(), auto_save.object->get_key().type_id,
                       auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id, res);
            ++status_data->failed_count_;
          } else {
            FWLOGINFO("auto do {} to router object {}({}:{}:{}) success", get_action_name(auto_save.action),
                      auto_save.object->name(), auto_save.object->get_key().type_id,
                      auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id);
            ++status_data->success_count_;
          }

          RPC_RETURN_CODE(0);
        });
    bool need_wait = false;
    if (invoke_task.is_success()) {
      if (!task_type_trait::is_exiting(*invoke_task.get_success())) {
        pending_action_batch_tasks.emplace_back(std::move(*invoke_task.get_success()));
      }
    } else {
      need_wait = true;
    }
    if (pending_action_batch_tasks.size() >= pending_action_batch_count) {
      need_wait = true;
    }

    if (need_wait) {
      auto wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(get_shared_context(), pending_action_batch_tasks));
      pending_action_batch_tasks.clear();
      if (wait_result < 0) {
        FWLOGERROR("Wait sub tasks to failed, result: {}({})", wait_result,
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

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_auto_save_objects::on_success() {
  if (task_type_trait::get_task_id(router_manager_set::me()->pending_action_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(router_manager_set::me()->pending_action_task_);
  }

  FWLOGWARNING("auto save task done.(success save: {}, failed save: {})", status_data_->success_count_,
               status_data_->failed_count_);

  if (0 == status_data_->success_count_ && 0 == status_data_->failed_count_) {
    FWLOGWARNING("there is no need to start a auto save task when no object need save.");
  }
  return get_result();
}

int task_action_auto_save_objects::on_failed() {
  if (task_type_trait::get_task_id(router_manager_set::me()->pending_action_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(router_manager_set::me()->pending_action_task_);
  }

  FWLOGERROR("auto save task failed.(success save: {}, failed save: {}) ret: {}", status_data_->success_count_,
             status_data_->failed_count_, get_result());
  return get_result();
}

int task_action_auto_save_objects::on_timeout() {
  if (task_type_trait::get_task_id(router_manager_set::me()->pending_action_task_) ==
      get_shared_context().get_task_context().task_id) {
    task_type_trait::reset_task(router_manager_set::me()->pending_action_task_);
  }

  FWLOGWARNING("auto save task timeout(run {} seconds), we will continue on next round.",
               atfw::util::time::time_utility::get_sys_now() - status_data_->start_timepooint_);
  return 0;
}

int task_action_auto_save_objects::on_complete() {
  std::shared_ptr<::rpc::telemetry::group_type> telemetry_lifetime;
  opentelemetry::context::Context telemetry_context;
  if (status_data_->action_save_count > 0) {
    auto instrument = rpc::telemetry::global_service::mutable_metrics_counter_uint64(
        "service_router_auto_action_save", {"service_router_auto_action_save"});
    if (instrument) {
      instrument->Add(static_cast<uint64_t>(status_data_->action_save_count),
                      rpc::telemetry::global_service::get_metrics_labels(telemetry_lifetime), telemetry_context);
    }
  }

  if (status_data_->action_remove_cache_count > 0) {
    auto instrument = rpc::telemetry::global_service::mutable_metrics_counter_uint64(
        "service_router_auto_action_remove_cache", {"service_router_auto_action_remove_cache"});
    if (instrument) {
      instrument->Add(static_cast<uint64_t>(status_data_->action_remove_cache_count),
                      rpc::telemetry::global_service::get_metrics_labels(telemetry_lifetime), telemetry_context);
    }
  }

  if (status_data_->action_remove_object_count > 0) {
    auto instrument = rpc::telemetry::global_service::mutable_metrics_counter_uint64(
        "service_router_auto_action_remove_object", {"service_router_auto_action_remove_object"});
    if (instrument) {
      instrument->Add(static_cast<uint64_t>(status_data_->action_remove_object_count),
                      rpc::telemetry::global_service::get_metrics_labels(telemetry_lifetime), telemetry_context);
    }
  }
  return 0;
}

const char *task_action_auto_save_objects::get_action_name(uint32_t act) {
  switch (act) {
    case router_manager_set::EN_ASA_SAVE: {
      return "save";
    }
    case router_manager_set::EN_ASA_REMOVE_OBJECT: {
      return "remove object";
    }
    case router_manager_set::EN_ASA_REMOVE_CACHE: {
      return "remove cache";
    }
    default: {
      return "unknown action name";
    }
  }
}

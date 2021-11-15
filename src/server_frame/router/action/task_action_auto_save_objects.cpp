// Copyright 2021 atframework
// Created by owent on 2018/05/01.
//

#include "router/action/task_action_auto_save_objects.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <time/time_utility.h>

#include <config/logic_config.h>

#include <libatbus_protocol.h>

#include <logic/logic_server_setup.h>

#include <utility>

#include "router/router_manager_base.h"
#include "router/router_manager_set.h"
#include "router/router_object_base.h"

bool task_action_auto_save_objects::debug_receive_stop_when_running = false;

task_action_auto_save_objects::task_action_auto_save_objects(ctor_param_t &&param)
    : task_action_no_req_base(param), success_count_(0), failed_count_(0), start_timepooint_(0) {}

task_action_auto_save_objects::~task_action_auto_save_objects() {}

int task_action_auto_save_objects::operator()() {
  FWLOGINFO("auto save task started");
  util::time::time_utility::update();

  success_count_ = failed_count_ = 0;
  start_timepooint_ = util::time::time_utility::get_sys_now();
  while (true) {
    util::time::time_utility::update();
    if (router_manager_set::me()->save_list_.empty()) {
      break;
    }

    router_manager_set::auto_save_data_t auto_save = std::move(router_manager_set::me()->save_list_.front());
    router_manager_set::me()->save_list_.pop_front();

    // 如果已下线并且用户缓存失效则跳过
    if (!auto_save.object) {
      continue;
    }

    if (debug_receive_stop_when_running) {
      debug_receive_stop_when_running = false;

      logic_server_common_module *mod = logic_server_last_common_module();
      if (NULL != mod) {
        mod->debug_stop_app();
      }
    }

    int res = 0;
    switch (auto_save.action) {
      case router_manager_set::EN_ASA_SAVE: {
        // 有可能有可能手动触发了保存，导致多一次冗余的auto_save_data_t，就不需要再保存一次了
        if (false == auto_save.object->check_flag(router_object_base::flag_t::EN_ROFT_SCHED_SAVE_OBJECT)) {
          break;
        }

        res = auto_save.object->save(nullptr);

        if (res >= 0) {
          auto_save.object->refresh_save_time();
        }
        break;
      }
      case router_manager_set::EN_ASA_REMOVE_OBJECT: {
        // 有可能在一系列异步流程后又被mutable_object()了，这时候要放弃降级
        if (false == auto_save.object->check_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_OBJECT)) {
          break;
        }

        router_manager_base *mgr = router_manager_set::me()->get_manager(auto_save.type_id);
        if (nullptr != mgr) {
          mgr->remove_object(auto_save.object->get_key(), auto_save.object, nullptr);
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
          mgr->remove_cache(auto_save.object->get_key(), auto_save.object, nullptr);
        }
        break;
      }
      default: {
        break;
      }
    }

    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_TIMEOUT == res) {
      FWLOGERROR("auto do {} to router object {}({}:{}:{}) timeout", get_action_name(auto_save.action),
                 auto_save.object->name(), auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id,
                 auto_save.object->get_key().object_id);
      ++failed_count_;
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
    }

    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED == res) {
      FWLOGERROR("auto do {} to router object {}({}:{}:{}) but cancelled", get_action_name(auto_save.action),
                 auto_save.object->name(), auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id,
                 auto_save.object->get_key().object_id);
      ++failed_count_;
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
    }

    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED == res) {
      FWLOGERROR("auto do {} to router object {}({}:{}:{}) but killed", get_action_name(auto_save.action),
                 auto_save.object->name(), auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id,
                 auto_save.object->get_key().object_id);
      ++failed_count_;
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
    }

    if (res < 0) {
      FWLOGERROR("auto do {} to router object {}({}:{}:{}) failed, res: %d", get_action_name(auto_save.action),
                 auto_save.object->name(), auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id,
                 auto_save.object->get_key().object_id, res);
      ++failed_count_;
    } else {
      FWLOGINFO("auto do {} to router object {}({}:{}:{}) success", get_action_name(auto_save.action),
                auto_save.object->name(), auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id,
                auto_save.object->get_key().object_id);
      ++success_count_;
    }
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_auto_save_objects::on_success() {
  if (router_manager_set::me()->save_task_.get() == task_manager::task_t::this_task()) {
    router_manager_set::me()->save_task_.reset();
  }

  FWLOGWARNING("auto save task done.(success save: {}, failed save: {})", success_count_, failed_count_);

  if (0 == success_count_ && 0 == failed_count_) {
    FWLOGWARNING("there is no need to start a auto save task when no object need save.");
  }
  return get_result();
}

int task_action_auto_save_objects::on_failed() {
  if (router_manager_set::me()->save_task_.get() == task_manager::task_t::this_task()) {
    router_manager_set::me()->save_task_.reset();
  }

  FWLOGERROR("auto save task failed.(success save: {}, failed save: {}) ret: {}", success_count_, failed_count_,
             get_result());
  return get_result();
}

int task_action_auto_save_objects::on_timeout() {
  if (router_manager_set::me()->save_task_.get() == task_manager::task_t::this_task()) {
    router_manager_set::me()->save_task_.reset();
  }

  FWLOGWARNING("auto save task timeout(run {} seconds), we will continue on next round.",
               util::time::time_utility::get_sys_now() - start_timepooint_);
  return 0;
}

const char *task_action_auto_save_objects::get_action_name(uint32_t act) const {
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
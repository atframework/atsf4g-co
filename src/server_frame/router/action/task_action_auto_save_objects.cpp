//
// Created by owent on 2018/05/01.
//

#include <log/log_wrapper.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <libatbus_protocol.h>

#include "../router_manager_base.h"
#include "../router_manager_set.h"
#include "../router_object_base.h"
#include "task_action_auto_save_objects.h"

task_action_auto_save_objects::task_action_auto_save_objects(ctor_param_t COPP_MACRO_RV_REF param) : success_count_(0), failed_count_(0) {}

task_action_auto_save_objects::~task_action_auto_save_objects() {}

int task_action_auto_save_objects::operator()() {
    WLOGINFO("auto save task started");
    success_count_ = failed_count_ = 0;
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

        int res = 0;
        switch (auto_save.action) {
        case router_manager_set::EN_ASA_SAVE: {
            res = auto_save.object->save(UTIL_CONFIG_NULLPTR);

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
            if (UTIL_CONFIG_NULLPTR != mgr) {
                mgr->remove_object(auto_save.object->get_key(), auto_save.object, UTIL_CONFIG_NULLPTR);
            }
            break;
        }
        case router_manager_set::EN_ASA_REMOVE_CACHE: {
            // 有可能在一系列异步流程后缓存被续期了，这时候要放弃移除缓存
            if (false == auto_save.object->check_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE)) {
                break;
            }

            router_manager_base *mgr = router_manager_set::me()->get_manager(auto_save.type_id);
            if (UTIL_CONFIG_NULLPTR != mgr) {
                mgr->remove_cache(auto_save.object->get_key(), auto_save.object, UTIL_CONFIG_NULLPTR);
            }
            break;
        }
        default: {
            break;
        }
        }

        if (hello::err::EN_SYS_TIMEOUT == res) {
            WLOGERROR("auto do %s to router object %s(%u:%u:0x%llx) timeout", get_action_name(auto_save.action), auto_save.object->name(),
                      auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id_ull());
            ++failed_count_;
            return hello::err::EN_SUCCESS;
        }

        if (hello::err::EN_SYS_RPC_TASK_CANCELLED == res) {
            WLOGWARNING("auto do %s to router object %s(%u:%u:0x%llx) but cancelled", get_action_name(auto_save.action), auto_save.object->name(),
                        auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id_ull());
            ++failed_count_;
            return hello::err::EN_SUCCESS;
        }

        if (hello::err::EN_SYS_RPC_TASK_KILLED == res) {
            WLOGERROR("auto do %s to router object %s(%u:%u:0x%llx) but killed", get_action_name(auto_save.action), auto_save.object->name(),
                      auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id_ull());
            ++failed_count_;
            return hello::err::EN_SUCCESS;
        }

        if (res < 0) {
            WLOGERROR("auto do %s to router object %s(%u:%u:0x%llx) failed, res: %d", get_action_name(auto_save.action), auto_save.object->name(),
                      auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id_ull(), res);
            ++failed_count_;
        } else {
            WLOGDEBUG("auto do %s to router object %s(%u:%u:0x%llx) success", get_action_name(auto_save.action), auto_save.object->name(),
                      auto_save.object->get_key().type_id, auto_save.object->get_key().zone_id, auto_save.object->get_key().object_id_ull());
            ++success_count_;
        }
    }

    return hello::err::EN_SUCCESS;
}

int task_action_auto_save_objects::on_success() {
    if (router_manager_set::me()->save_task_.get() == task_manager::task_t::this_task()) {
        router_manager_set::me()->save_task_.reset();
    }

    WLOGINFO("auto save task done.(success save: %d, failed save: %d)", success_count_, failed_count_);

    if (0 == success_count_ && 0 == failed_count_) {
        WLOGWARNING("there is no need to start a auto save task when no object need save.");
    }
    return get_ret_code();
}

int task_action_auto_save_objects::on_failed() {
    if (router_manager_set::me()->save_task_.get() == task_manager::task_t::this_task()) {
        router_manager_set::me()->save_task_.reset();
    }

    WLOGERROR("auto save task failed.(success save: %d, failed save: %d) ret: %d", success_count_, failed_count_, get_ret_code());
    return get_ret_code();
}

int task_action_auto_save_objects::on_timeout() {
    if (router_manager_set::me()->save_task_.get() == task_manager::task_t::this_task()) {
        router_manager_set::me()->save_task_.reset();
    }

    WLOGWARNING("auto save task timeout, we will continue on next round.");
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
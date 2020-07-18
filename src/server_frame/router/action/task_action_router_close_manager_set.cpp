//
// Created by owent on 2019/06/20.
//

#include <log/log_wrapper.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <libatbus_protocol.h>

#include "../router_manager_base.h"
#include "../router_manager_set.h"
#include "../router_object_base.h"
#include "task_action_router_close_manager_set.h"

task_action_router_close_manager_set::task_action_router_close_manager_set(ctor_param_t COPP_MACRO_RV_REF param)
    : param_(param), success_count_(0), failed_count_(0), current_idx_(0) {}

task_action_router_close_manager_set::~task_action_router_close_manager_set() {}

int task_action_router_close_manager_set::operator()() {
    WLOGINFO("router close task started");
    success_count_ = failed_count_ = 0;
    util::time::time_utility::update();

    task_manager::task_t *task = task_manager::task_t::this_task();
    if (!task) {
        WLOGERROR("current not in a task");
        return hello::err::EN_SYS_RPC_NO_TASK;
    }

    while (param_.pending_list && current_idx_ < param_.pending_list->size()) {
        router_object_ptr_t obj = (*param_.pending_list)[current_idx_];
        ++current_idx_;

        // 如果已下线并且用户缓存失效则跳过
        if (!obj) {
            continue;
        }

        // 已降级或不是实体，不需要保存
        if (!obj->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
            continue;
        }

        router_manager_base *mgr = router_manager_set::me()->get_manager(obj->get_key().type_id);
        if (UTIL_CONFIG_NULLPTR == mgr) {
            WLOGERROR("router close task save router object %s(%u:%u:0x%llx) but can not find manager", obj->name(), obj->get_key().type_id,
                      obj->get_key().zone_id, obj->get_key().object_id_ull());
            ++failed_count_;
            continue;
        }

        // 管理器中的对象已被替换或移除则跳过
        if (mgr->get_base_cache(obj->get_key()) != obj) {
            continue;
        }

        // 降级的时候会保存
        bool res = mgr->remove_object(obj->get_key(), obj, UTIL_CONFIG_NULLPTR);

        if (task->is_timeout()) {
            WLOGERROR("router close task save router object %s(%u:0x%llx) timeout", obj->name(), obj->get_key().type_id, obj->get_key().object_id_ull());
            ++failed_count_;
            break;
        }

        if (task->is_canceled()) {
            WLOGWARNING("router close task save router object %s(%u:0x%llx) but cancelled", obj->name(), obj->get_key().type_id,
                        obj->get_key().object_id_ull());
            ++failed_count_;
            break;
        }

        if (task->is_faulted()) {
            WLOGERROR("router close task save router object %s(%u:0x%llx) but killed", obj->name(), obj->get_key().type_id, obj->get_key().object_id_ull());
            ++failed_count_;
            break;
        }

        if (!res) {
            WLOGERROR("router close task save router object %s(%u:0x%llx) failed", obj->name(), obj->get_key().type_id, obj->get_key().object_id_ull());
            ++failed_count_;
        } else {
            WLOGINFO("router close task save router object %s(%u:0x%llx) success", obj->name(), obj->get_key().type_id, obj->get_key().object_id_ull());
            ++success_count_;
        }
    }

    // 如果超时了可能被强杀，这时候要强制触发保存
    if (task->is_exiting()) {
        save_fallback();
    }

    return hello::err::EN_SUCCESS;
}

void task_action_router_close_manager_set::save_fallback() {
    while (param_.pending_list && current_idx_ < param_.pending_list->size()) {
        router_object_ptr_t obj = (*param_.pending_list)[current_idx_];
        ++current_idx_;

        // 如果已下线并且用户缓存失效则跳过
        if (!obj) {
            continue;
        }

        // 已降级或不是实体，不需要保存
        if (!obj->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
            continue;
        }

        router_manager_base *mgr = router_manager_set::me()->get_manager(obj->get_key().type_id);
        if (UTIL_CONFIG_NULLPTR == mgr) {
            WLOGERROR("router close task save router object %s(%u:0x%llx) but can not find manager", obj->name(), obj->get_key().type_id,
                      obj->get_key().object_id_ull());
            ++failed_count_;
            continue;
        }

        // 管理器中的对象已被替换或移除则跳过
        if (mgr->get_base_cache(obj->get_key()) != obj) {
            continue;
        }

        // 降级的时候会保存
        mgr->remove_object(obj->get_key(), obj, UTIL_CONFIG_NULLPTR);

        WLOGWARNING("router close task save router object %s(%u:0x%llx) for fallback(task killed), we don't know if it's success to save to DB", obj->name(),
                    obj->get_key().type_id, obj->get_key().object_id_ull());
    }
}

int task_action_router_close_manager_set::on_success() {
    if (router_manager_set::me()->closing_task_.get() == task_manager::task_t::this_task()) {
        router_manager_set::me()->closing_task_.reset();
    }

    WLOGINFO("router close task done.(success save: %d, failed save: %d)", success_count_, failed_count_);
    return get_ret_code();
}

int task_action_router_close_manager_set::on_failed() {
    if (router_manager_set::me()->closing_task_.get() == task_manager::task_t::this_task()) {
        router_manager_set::me()->closing_task_.reset();
    }

    WLOGERROR("router close task failed.(success save: %d, failed save: %d) ret: %d", success_count_, failed_count_, get_ret_code());
    return get_ret_code();
}

int task_action_router_close_manager_set::on_timeout() {
    if (router_manager_set::me()->closing_task_.get() == task_manager::task_t::this_task()) {
        router_manager_set::me()->closing_task_.reset();
    }

    WLOGWARNING("router close task timeout, we will continue on next round.");
    return 0;
}

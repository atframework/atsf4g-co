//
// Created by owt50 on 2016/9/26.
//

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/extern_log_categorize.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include "task_action_base.h"
#include "task_manager.h"

namespace detail {
    struct task_action_stat_guard {
        task_action_stat_guard(task_action_base *act) : action(act) {
            if (NULL != action) {
                util::time::time_utility::update();
                start = util::time::time_utility::now();
            }
        }

        ~task_action_stat_guard() {
            if (NULL == action) {
                return;
            }

            util::time::time_utility::update();
            util::time::time_utility::raw_time_t end = util::time::time_utility::now();
            if (0 != action->get_player_id()) {
                WCLOGINFO(log_categorize_t::PROTO_STAT, "%s|%llu|%lldus|%d|%d", action->name(), action->get_player_id_llu(),
                          static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()), action->get_ret_code(),
                          action->get_rsp_code());
            } else {
                WCLOGINFO(log_categorize_t::PROTO_STAT, "%s|NO PLAYER|%lldus|%d|%d", action->name(),
                          static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()), action->get_ret_code(),
                          action->get_rsp_code());
            }
        }

        util::time::time_utility::raw_time_t start;
        task_action_base *action;
    };
} // namespace detail

task_action_base::task_action_base() : player_id_(0), task_id_(0), ret_code_(0), rsp_code_(0), rsp_msg_disabled_(false), evt_disabled_(false) {}
task_action_base::~task_action_base() {}

const char *task_action_base::name() const {
    const char *ret = typeid(*this).name();
    if (NULL == ret) {
        return "RTTI Unavailable: task_action_base";
    }

    // some compiler will generate number to mark the type
    while (ret && *ret >= '0' && *ret <= '9') {
        ++ret;
    }
    return ret;
}

int task_action_base::operator()(void *priv_data) {
    detail::task_action_stat_guard stat(this);

    // reinterpret_cast<task_manager::start_data_t*>(priv_data);
    //
    task_manager::task_t *task = cotask::this_task::get<task_manager::task_t>();
    if (NULL == task) {
        WLOGERROR("task convert failed, must in task.");
        return hello::err::EN_SYS_INIT;
    }

    task_id_ = task->get_id();
    ret_code_ = hook_run();

    if (evt_disabled_) {
        if (ret_code_ < 0) {
            WLOGERROR("task %s [0x%llx] without evt ret code %d, rsp code %d\n", name(), get_task_id_llu(), ret_code_, rsp_code_);
        } else {
            WLOGDEBUG("task %s [0x%llx] without evt ret code %d, rsp code %d\n", name(), get_task_id_llu(), ret_code_, rsp_code_);
        }

        if (!rsp_msg_disabled_) {
            send_rsp_msg();
        }
        return ret_code_;
    }
    // 响应OnSuccess(这时候任务的status还是running)
    if (cotask::EN_TS_RUNNING == task->get_status() && ret_code_ >= 0) {
        int ret = 0;
        if (rsp_code_ < 0) {
            ret = on_failed();
            WLOGINFO("task %s [0x%llx] finished success but response errorcode, rsp code: %d\n", name(), get_task_id_llu(), rsp_code_);
        } else {
            ret = on_success();
        }

        int complete_res = on_complete();
        if (0 != complete_res) {
            ret = complete_res;
        }

        if (!rsp_msg_disabled_) {
            send_rsp_msg();
        }
        return ret;
    }

    if (hello::err::EN_SUCCESS == ret_code_) {
        if (task->is_timeout()) {
            ret_code_ = hello::err::EN_SYS_TIMEOUT;
        } else if (task->is_faulted()) {
            ret_code_ = hello::err::EN_SYS_RPC_TASK_KILLED;
        } else if (task->is_canceled()) {
            ret_code_ = hello::err::EN_SYS_RPC_TASK_CANCELLED;
        } else if (task->is_exiting()) {
            ret_code_ = hello::err::EN_SYS_RPC_TASK_EXITING;
        } else {
            ret_code_ = hello::err::EN_SYS_UNKNOWN;
        }
    }

    if (hello::EN_SUCCESS == rsp_code_) {
        if (task->is_timeout()) {
            rsp_code_ = hello::EN_ERR_TIMEOUT;
        } else if (task->is_faulted()) {
            rsp_code_ = hello::EN_ERR_SYSTEM;
        } else if (task->is_canceled()) {
            rsp_code_ = hello::EN_ERR_SYSTEM;
        } else if (task->is_exiting()) {
            rsp_code_ = hello::EN_ERR_SYSTEM;
        } else {
            rsp_code_ = hello::EN_ERR_UNKNOWN;
        }
    }

    WLOGERROR("task %s [0x%llx] ret code %d, rsp code %d\n", name(), get_task_id_llu(), ret_code_, rsp_code_);

    // 响应OnTimeout
    if (cotask::EN_TS_TIMEOUT == task->get_status()) {
        on_timeout();
    }

    // 如果不是running且不是timeout，可能是其他原因被kill掉了，响应OnFailed
    int ret = on_failed();

    int complete_res = on_complete();
    if (0 != complete_res) {
        ret = complete_res;
    }

    if (!rsp_msg_disabled_) {
        send_rsp_msg();
    }

    return ret;
}

int task_action_base::hook_run() { return (*this)(); }

int task_action_base::on_success() { return 0; }

int task_action_base::on_failed() { return 0; }

int task_action_base::on_timeout() { return 0; }

int task_action_base::on_complete() { return 0; }

uint64_t task_action_base::get_task_id() const { return task_id_; }

unsigned long long task_action_base::get_task_id_llu() const { return static_cast<unsigned long long>(task_id_); }

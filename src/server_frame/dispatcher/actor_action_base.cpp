//
// Created by owt50 on 2016/11/14.
//

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/extern_log_categorize.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include "actor_action_base.h"


namespace detail {
    struct actor_action_stat_guard {
        actor_action_stat_guard(actor_action_base *act) : action(act) {
            if (NULL != action) {
                util::time::time_utility::update();
                start = util::time::time_utility::now();
            }
        }

        ~actor_action_stat_guard() {
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
        actor_action_base *action;
    };
} // namespace detail

actor_action_base::actor_action_base() : player_id_(0), ret_code_(0), rsp_code_(0), status_(EN_AAS_CREATED), rsp_msg_disabled_(false), evt_disabled_(false) {}
actor_action_base::~actor_action_base() {
    if (EN_AAS_FINISHED != status_) {
        WLOGERROR("actor %s [%p] is created but not run", name(), this);
        set_rsp_code(hello::EN_ERR_TIMEOUT);
        set_ret_code(hello::err::EN_SYS_TIMEOUT);
    }
}

const char *actor_action_base::name() const {
    const char *ret = typeid(*this).name();
    if (NULL == ret) {
        return "RTTI Unavailable: actor_action_base";
    }

    // some compiler will generate number to mark the type
    while (ret && *ret >= '0' && *ret <= '9') {
        ++ret;
    }
    return ret;
}

int actor_action_base::hook_run() { return (*this)(); }

int32_t actor_action_base::run() {
    detail::actor_action_stat_guard stat(this);

    if (get_status() > EN_AAS_CREATED) {
        WLOGERROR("actor %s [%p] already running", name(), this);
        return hello::err::EN_SYS_BUSY;
    }

    status_ = EN_AAS_RUNNING;
    WLOGDEBUG("actor %s [%p] start to run", name(), this);
    ret_code_ = hook_run();

    // 响应OnSuccess(这时候任务的status还是running)
    int32_t ret = 0;
    if (!evt_disabled_) {
        if (rsp_code_ < 0) {
            ret = on_failed();
            WLOGINFO("actor %s [%p] finished success but response errorcode, rsp code: %d\n", name(), this, rsp_code_);
        } else {
            ret = on_success();
        }

        int complete_res = on_complete();
        if (0 != complete_res) {
            ret = complete_res;
        }
    }

    if (!rsp_msg_disabled_) {
        send_rsp_msg();
    }
    status_ = EN_AAS_FINISHED;
    return ret;
}

int actor_action_base::on_success() { return 0; }

int actor_action_base::on_failed() { return 0; }

int actor_action_base::on_complete() { return 0; }

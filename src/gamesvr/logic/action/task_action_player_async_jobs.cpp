//
// Created by owent on 2018/05/01.
//

#include <log/log_wrapper.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <time/time_utility.h>

#include <data/player.h>

#include "task_action_player_async_jobs.h"

task_action_player_async_jobs::task_action_player_async_jobs(ctor_param_t COPP_MACRO_RV_REF param) : param_(param) {}

task_action_player_async_jobs::~task_action_player_async_jobs() {}

int task_action_player_async_jobs::operator()() {
    if (!param_.user) {
        return hello::err::EN_SYS_PARAM;
    }

    // 这后面的都是玩家异步处理任务，一般用户刷新缓存和数据修复和数据patch。
    // 不成功不应该影响逻辑和数据，而是仅影响某些不重要的缓存滞后。

    return hello::err::EN_SUCCESS;
}

int task_action_player_async_jobs::on_success() {
    WLOGDEBUG("player %s(%u:%llu) do task_action_player_async_jobs success", param_.user ? param_.user->get_open_id().c_str() : "UNKNOWN",
              param_.user ? param_.user->get_zone_id() : 0, param_.user ? param_.user->get_user_id_llu() : 0);

    // 启动玩家数据异步命令patch任务
    if (param_.user) {
        param_.user->start_patch_remote_command();
    }
    return get_ret_code();
}

int task_action_player_async_jobs::on_failed() {
    WLOGERROR("player %s(%u%llu) do task_action_player_async_jobs failed, res: %d", param_.user ? param_.user->get_open_id().c_str() : "UNKNOWN",
              param_.user ? param_.user->get_zone_id() : 0, param_.user ? param_.user->get_user_id_llu() : 0, get_ret_code());
    return get_ret_code();
}

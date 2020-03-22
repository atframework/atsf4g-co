//
// Created by owent on 2018/05/01.
//

#include <log/log_wrapper.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <time/time_utility.h>

#include <data/player.h>
#include <router/router_player_cache.h>
#include <router/router_player_manager.h>

#include "task_action_player_remote_patch_jobs.h"

task_action_player_remote_patch_jobs::task_action_player_remote_patch_jobs(ctor_param_t COPP_MACRO_RV_REF param)
    : param_(param), need_restart_(false), is_writable_(false) {}

task_action_player_remote_patch_jobs::~task_action_player_remote_patch_jobs() {}

int task_action_player_remote_patch_jobs::operator()() {
    need_restart_ = false;
    is_writable_ = false;
    if (!param_.user) {
        return hello::err::EN_SYS_PARAM;
    }

    router_player_cache::key_t key(router_player_manager::me()->get_type_id(), param_.user->get_zone_id(), param_.user->get_user_id());
    router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

    // 缓存已被移除，当前player可能是上下文缓存，忽略patch
    if (!cache) {
        return hello::err::EN_SUCCESS;
    }

    // 缓存已被踢出，当前player是路由缓存，忽略patch
    if (!cache->is_writable()) {
        return hello::err::EN_SUCCESS;
    }

    // 注意这里会续期缓存生命周期，所以要确保前面判定都过后才能到这里
    if (param_.user != cache->get_object()) {
        return hello::err::EN_SUCCESS;
    }
    is_writable_ = true;

    /**
     * 一致性：
     *     执行远程命令patch期间可能触发下线/踢出流程。所以所有的操作需要带唯一ID，如果已经执行过则要忽略。
     *     需要再保存玩家数据成功后才能移除patch队列。
     *
     * 重试和超时保护：
     *     如果一次执行的任务过多，可能当前任务会超时，所以需要检测到运行了很长时间后中断退出，并重启一个任务继续后续流程。
     *     出错的流程不应该重启任务，而是放进队列尾等待后续重试，否则某些服务故障期间可能会导致无限循环。
     */

    int ret = hello::err::EN_SUCCESS;
    // while (is_writable_ && param_.user->is_inited() && false == need_restart_) {
    //     // TODO 拉取远程命令列表
    //     // TODO 如果没有待执行的远程命令，直接成功返回
    //     if (true) {
    //         break;
    //     }
    //
    //     // TODO patch逻辑 - 邮件？
    //     // TODO patch逻辑 - GM命令？
    //
    //     // 保存玩家数据
    //     ret = cache->save(NULL);
    //     if (ret < 0) {
    //         WPLOGERROR(*param_.user, "save failed, res: %d", res);
    //         break;
    //     }
    //
    //     // TODO 移除远程命令
    //
    //     // 如果对象被踢出（不可写），则放弃后续流程
    //     is_writable_ = cache->is_writable();
    //     // 执行时间过长则中断，下一次再启动流程
    //     need_restart_ = param_.timeout_timepoint - util::time::time_utility::get_now() < param_.timeout_duration / 2;
    // }

    // 可能是从中间中断的，需要重新计算一次是否可写和超时
    // is_writable_ = cache->is_writable();
    need_restart_ = param_.timeout_timepoint - util::time::time_utility::get_now() < param_.timeout_duration / 2;
    return ret;
}

int task_action_player_remote_patch_jobs::on_success() {
    if (param_.user) {
        WPLOGDEBUG(*param_.user, "do task_action_player_remote_patch_jobs success");
    }

    // 尝试再启动一次，启动排队后的任务
    if (is_writable_ && param_.user) {
        if (param_.user->remote_command_patch_task_.get() == task_manager::task_t::this_task()) {
            param_.user->remote_command_patch_task_.reset();
        }

        if (need_restart_) {
            param_.user->start_patch_remote_command();
        } else {
            param_.user->try_patch_remote_command();
        }
    }

    return get_ret_code();
}

int task_action_player_remote_patch_jobs::on_failed() {
    if (param_.user) {
        WPLOGERROR(*param_.user, "do task_action_player_remote_patch_jobs failed, res: %d", get_ret_code());
    }

    // 尝试再启动一次，启动排队后的任务
    if (is_writable_ && param_.user) {
        if (param_.user->remote_command_patch_task_.get() == task_manager::task_t::this_task()) {
            param_.user->remote_command_patch_task_.reset();
        }

        if (need_restart_) {
            param_.user->start_patch_remote_command();
        } else {
            param_.user->try_patch_remote_command();
        }
    }
    return get_ret_code();
}

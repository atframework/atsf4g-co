//
// Created by owent on 2016/10/6.
//

#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <rpc/db/player.h>

#include <data/player_cache.h>
#include <data/session.h>
#include <logic/session_manager.h>
#include <logic/player_manager.h>

#include <utility/protobuf_mini_dumper.h>

#include "task_action_player_logout.h"


task_action_player_logout::task_action_player_logout(ctor_param_t COPP_MACRO_RV_REF param) : ctor_param_(COPP_MACRO_STD_MOVE(param)) {}
task_action_player_logout::~task_action_player_logout() {}

int task_action_player_logout::operator()() {
    WLOGDEBUG("task_action_player_logout for session [0x%llx, 0x%llx] start", 
        static_cast<unsigned long long>(ctor_param_.atgateway_bus_id),
        static_cast<unsigned long long>(ctor_param_.atgateway_session_id)
    );
    session::key_t key;
    key.bus_id = ctor_param_.atgateway_bus_id;
    key.session_id = ctor_param_.atgateway_session_id;
    session::ptr_t s = session_manager::me()->find(key);
    session::flag_guard_t flag_guard;
    if (s) {
        flag_guard.setup(*s, session::flag_t::EN_SESSION_FLAG_CLOSING);
        // 如果正在其他任务中执行移除流程，这里直接跳过即可
        if (!flag_guard) {
            return hello::err::EN_SUCCESS;
        }

        // 连接断开的时候需要保存一下数据
        player_cache::ptr_t user = s->get_player();
        // 如果玩家数据是缓存，不是实际登入点，则不用保存
        if (user && user->is_writable()) {
            set_player_id(user->get_user_id());

            set_rsp_code(user->await_before_logout_tasks());
            if (get_rsp_code() < 0) {
                WPLOGERROR(*user, "kickoff failed, res: %d(%s)", get_rsp_code(), protobuf_mini_dumper_get_error_msg(get_rsp_code()));
                return hello::err::EN_SUCCESS;
            }

            if (!player_manager::me()->remove(user, false)) {
                set_rsp_code(hello::err::EN_SYS_PARAM);
                WPLOGERROR(*user, "logout failed, res: %d(%s)", hello::err::EN_SYS_PARAM, 
                    protobuf_mini_dumper_get_error_msg(hello::err::EN_SYS_PARAM));
            }
        }
    }

    session_manager::me()->remove(key);
    return hello::err::EN_SUCCESS;
}

int task_action_player_logout::on_success() { return get_ret_code(); }

int task_action_player_logout::on_failed() { return get_ret_code(); }
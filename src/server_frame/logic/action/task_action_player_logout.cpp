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
    session::key_t key;
    key.bus_id = ctor_param_.atgateway_bus_id;
    key.session_id = ctor_param_.atgateway_session_id;
    session::ptr_t s = session_manager::me()->find(key);
    if (s) {
        // 连接断开的时候需要保存一下数据
        player_cache::ptr_t user = s->get_player();
        // 如果玩家数据是缓存，不是实际登入点，则不用保存
        if (user && user->is_writable()) {
            int res = player_manager::me()->remove(user, false);
            if (res < 0) {
                WPLOGERROR(*user, "logout failed, res: %d(%s)", res, protobuf_mini_dumper_get_error_msg(res));
            }
        }
    }

    session_manager::me()->remove(key);
    return hello::err::EN_SUCCESS;
}

int task_action_player_logout::on_success() { return get_ret_code(); }

int task_action_player_logout::on_failed() { return get_ret_code(); }
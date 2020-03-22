//
// Created by owent on 2016/10/6.
//

#include <log/log_wrapper.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <rpc/db/login.h>

#include <data/player.h>
#include <data/session.h>
#include <logic/player_manager.h>
#include <logic/session_manager.h>


#include <config/logic_config.h>
#include <proto_base.h>
#include <time/time_utility.h>


#include "task_action_ping.h"

task_action_ping::task_action_ping(dispatcher_start_data_t COPP_MACRO_RV_REF param) : task_action_cs_req_base(COPP_MACRO_STD_MOVE(param)) {}
task_action_ping::~task_action_ping() {}

int task_action_ping::operator()() {
    session::ptr_t sess = get_session();
    if (!sess) {
        WLOGERROR("session not found.");
        return hello::err::EN_SYS_PARAM;
    }

    player::ptr_t user = get_player<player>();
    if (!user) {
        WLOGERROR("not logined.");
        set_rsp_code(hello::EN_ERR_LOGIN_NOT_LOGINED);
        return 0;
    }

    // 添加Pong包
    hello::CSMsg &rsp_msg = add_rsp_msg();
    rsp_msg.mutable_body()->mutable_msc_pong_rsp();

    // 用户更新心跳信息
    user->update_heartbeat();

    // 心跳超出容忍值，直接提下线
    if (user->get_heartbeat_data().continue_error_times >= logic_config::me()->get_cfg_logic().heartbeat_error_times) {
        // 封号一段时间

        set_rsp_code(hello::EN_ERR_LOGIN_BAN);
        int kick_off_reason = hello::EN_CRT_LOGIN_BAN;
        hello::table_login tb;
        do {
            std::string login_ver;
            int res = rpc::db::login::get(user->get_open_id().c_str(), user->get_zone_id(), tb, login_ver);
            if (res < 0) {
                WLOGERROR("call login rpc Get method failed, user %s, res: %d", user->get_open_id().c_str(), res);
                break;
            }

            if (!tb.has_except()) {
                tb.mutable_except()->set_last_except_time(0);
                tb.mutable_except()->set_except_con_times(0);
                tb.mutable_except()->set_except_sum_times(0);
            }
            tb.mutable_except()->set_except_sum_times(tb.except().except_sum_times() + 1);
            if (0 != tb.except().last_except_time() &&
                util::time::time_utility::get_now() - tb.except().last_except_time() <= logic_config::me()->get_cfg_logic().heartbeat_ban_time_bound) {
                tb.mutable_except()->set_except_con_times(tb.except().except_con_times() + 1);
            } else {
                tb.mutable_except()->set_except_con_times(1);
            }

            tb.mutable_except()->set_last_except_time(util::time::time_utility::get_now());

            if (tb.except().except_con_times() >= logic_config::me()->get_cfg_logic().heartbeat_ban_error_times) {
                tb.set_ban_time(static_cast<uint32_t>(util::time::time_utility::get_now() + logic_config::me()->get_cfg_logic().session_login_ban_time));
                kick_off_reason = hello::EN_CRT_LOGIN_BAN;
                set_rsp_code(hello::EN_ERR_LOGIN_BAN);
            } else {
                kick_off_reason = hello::EN_CRT_SPEED_WARNING;
                set_rsp_code(hello::EN_ERR_LOGIN_SPEED_WARNING);
            }
            // 保存封号结果
            res = rpc::db::login::set(user->get_open_id().c_str(), user->get_zone_id(), tb, login_ver);
            if (res < 0) {
                WLOGERROR("call login rpc Set method failed, user %s, zone id: %u, res: %d", user->get_open_id().c_str(), user->get_zone_id(), res);
            } else {
                user->load_and_move_login_info(COPP_MACRO_STD_MOVE(tb), login_ver);
            }
        } while (false);

        // 踢出原因是封号
        // 游戏速度异常，强制踢出，这时候也可能是某些手机切到后台再切回来会加速运行，这时候强制断开走登入流程，防止高频发包

        // 先发包
        send_rsp_msg();

        std::shared_ptr<session> sess = user->get_session();
        if (sess) {
            sess->send_kickoff(kick_off_reason);
        }

        // 再踢下线
        player_manager::me()->remove(user);
    }
    return hello::err::EN_SUCCESS;
}

int task_action_ping::on_success() { return get_ret_code(); }

int task_action_ping::on_failed() { return get_ret_code(); }
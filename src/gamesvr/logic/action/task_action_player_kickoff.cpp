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

#include <config/extern_service_types.h>
#include <proto_base.h>

#include "task_action_player_kickoff.h"

task_action_player_kickoff::task_action_player_kickoff(dispatcher_start_data_t COPP_MACRO_RV_REF param) : task_action_ss_req_base(COPP_MACRO_STD_MOVE(param)) {}
task_action_player_kickoff::~task_action_player_kickoff() {}

int task_action_player_kickoff::operator()() {
    msg_cref_type req_msg = get_request();

    uint64_t player_user_id = req_msg.head().player_user_id();
    uint32_t player_zone_id = req_msg.head().player_zone_id();
    const std::string player_open_id = req_msg.head().player_open_id();
    player::ptr_t user = player_manager::me()->find_as<player>(player_user_id, player_zone_id);
    if (!user) {
        WLOGERROR("user %s(%u:%llu) not found, maybe already logout.", player_open_id.c_str(), player_zone_id, static_cast<unsigned long long>(player_user_id));

        // 尝试保存用户数据
        hello::table_login user_lg;
        std::string version;
        int res = rpc::db::login::get(player_open_id.c_str(), player_zone_id, user_lg, version);
        if (res < 0) {
            WLOGERROR("user %s(%u:%llu) try load login data failed.", player_open_id.c_str(), player_zone_id, static_cast<unsigned long long>(player_user_id));
            set_rsp_code(hello::err::EN_DB_REPLY_ERROR);
            return hello::err::EN_SUCCESS;
        }

        if (user_lg.router_server_id() != logic_config::me()->get_self_bus_id()) {
            WLOGERROR("user %s(%u:%llu) login pd error(expected: 0x%llx, real: 0x%llx)", player_open_id.c_str(), player_zone_id, static_cast<unsigned long long>(player_user_id),
                      static_cast<unsigned long long>(user_lg.router_server_id()), static_cast<unsigned long long>(logic_config::me()->get_self_bus_id()));
            set_rsp_code(hello::EN_ERR_SYSTEM);
            return hello::err::EN_SUCCESS;
        }

        user_lg.set_router_server_id(0);
        res = rpc::db::login::set(player_open_id.c_str(), player_zone_id, user_lg, version);
        if (res < 0) {
            WLOGERROR("user %s(%u:%llu) try load login data failed.", player_open_id.c_str(), player_zone_id, static_cast<unsigned long long>(player_user_id));
            set_rsp_code(hello::err::EN_DB_SEND_FAILED);
            return hello::err::EN_SUCCESS;
        }

        return hello::err::EN_SUCCESS;
    }

    // 仅在有session时才下发踢出消息
    std::shared_ptr<session> sess = user->get_session();
    if (sess) {
        int32_t ret = sess->send_kickoff(::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
        if (ret) {
            WLOGERROR("task %s [0x%llx] send cs msg failed, ret: %d\n", name(), get_task_id_llu(), ret);

            // 发送失败也没有关系，下次客户端发包的时候自然会出错
        }
    }

    if (!player_manager::me()->remove(user, true)) {
        WLOGERROR("kickoff user %s(%u:%llu) failed", user->get_open_id().c_str(), player_zone_id, user->get_user_id_llu());
        set_rsp_code(hello::EN_ERR_SYSTEM);
        return hello::err::EN_SUCCESS;
    }

    return hello::err::EN_SUCCESS;
}

int task_action_player_kickoff::on_success() {
    hello::SSMsg &rsp = add_rsp_msg();
    rsp.mutable_body()->mutable_mss_player_kickoff_rsp();

    return get_ret_code();
}

int task_action_player_kickoff::on_failed() {
    hello::SSMsg &rsp = add_rsp_msg();
    rsp.mutable_body()->mutable_mss_player_kickoff_rsp();

    return get_ret_code();
}
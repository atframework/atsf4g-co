//
// Created by owt50 on 2016/10/9.
//

#include <dispatcher/cs_msg_dispatcher.h>

#include <logic/action/task_action_ping.h>
#include <logic/action/task_action_player_info_get.h>
#include <logic/action/task_action_player_login.h>

#include "handle_cs_msg.h"

int app_handle_cs_msg::init() {
    int ret = 0;

    REG_TASK_MSG_HANDLE(cs_msg_dispatcher, ret, task_action_player_login, hello::CSMsgBody::kMcsLoginReq);
    REG_ACTOR_MSG_HANDLE(cs_msg_dispatcher, ret, task_action_player_info_get, hello::CSMsgBody::kMcsPlayerGetinfoReq);
    REG_TASK_MSG_HANDLE(cs_msg_dispatcher, ret, task_action_ping, hello::CSMsgBody::kMcsPingReq);

    return ret;
}
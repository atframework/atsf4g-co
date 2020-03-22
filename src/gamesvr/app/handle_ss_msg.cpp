//
// Created by owt50 on 2016/10/9.
//

#include <dispatcher/ss_msg_dispatcher.h>

#include <logic/action/task_action_player_kickoff.h>

#include "handle_ss_msg.h"

int app_handle_ss_msg::init() {
    int ret = 0;

    REG_TASK_MSG_HANDLE(ss_msg_dispatcher, ret, task_action_player_kickoff, hello::SSMsgBody::kMssPlayerKickoffReq);

    return ret;
}
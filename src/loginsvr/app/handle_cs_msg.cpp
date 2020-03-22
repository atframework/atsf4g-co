//
// Created by owt50 on 2016/10/9.
//

#include <dispatcher/cs_msg_dispatcher.h>

#include <logic/task_action_login_authorization.h>

#include "handle_cs_msg.h"

int app_handle_cs_msg::init() {
    int ret = 0;

    REG_TASK_MSG_HANDLE(cs_msg_dispatcher, ret, task_action_login_authorization, hello::CSMsgBody::kMcsLoginAuthReq);

    return ret;
}
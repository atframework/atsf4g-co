// Copyright 2021 atframework
// Created by owent on 2016/10/9.
//

#include "app/handle_cs_msg.h"

#include <config/server_frame_build_feature.h>

#include <dispatcher/cs_msg_dispatcher.h>

#include <logic/action/task_action_player_info_get.h>
#include <logic/action/task_action_player_login.h>

int app_handle_cs_msg::init() {
  int ret = 0;

  REG_TASK_MSG_HANDLE(cs_msg_dispatcher, ret, task_action_player_login,
                      PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsgBody::kMcsLoginReq);
  REG_ACTOR_MSG_HANDLE(cs_msg_dispatcher, ret, task_action_player_info_get,
                       PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsgBody::kMcsPlayerGetinfoReq);

  return ret;
}
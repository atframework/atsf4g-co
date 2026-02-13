// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.LobbysvrClientService, please don't edit it

#include "handle_cs_rpc_lobbysvrclientservice.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/cs_msg_dispatcher.h>

#include <logic/action/task_action_access_update.h>
#include <logic/action/task_action_login.h>
#include <logic/action/task_action_ping.h>
#include <logic/player/task_action_player_get_info.h>

namespace handle {
namespace lobbysvrclientservice {
GAMECLIENT_SERVICE_API int register_handles_for_lobbysvrclientservice() {
  int ret = 0;
  REG_TASK_RPC_HANDLE(cs_msg_dispatcher, ret, task_action_login, hello::LobbysvrClientService::descriptor(),
                      "hello.LobbysvrClientService.login");
  REG_TASK_RPC_HANDLE(cs_msg_dispatcher, ret, task_action_ping, hello::LobbysvrClientService::descriptor(),
                      "hello.LobbysvrClientService.ping");
  REG_TASK_RPC_HANDLE(cs_msg_dispatcher, ret, task_action_access_update, hello::LobbysvrClientService::descriptor(),
                      "hello.LobbysvrClientService.access_update");
  REG_TASK_RPC_HANDLE(cs_msg_dispatcher, ret, task_action_player_get_info, hello::LobbysvrClientService::descriptor(),
                      "hello.LobbysvrClientService.player_get_info");
  return ret;
}
}  // namespace lobbysvrclientservice
}  // namespace handle

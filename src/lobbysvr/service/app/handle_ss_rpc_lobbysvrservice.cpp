// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.LobbysvrService, please don't edit it

#include "handle_ss_rpc_lobbysvrservice.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <protocol/pbdesc/lobby_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/ss_msg_dispatcher.h>

#include <logic/player/task_action_player_async_jobs_sync.h>
#include <logic/player/task_action_player_kickoff.h>

namespace handle {
namespace lobby {
GAME_SERVICE_API int register_handles_for_lobbysvrservice() {
  int ret = 0;
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_player_kickoff, hello::LobbysvrService::descriptor(),
                      "hello.LobbysvrService.player_kickoff");
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_player_async_jobs_sync, hello::LobbysvrService::descriptor(),
                      "hello.LobbysvrService.player_async_jobs_sync");
  return ret;
}
}  // namespace lobby
}  // namespace handle

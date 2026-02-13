// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.AuthsvrClientService, please don't edit it

#include "handle_cs_rpc_authsvrclientservice.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/cs_msg_dispatcher.h>

#include <logic/action/task_action_login_auth.h>

namespace handle {
namespace authsvrclientservice {
LOGINCLIENT_SERVICE_API int register_handles_for_authsvrclientservice() {
  int ret = 0;
  REG_TASK_RPC_HANDLE(cs_msg_dispatcher, ret, task_action_login_auth, hello::AuthsvrClientService::descriptor(),
                      "hello.AuthsvrClientService.login_auth");
  return ret;
}
}  // namespace authsvrclientservice
}  // namespace handle

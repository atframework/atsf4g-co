// Copyright 2022 atframework
// @brief Created by generate-for-pb.py for atframework.distributed_system.DtcoordsvrService, please don't edit it

#include "handle_ss_rpc_dtcoordsvrservice.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>
#include <protocol/pbdesc/distributed_transaction.pb.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <logic/action/task_action_query.h>
#include <logic/action/task_action_create.h>
#include <logic/action/task_action_commit.h>
#include <logic/action/task_action_reject.h>
#include <logic/action/task_action_commit_participator.h>
#include <logic/action/task_action_reject_participator.h>
#include <logic/action/task_action_remove.h>

namespace handle {
namespace transaction {
int register_handles_for_dtcoordsvrservice() {
  int ret = 0;
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_query, atframework::distributed_system::DtcoordsvrService::descriptor(), "atframework.distributed_system.DtcoordsvrService.query");
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_create, atframework::distributed_system::DtcoordsvrService::descriptor(), "atframework.distributed_system.DtcoordsvrService.create");
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_commit, atframework::distributed_system::DtcoordsvrService::descriptor(), "atframework.distributed_system.DtcoordsvrService.commit");
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_reject, atframework::distributed_system::DtcoordsvrService::descriptor(), "atframework.distributed_system.DtcoordsvrService.reject");
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_commit_participator, atframework::distributed_system::DtcoordsvrService::descriptor(), "atframework.distributed_system.DtcoordsvrService.commit_participator");
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_reject_participator, atframework::distributed_system::DtcoordsvrService::descriptor(), "atframework.distributed_system.DtcoordsvrService.reject_participator");
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_remove, atframework::distributed_system::DtcoordsvrService::descriptor(), "atframework.distributed_system.DtcoordsvrService.remove");
  return ret;
}
}  // namespace transaction
}


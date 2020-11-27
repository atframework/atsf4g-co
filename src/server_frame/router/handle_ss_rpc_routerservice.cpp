/**
 * @brief Created by generate-for-pb.py for hello.RouterService, please don't edit it
 */

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <router/action/task_action_router_update_sync.h>
#include <router/action/task_action_router_transfer.h>

#include "handle_ss_rpc_routerservice.h"

namespace handle {
    namespace router {
        int register_handles_for_routerservice() {
            int ret = 0;
            REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_router_update_sync, hello::RouterService::descriptor(), "hello.RouterService.router_update_sync");
            REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_router_transfer, hello::RouterService::descriptor(), "hello.RouterService.router_transfer");
            return ret;
        }
    } // namespace router
}


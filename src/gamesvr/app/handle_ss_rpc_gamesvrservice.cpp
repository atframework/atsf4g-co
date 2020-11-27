/**
 * @brief Created by generate-for-pb.py for hello.GamesvrService, please don't edit it
 */

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <logic/player/task_action_player_kickoff.h>

#include "handle_ss_rpc_gamesvrservice.h"

namespace handle {
    namespace game {
        int register_handles_for_gamesvrservice() {
            int ret = 0;
            REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_player_kickoff, hello::GamesvrService::descriptor(), "hello.GamesvrService.player_kickoff");
            return ret;
        }
    } // namespace game
}


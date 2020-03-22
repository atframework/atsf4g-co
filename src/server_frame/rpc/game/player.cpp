//
// Created by owt50 on 2016/9/28.
//

#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>


#include <config/extern_service_types.h>
#include <config/logic_config.h>


#include "../rpc_utils.h"
#include "player.h"

namespace rpc {
    namespace game {
        namespace player {
            int send_kickoff(uint64_t dst_bus_id, uint64_t user_id, uint32_t zone_id, const std::string &openid, int32_t reason) {
                task_manager::task_t *task = task_manager::task_t::this_task();
                if (!task) {
                    WLOGERROR("current not in a task");
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                hello::SSMsg req_msg;
                task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_self_bus_id());
                req_msg.mutable_head()->set_src_task_id(task->get_id());
                req_msg.mutable_head()->set_player_user_id(user_id);
                req_msg.mutable_head()->set_player_open_id(openid);
                req_msg.mutable_head()->set_player_zone_id(zone_id);
                req_msg.mutable_body()->mutable_mss_player_kickoff_req()->set_reason(reason);

                int res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);

                if (res < 0) {
                    return res;
                }

                hello::SSMsg rsp_msg;
                // 坝程擝作
                res = rpc::wait(rsp_msg);
                if (res < 0) {
                    return res;
                }

                if (rsp_msg.head().error_code()) {
                    return rsp_msg.head().error_code();
                }

                if (!rsp_msg.has_body() || !rsp_msg.body().has_mss_player_kickoff_rsp()) {
                    return hello::err::EN_PLAYER_KICKOUT;
                }

                return hello::err::EN_SUCCESS;
            }
        } // namespace player
    }     // namespace game
} // namespace rpc
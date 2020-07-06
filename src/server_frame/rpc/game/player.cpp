//
// Created by owt50 on 2016/9/28.
//

#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>


#include <config/extern_service_types.h>
#include <config/logic_config.h>

#include <rpc/db/uuid.h>

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
                res = rpc::wait(rsp_msg, req_msg.head().sequence());
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

            int64_t alloc_user_id() {
                int64_t prefix_id = rpc::db::uuid::generate_global_unique_id(hello::EN_GLOBAL_UUID_MAT_USER_ID, 0, 0);
                if (prefix_id < 0) {
                    return static_cast<int>(prefix_id);
                }

                int64_t suffix = prefix_id;
                while (suffix >= 8) {
                    suffix = (suffix >> 3) ^ (suffix & 0x07);
                }

                int64_t out = (static_cast<uint64_t>(prefix_id) << 3) | static_cast<uint64_t>(suffix);
                assert(is_valid_user_id(out));
                return out;
            }

            bool is_valid_user_id(int64_t in) {
                if (in <= 0) {
                    return false;
                }

                while (in >= 8) {
                    in = (in >> 3) ^ (in & 0x07);
                }

                return in == 0;
            }

        } // namespace player
    }     // namespace game
} // namespace rpc
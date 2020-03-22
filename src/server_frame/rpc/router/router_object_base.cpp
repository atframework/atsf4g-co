//
// Created by owent on 2018/05/01.
//

#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>


#include <config/extern_service_types.h>
#include <config/logic_config.h>


#include "../rpc_utils.h"

#include "router_object_base.h"


namespace rpc {
    namespace router {
        namespace robj {
            int send_update_sync(uint64_t dst_bus_id, hello::SSRouterUpdateSync &req) {
                hello::SSMsg req_msg;
                task_action_ss_req_base::init_msg(req_msg, dst_bus_id);
                req_msg.mutable_head()->set_src_task_id(0);
                // req_msg.mutable_head()->set_bus_id(logic_config::me()->get_self_bus_id());

                req_msg.mutable_body()->set_allocated_mss_router_update_sync(&req);

                int ret = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
                req_msg.mutable_body()->release_mss_router_update_sync();
                return ret;
            }

            int send_transfer(uint64_t dst_bus_id, hello::SSRouterTransferReq &req, hello::SSRouterTransferRsp &rsp) {
                task_manager::task_t *task = task_manager::task_t::this_task();
                if (!task) {
                    WLOGERROR("current not in a task");
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                hello::SSMsg req_msg;
                task_action_ss_req_base::init_msg(req_msg, dst_bus_id);
                req_msg.mutable_head()->set_src_task_id(task->get_id());
                // req_msg.mutable_head()->set_bus_id(logic_config::me()->get_self_bus_id());

                req_msg.mutable_body()->set_allocated_mss_router_transfer_req(&req);

                int res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);

                req_msg.mutable_body()->release_mss_router_transfer_req();
                if (res < 0) {
                    return res;
                }

                hello::SSMsg rsp_msg;
                // 协程操作
                res = rpc::wait(rsp_msg);
                if (res < 0) {
                    return res;
                }

                if (rsp_msg.has_body() && rsp_msg.body().has_mss_router_transfer_rsp()) {
                    rsp.Swap(rsp_msg.mutable_body()->mutable_mss_router_transfer_rsp());
                } else {
                    if (rsp_msg.head().error_code()) {
                        return rsp_msg.head().error_code();
                    }

                    return hello::err::EN_MSG_PARSE_FAIL;
                }

                return rsp_msg.head().error_code();
            }
        } // namespace robj
    }     // namespace router
} // namespace rpc
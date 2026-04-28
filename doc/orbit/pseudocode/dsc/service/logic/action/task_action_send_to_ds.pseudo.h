#pragma once

#include "../../app/handle_cs_rpc_externalservice.pseudo.h"
#include "../../../forwarding/reliable_forwarder.pseudo.h"
#include "../../../session/session_router.pseudo.h"

// Phase 4.1
// 目标: 固化 SendToDS 的业务动作，串起 owner 校验、目标 DS 路由与下行转发。
// 未来真实落点: src/dsc/service/logic/action/task_action_send_to_ds.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_send_to_ds(runtime_handle_t& runtime,
                                        rpc_context_t& rpc_context,
                                        const SendToDSReq& request,
                                        session::session_router& session_router,
                                        forwarding::reliable_forwarder& reliable_forwarder);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
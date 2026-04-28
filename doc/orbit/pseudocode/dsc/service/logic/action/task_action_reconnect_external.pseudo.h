#pragma once

#include "../../app/handle_cs_rpc_externalservice.pseudo.h"
#include "../../../forwarding/reconnect_replay.pseudo.h"
#include "../../../session/session_router.pseudo.h"

// Phase 4.4
// 目标: 固化 ReconnectExternal 的业务动作，串起 session rebind 与 replay response。
// 未来真实落点: src/dsc/service/logic/action/task_action_reconnect_external.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_reconnect_external(runtime_handle_t& runtime,
                                                rpc_context_t& rpc_context,
                                                const ReconnectExternalReq& request,
                                                session::session_router& session_router,
                                                forwarding::reconnect_replay& replay_engine);

int resume_external_session_after_reconnect(runtime_handle_t& runtime,
                                            rpc_context_t& rpc_context,
                                            const ReconnectExternalReq& request,
                                            session::session_router& session_router);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
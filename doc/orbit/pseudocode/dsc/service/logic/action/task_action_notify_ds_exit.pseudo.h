#pragma once

#include "../../app/handle_ss_rpc_controllerservice.pseudo.h"
#include "../../../registry/agent_registry.pseudo.h"
#include "../../../registry/disconnect_cleanup.pseudo.h"

// Phase 3.5
// 目标: 固化 NotifyDSExit 动作，串起 owner cleanup、agent load 更新和外部通知。
// 未来真实落点: src/dsc/service/logic/action/task_action_notify_ds_exit.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_notify_ds_exit(runtime_handle_t& runtime,
                                            rpc_context_t& rpc_context,
                                            const NotifyDSExitReq& request,
                                            registry::disconnect_cleanup& disconnect_cleanup,
                                            session::session_router& session_router,
                                            registry::agent_registry& agent_registry);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
#pragma once

#include "../../app/handle_ss_rpc_controllerservice.pseudo.h"
#include "../../../registry/agent_registry.pseudo.h"

// Phase 3.3
// 目标: 固化 HeartbeatAgent 动作，刷新 liveness 与轻量 load 快照。
// 未来真实落点: src/dsc/service/logic/action/task_action_heartbeat_agent.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_heartbeat_agent(runtime_handle_t& runtime,
                                             rpc_context_t& rpc_context,
                                             const HeartbeatAgentReq& request,
                                             registry::agent_registry& agent_registry);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
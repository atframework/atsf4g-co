#pragma once

#include "../../app/handle_ss_rpc_controllerservice.pseudo.h"
#include "../../../registry/agent_registry.pseudo.h"

// Phase 3.2
// 目标: 固化 RegisterAgent 动作，串起 agent_registry 的首次插入与注册刷新。
// 未来真实落点: src/dsc/service/logic/action/task_action_register_agent.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_register_agent(runtime_handle_t& runtime,
                                            rpc_context_t& rpc_context,
                                            const RegisterAgentReq& request,
                                            registry::agent_registry& agent_registry);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
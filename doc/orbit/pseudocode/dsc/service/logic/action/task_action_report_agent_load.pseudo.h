#pragma once

#include "../../app/handle_ss_rpc_controllerservice.pseudo.h"
#include "../../../registry/agent_registry.pseudo.h"

// Phase 3.4
// 目标: 固化 ReportAgentLoad 动作，把负载快照写入 agent_registry。
// 未来真实落点: src/dsc/service/logic/action/task_action_report_agent_load.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_report_agent_load(runtime_handle_t& runtime,
                                               rpc_context_t& rpc_context,
                                               const ReportAgentLoadReq& request,
                                               registry::agent_registry& agent_registry);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
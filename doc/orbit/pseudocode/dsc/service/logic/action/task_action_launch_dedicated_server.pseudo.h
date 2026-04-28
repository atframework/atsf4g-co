#pragma once

#include "../../app/handle_cs_rpc_externalservice.pseudo.h"
#include "../../../scheduler/scheduler_service.pseudo.h"
#include "../../../session/launch_flow.pseudo.h"

// Phase 3.8
// 目标: 固化 LaunchDedicatedServer 动作，串起 select_agent_for_launch 与 reserve_and_dispatch。
// 未来真实落点: src/dsc/service/logic/action/task_action_launch_dedicated_server.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_launch_dedicated_server(runtime_handle_t& runtime,
                                                     rpc_context_t& rpc_context,
                                                     const LaunchDedicatedServerReq& request,
                                                     scheduler::scheduler_service& scheduler_service,
                                                     session::launch_flow& launch_flow);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
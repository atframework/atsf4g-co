#pragma once

#include "../../rpc/agentservice.pseudo.h"
#include "../../../agent/controller_reporter.pseudo.h"
#include "../../../agent/load_reporter.pseudo.h"

// Phase 2
// 目标: 固化 QueryDSState、drain agent 和启动恢复 reconcile 三条管理路径。
// 未来真实落点: src/dsa/service/logic/action/task_action_query_ds_state.cpp

namespace atorbit {
namespace shared {
namespace runtime {
class service_shared_context;
}
}
namespace dsa {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_query_ds_state(runtime_handle_t& runtime, rpc_context_t& rpc_context, const QueryDSStateReq& request);
rpc_result_t run_task_action_drain_agent(runtime_handle_t& runtime, rpc_context_t& rpc_context, const DrainAgentReq& request);
int start_dsa_startup_reconcile(shared::runtime::service_shared_context& shared_context,
                                controller_reporter_handle_t& controller_reporter,
                                load_reporter_handle_t& load_reporter,
                                resource_ledger_handle_t& resource_ledger);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsa
}  // namespace atorbit

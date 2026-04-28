#pragma once

#include "../../rpc/agentservice.pseudo.h"
#include "../../../../shared/runtime/resource_ledger.pseudo.h"
#include "../../../agent/controller_reporter.pseudo.h"
#include "../../../agent/load_reporter.pseudo.h"
#include "../../../agent/local_channel_service.pseudo.h"
#include "../../../heartbeat/heartbeat_monitor.pseudo.h"

// Phase 2
// 目标: 固化 AgentService::StopDS 的 graceful stop / timeout escalate / cleanup 流程。
// 未来真实落点: src/dsa/service/logic/action/task_action_stop_ds.cpp
//
// 关键步骤：
// 1. 通过 local_channel_service 向目标 DS 下发 stop 指令。
// 2. 在 heartbeat_monitor 中标记 exiting，等待正常退出。
// 3. 若超过 graceful deadline，则升级为 kill path。
// 4. 通过 controller_reporter 上报 NotifyDSExit。
// 5. 通过 load_reporter 刷新并上报当前 agent 负载。

namespace atorbit {
namespace dsa {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_stop_ds(runtime_handle_t& runtime,
									 rpc_context_t& rpc_context,
									 const StopDSReq& request,
									 agent::local_channel_service& local_channel_service,
									 heartbeat::heartbeat_monitor& heartbeat_monitor,
									 shared::runtime::resource_ledger& resource_ledger,
									 agent::controller_reporter& controller_reporter,
									 agent::load_reporter& load_reporter);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsa
}  // namespace atorbit

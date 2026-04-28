#pragma once

#include "../../rpc/agentservice.pseudo.h"
#include "../../../../shared/runtime/resource_ledger.pseudo.h"
#include "../../../agent/controller_reporter.pseudo.h"
#include "../../../agent/load_reporter.pseudo.h"
#include "../../../heartbeat/heartbeat_monitor.pseudo.h"
#include "../../../process/start_ds_process.pseudo.h"

// Phase 2
// 目标: 固化 AgentService::StartDS 的业务动作，串起 reserve -> launch -> ready tracking -> DSC report.
// 未来真实落点: src/dsa/service/logic/action/task_action_start_ds.cpp
//
// 关键步骤：
// 1. 校验 DSA 当前未进入 drain 状态。
// 2. 调用 resource_ledger.reserve 预扣 expected cpu / memory。
// 3. 调用 start_ds_process.launch 拉起子进程并建立 local channel。
// 4. 将 process_id 注册到 heartbeat_monitor，刷新超时 deadline。
// 5. 通过 controller_reporter 上报 NotifyDSStarted。
// 6. 通过 load_reporter 构建最新快照并上报 ReportAgentLoad。

namespace atorbit {
namespace dsa {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_start_ds(runtime_handle_t& runtime,
									  rpc_context_t& rpc_context,
									  const StartDSReq& request,
									  shared::runtime::resource_ledger& resource_ledger,
									  process::start_ds_process& start_ds_process,
									  heartbeat::heartbeat_monitor& heartbeat_monitor,
									  agent::controller_reporter& controller_reporter,
									  agent::load_reporter& load_reporter);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsa
}  // namespace atorbit

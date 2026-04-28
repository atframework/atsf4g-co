#pragma once

#include "../../app/handle_ss_rpc_managerservice.pseudo.h"
#include "../../../topology/cluster_catalog.pseudo.h"

// Phase 5.3
// 目标: 固化 ReportControllerSnapshot 动作，把 DSC 周期汇总写入 DSM controller inventory。
// 未来真实落点: src/dsm/service/logic/action/task_action_report_controller_snapshot.cpp

namespace atorbit {
namespace dsm {
namespace service {
namespace logic {
namespace action {

app::rpc_result_t run_task_action_report_controller_snapshot(app::runtime_handle_t& runtime,
                                                             app::rpc_context_t& rpc_context,
                                                             const app::ReportControllerSnapshotReq& request,
                                                             topology::cluster_catalog& cluster_catalog);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsm
}  // namespace atorbit
#pragma once

#include "../../app/handle_ss_rpc_managerservice.pseudo.h"
#include "../../../topology/cluster_catalog.pseudo.h"

// Phase 5.4
// 目标: 固化 QueryInventory 动作，基于当前 cluster_catalog 提供 region / controller 摘要和 DS detail 查询。
// 未来真实落点: src/dsm/service/logic/action/task_action_query_inventory.cpp

namespace atorbit {
namespace dsm {
namespace service {
namespace logic {
namespace action {

app::rpc_result_t run_task_action_query_inventory(app::runtime_handle_t& runtime,
                                                  app::rpc_context_t& rpc_context,
                                                  const app::QueryInventoryReq& request,
                                                  const topology::cluster_catalog& cluster_catalog);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsm
}  // namespace atorbit
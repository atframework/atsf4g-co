#pragma once

#include "../../app/handle_ss_rpc_managerservice.pseudo.h"
#include "../../../topology/cluster_catalog.pseudo.h"

// Phase 5.2
// 目标: 固化 RegisterController 的业务动作，串起 request view -> cluster_catalog 的状态落点。
// 未来真实落点: src/dsm/service/logic/action/task_action_register_controller.cpp

namespace atorbit {
namespace dsm {
namespace service {
namespace logic {
namespace action {

app::rpc_result_t run_task_action_register_controller(app::runtime_handle_t& runtime,
                                                      app::rpc_context_t& rpc_context,
                                                      const app::RegisterControllerReq& request,
                                                      topology::cluster_catalog& cluster_catalog);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsm
}  // namespace atorbit
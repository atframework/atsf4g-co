#pragma once

#include "../../app/handle_cs_rpc_externalservice.pseudo.h"
#include "../../../session/session_router.pseudo.h"

// Phase 3.6
// 目标: 固化 ConnectExternal 动作，串起 Unique ID 首连注册与 duplicate live connection 拒绝。
// 未来真实落点: src/dsc/service/logic/action/task_action_connect_external.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_connect_external(runtime_handle_t& runtime,
                                              rpc_context_t& rpc_context,
                                              const ConnectExternalReq& request,
                                              session::session_router& session_router);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
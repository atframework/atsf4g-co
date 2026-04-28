#pragma once

#include "../../app/handle_ss_rpc_controllerservice.pseudo.h"
#include "../../../forwarding/upstream_buffer_store.pseudo.h"
#include "../../../session/session_router.pseudo.h"

// Phase 4.2
// 目标: 固化 ForwardFromDS 入站动作，串起 owner 查找、在线直送与离线缓冲。
// 未来真实落点: src/dsc/service/logic/action/task_action_handle_upstream_message.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

class external_upstream_sender {
public:
  virtual ~external_upstream_sender() = default;

  virtual forwarding::result_code_t send_upstream_message(
      session::unique_id_t owner_unique_id,
      const forwarding::buffered_upstream_message_t& message) = 0;
};

rpc_result_t run_task_action_handle_upstream_message(runtime_handle_t& runtime,
                                                     rpc_context_t& rpc_context,
                                                     const ForwardFromDSReq& request,
                                                     session::session_router& session_router,
                                                     external_upstream_sender& external_upstream_sender,
                                                     forwarding::upstream_buffer_store& upstream_buffer_store);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
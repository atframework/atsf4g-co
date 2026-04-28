#pragma once

#include "../../../session/session_router.pseudo.h"

// Phase 4.3
// 目标: 把 DSA disconnect / route lost / DSM stop 一类事件统一映射成 Service 侧 DS 断链通知。
// 未来真实落点: src/dsc/service/logic/action/task_action_notify_ds_disconnect.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

using result_code_t = int;

struct ds_disconnect_notification_t {
  session::ds_composite_key_t ds_key;
  int disconnect_reason = 0;
};

class external_disconnect_notifier {
public:
  virtual ~external_disconnect_notifier() = default;

  virtual result_code_t notify_ds_disconnect(session::unique_id_t owner_unique_id,
                                             const session::ds_composite_key_t& ds_key,
                                             int disconnect_reason) = 0;
};

result_code_t run_task_action_notify_ds_disconnect(runtime_handle_t& runtime,
                                                   const ds_disconnect_notification_t& notification,
                                                   session::session_router& session_router,
                                                   external_disconnect_notifier& notifier);

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
#include "task_action_notify_ds_disconnect.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

result_code_t run_task_action_notify_ds_disconnect(runtime_handle_t& runtime,
                                                   const ds_disconnect_notification_t& notification,
                                                   session::session_router& session_router,
                                                   external_disconnect_notifier& notifier) {
  (void)runtime;
  if (0 == notification.ds_key.dsa_id || 0 == notification.ds_key.ds_id || 0 == notification.disconnect_reason) {
    return -1;
  }

  auto owner_unique_id = session_router.find_owner_unique_id(notification.ds_key);
  if (0 == owner_unique_id) {
    return -2;
  }

  return notifier.notify_ds_disconnect(owner_unique_id, notification.ds_key, notification.disconnect_reason);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
#include "task_action_notify_ds_disconnect.pseudo.h"

namespace {

class fake_external_disconnect_notifier : public atorbit::dsc::service::logic::action::external_disconnect_notifier {
public:
  unsigned long long last_owner_unique_id = 0;
  atorbit::dsc::session::ds_composite_key_t last_ds_key;
  int last_disconnect_reason = 0;
  int call_count = 0;
  int next_result = 0;

  atorbit::dsc::service::logic::action::result_code_t notify_ds_disconnect(
      atorbit::dsc::session::unique_id_t owner_unique_id,
      const atorbit::dsc::session::ds_composite_key_t& ds_key,
      int disconnect_reason) override {
    last_owner_unique_id = owner_unique_id;
    last_ds_key = ds_key;
    last_disconnect_reason = disconnect_reason;
    ++call_count;
    return next_result;
  }
};

}  // namespace

CASE_TEST(task_action_notify_ds_disconnect, run_task_action_notify_ds_disconnect_routes_notification_to_owner) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::logic::action::ds_disconnect_notification_t notification;
  atorbit::dsc::session::session_router session_router;
  fake_external_disconnect_notifier notifier;
  atorbit::dsc::session::ds_composite_key_t ds_key;

  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;
  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  session_router.bind_ds_owner(9527, ds_key);
  notification.ds_key = ds_key;
  notification.disconnect_reason = 3;

  auto result = atorbit::dsc::service::logic::action::run_task_action_notify_ds_disconnect(
      runtime, notification, session_router, notifier);

  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(1, notifier.call_count);
  CASE_EXPECT_EQ(9527, notifier.last_owner_unique_id);
  CASE_EXPECT_EQ(3, notifier.last_disconnect_reason);
}

CASE_TEST(task_action_notify_ds_disconnect, run_task_action_notify_ds_disconnect_rejects_unknown_ds_owner) {
  atorbit::dsc::service::app::runtime_handle_t runtime = 0;
  atorbit::dsc::service::logic::action::ds_disconnect_notification_t notification;
  atorbit::dsc::session::session_router session_router;
  fake_external_disconnect_notifier notifier;

  notification.ds_key.dsa_id = 7001;
  notification.ds_key.ds_id = 8001;
  notification.disconnect_reason = 3;

  auto result = atorbit::dsc::service::logic::action::run_task_action_notify_ds_disconnect(
      runtime, notification, session_router, notifier);

  CASE_EXPECT_EQ(-2, result);
  CASE_EXPECT_EQ(0, notifier.call_count);
}
#include "external_event_stream.pseudo.h"

namespace atorbit {
namespace sdk {
namespace external {

void external_event_stream::set_ds_message_callback(ds_message_callback_t callback) {
  ds_message_callback_ = callback;
}

void external_event_stream::set_ds_disconnect_callback(ds_disconnect_callback_t callback) {
  ds_disconnect_callback_ = callback;
}

void external_event_stream::set_ds_exit_callback(ds_exit_callback_t callback) {
  ds_exit_callback_ = callback;
}

void external_event_stream::dispatch_ds_message(const ds_identity_t& ds, message_buffer_t payload) const {
  if (nullptr == ds_message_callback_ || nullptr == payload) {
    return;
  }

  ds_message_callback_(ds, payload);
}

void external_event_stream::dispatch_ds_disconnect(const ds_identity_t& ds, int disconnect_reason) const {
  if (nullptr == ds_disconnect_callback_) {
    return;
  }

  ds_disconnect_event_t event;
  event.ds = ds;
  event.disconnect_reason = disconnect_reason;
  ds_disconnect_callback_(event);
}

void external_event_stream::dispatch_ds_exit(const ds_identity_t& ds,
                                             int exit_reason,
                                             int exit_code,
                                             message_buffer_t user_data) const {
  if (nullptr == ds_exit_callback_) {
    return;
  }

  ds_exit_event_t event;
  event.ds = ds;
  event.exit_reason = exit_reason;
  event.exit_code = exit_code;
  event.user_data = user_data;
  ds_exit_callback_(event);
}

}  // namespace external
}  // namespace sdk
}  // namespace atorbit
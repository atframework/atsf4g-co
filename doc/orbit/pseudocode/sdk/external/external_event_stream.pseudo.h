#pragma once

// Phase 4.4
// 目标: 抽离 Service SDK 中的上行消息、断链、退出事件分发。
// 未来真实落点: include/atorbit/sdk/external/external_event_stream.h

namespace atorbit {
namespace sdk {
namespace external {

using message_buffer_t = const char*;

struct ds_identity_t {
  unsigned long long dsa_id = 0;
  unsigned long long ds_id = 0;
};

struct ds_disconnect_event_t {
  ds_identity_t ds;
  int disconnect_reason = 0;
};

struct ds_exit_event_t {
  ds_identity_t ds;
  int exit_reason = 0;
  int exit_code = 0;
  message_buffer_t user_data = nullptr;
};

using ds_message_callback_t = void (*)(const ds_identity_t&, message_buffer_t payload);
using ds_disconnect_callback_t = void (*)(const ds_disconnect_event_t& event);
using ds_exit_callback_t = void (*)(const ds_exit_event_t& event);

class external_event_stream {
public:
  void set_ds_message_callback(ds_message_callback_t callback);
  void set_ds_disconnect_callback(ds_disconnect_callback_t callback);
  void set_ds_exit_callback(ds_exit_callback_t callback);

  void dispatch_ds_message(const ds_identity_t& ds, message_buffer_t payload) const;
  void dispatch_ds_disconnect(const ds_identity_t& ds, int disconnect_reason) const;
  void dispatch_ds_exit(const ds_identity_t& ds, int exit_reason, int exit_code, message_buffer_t user_data) const;

private:
  ds_message_callback_t ds_message_callback_ = nullptr;
  ds_disconnect_callback_t ds_disconnect_callback_ = nullptr;
  ds_exit_callback_t ds_exit_callback_ = nullptr;
};

}  // namespace external
}  // namespace sdk
}  // namespace atorbit
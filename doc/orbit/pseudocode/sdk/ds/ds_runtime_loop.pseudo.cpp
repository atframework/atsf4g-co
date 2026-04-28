#include "ds_runtime_loop.pseudo.h"

namespace atorbit {
namespace sdk {
namespace ds {

result_code_t ds_runtime_loop::start(const char* local_endpoint, unsigned long long ds_id) {
  if (nullptr == local_endpoint || 0 == ds_id || register_sent_) {
    return -1;
  }

  local_endpoint_ = local_endpoint;
  ds_id_ = ds_id;
  stopping_ = false;
  stopped_ = false;
  last_exit_code_ = 0;
  last_exit_user_data_ = nullptr;

  auto register_result = send_register();
  if (register_result != 0) {
    local_endpoint_ = nullptr;
    ds_id_ = 0;
    register_sent_ = false;
    return register_result;
  }

  return 0;
}

result_code_t ds_runtime_loop::send_message(message_buffer_t payload) const {
  if (!is_started() || nullptr == payload) {
    return -1;
  }

  // 把业务消息封装成 DSForwardToExternal
  // 写入本地 channel，交给 DSA 转发到 DSC 和 service 侧
  return 0;
}

result_code_t ds_runtime_loop::poll_message_once(unsigned long long last_downstream_seq,
                                                 downstream_callback_t downstream_callback,
                                                 unsigned long long& next_last_downstream_seq) const {
  if (!is_started()) {
    return -1;
  }

  downstream_message_t output_message;
  auto recv_result = recv_message(last_downstream_seq, output_message);
  if (recv_result != 0) {
    return recv_result;
  }

  if (output_message.require_ack) {
    auto ack_result = ack_downstream(output_message.seq);
    if (ack_result != 0) {
      return ack_result;
    }
  }

  next_last_downstream_seq = output_message.seq;
  if (nullptr != downstream_callback) {
    downstream_callback(output_message);
  }

  return 0;
}

result_code_t ds_runtime_loop::send_heartbeat(const heartbeat_sample_t& sample) const {
  if (!is_started() || sample.cpu_used < 0 || sample.memory_used_mb < 0) {
    return -1;
  }

  // 定期上报心跳和资源使用，保持 DSA 侧存活状态
  return 0;
}

result_code_t ds_runtime_loop::stop_self(int exit_code, message_buffer_t user_data) const {
  if (!is_started()) {
    return -1;
  }

  auto flush_result = flush_outbound_before_stop();
  if (flush_result != 0) {
    return flush_result;
  }

  stopping_ = true;
  auto stop_notice_result = send_stop_notice(exit_code, user_data);
  if (stop_notice_result != 0) {
    stopping_ = false;
    return stop_notice_result;
  }

  auto wait_result = wait_for_local_channel_drain();
  if (wait_result != 0) {
    stopping_ = false;
    return wait_result;
  }

  last_exit_code_ = exit_code;
  last_exit_user_data_ = user_data;
  stopping_ = false;
  stopped_ = true;
  return 0;
}

bool ds_runtime_loop::is_started() const {
  return nullptr != local_endpoint_ && 0 != ds_id_ && register_sent_ && !stopped_;
}

result_code_t ds_runtime_loop::send_register() {
  if (nullptr == local_endpoint_ || 0 == ds_id_) {
    return -1;
  }

  register_sent_ = true;
  return 0;
}

result_code_t ds_runtime_loop::recv_message(unsigned long long last_downstream_seq,
                                            downstream_message_t& output_message) const {
  output_message.seq = last_downstream_seq + 1;
  output_message.payload = "pending://downstream_payload_from_dsa";
  output_message.require_ack = true;
  return 0;
}

result_code_t ds_runtime_loop::ack_downstream(unsigned long long seq) const {
  if (0 == seq) {
    return -1;
  }

  return 0;
}

result_code_t ds_runtime_loop::flush_outbound_before_stop() const {
  return 0;
}

result_code_t ds_runtime_loop::send_stop_notice(int exit_code, message_buffer_t user_data) const {
  (void)exit_code;
  (void)user_data;
  return 0;
}

result_code_t ds_runtime_loop::wait_for_local_channel_drain() const {
  return 0;
}

}  // namespace ds
}  // namespace sdk
}  // namespace atorbit
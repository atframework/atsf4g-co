#include "ds_client.pseudo.h"

namespace atorbit {
namespace sdk {
namespace ds {

result_code_t ds_client::init(const char* local_endpoint, unsigned long long ds_id) {
  if (nullptr == local_endpoint || 0 == ds_id || state_ != ds_client_state_t::k_created) {
    return -1;
  }

  state_ = ds_client_state_t::k_running;

  auto start_result = runtime_loop_.start(local_endpoint, ds_id);
  if (start_result != 0) {
    state_ = ds_client_state_t::k_created;
    return start_result;
  }

  return 0;
}

result_code_t ds_client::send_message(message_buffer_t payload) {
  if (ensure_running() != 0) {
    return -1;
  }

  return runtime_loop_.send_message(payload);
}

result_code_t ds_client::poll_message_once() {
  if (ensure_running() != 0) {
    return -1;
  }

  unsigned long long next_last_downstream_seq = last_downstream_seq_;
  auto poll_result = runtime_loop_.poll_message_once(
      last_downstream_seq_, downstream_callback_, next_last_downstream_seq);
  if (poll_result != 0) {
    return poll_result;
  }

  last_downstream_seq_ = next_last_downstream_seq;
  return poll_result;
}

void ds_client::set_message_callback(downstream_callback_t callback) {
  // 保存来自 Service 侧下行消息的回调；收到消息后主动回调业务层
  downstream_callback_ = callback;
}

result_code_t ds_client::send_heartbeat(const heartbeat_sample_t& sample) {
  if (ensure_running() != 0) {
    return -1;
  }

  return runtime_loop_.send_heartbeat(sample);
}

result_code_t ds_client::stop_self(int exit_code, message_buffer_t user_data) {
  if (ensure_running() != 0) {
    return -1;
  }

  state_ = ds_client_state_t::k_stopping;
  auto stop_result = runtime_loop_.stop_self(exit_code, user_data);
  if (stop_result != 0) {
    state_ = ds_client_state_t::k_running;
    return stop_result;
  }

  state_ = ds_client_state_t::k_stopped;
  return 0;
}

result_code_t ds_client::ensure_running() const {
  if (state_ != ds_client_state_t::k_running || !runtime_loop_.is_started()) {
    return -1;
  }

  return 0;
}

}  // namespace ds
}  // namespace sdk
}  // namespace atorbit

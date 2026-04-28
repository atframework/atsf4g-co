#include "local_channel_service.pseudo.h"

namespace atorbit {
namespace dsa {
namespace agent {

int local_channel_service::register_ds(unsigned long long ds_id, const char* local_endpoint, int process_id) {
  if (0 == ds_id || nullptr == local_endpoint || process_id <= 0) {
    return -1;
  }

  auto* record = find_record(ds_id);
  if (nullptr != record) {
    record->local_endpoint = local_endpoint;
    record->process_id = process_id;
    return 0;
  }

  auto& channel = channels_[channel_count_++];
  channel.ds_id = ds_id;
  channel.local_endpoint = local_endpoint;
  channel.process_id = process_id;
  channel.exit_notified = false;
  return 0;
}

int local_channel_service::remove_ds(unsigned long long ds_id) {
  for (unsigned long long index = 0; index < channel_count_; ++index) {
    if (channels_[index].ds_id != ds_id) {
      continue;
    }

    channels_[index] = channels_[channel_count_ - 1];
    --channel_count_;
    return 0;
  }

  return -1;
}

bool local_channel_service::has_ds(unsigned long long ds_id) const {
  return nullptr != find_record(ds_id);
}

int local_channel_service::handle_register_message(const local_register_message_t& message) {
  return register_ds(message.ds_id, message.local_endpoint, message.process_id);
}

int local_channel_service::handle_heartbeat_message(const local_heartbeat_message_t& message) {
  auto* record = find_record(message.ds_id);
  if (nullptr == record) {
    return -1;
  }

  record->last_heartbeat_ms = message.timestamp_ms;
  return 0;
}

int local_channel_service::handle_exit_message(const local_exit_message_t& message) {
  auto* record = find_record(message.ds_id);
  if (nullptr == record) {
    return -1;
  }

  record->exit_notified = true;
  (void)message.exit_code;
  (void)message.user_data;
  return 0;
}

int local_channel_service::forward_to_ds(const downstream_packet_t& packet) {
  if (!has_ds(packet.ds_id)) {
    return -1;
  }

  // 根据 ds_id 找到本地 endpoint，并把 packet 写入本地 channel。
  (void)packet.sequence;
  (void)packet.payload;
  return 0;
}

int local_channel_service::ack_upstream(unsigned long long ds_id, unsigned long long sequence) {
  if (!has_ds(ds_id)) {
    return -1;
  }

  // 记录该 sequence 已由上游确认，允许清理本地缓冲。
  (void)sequence;
  return 0;
}

local_channel_service::channel_record_t* local_channel_service::find_record(unsigned long long ds_id) {
  for (unsigned long long index = 0; index < channel_count_; ++index) {
    if (channels_[index].ds_id == ds_id) {
      return &channels_[index];
    }
  }

  return nullptr;
}

const local_channel_service::channel_record_t* local_channel_service::find_record(unsigned long long ds_id) const {
  for (unsigned long long index = 0; index < channel_count_; ++index) {
    if (channels_[index].ds_id == ds_id) {
      return &channels_[index];
    }
  }

  return nullptr;
}

}  // namespace agent
}  // namespace dsa
}  // namespace atorbit

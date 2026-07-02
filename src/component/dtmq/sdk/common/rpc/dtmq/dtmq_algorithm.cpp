// Copyright 2026 atframework

#include "rpc/dtmq/dtmq_algorithm.h"

#include <xxhash.h>

#include <string/string_format.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.dtmq.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstdint>
#include <string>

namespace rpc {
namespace dtmq {

DTMQ_COMMON_SDK_API uint64_t get_hash_code(const atfw::dtmq::DChannelMessage& channel_log) noexcept {
  return channel_log.hash_code();
}

DTMQ_COMMON_SDK_API void set_hash_code(atfw::dtmq::DChannelMessage& channel_log, uint64_t hash_code) noexcept {
  channel_log.set_hash_code(hash_code);
}

DTMQ_COMMON_SDK_API uint64_t calculate_hash_code(uint64_t previous,
                                                 const atfw::dtmq::DChannelMessage& channel_log) noexcept {
  uint64_t buffer[1] = {static_cast<uint64_t>(channel_log.sequence())};

  return static_cast<uint64_t>(XXH64(buffer, sizeof(buffer), static_cast<XXH64_hash_t>(previous)));
}

DTMQ_COMMON_SDK_API std::string make_unicast_channel_id(uint32_t type_id, uint64_t zone_id, uint64_t instance_id) {
  return atfw::util::string::format("channel:{}:{}:{}", type_id, zone_id, instance_id);
}

DTMQ_COMMON_SDK_API std::string make_zone_broadcast_channel_id(uint32_t type_id, uint64_t zone_id) {
  return atfw::util::string::format("zone:{}:{}", type_id, zone_id);
}

DTMQ_COMMON_SDK_API std::string make_world_broadcast_channel_id(uint32_t type_id, uint64_t world_id) {
  return atfw::util::string::format("world:{}:{}", type_id, world_id);
}

DTMQ_COMMON_SDK_API std::string make_world_partition_channel_id(uint32_t type_id, uint64_t world_id,
                                                                uint64_t partition_id) {
  return atfw::util::string::format("world-partition:{}:{}:{}", type_id, world_id, partition_id);
}

DTMQ_COMMON_SDK_API bool is_global_channel(const atfw::dtmq::DChannelIdKey& channel_key) noexcept {
  switch (channel_key.cast_type_case()) {
    case atfw::dtmq::DChannelIdKey::kBroadcastWorldPartitionId:
    case atfw::dtmq::DChannelIdKey::kBroadcastWorld:
    case atfw::dtmq::DChannelIdKey::kBroadcastZone:
      return true;
    default:
      return false;
  }
}

DTMQ_COMMON_SDK_API std::string calculate_channel_id(const atfw::dtmq::DChannelIdKey& channel_key) {
  switch (channel_key.cast_type_case()) {
    case atfw::dtmq::DChannelIdKey::kBroadcastWorldPartitionId: {
      if (channel_key.broadcast_world_partition_id() == 0) {
        return {};
      }
      return make_world_partition_channel_id(channel_key.channel_type(), logic_config::me()->get_local_world_id(),
                                             channel_key.broadcast_world_partition_id());
    }
    case atfw::dtmq::DChannelIdKey::kBroadcastWorld: {
      return make_world_broadcast_channel_id(channel_key.channel_type(), logic_config::me()->get_local_world_id());
    }
    case atfw::dtmq::DChannelIdKey::kBroadcastZone: {
      return make_zone_broadcast_channel_id(channel_key.channel_type(), channel_key.channel_zone_id());
    }
    case atfw::dtmq::DChannelIdKey::kUnicastInstanceId: {
      if (channel_key.unicast_instance_id() == 0) {
        return {};
      }
      return make_unicast_channel_id(channel_key.channel_type(), channel_key.channel_zone_id(),
                                     channel_key.unicast_instance_id());
    }
    default: {
      return {};
    }
  }
}

DTMQ_COMMON_SDK_API int32_t generate_channel_id(atfw::dtmq::DChannelIdKey& channel_key) {
  if (channel_key.channel_type() == 0) {
    return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL;
  }

  if (!channel_key.channel_id().empty()) {
    return 0;
  }

  channel_key.set_channel_id(calculate_channel_id(channel_key));
  if (channel_key.channel_id().empty()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL;
  }

  return 0;
}

}  // namespace dtmq
}  // namespace rpc

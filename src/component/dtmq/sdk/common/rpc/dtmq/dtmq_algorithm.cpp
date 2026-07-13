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
  uint64_t content_hash = static_cast<uint64_t>(channel_log.detail().command_case());
  if (!channel_log.detail().text().empty()) {
    content_hash = XXH64(channel_log.detail().text().data(), channel_log.detail().text().size(),
                         static_cast<XXH64_hash_t>(previous));
  } else if (!channel_log.detail().event().type_url().empty()) {
    content_hash = XXH64(channel_log.detail().event().value().data(), channel_log.detail().event().value().size(),
                         static_cast<XXH64_hash_t>(previous));
  }

  uint64_t buffer[2] = {static_cast<uint64_t>(channel_log.sequence()), content_hash};
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

}  // namespace dtmq
}  // namespace rpc

// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/dtmq/dtmq_algorithm.h"

#include <xxhash.h>

#include <common/string_oprs.h>
#include <gsl/select-gsl.h>
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
  switch (channel_log.detail().command_case()) {
    case atfw::dtmq::DChannelMessageDetail::kText:
      content_hash = XXH64(channel_log.detail().text().data(), channel_log.detail().text().size(),
                           static_cast<XXH64_hash_t>(previous));
      break;
    case atfw::dtmq::DChannelMessageDetail::kEvent:
      content_hash = XXH64(channel_log.detail().event().value().data(), channel_log.detail().event().value().size(),
                           static_cast<XXH64_hash_t>(previous));
      break;
    default:
      // 只关注文本和事件内容即可，其他都是内部事件，sequence+类型正确即可
      break;
  }

  uint64_t buffer[3] = {static_cast<uint64_t>(channel_log.sequence()),
                        static_cast<uint64_t>(channel_log.detail().command_case()), content_hash};
  return static_cast<uint64_t>(XXH64(buffer, sizeof(buffer), static_cast<XXH64_hash_t>(previous)));
}

DTMQ_COMMON_SDK_API std::string make_unicast_channel_id(uint32_t type_id, uint64_t zone_id, uint64_t instance_id) {
  return atfw::util::string::format("channel:{}:{}:{}", type_id, zone_id, instance_id);
}

DTMQ_COMMON_SDK_API uint32_t parse_unicast_channel_type_from_channel_id(gsl::string_view channel_id) {
  if (channel_id.size() <= 8 || channel_id.substr(0, 8) != "channel:") {
    return 0;
  }

  auto substr = channel_id.substr(8);
  auto pos = substr.find(':');
  if (pos != gsl::string_view::npos) {
    substr = substr.substr(0, pos);
  }

  return atfw::util::string::to_int<uint32_t>(substr);
}

DTMQ_COMMON_SDK_API std::string make_zone_broadcast_channel_id(uint32_t type_id, uint64_t zone_id) {
  return atfw::util::string::format("zone:{}:{}", type_id, zone_id);
}

DTMQ_COMMON_SDK_API uint32_t parse_zone_broadcast_channel_type_from_channel_id(gsl::string_view channel_id) {
  if (channel_id.size() <= 5 || channel_id.substr(0, 5) != "zone:") {
    return 0;
  }

  auto substr = channel_id.substr(5);
  auto pos = substr.find(':');
  if (pos != gsl::string_view::npos) {
    substr = substr.substr(0, pos);
  }

  return atfw::util::string::to_int<uint32_t>(substr);
}

DTMQ_COMMON_SDK_API std::string make_world_broadcast_channel_id(uint32_t type_id, uint64_t world_id) {
  return atfw::util::string::format("world:{}:{}", type_id, world_id);
}

DTMQ_COMMON_SDK_API uint32_t parse_world_broadcast_channel_type_from_channel_id(gsl::string_view channel_id) {
  if (channel_id.size() <= 6 || channel_id.substr(0, 6) != "world:") {
    return 0;
  }

  auto substr = channel_id.substr(6);
  auto pos = substr.find(':');
  if (pos != gsl::string_view::npos) {
    substr = substr.substr(0, pos);
  }

  return atfw::util::string::to_int<uint32_t>(substr);
}

DTMQ_COMMON_SDK_API std::string make_world_partition_channel_id(uint32_t type_id, uint64_t world_id,
                                                                uint64_t partition_id) {
  return atfw::util::string::format("world-partition:{}:{}:{}", type_id, world_id, partition_id);
}

DTMQ_COMMON_SDK_API uint32_t parse_world_partition_channel_type_from_channel_id(gsl::string_view channel_id) {
  if (channel_id.size() <= 16 || channel_id.substr(0, 16) != "world-partition:") {
    return 0;
  }

  auto substr = channel_id.substr(16);
  auto pos = substr.find(':');
  if (pos != gsl::string_view::npos) {
    substr = substr.substr(0, pos);
  }

  return atfw::util::string::to_int<uint32_t>(substr);
}

}  // namespace dtmq
}  // namespace rpc

// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#include "rpc/cache/cache_algorithm.h"

#include <algorithm/murmur_hash.h>

namespace rpc {
namespace cache_api {
CACHE_RPC_API size_t cache_watcher_hash_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_watcher &key) const {
  uint32_t cache_type_id = static_cast<uint32_t>(key.cache_type());
  uint32_t zone_id = key.zone_id();
  uint64_t instance_id = key.instance_id();
  unsigned char buffer[sizeof(cache_type_id) + sizeof(zone_id) + sizeof(instance_id)] = {0};

  memcpy(buffer, &cache_type_id, sizeof(cache_type_id));
  memcpy(buffer + sizeof(cache_type_id), &zone_id, sizeof(zone_id));
  memcpy(buffer + sizeof(cache_type_id) + sizeof(zone_id), &instance_id, sizeof(instance_id));

  uint64_t out[2];
  // 随便搞个素数作magic number
  util::hash::murmur_hash3_x64_128(buffer, static_cast<int>(sizeof(buffer)), 0x05D6649FU, out);

  return out[0];
}

CACHE_RPC_API bool cache_watcher_equal_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_watcher &left,
                                                     const PROJECT_NAMESPACE_ID::object_cache_watcher &right) const {
  return left.cache_type() == right.cache_type() && left.instance_id() == right.instance_id() &&
         left.zone_id() == right.zone_id();
}

CACHE_RPC_API size_t cache_key_hash_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_key &key) const {
  uint32_t cache_type_id = static_cast<uint32_t>(key.cache_type());
  uint32_t zone_id = key.zone_id();
  uint64_t instance_id = key.instance_id();
  unsigned char buffer[sizeof(cache_type_id) + sizeof(zone_id) + sizeof(instance_id)] = {0};

  memcpy(buffer, &cache_type_id, sizeof(cache_type_id));
  memcpy(buffer + sizeof(cache_type_id), &zone_id, sizeof(zone_id));
  memcpy(buffer + sizeof(cache_type_id) + sizeof(zone_id), &instance_id, sizeof(instance_id));

  uint64_t out[2];
  // 随便搞个素数作magic number
  util::hash::murmur_hash3_x64_128(buffer, static_cast<int>(sizeof(buffer)), 0x05D6649FU, out);

  return out[0];
}

CACHE_RPC_API bool cache_key_equal_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_key &left,
                                                 const PROJECT_NAMESPACE_ID::object_cache_key &right) const {
  return left.cache_type() == right.cache_type() && left.instance_id() == right.instance_id() &&
         left.zone_id() == right.zone_id();
}

}  // namespace cache_api
}  // namespace rpc

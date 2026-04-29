// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstddef>
#include <cstdint>

#include "config/compile_optimize.h"
#include "protocol/pbdesc/com.struct.cache.pb.h"

PROJECT_NAMESPACE_BEGIN
class user_login_data;
class user_data;
class DUserProfile;
class DClientDeviceInfo;
class DUserBasicData;
class DUserCacheMeta;
class DCacheApiMetaData;

class object_cache_watcher;
class object_cache_key;
PROJECT_NAMESPACE_END

namespace google {
namespace protobuf {
class Any;
}
}  // namespace google

namespace rpc {
class context;

namespace cache_api {
struct cache_watcher_hash_t {
  ATFW_UTIL_FORCEINLINE cache_watcher_hash_t() = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_hash_t(const cache_watcher_hash_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_hash_t(cache_watcher_hash_t&&) = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_hash_t& operator=(const cache_watcher_hash_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_hash_t& operator=(cache_watcher_hash_t&&) = default;
  ATFW_UTIL_FORCEINLINE ~cache_watcher_hash_t() = default;

  CACHE_RPC_API size_t operator()(const PROJECT_NAMESPACE_ID::object_cache_watcher& key) const;
};

struct cache_watcher_equal_t {
  ATFW_UTIL_FORCEINLINE cache_watcher_equal_t() = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_equal_t(const cache_watcher_equal_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_equal_t(cache_watcher_equal_t&&) = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_equal_t& operator=(const cache_watcher_equal_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_watcher_equal_t& operator=(cache_watcher_equal_t&&) = default;
  ATFW_UTIL_FORCEINLINE ~cache_watcher_equal_t() = default;

  CACHE_RPC_API bool operator()(const PROJECT_NAMESPACE_ID::object_cache_watcher& left,
                                const PROJECT_NAMESPACE_ID::object_cache_watcher& right) const;
};

struct cache_key_hash_t {
  ATFW_UTIL_FORCEINLINE cache_key_hash_t() = default;
  ATFW_UTIL_FORCEINLINE cache_key_hash_t(const cache_key_hash_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_key_hash_t(cache_key_hash_t&&) = default;
  ATFW_UTIL_FORCEINLINE cache_key_hash_t& operator=(const cache_key_hash_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_key_hash_t& operator=(cache_key_hash_t&&) = default;
  ATFW_UTIL_FORCEINLINE ~cache_key_hash_t() = default;

  CACHE_RPC_API size_t operator()(const PROJECT_NAMESPACE_ID::object_cache_key& key) const;
};

struct cache_key_equal_t {
  ATFW_UTIL_FORCEINLINE cache_key_equal_t() = default;
  ATFW_UTIL_FORCEINLINE cache_key_equal_t(const cache_key_equal_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_key_equal_t(cache_key_equal_t&&) = default;
  ATFW_UTIL_FORCEINLINE cache_key_equal_t& operator=(const cache_key_equal_t&) = default;
  ATFW_UTIL_FORCEINLINE cache_key_equal_t& operator=(cache_key_equal_t&&) = default;
  ATFW_UTIL_FORCEINLINE ~cache_key_equal_t() = default;

  CACHE_RPC_API bool operator()(const PROJECT_NAMESPACE_ID::object_cache_key& left,
                                const PROJECT_NAMESPACE_ID::object_cache_key& right) const;
};

CACHE_RPC_API void update_cache_content_from_meta(::rpc::context& ctx, PROJECT_NAMESPACE_ID::DUserBasicData& output,
                                                  const PROJECT_NAMESPACE_ID::DUserCacheMeta& input);
CACHE_RPC_API void update_cache_meta_from_origin_data(
    ::rpc::context& ctx, PROJECT_NAMESPACE_ID::DUserCacheMeta& output, uint64_t data_version,
    const PROJECT_NAMESPACE_ID::user_login_data* input_login_data,
    const PROJECT_NAMESPACE_ID::user_data* input_user_data,
    const PROJECT_NAMESPACE_ID::DUserProfile* input_user_profile,
    const PROJECT_NAMESPACE_ID::DClientDeviceInfo* input_client_device_info);

CACHE_RPC_API void pick_key_from_meta(::rpc::context& ctx, PROJECT_NAMESPACE_ID::object_cache_key& output,
                                      const PROJECT_NAMESPACE_ID::DCacheApiMetaData& input);

CACHE_RPC_API void pick_key_from_content(::rpc::context& ctx, PROJECT_NAMESPACE_ID::object_cache_key& output,
                                         const PROJECT_NAMESPACE_ID::DCacheApiObjectData& input);

CACHE_RPC_API bool pack_cache_meta_to_any(::rpc::context& ctx, google::protobuf::Any& output,
                                          const PROJECT_NAMESPACE_ID::DCacheApiMetaData& input);

CACHE_RPC_API bool unpack_cache_meta_from_any(::rpc::context& ctx, PROJECT_NAMESPACE_ID::DCacheApiMetaData& output,
                                              const google::protobuf::Any& input);

CACHE_RPC_API bool pack_cache_content_to_any(::rpc::context& ctx, google::protobuf::Any& output,
                                             const PROJECT_NAMESPACE_ID::DCacheApiObjectData& input);

CACHE_RPC_API bool unpack_cache_content_from_any(::rpc::context& ctx, PROJECT_NAMESPACE_ID::DCacheApiObjectData& output,
                                                 const google::protobuf::Any& input);
}  // namespace cache_api
}  // namespace rpc

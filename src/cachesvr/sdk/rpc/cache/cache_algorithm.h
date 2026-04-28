// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/cache_service.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <cstddef>
#include <cstdint>

#include <unordered_map>
#include <unordered_set>
#include "config/compile_optimize.h"

namespace rpc {
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
}  // namespace cache_api
}  // namespace rpc

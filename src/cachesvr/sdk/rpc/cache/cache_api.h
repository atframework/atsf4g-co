// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#pragma once

#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/any.pb.h>
#include <protocol/common/com.struct.cache.common.pb.h>
#include <protocol/pbdesc/cache_service.pb.h>
#include <protocol/pbdesc/svr.struct.cache.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

#include <cstdint>

#include "rpc/cache/cache_algorithm.h"

namespace rpc {
class context;

namespace cache_api {
CACHE_RPC_API uint64_t get_cachesvr_server_id(PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type, uint32_t zone_id,
                                              uint64_t instance_id);
CACHE_RPC_API uint64_t get_cachesvr_server_id(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key);
CACHE_RPC_API uint64_t get_cachesvr_server_id(const PROJECT_NAMESPACE_ID::object_cache_watch_key &cache_key);

CACHE_RPC_API bool has_cachesvr();

ATFW_EXPLICIT_NODISCARD_ATTR CACHE_RPC_API rpc::result_code_type batch_get_cache(
    ::rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> &&cache_keys,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &cache_contents);

CACHE_RPC_API void pick_key_from_meta(::rpc::context &ctx, PROJECT_NAMESPACE_ID::object_cache_key &output,
                                      const ::google::protobuf::Any &input);

CACHE_RPC_API void set_cache_expired(::rpc::context &ctx, PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type,
                                     uint32_t zone_id, uint64_t instance_id);

}  // namespace cache_api
}  // namespace rpc

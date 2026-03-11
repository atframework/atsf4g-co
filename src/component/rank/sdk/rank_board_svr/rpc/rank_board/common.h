#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include "protocol/common/com.struct.rank.common.pb.h"
#include "protocol/pbdesc/com.struct.rank.pb.h"

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/etcdcli/etcd_discovery.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include "config/server_frame_build_feature.h"

#include <dispatcher/task_type_traits.h>
#include <rpc/rpc_common_types.h>
#include <rpc/rpc_context.h>

#include <design_pattern/singleton.h>
#include <xxhash.h>

PROJECT_NAMESPACE_BEGIN

namespace rank_api {

template <class T>
static void copy_buffer_into(char* start, size_t offset, T value) {
  memcpy(start + offset, &value, sizeof(value));
}

struct RANK_BOARD_SDK_API rank_key_hash_type {
  size_t operator()(const ::PROJECT_NAMESPACE_ID::DRankKey& rank_key) const {
    char hash_buffer[sizeof(int32_t) * 4];
    copy_buffer_into(hash_buffer, 0, rank_key.rank_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t), rank_key.rank_instance_id());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, rank_key.sub_rank_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, rank_key.sub_rank_instance_id());
    // 随便搞个素数作magic number
    return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), static_cast<XXH64_hash_t>(920431627)));
  }
};
struct RANK_BOARD_SDK_API rank_key_equal_type {
  bool operator()(const ::PROJECT_NAMESPACE_ID::DRankKey& lhs, const ::PROJECT_NAMESPACE_ID::DRankKey& rhs) const {
    return lhs.rank_type() == rhs.rank_type() && lhs.rank_instance_id() == rhs.rank_instance_id()
           && lhs.sub_rank_type() == rhs.sub_rank_type() && lhs.sub_rank_instance_id() == rhs.sub_rank_instance_id();
  }
};
}  // namespace rank_api
PROJECT_NAMESPACE_END
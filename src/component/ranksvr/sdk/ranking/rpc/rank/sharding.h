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

PROJECT_NAMESPACE_BEGIN

namespace rank_api {
/**
 * @brief 获取主节点
 * @param rank_key 排行榜key
 * @return node_id
 */
RANK_RPC_API uint64_t get_rank_main_server_id(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key);

/**
 * @brief 获取从节点信息(如果没有从节点信息则返回主节点)
 * @param rank_key 排行榜key
 * @param slave_server_ids 从节点id集合
 */
EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type get_rank_slave_server_ids(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, std::vector<uint64_t>& slave_server_ids);

/**
 * @brief 随机获取一个从节点(如果没有从节点信息则返回主节点)
 * @param rank_key 排行榜key
 * @param slave_server_id 从节点id
 */
EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type get_rank_slave_server_random(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, uint64_t& slave_server_id);
}  // namespace rank_api

PROJECT_NAMESPACE_END
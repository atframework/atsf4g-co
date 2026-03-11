
#pragma once

#include <std/explicit_declare.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/repeated_field.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>
#include "dispatcher/dispatcher_type_defines.h"

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#include <config/compile_optimize.h>

#include <std/explicit_declare.h>

PROJECT_NAMESPACE_BEGIN

class SSRankGetSpecifyRankReq;
class SSRankGetSpecifyRankRsp;
class SSRankSetScoreReq;
class SSRankSetScoreRsp;
class SSRankDelUserReq;
class SSRankDelUserRsp;
class SSRankGetTopReq;
class SSRankGetTopRsp;
class SSRankClearReq;
class SSRankClearRsp;
class DRankQueryRspData;

class DRankUserKey;
class DRankKey;
class DRankUserBoardData;
class DRankCustomData;

PROJECT_NAMESPACE_END

namespace rpc {

class context;

namespace rank_board {
/**
 * @brief 获取某个玩家的分数
 * @param user 玩家key
 * @param rank 排行榜key
 * @param output 结果
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type get_special_one(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankUserKey& user, const PROJECT_NAMESPACE_ID::DRankKey& rank,
    PROJECT_NAMESPACE_ID::DRankUserBoardData& output);

/**
 * @brief 拉取榜单数据
 * @param rank 排行榜key
 * @param start_no 起始排名
 * @param count 个数
 * @param rank 排行榜key
 * @param output 结果
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type get_top(rpc::context& ctx,
                                                                   const PROJECT_NAMESPACE_ID::DRankKey& rank,
                                                                   uint32_t start_no, uint32_t count,
                                                                   PROJECT_NAMESPACE_ID::DRankQueryRspData& output);

/**
 * @brief 获取玩家前后排名count的排行榜数据
 * @param rank 排行榜key
 * @param user 玩家key
 * @param count 前后的数量
 * @param output 结果
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type get_special_one_front_back(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank, const PROJECT_NAMESPACE_ID::DRankUserKey& user,
    uint32_t count, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBoardData>& output);

/**
 * @brief 设置玩家分数
 * @param user 玩家key
 * @param rank 排行榜key
 * @param score 上报的成绩
 * @param custom_data 榜单自定义数据
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type update_score(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankUserKey& user, const PROJECT_NAMESPACE_ID::DRankKey& rank,
    int64_t score, const PROJECT_NAMESPACE_ID::DRankCustomData& custom_data);

/**
 * @brief 修改玩家分数(增量加减)
 * @param user 玩家key
 * @param rank 排行榜key
 * @param score 上报的变化值
 * @param custom_data 榜单自定义数据 会覆盖之前的上报
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type modify_score(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankUserKey& user, const PROJECT_NAMESPACE_ID::DRankKey& rank,
    int64_t score, const PROJECT_NAMESPACE_ID::DRankCustomData& custom_data);

/**
 * @brief 从排行榜上移除玩家
 * @param user 玩家key
 * @param rank 排行榜key
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type remove_one(rpc::context& ctx,
                                                                      const PROJECT_NAMESPACE_ID::DRankUserKey& user,
                                                                      const PROJECT_NAMESPACE_ID::DRankKey& rank);

/**
 * @brief 删除排行榜
 * @param rank 排行榜key
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type clear_rank(rpc::context& ctx,
                                                                      const PROJECT_NAMESPACE_ID::DRankKey& rank);

/**
 * @brief 查询镜像数据
 * @param rank 排行榜key
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type get_top_from_mirror(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank, uint32_t zone_id, uint32_t start_no, uint32_t count,
    int64_t mirror_id, PROJECT_NAMESPACE_ID::DRankQueryRspData& output);

/**
 * @brief 拉取排行榜镜像
 * @param rank 排行榜key
 * @param[out] mirror_id 返回的镜像id
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type make_new_mirror(rpc::context& ctx,
                                                                           const PROJECT_NAMESPACE_ID::DRankKey& rank,
                                                                           int64_t& mirror_id);
};  // namespace rank
};  // namespace rpc
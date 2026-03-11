#pragma once

#include <design_pattern/singleton.h>
#include <cstdint>
#include <ctime>
#include <memory>
#include <unordered_map>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/common/com.struct.rank.common.pb.h>
#include <protocol/pbdesc/com.struct.rank.pb.h>
#include <protocol/pbdesc/rank_board_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>
#include <rpc/rpc_shared_message.h>


#include <rpc/db/db_utils.h>
#include <rpc/rpc_common_types.h>
#include "config/server_frame_build_feature.h"

#include "logic/rank.h"
#include "rpc/rank_board/common.h"
#include "rpc/rpc_context.h"

struct rank_sort_type_hash_type {
  inline size_t operator()(const PROJECT_NAMESPACE_ID::EnRankSortType& sort_type) const;
};
struct rank_sort_type_equal_type {
  inline bool operator()(const PROJECT_NAMESPACE_ID::EnRankSortType& lhs,
                         const PROJECT_NAMESPACE_ID::EnRankSortType& rhs) const;
};

class rank_manager : public util::design_pattern::singleton<rank_manager> {

 public:
  rank_manager();

  void tick();
  int init();

  void stop();

  rank_ptr_type get_rank(const PROJECT_NAMESPACE_ID::DRankKey& rank_key) const;

  void refresh_limit_second(rpc::context& ctx, time_t now_tm);

  /**
   * @brief 获取可变排行榜对象
   * @param ctx rpc上下文
   * @param key 排行榜key
   * @param out_rank 返回的排行榜对象
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type mutable_rank(rpc::context& ctx,
                                                             const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                             rank_ptr_type& out_rank);
  /**
   * @brief 获取可变排行榜对象
   * @param ctx rpc上下文
   * @param key 排行榜key
   * @param out_rank 返回的排行榜对象
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type mutable_main_rank(rpc::context& ctx,
                                                                  const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                                  rank_ptr_type& out_rank);

  /**
   * @brief 修改玩家分数
   * @param ctx rpc上下文
   * @param key 排行榜key
   * @param user_key 玩家排序键
   * @param score 分数
   * @param custom_data 自定义数据
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type modify_score(rpc::context& ctx,
                                                             const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                             const PROJECT_NAMESPACE_ID::DRankUserKey& user_key,
                                                             int64_t score,
                                                             const PROJECT_NAMESPACE_ID::DRankCustomData& custom_data);

  /**
   * @brief 查询指定key排行数据
   * @param ctx rpc上下文
   * @param key 排行榜key
   * @param sort_key 玩家排序键
   * @param output 返回的玩家排行数据
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type query_one_user(rpc::context& ctx,
                                                               const 
                                                               PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                               const PROJECT_NAMESPACE_ID::DRankUserKey& sort_key,
                                                               PROJECT_NAMESPACE_ID::DRankUserBoardData& output);
  /**
   * @brief 查询排行数据
   * @param ctx rpc上下文
   * @param key 排行榜key
   * @param from 查询的起始位置
   * @param count 查询的数量
   * @param output 返回的排行榜对象
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type query_top(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                          uint32_t from, uint32_t count,
                                                          PROJECT_NAMESPACE_ID::DRankQueryRspData& output);

  inline bool is_running() { return init_ && !closing_; }
  inline bool is_init() { return init_; }
  inline bool is_closing() { return closing_; }
  bool is_closed();
  void is_finished() { init_ = true; }

 private:
  rank::compare_fn_t get_compare_fn(PROJECT_NAMESPACE_ID::EnRankSortType);

  std::vector<uint64_t> get_slave_nodes(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank,
                                        uint64_t main_node);




  EXPLICIT_NODISCARD_ATTR rpc::result_code_type upgrade_rank_to_main(rpc::context& ctx,
                                                                     const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                                     int32_t db_router_version);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type check_slave_and_highest_data_version_slave(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
      const PROJECT_NAMESPACE_ID::table_rank_router& new_db_router, std::pair<uint64_t, int64_t>& highest_slave_node);

 private:
  std::unordered_map<PROJECT_NAMESPACE_ID::DRankKey, rank_ptr_type, PROJECT_NAMESPACE_ID::rank_api::rank_key_hash_type,
                     PROJECT_NAMESPACE_ID::rank_api::rank_key_equal_type>
      rank_map_;
  std::unordered_map<PROJECT_NAMESPACE_ID::EnRankSortType, rank::compare_fn_t, rank_sort_type_hash_type,
                     rank_sort_type_equal_type>
      compare_mp_;
  bool init_;
  bool closing_;

  std::unordered_map<
      PROJECT_NAMESPACE_ID::DRankKey, std::pair<task_type_trait::task_type, PROJECT_NAMESPACE_ID::SSRankLoadMainRsp>,
      PROJECT_NAMESPACE_ID::rank_api::rank_key_hash_type, PROJECT_NAMESPACE_ID::rank_api::rank_key_equal_type>
      load_main_task_mp_;
  time_t last_refresh_second_;
};
#pragma once

#include <config/compiler_features.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.pb.h>
#include <protocol/pbdesc/com.struct.rank.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>
#include <protocol/pbdesc/svr.struct.rank.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_stl_unordered_map.h>

#include <dispatcher/task_manager.h>
#include <dispatcher/task_type_traits.h>

#include <rank/logic_rank_algorithm.h>
#include <rank/logic_rank_handle.h>
#include <rpc/rpc_common_types.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tdr2pb {
namespace local {
class TABLE_USER_DEF;
}
}  // namespace tdr2pb

class player;

namespace rpc {
class context;
}

class user_rank_manager {
 public:
  struct rank_board_cache {
    uint32_t rank_no;  // 未上榜则为0
    uint32_t score;

    inline rank_board_cache() : rank_no(0), score(0) {}
    inline rank_board_cache(uint32_t n, uint32_t s) : rank_no(n), score(s) {}
  };

 public:
  explicit user_rank_manager(player &owner);
  ~user_rank_manager();

  // 创建默认角色数据
  rpc::result_code_type create_init(rpc::context &ctx);

  // 登入读取用户数据
  void login_init(rpc::context &ctx);

  void force_refresh_feature_limit_second(rpc::context &ctx);
  // 刷新功能限制次数
  void refresh_feature_limit_second(rpc::context &ctx);

  // 从table数据初始化
  void init_from_table_data(rpc::context &ctx, const PROJECT_NAMESPACE_ID::table_user &player_table);

  int dump(rpc::context &ctx, PROJECT_NAMESPACE_ID::table_user &user);

  bool is_dirty() const noexcept;

  void clear_dirty();

  bool is_io_task_running() const;

  void try_start_io_task(rpc::context &ctx);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type wait_for_async_task(rpc::context &ctx);

  bool is_rank_writable_now(const logic_rank_rule_cfg_t &cfg, time_t now) const noexcept;

  int32_t get_default_role_guid();
  bool check_custom_key_exsit(int64_t guid);
  bool check_rank_report_limit(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                               PROJECT_NAMESPACE_ID::DRankUnsubmitData &unsubmit);

  logic_rank_handle_variant get_rank_handle(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg);

  logic_rank_handle_key get_current_rank_data_key(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg);
  logic_rank_handle_key get_current_rank_data_key(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                  uint32_t subrank_instance_id);

  /**
   * @brief 批量提交排行榜分数（本接口不支持set分数）
   * @note 某些榜使用set接口，请不要和 add_rank_score/add_rank_score/submit_rank_score_no_wait 混合使用
   *
   * @param ctx 上下文
   * @param need_submit_vec 待提交的数据
   * @return rpc::result_code_type
   */
  void submit_rank_score_no_wait(rpc::context &ctx,
                                 const std::vector<PROJECT_NAMESPACE_ID::user_rank_unsubmit_data> &need_submit_vec);
  /**
   * @brief 批量修改排行榜缓存不提交（本接口不支持set分数）
   * @note 某些榜使用set接口，请不要和 add_rank_score/add_rank_score/submit_rank_score_no_wait 混合使用
   *
   * @param need_submit_vec 待提交的数据
   * @return rpc::result_code_type
   */
  void update_rank_cache(const std::vector<PROJECT_NAMESPACE_ID::user_rank_unsubmit_data> &need_submit_vec);

  /**
   * @biref 获取指定排行榜的缓存数据
   * @return 排行榜的缓存数据,失败或者无效返回全0
   */
  EXPLICIT_NODISCARD_ATTR rpc::rpc_result<rank_board_cache> get_rank_cache(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key);

  /**
   * @brief 设置排行榜分数（只允许设置自己的分数）
   * @note 某些榜使用set接口，请不要和 add_rank_score/add_rank_score 混合使用
   *
   * @param ctx 上下文
   * @param rank_key 排行榜Key
   * @param cfg 对应的排行榜配置（ABC榜中rank_key不能高效的反向查找对应配置）
   * @param score 目标分数
   * @param user_extend 用户扩展数据，nullptr 表示不更新
   * @param sync_mode 同步模式（等待返回）
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type set_rank_score(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key, uint32_t score,
      const logic_rank_user_extend_span *user_extend = nullptr, bool sync_mode = false);

  /**
   * @brief 对指定用户增加分数
   * @note 某些榜使用add/sub接口，请不要和 set_rank_score 混合使用
   *
   * @param ctx 上下文
   * @param rank_key 排行榜Key
   * @param cfg 对应的排行榜配置（ABC榜中rank_key不能高效的反向查找对应配置）
   * @param target_user_zone_id 目标用户zone id(可能跨服)
   * @param target_user_id 目标用户user id
   * @param score 要增加的分数
   * @param user_extend 用户扩展数据，必须保持同一种类型的数据生成是一致的
   * @param sync_mode 同步模式（等待返回）
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_rank_score(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      uint32_t target_user_zone_id, uint64_t target_user_id,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key, uint32_t score,
      logic_rank_user_extend_span user_extend = {}, bool sync_mode = false);
  /**
   * @brief 对指定用户扣除分数
   * @note 某些榜使用add/sub接口，请不要和 set_rank_score 混合使用
   *
   * @param ctx 上下文
   * @param rank_key 排行榜Key
   * @param cfg 对应的排行榜配置（ABC榜中rank_key不能高效的反向查找对应配置）
   * @param target_user_zone_id 目标用户zone id(可能跨服)
   * @param target_user_id 目标用户user id
   * @param score 要扣除的分数
   * @param user_extend 用户扩展数据，必须保持同一种类型的数据生成是一致的
   * @param sync_mode 同步模式（等待返回)
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type sub_rank_score(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      uint32_t target_user_zone_id, uint64_t target_user_id,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key, uint32_t score,
      logic_rank_user_extend_span user_extend = {}, bool sync_mode = false);

  /**
   * @brief 查询rank_top排名
   *
   * @note 1.单次查询名次的总数不能超过 RANK_GET_TOP_MAX_COUNT
   *
   * @param ctx 上下文
   * @param rank_key 排行榜Key
   * @param cfg 对应的排行榜配置（ABC榜中rank_key不能高效的反向查找对应配置）
   * @param response 回包
   * @param total_count 排行榜总数
   * @param from_rank_no 起始名次位置
   * @param rank_count 查询名次总数
   * @param ignore_zero 0值视为无效值
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_top_rank(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
      uint32_t from_rank_no, uint32_t rank_count, bool ignore_zero = false);

  /**
   * @brief 查询自己在rank_top中的排名及该用户上下范围排名的用户
   * @note   该请求类与GetSpecialUserTopRankReq请求类的区别在于:
   *         1.本类只可请求一个指定用户以及其上下文用户, 共RANK_GET_TOP_RANK_MAX_COUNT个
   *         2.本请求类对应的响应包中会携带每个用户的私人数据
   * @param ctx 上下文
   * @param rank_key 排行榜Key
   * @param cfg 对应的排行榜配置（ABC榜中rank_key不能高效的反向查找对应配置）
   * @param response 回包
   * @param total_count 排行榜总数
   * @param up_count 指定用户上面uiUpCount个排名的其它用户，0表示不查询该用户上面的排名区间
   * @param down_count 指定用户下面uiUpCount个排名的其它用户，0表示不查询该用户下面的排名区间
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_self_top_rank(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
      uint32_t up_count = 0, uint32_t down_count = 0);

  /**
   * @brief 查询指定一个用户在rank_top中的排名及该用户上下范围排名的用户
   * @note   该请求类与GetSpecialUserTopRankReq请求类的区别在于:
   *         1.本类只可请求一个指定用户以及其上下文用户, 共RANK_GET_TOP_RANK_MAX_COUNT个
   *         2.本请求类对应的响应包中会携带每个用户的私人数据
   * @param ctx 上下文
   * @param rank_key 排行榜Key
   * @param cfg 对应的排行榜配置（ABC榜中rank_key不能高效的反向查找对应配置）
   * @param target_user_zone_id 目标用户zone id(可能跨服)
   * @param target_user_id 目标用户user id
   * @param response 回包
   * @param total_count 排行榜总数
   * @param up_count 指定用户上面uiUpCount个排名的其它用户，0表示不查询该用户上面的排名区间
   * @param down_count 指定用户下面uiUpCount个排名的其它用户，0表示不查询该用户下面的排名区间
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_special_top_rank(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      uint32_t target_user_zone_id, uint64_t target_user_id,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
      uint32_t up_count = 0, uint32_t down_count = 0);

  /**
   * @brief  将玩家冲所有榜单上删除 处罚用户使用 不改变排行榜缓存，调用前请先确定用户所有榜单被封禁
   * @param ctx 上下文
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type clear_user_all_rank(rpc::context &ctx);

  /**
   * @brief  将玩家从指定的榜单上删除 处罚用户使用 不改变排行榜缓存，调用前请先确定用户对应榜单被封禁||缓存被删除
   * @param ctx 上下文
   * @param ban_id 封禁id
   * @return rpc::result_code_type
   */

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type clear_user_one_rank(
      rpc::context &ctx, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rank_cfg);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type clear_user_one_rank(rpc::context &ctx, uint32_t rank_type,
                                                                    uint32_t rank_instance_id);
  /**
   * @brief  将玩家从指定的榜单上删除 处罚用户使用 不改变排行榜缓存，调用前请先确定用户对应榜单被封禁||缓存被删除
   * @param ctx 上下文
   * @param ban_id 封禁id
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type clear_instance_rank(
      rpc::context &ctx, uint32_t rank_type, uint32_t rank_instance_id,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key);
  /**
   * @brief 重置IO任务的保护时间，通常是IO任务正常结束了
   */
  void reset_io_task_protect();

  /***
   * @brief 删除角色的排行榜缓存，此接口只应该在删除角色时调用
   */
  void delete_instance_rank_cache(const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key);

  /***
   * @brief 删除指定的排行榜缓存
   */
  void delete_rank_cache(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rule);

  /***
   * @brief 删除角色的排行榜缓存和排行榜上的数据，此接口只应该在删除角色时调用
   */
  void delete_instance_rank_data(const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_settle_reward(
      rpc::context &ctx, const PROJECT_NAMESPACE_ID::DRankBoardBasicData &rank_basic, int32_t pool_id,
      PROJECT_NAMESPACE_ID::EnRankPeriodRewardType pool_type, bool save_history, time_t cycle_no, bool is_custom,
      time_t deliver_time, int32_t season_id);

  EXPLICIT_NODISCARD_ATTR rpc::result_void_type set_client_rank_cache_expired(rpc::context &ctx);

 private:
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_top_rank(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
      uint32_t from_rank_no, uint32_t rank_count, bool ignore_zero, bool allow_submit_local);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_special_top_rank(
      rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      uint32_t target_user_zone_id, uint64_t target_user_id,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
      uint32_t up_count, uint32_t down_count, bool allow_submit_local);

  struct rank_configure_index {
    uint32_t rank_type;
    uint32_t rank_instance_id;
  };

  struct rank_data_index : public logic_rank_handle_key {
    explicit rank_data_index(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rule) noexcept;

    /**
     * @brief 构建排行榜索引Key，复用数据Key的instance id
     *
     * @param rule
     * @param other 数据Key
     */
    rank_data_index(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rule,
                    const logic_rank_handle_key &other) noexcept;

    explicit rank_data_index(const rank_data_index &other) noexcept;
    explicit rank_data_index(uint32_t rank_type, uint32_t instance_id, uint32_t sub_rank_type,
                             uint32_t sub_instance_id) noexcept;

    rank_data_index(rank_data_index &&other) = delete;

    rank_data_index &operator=(const rank_data_index &other) noexcept;
    rank_data_index &operator=(rank_data_index &&other) = delete;
  };

  struct rank_data_type {
    rank_configure_index index;
    PROJECT_NAMESPACE_ID::DRankUserBoard rank_data;

    // 剪枝优化，减少kLocal模式不必要的赛季计算
    time_t local_mode_next_settlement_timepoint;
  };

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type run_io_task(rpc::context &ctx);

  void check_and_settlement_local_rank_data(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                            const std::shared_ptr<rank_data_type> &randk_data,
                                            const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
                                            time_t now) noexcept;
  std::shared_ptr<rank_data_type> get_rank_data(const rank_data_index &rank_index) const noexcept;
  std::shared_ptr<rank_data_type> mutable_rank_data(const rank_data_index &rank_index,
                                                    const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                    logic_rank_user_extend_span user_extend) noexcept;

  PROJECT_NAMESPACE_ID::DRankInstanceBoard *get_instance_rank_data(
      std::shared_ptr<user_rank_manager::rank_data_type> rank_data,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key);
  PROJECT_NAMESPACE_ID::DRankInstanceBoard *mutable_instance_rank_data(
      std::shared_ptr<user_rank_manager::rank_data_type> rank_data,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key);

  PROJECT_NAMESPACE_ID::DRankUnsubmitData *get_unsubmit_data(const rank_data_index &rank_index,
                                                             uint32_t target_user_zone_id, uint64_t target_user_id);
  PROJECT_NAMESPACE_ID::DRankUnsubmitData *mutable_unsubmit_data(
      const rank_data_index &rank_index, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
      uint32_t target_user_zone_id, uint64_t target_user_id,
      const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key, logic_rank_user_extend_span user_extend);
  void merge_unsubmit_data(const rank_data_index &rank_index, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                           const PROJECT_NAMESPACE_ID::DRankUnsubmitData &unsubmit);

  /**
   * @brief 执行IO提交
   *
   * @param ctx 上下文
   * @param rank_index 排行榜缓存索引Key
   * @param rank_key 排行榜Key(在ABC三榜切换类型的榜中，key可能和index不一样)
   * @param cfg 排行榜配置
   * @param unsubmit 待提交数据
   * @param now 当前时间
   * @return rpc::result_code_type
   */
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type submit(rpc::context &ctx, const rank_data_index &rank_index,
                                                       const logic_rank_handle_key &rank_key,
                                                       const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                       PROJECT_NAMESPACE_ID::DRankUnsubmitData &unsubmit, time_t now);

  /**
   * @brief Patch/设置下一次IO任务时间
   *
   * @note 可以用于设置预期的解锁时间和IO任务保护时间的最小值
   *
   * @param t 预期的时间
   */
  void patch_next_io_task_timepoint(time_t t);

  void update_rank_score_cache(rpc::context &ctx, const rank_data_index &rank_index,
                               const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                               const logic_rank_handle_data &record, time_t now);
  void patch_rank_score_action(rpc::context &ctx, const rank_data_index &rank_index,
                               const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg, rank_data_type &rank_data,
                               const PROJECT_NAMESPACE_ID::user_async_job_update_rank &notify);

  void append_pending_update_by_io_task(const rank_data_index &rank_index,
                                        const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg);
  void append_pending_update_without_action(const rank_data_index &rank_index,
                                            const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                            bool trigger_by_io_task = false);

  bool check_rank_instance_key_invalid(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                       const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key);

 public:
  void append_pending_update(const PROJECT_NAMESPACE_ID::user_async_job_update_rank &rank_job);

  void convert_to(PROJECT_NAMESPACE_ID::DRankUserBasicData &output, const logic_rank_handle_data &input);

 private:
  player *const owner_;
  bool is_dirty_;

  // 保护过于频繁得启动刷新/提交排行榜任务
  time_t io_task_next_timepoint_;

  // 定期重新拉取所有的榜，清理过期榜记录
  time_t next_auto_update_score_timepoint_;

  mutable task_type_trait::task_type io_task_;
  std::list<PROJECT_NAMESPACE_ID::user_async_job_update_rank> pending_update_score_ranks_;
  // 内部流程的更新缓存，用于加速去重
  atfw::memory::stl::unordered_map<rank_data_index, bool, logic_rank_handle_key_hash> pending_update_no_action_index_;

  // 索引只能通过key获得
  atfw::memory::stl::unordered_map<rank_data_index, std::shared_ptr<rank_data_type>, logic_rank_handle_key_hash>
      db_data_;
};

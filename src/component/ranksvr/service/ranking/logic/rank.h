#pragma once

#include <std/smart_ptr.h>

#include <design_pattern/singleton.h>

#include <dispatcher/task_manager.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.struct.pb.h>
#include <protocol/pbdesc/com.struct.rank.pb.h>
#include <protocol/config/com.struct.rank.config.pb.h>
#include <protocol/pbdesc/rank_service.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on
#include <rpc/rpc_shared_message.h>

#include <rpc/rpc_common_types.h>
#include <stdint.h>
#include <utility/persistent_btree.h>
#include <cstddef>
#include <ctime>
#include <deque>
#include <map>
#include <unordered_map>

#include "config/logic_config.h"
#include "config/server_frame_build_feature.h"
#include "dispatcher/task_type_traits.h"
#include "log/log_wrapper.h"
#include "memory/rc_ptr.h"

#include "logic/rank_mirror_manager.h"
#include "logic/rank_wal_handle.h"
#include "rpc/rpc_context.h"

class rank;
using rank_ptr_type = util::memory::strong_rc_ptr<rank>;

class rank : public util::memory::enable_shared_rc_from_this<rank> {
 public:
  using compare_fn_t = bool (*)(const PROJECT_NAMESPACE_ID::rank_sort_data& l,
                                const PROJECT_NAMESPACE_ID::rank_sort_data& r);

  rank(const PROJECT_NAMESPACE_ID::DRankKey& rank_id, uint32_t capacity, compare_fn_t compare_fn, int64_t data_version);
  ~rank();

  int tick();

  void refresh_limit_second(rpc::context& ctx, time_t now_tm);

  void slave_confirm_info(rpc::context& ctx, uint64_t slave_node, int64_t data_version);
  /**
   * @brief 按照排行榜key查询玩家排名信息
   *
   * @param key 排行榜key
   * @param output 返回的数据，排行榜排名等数据
   * @return int32_t
   */
  int32_t query_one_user_by_key(const PROJECT_NAMESPACE_ID::DRankUserKey& key,
                                PROJECT_NAMESPACE_ID::DRankUserBoardData& output);

  int32_t query_one_user_by_score(const PROJECT_NAMESPACE_ID::rank_sort_score& key,
                                  PROJECT_NAMESPACE_ID::rank_data& output);

  int32_t query_one_user_by_rank_no(int32_t rank_no, PROJECT_NAMESPACE_ID::rank_data& output);

  int32_t query_rank_top(uint32_t from, uint32_t count, PROJECT_NAMESPACE_ID::DRankQueryRspData& output);

  int32_t query_rank_user_front_back(const PROJECT_NAMESPACE_ID::DRankUserKey& key, uint32_t count,
                                     PROJECT_NAMESPACE_ID::DRankQueryRspData& output);

  int32_t update_score(const PROJECT_NAMESPACE_ID::rank_storage_data& data);
  int32_t update_score(const PROJECT_NAMESPACE_ID::DRankEventUpdateUserScore& data);

  int32_t modify_score(const PROJECT_NAMESPACE_ID::rank_storage_data& data);

  //   int32_t add_user_score(const PROJECT_NAMESPACE_ID::DRankUserKey& key,
  //                          const PROJECT_NAMESPACE_ID::rank_sort_score& score);

  int32_t del_one_user(const PROJECT_NAMESPACE_ID::DRankUserKey& key);

  int32_t clear_rank();

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type init_rank_from_db(rpc::context& ctx);
  void fetch_rank_data(google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::rank_data>& output);

  inline int64_t get_data_version() { return data_version_; }

  void async_save_rank_data(rpc::context& ctx);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type save_rank_data(rpc::context& ctx);

  bool is_main_node() const;
  bool is_slave_node() const;
  bool is_readable() const;

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type switch_to_main(rpc::context& ctx,
                                                               const PROJECT_NAMESPACE_ID::table_rank_router& db_router,
                                                               int32_t db_router_version);
  void switch_to_slave(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankRouterData& router_data);
  const PROJECT_NAMESPACE_ID::DRankRouterData& get_router_data() const;
  void set_router_data(const PROJECT_NAMESPACE_ID::table_rank_router& db_router, int32_t db_router_version);

  bool is_io_task_running();
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_io_task(rpc::context& ctx);
  static bool is_task_running(task_type_trait::task_type& task);

  const PROJECT_NAMESPACE_ID::DRankKey& get_key() { return key_; }
  bool is_init() const { return is_init_; }

  void broadcast_events(rpc::context& ctx, PROJECT_NAMESPACE_ID::DRankEventLog&& log);

  time_t get_last_save_router_data_time() const { return last_save_router_data_time_; }

  util::memory::strong_rc_ptr<rank_mirror_manager> get_mirror_manager() { return mirror_manager_; }

 private:
  void del_data_from_btree(const PROJECT_NAMESPACE_ID::rank_sort_data& score);
  void insert_data_from_btree(const PROJECT_NAMESPACE_ID::rank_sort_data& score);
  inline int64_t get_next_data_version() { return data_version_; }

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type notify_switch_to_slave(rpc::context& ctx);
  void async_heartbeat(rpc::context& ctx);
  void async_router_lock(rpc::context& ctx);

  void increase_data_version();

  /* 内部或者子对象用
   */
  util::memory::strong_rc_ptr<rank_tree> get_tree() { return btree_; }

 private:
  PROJECT_NAMESPACE_ID::DRankKey key_;
  uint32_t capacity_;
  std::map<PROJECT_NAMESPACE_ID::DRankUserKey, PROJECT_NAMESPACE_ID::rank_storage_data> mp_;
  util::memory::strong_rc_ptr<rank_tree> btree_;
  std::deque<util::memory::strong_rc_ptr<btree_node<PROJECT_NAMESPACE_ID::rank_sort_data>>> history_version_;
  int64_t data_version_;

  PROJECT_NAMESPACE_ID::DRankRouterData router_data_;
  time_t last_save_router_data_time_ = 0;  // 路由表更新时间
  time_t last_heartbeat_time_ = 0;
  bool is_heartbeat_running_ = false;

  task_type_trait::task_type io_task_;
  bool is_init_ = false;

  int64_t next_daily_settlement_id_;
  int64_t next_custom_settlement_id_;
  time_t next_settlement_timepoint_;
  bool is_saving_mirror_;

  mutable task_type_trait::task_type settlement_task_;
  rank_wal_publisher_log_operator::strong_ptr<rank_wal_publisher_type> wal_publisher_ = nullptr;

  util::memory::strong_rc_ptr<rank_mirror_manager> mirror_manager_ = nullptr;
  time_t last_save_time_ = 0;

  friend class rank_mirror_manager;
};

#ifdef _MSC_VER
#  define FWRLOGERROR(RANK, fmt, ...) \
    FWLOGERROR("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), __VA_ARGS__)

#  define FWRLOGTRACE(RANK, fmt, ...) \
    FWLOGTRACE("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), __VA_ARGS__)

#  define FWRLOGDEBUG(RANK, fmt, ...) \
    FWLOGDEBUG("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), __VA_ARGS__)

#  define FWRLOGNOTICE(RANK, fmt, ...) \
    FWLOGNOTICE("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), __VA_ARGS__)

#  define FWRLOGINFO(RANK, fmt, ...) \
    FWLOGINFO("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), __VA_ARGS__)

#  define FWRLOGWARNING(RANK, fmt, ...) \
    FWLOGWARNING("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), __VA_ARGS__)

#  define FWRLOGFATAL(RANK, fmt, ...) \
    FWLOGFATAL("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), __VA_ARGS__)
#else
#  define FWRLOGERROR(RANK, fmt, args...) \
    FWLOGERROR("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), ##args)

#  define FWRLOGTRACE(RANK, fmt, args...) \
    FWLOGTRACE("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), ##args)

#  define FWRLOGDEBUG(RANK, fmt, args...) \
    FWLOGDEBUG("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), ##args)

#  define FWRLOGNOTICE(RANK, fmt, args...) \
    FWLOGNOTICE("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), ##args)

#  define FWRLOGINFO(RANK, fmt, args...) \
    FWLOGINFO("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), ##args)

#  define FWRLOGWARNING(RANK, fmt, args...) \
    FWLOGWARNING("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), ##args)

#  define FWRLOGFATAL(RANK, fmt, args...) \
    FWLOGFATAL("rank ({}:{}) " fmt, (RANK).get_key().rank_type(), (RANK).get_key().rank_instance_id(), ##args)
#endif
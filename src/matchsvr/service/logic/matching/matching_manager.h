// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>
#include <protocol/pbdesc/orbit_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <rpc/rpc_common_types.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "logic/matching/matching_room.h"
#include "logic/matching/matching_unit.h"

namespace rpc {
class context;
}

// 全局匹配管理器。它拥有房间和运行时索引，但不持有任何配置表副本。
class matching_manager : public util::design_pattern::singleton<matching_manager> {
 public:
  // 粗粒度匹配桶索引键。
  struct bucket_key {
    int32_t level_type = 0;
    std::string region;
    std::string battle_version;
    int32_t matching_pool_id = 0;

    bool operator<(const bucket_key& other) const noexcept;
  };

  struct migration_candidate {
    std::vector<uint64_t> unit_ids;
    uint32_t faction_capacity = 0;
    bool complete_faction = false;

    // 按完整 faction 优先、Unit ID 字典序稳定排列迁移原子。
    bool operator<(const migration_candidate& other) const noexcept;
  };

  struct candidate_evaluation {
    bool accepted = false;
    PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation target_result;
  };

  // 可加入房间的完整查找结果；房间与 assignments 始终来自同一个候选评估。
  struct joinable_room_result {
    matching_room::ptr_t room;
    PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation evaluation;
  };

  // 桶内候选按创建时间排序，相同时间再按 ID 保持确定性。
  struct queue_entry {
    int64_t created_time = 0;
    std::string matching_id;

    bool operator<(const queue_entry& other) const noexcept;
    // 按 queue_entry 的同一顺序比较两个有效房间指针。
    static bool room_precedes(const matching_room::ptr_t& left, const matching_room::ptr_t& right) noexcept;
  };

 protected:
  matching_manager();
  ~matching_manager();

 public:
  // 初始化匹配管理器。
  int32_t init();
  // 处理搜索超时和终态房间延迟回收。
  int32_t tick();
  // 清空所有运行时房间和索引，主要用于服务退出与测试隔离。
  void clear();

  // 创建一个新 unit 的匹配，优先补齐同粗桶中的待补 faction，再回退到最老的兼容房间。
  int32_t create_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCreateReq& request,
                          PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response);
  // 取消仍处于搜索阶段的完整 unit。
  int32_t cancel_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCancelReq& request,
                          PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response);
  // 按 matching_id 或活动 unit_id 查询快照。
  int32_t check_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingCheckReq& request,
                         PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response);
  // 接收成员的战斗确认；拒绝时整 Unit 退出，剩余 Unit 回到撮合。
  int32_t confirm_matching(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingConfirmReq& request,
                           PROJECT_NAMESPACE_ID::SSMatchingSnapshot& response);
  int32_t acknowledge_matching_events(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingEventAckReq& request);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type orbit_room_ready(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq& request,
      PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp& response, uint64_t source_server_id);

  // 返回当前处于搜索阶段的玩家总数，用于选择规则组。
  int32_t get_total_matching_user_count() const noexcept;
  // 返回当前保留的房间数，包括短暂保留供查询的终态房间。
  size_t get_room_count() const noexcept;
  // 返回指定房间的 Unit/faction 数量；房间不存在时为 0。
  size_t get_room_unit_count(const std::string& matching_id) const noexcept;
  size_t get_room_faction_count(const std::string& matching_id) const noexcept;

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  // 测试专用：从确认阶段进入等待 Orbit ready 的状态，不发起真实 Orbit RPC。
  bool prepare_battle_creation_for_test(const std::string& matching_id, uint64_t orbit_server_id, int64_t expire_time);
#endif

 private:
  struct user_key {
    uint64_t user_id = 0;
    uint32_t zone_id = 0;

    bool operator==(const user_key& other) const noexcept;
  };

  struct user_key_hash {
    size_t operator()(const user_key& value) const noexcept;
  };

  // 把协议 scope 转为可排序的四维粗桶键。
  static bucket_key make_bucket_key(const PROJECT_NAMESPACE_ID::DMatchingScope& scope);
  // 查找请求指定的房间；matching_id 已过期或不再包含 unit 时回退到活动 unit 索引。
  matching_room::ptr_t find_room(const std::string& matching_id, uint64_t unit_id) const;
  matching_unit::ptr_t find_unit(uint64_t unit_id) const;
  // 记录内部 Room 事件，并把真正的玩家可见变化发布到稳定 Unit 事件流。
  void publish_room_event(rpc::context& ctx, const matching_room::ptr_t& room,
                          PROJECT_NAMESPACE_ID::DMatchingEventLog&& event_log);
  // 在相同粗桶中优先查找能补齐已有 faction 的房间，并返回同次检查产生的 assignments。
  joinable_room_result find_joinable_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                          const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now) const;
  // 固定目标房间，按稳定顺序从同一粗桶内更新的 donor 连续拉取迁移原子，返回实际迁移数。
  size_t rebalance_room(rpc::context& ctx, const matching_room::ptr_t& target_room, int64_t now,
                        size_t max_migration_count);
  // 原子迁移一个或多个 Unit；多个 Unit 只用于保持满员 faction 的成员关系。
  bool move_units(rpc::context& ctx, const matching_room::ptr_t& source_room, const matching_room::ptr_t& target_room,
                  const std::vector<uint64_t>& unit_ids, int64_t now);
  // 建立 unit 和玩家的全局活动索引。
  void index_unit(const std::string& matching_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 删除 unit 和玩家的全局活动索引。
  void unindex_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 把仍可加入的房间插入粗桶。
  void index_room(const matching_room::ptr_t& room);
  // 从粗桶移除房间。
  void unindex_room(const matching_room& room);
  // 释放房间中所有 unit 的活动索引。
  void unindex_all_units(const matching_room& room);
  // 若规则已满足，向 orbitsvr 发起创建房间请求。
  void start_battle(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now);
  // 保留固定容量 faction 并动态选择最终模板，用于创建、迁房、查询和定时推进。
  void evaluate_room(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now);
  // 淘汰确认超时的 Unit，并让仍有效的 Unit 回到撮合。
  void handle_confirm_timeout(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now);
  // 创建战斗或等待 ready 回调超时后释放全部 Unit。
  void handle_battle_create_timeout(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now);

  // 按确定性顺序收集 donor 中可保持 Unit 或完整 faction 原子性的迁移候选。
  std::vector<migration_candidate> collect_migration_candidates(rpc::context& ctx, const matching_room& source_room);
  // 单次校验迁移候选，并返回提交到 target 的完整 faction assignments。
  candidate_evaluation evaluate_candidate(const matching_room::ptr_t& target_room,
                                          const matching_room::ptr_t& source_room, const migration_candidate& candidate,
                                          int32_t current_global_matching_users);

  // matching_id 到房间对象的唯一所有权索引。
  std::unordered_map<std::string, matching_room::ptr_t> rooms_;
  // Unit 生命周期对象独立于房间；迁房只更新弱引用，不移动订阅或 WAL。
  std::unordered_map<uint64_t, matching_unit::ptr_t> units_;
  // Unit WAL 有界重试的轮转游标，避免 Unit 数量超过单 tick 上限时发生饥饿。
  uint64_t wal_retry_cursor_ = 0;
  // 活动 unit_id 到 matching_id 的索引。
  std::unordered_map<uint64_t, std::string> unit_to_room_;
  // 活动玩家到 unit_id 的冲突索引，保证同一 matchsvr 内只能参加一个匹配。
  std::unordered_map<user_key, uint64_t, user_key_hash> user_to_unit_;
  // 四维粗桶以及桶内的老房间优先队列。
  std::map<bucket_key, std::set<queue_entry>> searching_rooms_by_bucket_;
};

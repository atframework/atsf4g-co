// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "logic/matching/matching_room.h"

namespace rpc {
class context;
}

// 全局匹配管理器。它拥有房间和运行时索引，但不持有任何配置表副本。
class matching_manager : public util::design_pattern::singleton<matching_manager> {
 public:
  using battle_start_handler_t =
      std::function<int32_t(const PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot&, std::string&)>;

  // 硬隔离索引键。
  struct bucket_key {
    int32_t level_type = 0;
    std::string region;
    std::string battle_version;
    int32_t matching_pool_id = 0;

    bool operator<(const bucket_key& other) const noexcept;
  };

  // 桶内候选按创建时间排序，相同时间再按 ID 保持确定性。
  struct queue_entry {
    int64_t created_time = 0;
    std::string matching_id;

    bool operator<(const queue_entry& other) const noexcept;
  };

 protected:
  matching_manager();
  ~matching_manager();

 public:
  // 安装默认 battlesvr 桩并准备运行。
  int32_t init();
  // 处理搜索超时和终态房间延迟回收。
  int32_t tick();
  // 清空所有运行时房间和索引，主要用于服务退出与测试隔离。
  void clear();

  // 创建一个新 unit 的匹配，优先加入同硬隔离桶中最老的兼容房间。
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

  // 注入真实 battlesvr 调用；首版默认处理器只生成桩 room_id 并返回成功。
  void set_battle_start_handler(battle_start_handler_t handler);
  // 返回当前处于搜索阶段的玩家总数，用于选择规则组。
  int32_t get_total_matching_user_count() const noexcept;
  // 返回当前保留的房间数，包括短暂保留供查询的终态房间。
  size_t get_room_count() const noexcept;

 private:
  struct user_key {
    uint64_t user_id = 0;
    uint32_t zone_id = 0;

    bool operator==(const user_key& other) const noexcept;
  };

  struct user_key_hash {
    size_t operator()(const user_key& value) const noexcept;
  };

  // 把协议 scope 转为可排序的硬隔离键。
  static bucket_key make_bucket_key(const PROJECT_NAMESPACE_ID::DMatchingScope& scope);
  // 查找请求指定的房间；matching_id 已过期或不再包含 unit 时回退到活动 unit 索引。
  matching_room::ptr_t find_room(const std::string& matching_id, uint64_t unit_id) const;
  // 在相同硬隔离桶中查找一个可容纳 unit 的房间；迁房时只向人数严格更多的房间收敛。
  matching_room::ptr_t find_joinable_room(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                          const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
                                          const matching_room* source_room,
                                          int32_t& result_template_id) const;
  // 将 unit 从源房间迁到目标房间；先加入目标再移除源房间，失败时不改变现有归属。
  bool move_unit(rpc::context& ctx, const matching_room::ptr_t& source_room, const matching_room::ptr_t& target_room,
                 const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now);
  // 建立 unit 和玩家的全局活动索引。
  void index_unit(const std::string& matching_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 删除 unit 和玩家的全局活动索引。
  void unindex_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 把仍可加入的房间插入硬隔离桶。
  void index_room(const matching_room::ptr_t& room);
  // 从硬隔离桶移除房间。
  void unindex_room(const matching_room& room);
  // 释放房间中所有 unit 的活动索引。
  void unindex_all_units(const matching_room& room);
  // 若规则已满足，直接调用 battlesvr 处理器并进入终态。
  void start_battle(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now);
  // 重新计算房间当前模板和成局状态，用于创建、迁房、查询和定时推进。
  void evaluate_room(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now);
  // 淘汰确认超时的 Unit，并让仍有效的 Unit 回到撮合。
  void handle_confirm_timeout(rpc::context& ctx, const matching_room::ptr_t& room, int64_t now);
  // 默认 battlesvr 桩：保留完整请求边界但不依赖尚未实现的 battlesvr。
  static int32_t stub_start_battle(const PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& snapshot,
                                   std::string& battle_room_id);

  // matching_id 到房间对象的唯一所有权索引。
  std::unordered_map<std::string, matching_room::ptr_t> rooms_;
  // 活动 unit_id 到 matching_id 的索引。
  std::unordered_map<uint64_t, std::string> unit_to_room_;
  // 活动玩家到 unit_id 的冲突索引，保证同一 matchsvr 内只能参加一个匹配。
  std::unordered_map<user_key, uint64_t, user_key_hash> user_to_unit_;
  // 四维硬隔离桶以及桶内的老房间优先队列。
  std::map<bucket_key, std::set<queue_entry>> searching_rooms_by_bucket_;
  // battlesvr 请求边界；未来替换桩时无需修改撮合核心。
  battle_start_handler_t battle_start_handler_;
};

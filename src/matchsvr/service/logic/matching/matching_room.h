// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.match.pb.h>
#include <protocol/pbdesc/com.struct.orbit.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "logic/matching/matching_wal_handle.h"

namespace rpc {
class context;
}

// 一次匹配房间。房间只管理不可拆分的组队 unit 和自身状态，不负责选择配置或全局索引。
class matching_room {
 public:
  using ptr_t = std::shared_ptr<matching_room>;

  // 使用已确定的硬隔离范围创建一个空房间。
  matching_room(std::string matching_id, const PROJECT_NAMESPACE_ID::DMatchingScope& scope, int64_t now,
                int64_t expire_time);

  // 返回房间的稳定 ID。
  const std::string& get_matching_id() const noexcept { return matching_id_; }
  // 返回 level/region/version/pool 四维硬隔离范围。
  const PROJECT_NAMESPACE_ID::DMatchingScope& get_scope() const noexcept { return scope_; }
  // 返回当前房间状态。
  PROJECT_NAMESPACE_ID::EnMatchingRoomStatus get_status() const noexcept { return status_; }
  // 返回房间创建时间，候选房间按它从旧到新排序。
  int64_t get_created_time() const noexcept { return created_time_; }
  // 返回搜索超时时间。
  int64_t get_expire_time() const noexcept { return expire_time_; }
  // 返回终态发生时间，用于延迟回收以支持查询。
  int64_t get_terminal_time() const noexcept { return terminal_time_; }
  // 返回确认阶段截止时间；非确认阶段为 0。
  int64_t get_confirm_expire_time() const noexcept { return confirm_expire_time_; }
  // 返回房间 WAL 最近分配的事件 ID。
  int64_t get_last_event_id() const noexcept { return last_event_id_; }
  // 返回当前匹配结果模板 ID。
  int32_t get_result_template_id() const noexcept { return result_template_id_; }
  // 返回当前所有 unit，key 为 unit_id。
  const std::unordered_map<uint64_t, PROJECT_NAMESPACE_ID::DMatchingUnit>& get_units() const noexcept { return units_; }

  PROJECT_NAMESPACE_ID::DOrbitUserInitDataDetail get_orbit_user_init_detail(
      const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const;

  void add_orbit_user_init_detail(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                  const PROJECT_NAMESPACE_ID::DOrbitUserInitDataDetail& detail);

  const PROJECT_NAMESPACE_ID::DOrbitRoomKey& get_orbit_room_key() const noexcept { return orbit_room_key_; }

  // 统计房间内的真实玩家数量。
  size_t get_user_count() const noexcept;
  // 判断 unit 是否仍在本房间。
  bool has_unit(uint64_t unit_id) const noexcept;
  // 判断玩家是否仍在本房间。
  bool has_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const noexcept;

  // 原子加入一个不可拆分 unit；重复 unit 或玩家返回 false。
  bool add_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 从仍在匹配的房间移除 unit；返回是否实际移除。
  bool remove_unit(uint64_t unit_id);
  // 撮合完成后进入战斗确认，并把全部成员重置为待确认。
  void begin_confirmation(int64_t expire_time) noexcept;
  // 更新单个成员的确认选择；只有确认阶段且成员存在时成功。
  bool confirm_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool accepted) noexcept;
  // 判断房间内所有成员是否均已接受。
  bool are_all_users_confirmed() const noexcept;
  // 确认失败移除 Unit 后，让剩余 Unit 回到正常撮合。
  void resume_matching(int64_t expire_time) noexcept;
  // 保存当前规则选中的结果模板。
  void set_result_template_id(int32_t value) noexcept;
  // 有新 unit 加入时延长房间级搜索截止时间，避免迁入老房间后立即超时。
  void extend_expire_time(int64_t value) noexcept;
  // 标记正在请求 battlesvr，之后不再接受新 unit。
  void mark_creating_battle(uint64_t orbit_server_id) noexcept;
  uint64_t get_orbit_server_id() const noexcept;
  // 锁定首次 orbitsvr ready 回调，防止重复初始化玩家。
  bool begin_orbit_ready() noexcept;
  // 标记战斗房间创建完成。
  void mark_finished(int64_t now);
  // 标记战斗请求失败。
  void mark_failed(int32_t result, int64_t now) noexcept;
  // 标记搜索超时。
  void mark_timeout(int64_t now) noexcept;
  // 标记所有 unit 已取消。
  void mark_cancelled(int64_t now) noexcept;
  // 导出不暴露内部容器的协议快照。
  void dump(PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& output) const;

  // 为玩家创建或刷新 WAL 订阅；acknowledge_event_id 用于增量重放。
  bool subscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, uint64_t server_id,
                 int64_t acknowledge_event_id);
  // 主动移除玩家订阅。
  void unsubscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);
  // 读取订阅路由，供迁房时把订阅者转移到目标房间。
  bool get_subscriber_route(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, uint64_t& server_id,
                            int64_t& acknowledge_event_id);
  // 追加并立即广播一条房间 WAL 日志。
  void publish(rpc::context& ctx, PROJECT_NAMESPACE_ID::DMatchingEventLog&& event_log);

 private:
  // 匹配房间的稳定 ID。
  std::string matching_id_;
  // 四维硬隔离范围。
  PROJECT_NAMESPACE_ID::DMatchingScope scope_;
  // 房间当前状态。
  PROJECT_NAMESPACE_ID::EnMatchingRoomStatus status_;
  // unit_id 到完整组队数据的映射。
  std::unordered_map<uint64_t, PROJECT_NAMESPACE_ID::DMatchingUnit> units_;

  // 创建时间，同时作为老房间优先的排序时间。
  int64_t created_time_;
  // 匹配搜索截止时间。
  int64_t expire_time_;
  // 进入终态的时间，非终态时为 0。
  int64_t terminal_time_;
  // 确认阶段截止时间。
  int64_t confirm_expire_time_;
  // 单房间单调递增 WAL 事件 ID。
  int64_t last_event_id_;
  // 当前规则选中的结果模板 ID。
  int32_t result_template_id_;
  // 房间最终业务结果。
  int32_t result_;
  // 是否已经开始处理 orbitsvr ready 回调。
  bool orbit_ready_processing_;
  // 本房间创建请求选中的 orbitsvr，用于校验 ready 回调来源及通知 lobbysvr。
  uint64_t orbit_server_id_;

  // OrbitRoom Key
  PROJECT_NAMESPACE_ID::DOrbitRoomKey orbit_room_key_;

  std::unordered_map<std::string, PROJECT_NAMESPACE_ID::DOrbitUserInitDataDetail> orbit_users_init_detail_;

  // 房间级 WAL publisher，负责日志保留、重放、快照和跨服通知。
  matching_wal_log_operator::strong_ptr<matching_wal_publisher> wal_publisher_;
};

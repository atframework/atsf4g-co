// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <rpc/rpc_common_types.h>
#include <time/jiffies_timer.h>
#include <time/time_utility.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "logic/room/team_room.h"

namespace rpc {
class context;
}

// 组队房间管理器。持有本节点所有队伍房间对象和共享的 jiffies_timer 时间轮。
// 每个房间在时间轮上有且只有一个定时器(见 team_room::get_next_timer_event)，
// 定时器到期驱动房间执行下一个定时 action(剔除无心跳成员、日志压缩、乐观锁续租等)。
class team_room_manager : public util::design_pattern::singleton<team_room_manager> {
 public:
  using room_ptr_t = team_room::ptr_t;
  using timer_set_type = atfw::util::time::jiffies_timer<>;
  // 房间以 DTeamKey(zone_id + team_id) 为 key
  using room_map_t = std::unordered_map<atfw::team::DTeamKey, room_ptr_t, rpc::team::team_api::team_key_hash_t,
                                        rpc::team::team_api::team_key_equal_t>;

 protected:
  team_room_manager();
  ~team_room_manager();

 public:
  int32_t init();
  // 驱动时间轮触发各房间的定时 action，并回收已销毁房间
  int32_t tick(rpc::context& ctx);
  // 清空所有房间，主要用于服务退出与测试隔离
  void clear();

  room_ptr_t get_room(const atfw::team::DTeamKey& team_key) const;
  // 查找或创建队伍房间(创建频道订阅)，来自外部服务的写请求经由此获得房间协调者
  room_ptr_t mutable_room(rpc::context& ctx, const atfw::team::DTeamKey& team_key);
  void remove_room(const atfw::team::DTeamKey& team_key, const team_room* expected = nullptr);

  // 重设房间的唯一定时器(移除旧定时器并按新的触发时间点调度)
  int32_t reset_room_timer(team_room& room, std::chrono::system_clock::time_point timepoint);
  // 移除房间的定时器(房间销毁回收前调用)
  void remove_room_timer(team_room& room);

  // 房间有待发送的成员频道消息时注册进来，一组事件处理完后由 flush_pending_channel_message 统一发送
  void mark_room_pending_flush(team_room& room);
  // 房间队列已被自身 flush 清空后注销，避免注册表遗留无意义记录
  void unmark_room_pending_flush(team_room& room);
  // 仅对注册过的(有变化的)房间发送待发的成员频道消息，发送后清空注册表
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type flush_pending_channel_message(rpc::context& ctx);

  size_t get_room_count() const noexcept;

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  // 测试钩子: 是否仍有房间维护任务在执行。
  bool debug_has_running_maintenance_task() const noexcept;
  // 测试钩子: 重建时间轮。jiffies_timer 的 init 对已初始化实例是 no-op，last_tick_ 只前进不后退；
  // 使用时间偏移的用例会把它推到未来(后续用例的定时器被钳制到 last_tick_+1 而永不触发)，
  // 因此每个测试环境启动时重建时间轮以隔离
  void reset_timer_wheel_for_test() {
    timer_set_ = timer_set_type{};
    timer_set_.init(static_cast<time_t>(
        std::chrono::duration_cast<std::chrono::seconds>(atfw::util::time::time_utility::now().time_since_epoch())
            .count()));
    timer_running_ = true;
  }
#endif

 private:
  room_map_t rooms_;
  // 有待发送成员频道消息的房间注册表(避免 flush 时全量扫描所有房间)
  room_map_t pending_flush_rooms_;
  timer_set_type timer_set_;
  bool timer_running_ = false;
};

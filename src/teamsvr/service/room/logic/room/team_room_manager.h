// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <rpc/rpc_common_types.h>
#include <time/jiffies_timer.h>

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

 protected:
  team_room_manager();
  ~team_room_manager();

 public:
  int32_t init();
  // 驱动时间轮触发各房间的定时 action，并回收已销毁房间
  int32_t tick(rpc::context& ctx);
  // 清空所有房间，主要用于服务退出与测试隔离
  void clear();

  room_ptr_t get_room(int64_t team_id) const;
  // 查找或创建队伍房间(创建频道订阅)，来自外部服务的写请求经由此获得房间协调者
  room_ptr_t mutable_room(rpc::context& ctx, int64_t team_id);
  void remove_room(int64_t team_id);

  // 重设房间的唯一定时器(移除旧定时器并按新的触发时间点调度)
  int32_t reset_room_timer(team_room& room, std::chrono::system_clock::time_point timepoint);
  // 移除房间的定时器(房间销毁回收前调用)
  void remove_room_timer(team_room& room);

  size_t get_room_count() const noexcept;

 private:
  std::unordered_map<int64_t, room_ptr_t> rooms_;
  timer_set_type timer_set_;
  bool timer_running_ = false;
};

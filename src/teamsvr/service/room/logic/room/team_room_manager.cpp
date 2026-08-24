// Copyright 2026 atframework

#include "logic/room/team_room_manager.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <string/string_format.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <logic/logic_server_setup.h>
#include <memory/object_allocator.h>
#include <rpc/rpc_context.h>

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <utility>

namespace {
// 房间定时 action 使用秒级精度时间轮
static inline time_t team_room_get_timer_tick(std::chrono::system_clock::time_point timepoint) {
  return static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(timepoint.time_since_epoch()).count());
}

static inline time_t team_room_now_timer_tick() {
  return team_room_get_timer_tick(atfw::util::time::time_utility::now());
}
}  // namespace

team_room_manager::team_room_manager() = default;

team_room_manager::~team_room_manager() { clear(); }

int32_t team_room_manager::init() {
  timer_set_.init(team_room_now_timer_tick());
  timer_running_ = true;
  return 0;
}

int32_t team_room_manager::tick(rpc::context& ctx) {
  int32_t ret = 0;

  // 驱动时间轮，触发各房间到期的定时 action
  if (timer_running_) {
    timer_set_.set_private_data(reinterpret_cast<void*>(&ctx));
    int res = timer_set_.tick(team_room_now_timer_tick());
    timer_set_.set_private_data(nullptr);
    if (res < 0) {
      FCTXLOGERROR(ctx, "team_room_manager tick timer error: {}", res);
    } else {
      ret += res;
    }
  }

  return ret;
}

void team_room_manager::clear() {
  room_map_t rooms;
  rooms.swap(rooms_);
  pending_flush_rooms_.clear();
  for (auto& pair : rooms) {
    if (pair.second) {
      remove_room_timer(*pair.second);
      pair.second->on_remove();
    }
  }
}

team_room_manager::room_ptr_t team_room_manager::get_room(const atfw::team::DTeamKey& team_key) const {
  auto iter = rooms_.find(team_key);
  if (iter == rooms_.end()) {
    return nullptr;
  }
  return iter->second;
}

team_room_manager::room_ptr_t team_room_manager::mutable_room(rpc::context& ctx, const atfw::team::DTeamKey& team_key) {
  // zone_id 为 0 是合法的不分区队伍;team_id 为 0 不是有效队伍标识
  if (0 == team_key.team_id()) {
    FCTXLOGERROR(ctx, "team_room_manager reject invalid team key {}:{}", team_key.zone_id(), team_key.team_id());
    return nullptr;
  }

  auto exist = get_room(team_key);
  if (exist) {
    return exist;
  }

  auto room = team_room::create(ctx, team_key);
  if (!room) {
    FWLOGERROR("team_room_manager alloc room for team {}:{} failed", team_key.zone_id(), team_key.team_id());
    return nullptr;
  }

  rooms_[team_key] = room;

  // 初始定时器就是GC定时器，后续由房间自己维护
  FCTXLOGINFO(ctx, "team_room_manager create room for team {}:{} success", team_key.zone_id(), team_key.team_id());
  return room;
}

void team_room_manager::remove_room(const atfw::team::DTeamKey& team_key, const team_room* expected) {
  auto iter = rooms_.find(team_key);
  if (iter == rooms_.end() || (expected != nullptr && iter->second.get() != expected)) {
    return;
  }
  auto room = iter->second;
  if (room) {
    remove_room_timer(*room);
    room->on_remove();
  }

  rooms_.erase(iter);
  pending_flush_rooms_.erase(team_key);
}

int32_t team_room_manager::reset_room_timer(team_room& room, std::chrono::system_clock::time_point timepoint) {
  if (!timer_running_) {
    timer_set_.init(team_room_now_timer_tick());
    timer_running_ = true;
  }

  if (room.timer_timeout_ != std::chrono::system_clock::from_time_t(0) && room.timer_timeout_ <= timepoint) {
    // 定时器只能提前，不能延后，避免定时器被延迟到过期事件之后才触发
    return 0;
  }

  remove_room_timer(room);
  room.timer_timeout_ = std::chrono::system_clock::from_time_t(0);

  // 检查如果房间已被移除，则不需要再附加定时器
  auto iter = rooms_.find(room.get_team_key());
  if (iter == rooms_.end() || iter->second.get() != &room) {
    return 0;
  }

  time_t timeout_tick = (std::max)(team_room_get_timer_tick(timepoint), team_room_now_timer_tick());
  if (timeout_tick <= timer_set_.get_last_tick()) {
    timeout_tick = timer_set_.get_last_tick() + 1;
  }

  atfw::util::memory::weak_rc_ptr<team_room> room_weak = room.shared_from_this();
  atfw::team::DTeamKey team_key = room.get_team_key();
  auto fn = [room_weak, team_key](time_t /*tick_time*/, const timer_set_type::timer_t& /*timer*/) {
    if (room_weak.expired()) {
      return;
    }
    auto room = room_weak.lock();
    if (!room) {
      return;
    }
    room->timer_timeout_ = std::chrono::system_clock::from_time_t(0);

    if (team_room_manager::is_instance_destroyed()) {
      return;
    }

    // 房间可能已被回收(定时器未来得及移除)，校验 manager 仍持有该房间
    if (team_room_manager::me()->get_room(team_key).get() != room.get()) {
      return;
    }

    auto* ctx = reinterpret_cast<rpc::context*>(team_room_manager::me()->timer_set_.get_private_data());
    if (nullptr != ctx) {
      room->on_timer(*ctx);
    } else {
      room->on_timer(logic_server_get_current_tick_context());
    }
  };

  int res =
      timer_set_.add_timer(timeout_tick - timer_set_.get_last_tick(), std::move(fn), nullptr, &room.timer_watcher_);
  if (res < 0) {
    FWLOGERROR("team_room_manager add timer for team {}:{} failed: {}", team_key.zone_id(), team_key.team_id(), res);
    room.timer_watcher_.reset();
  } else {
    room.timer_timeout_ = timepoint;
  }
  return res;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void team_room_manager::remove_room_timer(team_room& room) {
  if (room.timer_watcher_.expired()) {
    return;
  }
  auto timer_ptr = room.timer_watcher_.lock();
  room.timer_watcher_.reset();
  if (timer_ptr) {
    timer_set_type::remove_timer(*timer_ptr);
  }
}

size_t team_room_manager::get_room_count() const noexcept { return rooms_.size(); }

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
bool team_room_manager::debug_has_running_maintenance_task() const noexcept {
  for (const auto& room_pair : rooms_) {
    if (room_pair.second && room_pair.second->debug_maintenance_task_running()) {
      return true;
    }
  }
  return false;
}
#endif

void team_room_manager::mark_room_pending_flush(team_room& room) {
  // 房间可能已被回收，仅注册 manager 仍持有的房间
  auto iter = rooms_.find(room.get_team_key());
  if (iter == rooms_.end() || iter->second.get() != &room) {
    return;
  }
  pending_flush_rooms_[room.get_team_key()] = iter->second;
}

void team_room_manager::unmark_room_pending_flush(team_room& room) {
  auto iter = pending_flush_rooms_.find(room.get_team_key());
  if (iter != pending_flush_rooms_.end() && iter->second.get() == &room) {
    pending_flush_rooms_.erase(iter);
  }
}

rpc::result_code_type team_room_manager::flush_pending_channel_message(rpc::context& ctx) {
  if (pending_flush_rooms_.empty()) {
    RPC_RETURN_CODE(0);
  }

  room_map_t pending_rooms;
  pending_rooms.swap(pending_flush_rooms_);
  for (auto& pair : pending_rooms) {
    if (pair.second) {
      RPC_AWAIT_IGNORE_RESULT(pair.second->flush_pending_channel_message(ctx));
    }
  }
  RPC_RETURN_CODE(0);
}

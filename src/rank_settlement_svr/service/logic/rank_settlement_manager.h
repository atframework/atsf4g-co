#pragma once

#include <design_pattern/singleton.h>

#include <dispatcher/task_manager.h>

#include <stdint.h>
#include <cstddef>
#include <unordered_map>

class rank_settlement_manager : public util::design_pattern::singleton<rank_settlement_manager> {
 protected:
  rank_settlement_manager();

 public:
  int init();

  int tick();

  inline void stop() { is_exiting_ = true; }

  inline bool is_exiting() const { return is_exiting_; }

  bool is_update_task_running() const;

  /**
   * @brief 尝试开始执行更新排行榜奖励记录的任务，如果已经有一个任务正在运行中了，会直接返回
   * @return
   * 启动新任务返回true，如果有正在运行的异步任务或者保护时间间隔未到而导致放弃执行则返回false
   */
  bool try_update();

  /**
   * @brief 重置远程命令任务的定时间隔
   */
  void reset_update_protect(
      std::chrono::system_clock::time_point next_update_timepoint = std::chrono::system_clock::from_time_t(0));
  inline std::chrono::system_clock::time_point get_next_update_timepoint() const { return next_update_timepoint_; }

 private:
  friend class task_action_rank_update_settlement;

  bool is_exiting_;
  std::chrono::system_clock::time_point next_update_timepoint_;
  mutable task_type_trait::task_type update_task_;

};

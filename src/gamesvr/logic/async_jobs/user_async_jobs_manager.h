// Copyright 2021 atframework
// Created by owent on 2018-05-01.
//

#pragma once

#include <memory/lru_map.h>
#include <memory/rc_ptr.h>

#include <memory/object_atfw_memory_lru_map.h>
#include <memory/object_stl_unordered_map.h>

#include <dispatcher/task_manager.h>

#include <rpc/rpc_async_invoke.h>

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hello {
class table_user;
}

namespace rpc {
class context;
}

PROJECT_NAMESPACE_BEGIN
class table_user_async_jobs_blob_data;
PROJECT_NAMESPACE_END

class player;

class user_async_jobs_manager {
 public:
  using async_job_ptr_type = util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data>;

 public:
  explicit user_async_jobs_manager(player& owner);
  ~user_async_jobs_manager();

  // 创建默认角色数据
  void create_init(rpc::context& ctx, uint32_t version_type);

  // 登入读取用户数据
  void login_init(rpc::context& ctx);

  // 刷新功能限制次数
  void refresh_feature_limit(rpc::context& ctx);

  // 从table数据初始化
  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& player_table);

  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& user) const;

  bool is_dirty() const;

  void clear_dirty();

 public:
  bool is_async_jobs_task_running() const;

  /**
   * @brief 尝试开始执行远程命令，如果已经有一个命令正在运行中了，会等待那个命令完成后再启动一次
   * @note 远程命令一般用于多写入方，利用数据库插入命令。然后再通知玩家对象，单点执行读入数据。
   * @note 比如说多个玩家对一个玩家发送消息或邮件，可以插入消息或邮件的command到数据库，然后这里拉取后append到玩家数据里
   * @return 启动新任务返回true，如果有正在运行的异步任务或者保护时间间隔未到而导致放弃执行则返回false
   */
  bool try_async_jobs(rpc::context& ctx);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type wait_for_async_task(rpc::context& ctx);

  void force_async_job(int32_t jobs_type);

  /**
   * @brief 重置远程命令任务的定时间隔
   */
  void reset_async_jobs_protect();

  void clear_job_uuids(int32_t job_type);
  void add_job_uuid(int32_t job_type, const std::string& uuid);
  bool is_job_uuid_exists(int32_t job_type, const std::string& uuid);

  void add_retry_job(int32_t job_type, const async_job_ptr_type& job_data);
  void remove_retry_job(int32_t job_type, const std::string& uuid);
  std::vector<async_job_ptr_type> get_retry_jobs(int32_t job_type) const;

 private:
  friend class task_action_player_remote_patch_jobs;

 private:
  player* const owner_;

  mutable task_type_trait::task_type remote_command_patch_task_;

  bool is_dirty_;
  time_t remote_command_patch_task_next_timepoint_;

  struct history_item {
    int64_t timeout = 3;
  };
  using history_map_type =
      atfw::memory::util::lru_map_st<std::string, history_item, std::hash<std::string>, std::equal_to<std::string>>;
  atfw::memory::stl::unordered_map<int32_t, history_map_type> history_uuids_;
  std::unordered_set<int32_t> force_async_job_type_;

  std::unordered_map<int32_t, std::unordered_map<std::string, async_job_ptr_type>> retry_jobs_;
};

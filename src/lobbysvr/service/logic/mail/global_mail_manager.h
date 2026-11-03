// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#pragma once

#pragma once

#include <design_pattern/singleton.h>

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

#include <memory/object_stl_unordered_map.h>

#include <dispatcher/task_manager.h>

#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "logic/mail/mail_defs.h"

namespace atframework {
namespace mail {
class table_global_mail_blob_data;
}  // namespace mail
}  // namespace atframework

class global_mail_manager : public atfw::util::design_pattern::singleton<global_mail_manager> {
 public:
  using global_mail_box_t = atfw::memory::stl::unordered_map<int64_t, std::pair<uint32_t, mail_data_ptr_t> >;
  using global_mail_box_by_type_t =
      atfw::memory::stl::unordered_map<uint32_t, std::unordered_map<int32_t, mail_box_ptr_t> >;
  using global_mail_record_map_t =
      atfw::memory::stl::unordered_map<int64_t, std::pair<uint32_t, PROJECT_NAMESPACE_ID::DMailRecord> >;

 protected:
  global_mail_manager();

 public:
  int tick(rpc::context& ctx);

  bool is_async_task_running();

  void try_to_start_async_jobs(rpc::context& ctx);

  /**
   * @brief 重置远程命令任务的定时间隔
   */
  void reset_async_jobs_protect(rpc::context& ctx);

  /**
   * @brief 重置远程命令任务的定时间隔
   * @return 是否数据库需要回写
   */
  bool update_from_db(rpc::context& ctx, uint32_t zone_id, int32_t major_type,
                      atfw::mail::table_global_mail_blob_data& blob_data, bool rewrite_db_data);

  /**
   * @brief 检查邮件是否可以从merge历史记录里移除
   * @param record 邮件记录
   * @note 如果检测到全服邮件已删除，并且删除时间已经过了误差容忍时间，则可以从历史记录里移除
   * @return 可以移除则返回true
   */
  static bool is_history_removeable(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record);

  /**
   * @brief 检查邮件是否可以从邮箱里移除
   * @param record 邮件记录
   * @note 如果检测到全服邮件已删除，则可以从邮箱里移除
   * @return 可以移除则返回true
   */
  static bool is_record_removeable(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record);

  static void update_global_mail_record(rpc::context& ctx, PROJECT_NAMESPACE_ID::DMailRecord& dst,
                                        const PROJECT_NAMESPACE_ID::DMailRecord& src);

  int add_global_mail(rpc::context& ctx, uint32_t zone_id, const PROJECT_NAMESPACE_ID::DMailRecord& mail);

  bool update_global_mail(rpc::context& ctx, uint32_t zone_id, const PROJECT_NAMESPACE_ID::DMailRecord& mail);

  mail_data_ptr_t get_mail_raw(int64_t mail_id) const;
  mail_box_const_ptr_t get_mail_box_by_type(uint32_t zone_id, int32_t major_type) const;
  const global_mail_box_t& get_all_global_mails() const { return mail_box_id_index_; }

  void resolve_limits();

 private:
  mail_box_ptr_t internal_get_mail_box_by_type(uint32_t zone_id, int32_t major_type);
  void refresh_mail_box_future_cache(rpc::context& ctx, mail_box_t&);
  void compact_mails(rpc::context& ctx, mail_box_t&);
  void remove_global_mail(rpc::context& ctx, int64_t mail_id);
  void fetch_all_unloaded_mails(rpc::context& ctx, std::vector<int64_t>& out);
  void set_mail_content_loaded(int64_t mail_id);

 private:
  struct global_mail_box_type_index_cleanup_guard;
  atfw::util::memory::strong_rc_ptr<global_mail_box_type_index_cleanup_guard> make_mail_box_type_index_cleanup_guard();

 private:
  task_type_trait::task_type mail_async_task_;
  std::chrono::system_clock::time_point task_next_timepoint_;
  std::chrono::system_clock::time_point last_success_fetch_timepoint_;
  bool force_users_reload_;

  global_mail_box_t mail_box_id_index_;
  global_mail_box_by_type_t mail_box_type_index_;
  atfw::util::memory::weak_rc_ptr<global_mail_box_type_index_cleanup_guard> mail_box_type_index_ref_;
  global_mail_record_map_t pending_to_remove_;
  mail_index_t mail_unloaded_index_;
  std::unordered_set<int64_t> pending_to_remove_contents_;

  friend class task_action_global_mail_async_jobs;
};

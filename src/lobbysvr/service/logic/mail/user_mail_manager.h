// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#pragma once

#include <memory/rc_ptr.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <dispatcher/task_manager.h>

#include <rpc/rpc_common_types.h>

#include <logic/mail/mail_utils.h>

#include <ItemAlgorithm/ItemContainerTypes.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data/user_type_define.h"
#include "logic/mail/mail_defs.h"

PROJECT_NAMESPACE_BEGIN
class table_user;
PROJECT_NAMESPACE_END

class user;

class user_mail_manager {
 public:
  explicit user_mail_manager(user& owner);
  ~user_mail_manager();

  // 创建默认角色数据
  void create_init(rpc::context& ctx);

  // 登入读取用户数据
  void login_init(rpc::context& ctx);

  // 刷新功能限制次数
  void refresh_feature_limit_second(rpc::context& ctx);

  // 从table数据初始化
  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);

  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& user);

  bool is_dirty() const;

  void clear_dirty();

  bool is_async_task_running() const;

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type wait_for_async_task(rpc::context& ctx);

  void reset_global_mails_cache();

 public:
  mail_data_ptr_t get_mail_raw(int64_t mail_id) const;
  atfw::util::memory::strong_rc_ptr<mail_box_t> get_mail_box_by_major_type(int32_t major_type) const;

  int add_mail(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& mail,
               const PROJECT_NAMESPACE_ID::DMailContent* ATFW_UTIL_MACRO_NULLABLE content = nullptr);
  int add_global_mail(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& mail,
                      const PROJECT_NAMESPACE_ID::DMailContent& content);
  int remove_mail(rpc::context& ctx, int64_t mail_id,
                  PROJECT_NAMESPACE_ID::DMailOperationResult* ATFW_UTIL_MACRO_NULLABLE out = nullptr);
  int read_mail(rpc::context& ctx, int64_t mail_id,
                PROJECT_NAMESPACE_ID::DMailOperationResult* ATFW_UTIL_MACRO_NULLABLE out = nullptr,
                bool need_remove = true);
  int read_all(rpc::context& ctx, int32_t major_type, int32_t minor_type,
               ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMailOperationResult>*
                   ATFW_UTIL_MACRO_NULLABLE out = nullptr,
               bool need_remove = true);
  rpc::result_code_type receive_mail_attachments(
      rpc::context& ctx, int64_t mail_id,
      PROJECT_NAMESPACE_ID::DMailOperationResult* ATFW_UTIL_MACRO_NULLABLE out = nullptr, bool need_remove = true);
  rpc::result_code_type receive_all(rpc::context& ctx, int32_t major_type, int32_t minor_type,
                                    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMailOperationResult>*
                                        ATFW_UTIL_MACRO_NULLABLE out = nullptr,
                                    bool need_remove = true);

  void pack_mail_user(PROJECT_NAMESPACE_ID::DMailUserInfo& user_info) const;

  int add_mail_with_template(rpc::context& ctx, int32_t mail_template_id,
                             const PROJECT_NAMESPACE_ID::item_algorithm::ItemOperationSource& reason);

  static bool is_mail_shown(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailContent& content);

  static bool is_mail_shown(rpc::context& ctx, const mail_data_ptr_t& mail);

  void send_all_syn_msg(rpc::context& ctx);

  void clear_dirty_cache();

  PROJECT_NAMESPACE_ID::DMailRecord& mutable_dirty_mail(const PROJECT_NAMESPACE_ID::DMailRecord& record,
                                                        bool is_new = false);

  void remove_expired_mails(rpc::context& ctx);

  void fetch_all_user_mail_ids(std::vector<int64_t>& out) const;

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_reward_by_mail(
      rpc::context& ctx, int64_t mail_id, int32_t mail_template_id, std::chrono::system_clock::time_point delivery_time,
      std::chrono::system_clock::time_point expired_time, PROJECT_NAMESPACE_ID::EnItemChgMajorType main_type,
      const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemScopeOffset>& reward_items,
      const mail_render_extension_type* ATFW_UTIL_MACRO_NULLABLE extension) const;

 private:
  void refresh_mail_box_future_cache(rpc::context& ctx, mail_box_t&);
  void compact_mails(rpc::context& ctx);
  void compact_mails(rpc::context& ctx, mail_box_t&);
  void scan_and_remove_expired_mails(rpc::context& ctx);
  void remove_user_mail(rpc::context& ctx, int64_t mail_id);
  bool need_start_async_jobs();
  void try_to_start_async_jobs(rpc::context& ctx);
  rpc::result_code_type receive_mail_attachments(rpc::context& ctx, mail_data_t& mail, bool oss_need_remove);

  // ==================== APIs just for task_action_mail_async_jobs ====================
 private:
  void fetch_all_unloaded_mails(rpc::context& ctx, std::vector<int64_t>& out);
  void set_mail_content_loaded(int64_t mail_id);
  int merge_global_mails(rpc::context& ctx);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  bool is_dirty_;
  bool is_global_mails_merged_;
  size_t lazy_save_counter_;
  mail_index_t mail_box_id_index_;
  std::unordered_map<int32_t, atfw::util::memory::strong_rc_ptr<mail_box_t> > mail_box_type_index_;
  mail_index_t mail_box_unloaded_index_;

  mail_record_map_t pending_to_remove_;
  mail_record_map_t reveived_global_mails_;

  mutable task_type_trait::task_type mail_async_task_;
  std::chrono::system_clock::time_point mail_async_task_protect_timepoint_;
  friend class task_action_mail_async_jobs;

  struct cache_t {
    mail_record_map_t dirty_mails;
    std::unordered_set<int64_t> new_mails;
  };
  cache_t dirty_cache_;
};

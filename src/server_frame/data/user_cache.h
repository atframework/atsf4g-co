// Copyright 2021 atframework

#pragma once

#include <config/compiler_features.h>
#include <design_pattern/noncopyable.h>

#include <gsl/select-gsl.h>
#include <std/explicit_declare.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <utility/protobuf_mini_dumper.h>

#include <log/log_wrapper.h>

#include <config/server_frame_build_feature.h>

#include <memory>
#include <string>

#include "dispatcher/task_type_traits.h"
#include "rpc/rpc_common_types.h"

#include <logic/task_lock.h>

namespace rpc {
class context;
}

class session;
class task_action_cs_req_base;

/**
 * @brief 用户数据包装，自动标记写脏
 * @note 能够隐式转换到只读类型，手动使用get或ref函数提取数据会视为即将写脏
 */
template <typename Ty>
class ATFW_UTIL_SYMBOL_VISIBLE user_cache_dirty_wrapper {
 public:
  using value_type = Ty;

  ATFW_UTIL_FORCEINLINE user_cache_dirty_wrapper() : dirty_(false) {}

  ATFW_UTIL_FORCEINLINE bool is_dirty() const { return dirty_; }

  ATFW_UTIL_FORCEINLINE void mark_dirty() { dirty_ = true; }

  ATFW_UTIL_FORCEINLINE void clear_dirty() { dirty_ = false; }

  ATFW_UTIL_FORCEINLINE const value_type *operator->() const noexcept { return &real_data_; }

  ATFW_UTIL_FORCEINLINE operator const value_type &() const noexcept { return real_data_; }

  ATFW_UTIL_FORCEINLINE const value_type &operator*() const noexcept { return real_data_; }

  ATFW_UTIL_FORCEINLINE const value_type *get() const { return &real_data_; }

  ATFW_UTIL_FORCEINLINE value_type *get() {
    mark_dirty();
    return &real_data_;
  }

  ATFW_UTIL_FORCEINLINE const value_type &ref() const { return real_data_; }

  ATFW_UTIL_FORCEINLINE value_type &ref() {
    mark_dirty();
    return real_data_;
  }

 private:
  value_type real_data_;
  bool dirty_;
};

class user_cache;

class ATFW_UTIL_SYMBOL_VISIBLE initialization_task_lock_guard {
 public:
  SERVER_FRAME_API ~initialization_task_lock_guard();
  SERVER_FRAME_API initialization_task_lock_guard(std::shared_ptr<user_cache> user_inst,
                                                  task_type_trait::id_type task_id) noexcept;

  SERVER_FRAME_API initialization_task_lock_guard(initialization_task_lock_guard &&) noexcept;
  SERVER_FRAME_API initialization_task_lock_guard &operator=(initialization_task_lock_guard &&) noexcept;

  SERVER_FRAME_API bool has_value() const noexcept;

 private:
  initialization_task_lock_guard(const initialization_task_lock_guard &) = delete;
  initialization_task_lock_guard &operator=(const initialization_task_lock_guard &) = delete;

 private:
  std::shared_ptr<user_cache> guard_;
};

class ATFW_UTIL_SYMBOL_VISIBLE user_cache : public std::enable_shared_from_this<user_cache> {
 public:
  using ptr_t = std::shared_ptr<user_cache>;
  friend class user_manager;

 protected:
  struct ATFW_UTIL_SYMBOL_VISIBLE fake_constructor {};

 public:
  SERVER_FRAME_API explicit user_cache(fake_constructor &);
  SERVER_FRAME_API virtual ~user_cache();

  SERVER_FRAME_API virtual bool can_be_writable() const;

  SERVER_FRAME_API virtual bool is_writable() const;

  // 初始化，默认数据
  SERVER_FRAME_API virtual void init(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  SERVER_FRAME_API static ptr_t create(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  // 创建默认角色数据
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type create_init(rpc::context &ctx);

  // 登入读取用户数据
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type login_init(rpc::context &ctx);

  // 是否脏（有数据变更）
  SERVER_FRAME_API virtual bool is_dirty() const;

  // 清理脏（有数据变更）数据标记
  SERVER_FRAME_API virtual void clear_dirty();

  // 刷新功能限制次数
  SERVER_FRAME_API virtual void refresh_feature_limit(rpc::context &ctx);

  // GM操作
  SERVER_FRAME_API virtual bool gm_init();

  // 是否GM操作
  SERVER_FRAME_API virtual bool is_gm() const;

  // 登入事件
  SERVER_FRAME_API virtual void on_login(rpc::context &ctx);

  // 登出事件
  SERVER_FRAME_API virtual void on_logout(rpc::context &ctx);

  // 完成保存事件
  SERVER_FRAME_API virtual void on_saved(rpc::context &ctx);

  // 更新session事件
  SERVER_FRAME_API virtual void on_update_session(rpc::context &ctx, const std::shared_ptr<session> &from,
                                                  const std::shared_ptr<session> &to);

  // 从table数据初始化
  SERVER_FRAME_API virtual void init_from_table_data(rpc::context &ctx,
                                                     const PROJECT_NAMESPACE_ID::table_user &stTableuser_cache);

  /**
   * @brief 转储数据
   * @param user_inst 转储目标
   * @param always 是否忽略脏数据
   * @return 0或错误码
   */
  SERVER_FRAME_API virtual int dump(rpc::context &ctx, PROJECT_NAMESPACE_ID::table_user &user_inst, bool always);

  ATFW_UTIL_FORCEINLINE void dump_user_key(PROJECT_NAMESPACE_ID::DUserIDKey &user_key) const noexcept {
    user_key.set_zone_id(zone_id_);
    user_key.set_user_id(user_id_);
  }

  /**
   * @brief 下发同步消息
   */
  SERVER_FRAME_API virtual void send_all_syn_msg(rpc::context &ctx);

  /**
   * @brief 等待登出前需要结算完的任务
   */
  SERVER_FRAME_API virtual rpc::result_code_type await_before_logout_tasks(rpc::context &ctx);

  SERVER_FRAME_API virtual int32_t client_rpc_filter(rpc::context &ctx, task_action_cs_req_base &cs_task_action,
                                                     const atframework::DispatcherOptions *dispatcher_options);

  /**
   * @brief 监视关联的Session
   * @param session_ptr 关联的Session
   */
  SERVER_FRAME_API void set_session(rpc::context &ctx, std::shared_ptr<session> session_ptr);

  /**
   * @brief 获取关联的Session
   * @return 关联的Session
   */
  SERVER_FRAME_API std::shared_ptr<session> get_session();

  SERVER_FRAME_API bool has_session() const;

  ATFW_UTIL_FORCEINLINE const std::string &get_open_id() const { return openid_id_; }
  ATFW_UTIL_FORCEINLINE uint64_t get_user_id() const { return user_id_; }
  ATFW_UTIL_FORCEINLINE unsigned long long get_user_id_llu() const {
    return static_cast<unsigned long long>(get_user_id());
  }

  ATFW_UTIL_FORCEINLINE bool is(const PROJECT_NAMESPACE_ID::DUserIDKey &user_key) const noexcept {
    return get_user_id() == user_key.user_id() && get_zone_id() == user_key.zone_id();
  }

  ATFW_UTIL_FORCEINLINE uint64_t get_user_cas_version() const { return user_cas_version_; }
  ATFW_UTIL_FORCEINLINE uint64_t &get_user_cas_version() { return user_cas_version_; }
  ATFW_UTIL_FORCEINLINE void set_user_cas_version(uint64_t version) { user_cas_version_ = version; }

  /**
   * @brief 获取大区号
   */
  ATFW_UTIL_FORCEINLINE uint32_t get_zone_id() const { return zone_id_; }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::table_login_lock &get_login_lock() const { return login_lock_; }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::table_login_lock &get_login_lock() { return login_lock_; }
  SERVER_FRAME_API void load_and_move_login_lock(PROJECT_NAMESPACE_ID::table_login_lock &&lg, uint64_t ver);

  ATFW_UTIL_FORCEINLINE uint64_t get_login_lock_cas_version() const { return login_lock_version_; }
  ATFW_UTIL_FORCEINLINE uint64_t &get_login_lock_cas_version() { return login_lock_version_; }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::account_information &get_account_info() const {
    return account_info_;
  }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::account_information &get_account_info() { return account_info_.ref(); }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::user_login_data &get_login_info() const { return login_info_; }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::user_login_data &get_login_info() { return login_info_.ref(); }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::user_data &get_user_data() const { return user_data_; }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::user_data &get_user_data() { return user_data_.ref(); }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::user_option_public_data &get_user_option_public_data() const {
    return user_option_public_data_;
  }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::user_option_public_data &get_user_option_public_data() {
    return user_option_public_data_.ref();
  }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::user_option_private_data &get_user_option_private_data() const {
    return user_option_private_data_;
  }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::user_option_private_data &get_user_option_private_data() {
    return user_option_private_data_.ref();
  }

  ATFW_UTIL_FORCEINLINE bool has_create_init() const { return create_init_; }

  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type wait_task_lock(rpc::context &ctx);
  SERVER_FRAME_API void task_lock_init_task(uint64_t task_id);
  SERVER_FRAME_API void task_lock_remove_task(uint64_t task_id);

  SERVER_FRAME_API bool is_new_user() const;

  ATFW_UTIL_FORCEINLINE uint64_t get_data_version() const { return data_version_; }

  ATFW_UTIL_FORCEINLINE std::unordered_map<const char *, std::deque<int64_t>> &get_protocol_frequency_limit() {
    return protocol_frequency_limit_;
  }

  SERVER_FRAME_API uint64_t alloc_server_sequence();

  SERVER_FRAME_API void set_quick_save() const;

  SERVER_FRAME_API bool has_initialization_task_id() const noexcept;
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type await_initialization_task(rpc::context &ctx);

 private:
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::user_data &mutable_user_data() { return user_data_.ref(); }

 protected:
  ATFW_UTIL_FORCEINLINE void set_data_version(uint32_t ver) { data_version_ = ver; }

 private:
  friend class initialization_task_lock_guard;

  std::string openid_id_;
  uint64_t user_id_;
  uint32_t zone_id_;

  PROJECT_NAMESPACE_ID::table_login_lock login_lock_;
  uint64_t login_lock_version_;
  uint64_t user_cas_version_;
  bool create_init_;

  std::weak_ptr<session> session_;

  std::shared_ptr<task_lock> task_lock_;
  task_type_trait::id_type initialization_task_id_;

  user_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::user_login_data> login_info_;
  user_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::account_information> account_info_;
  user_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::user_data> user_data_;
  user_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::user_option_public_data> user_option_public_data_;
  user_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::user_option_private_data> user_option_private_data_;

  uint64_t server_sequence_;
  uint64_t data_version_;

  std::unordered_map<const char *, std::deque<int64_t>> protocol_frequency_limit_;
};

ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_BEGIN
template <class CharT>
struct ATFW_UTIL_SYMBOL_VISIBLE formatter<user_cache, CharT> : formatter<basic_string_view<CharT>, CharT> {
  template <class FormatContext>
  auto format(const user_cache &user_inst, FormatContext &ctx) const {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "user {}({}:{})", user_inst.get_open_id(), user_inst.get_zone_id(),
                                       user_inst.get_user_id());
  }
};
ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_END

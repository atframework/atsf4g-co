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

#include <bitset>
#include <memory>
#include <string>

#include "data/player_key_hash_helper.h"
#include "dispatcher/task_type_traits.h"
#include "rpc/rpc_common_types.h"

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
class ATFW_UTIL_SYMBOL_VISIBLE player_cache_dirty_wrapper {
 public:
  using value_type = Ty;

  ATFW_UTIL_FORCEINLINE player_cache_dirty_wrapper() : dirty_(false) {}

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

class player_cache;

class ATFW_UTIL_SYMBOL_VISIBLE initialization_task_lock_guard {
 public:
  SERVER_FRAME_API ~initialization_task_lock_guard();
  SERVER_FRAME_API initialization_task_lock_guard(std::shared_ptr<player_cache> user,
                                                  task_type_trait::id_type task_id) noexcept;

  SERVER_FRAME_API initialization_task_lock_guard(initialization_task_lock_guard &&) noexcept;
  SERVER_FRAME_API initialization_task_lock_guard &operator=(initialization_task_lock_guard &&) noexcept;

  SERVER_FRAME_API bool has_value() const noexcept;

 private:
  initialization_task_lock_guard(const initialization_task_lock_guard &) = delete;
  initialization_task_lock_guard &operator=(const initialization_task_lock_guard &) = delete;

 private:
  std::shared_ptr<player_cache> guard_;
};

class ATFW_UTIL_SYMBOL_VISIBLE player_cache : public std::enable_shared_from_this<player_cache> {
 public:
  using ptr_t = std::shared_ptr<player_cache>;
  friend class player_manager;

 protected:
  struct ATFW_UTIL_SYMBOL_VISIBLE fake_constructor {};

 public:
  SERVER_FRAME_API explicit player_cache(fake_constructor &);
  SERVER_FRAME_API virtual ~player_cache();

  SERVER_FRAME_API virtual bool can_be_writable() const;

  SERVER_FRAME_API virtual bool is_writable() const;

  // 初始化，默认数据
  SERVER_FRAME_API virtual void init(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  SERVER_FRAME_API static ptr_t create(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  // 创建默认角色数据
  SERVER_FRAME_API virtual void create_init(rpc::context &ctx);

  // 登入读取用户数据
  SERVER_FRAME_API virtual void login_init(rpc::context &ctx);

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
                                                     const PROJECT_NAMESPACE_ID::table_user &stTableplayer_cache);

  /**
   * @brief 转储数据
   * @param user 转储目标
   * @param always 是否忽略脏数据
   * @return 0或错误码
   */
  SERVER_FRAME_API virtual int dump(rpc::context &ctx, PROJECT_NAMESPACE_ID::table_user &user, bool always);

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

  ATFW_UTIL_FORCEINLINE uint64_t get_user_cas_version() const { return user_cas_version_; }
  ATFW_UTIL_FORCEINLINE uint64_t& get_user_cas_version() { return user_cas_version_; }
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

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::user_login_data &get_login_info() const {
    return login_info_;
  }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::user_login_data &get_login_info() { return login_info_.ref(); }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::user_data &get_player_data() const { return player_data_; }

  ATFW_UTIL_FORCEINLINE bool has_create_init() const {
    return create_init_;
  }

  SERVER_FRAME_API bool is_new_user() const;

  ATFW_UTIL_FORCEINLINE uint64_t get_data_version() const { return data_version_; }

  SERVER_FRAME_API uint64_t alloc_server_sequence();

  SERVER_FRAME_API void set_quick_save() const;

  SERVER_FRAME_API bool has_initialization_task_id() const noexcept;
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type await_initialization_task(rpc::context &ctx);

 private:
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::user_data &mutable_player_data() { return player_data_.ref(); }

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

  task_type_trait::id_type initialization_task_id_;

  player_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::user_login_data> login_info_;
  player_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::account_information> account_info_;
  player_cache_dirty_wrapper<PROJECT_NAMESPACE_ID::user_data> player_data_;
  uint64_t server_sequence_;
  uint64_t data_version_;
};

// 玩家日志输出工具
#ifdef _MSC_VER
#  define FWPLOGTRACE(PLAYER, fmt, ...)                                                                         \
    FWLOGTRACE("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
               __VA_ARGS__)
#  define FWPLOGDEBUG(PLAYER, fmt, ...)                                                                         \
    FWLOGDEBUG("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
               __VA_ARGS__)
#  define FWPLOGNOTICE(PLAYER, fmt, ...)                                                                         \
    FWLOGNOTICE("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
                __VA_ARGS__)
#  define FWPLOGINFO(PLAYER, fmt, ...)                                                                         \
    FWLOGINFO("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
              __VA_ARGS__)
#  define FWPLOGWARNING(PLAYER, fmt, ...)                                                                         \
    FWLOGWARNING("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
                 __VA_ARGS__)
#  define FWPLOGERROR(PLAYER, fmt, ...)                                                                         \
    FWLOGERROR("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
               __VA_ARGS__)
#  define FWPLOGFATAL(PLAYER, fmt, ...)                                                                         \
    FWLOGFATAL("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
               __VA_ARGS__)

#else
#  define FWPLOGTRACE(PLAYER, fmt, args...) \
    FWLOGTRACE("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), ##args)
#  define FWPLOGDEBUG(PLAYER, fmt, args...) \
    FWLOGDEBUG("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), ##args)
#  define FWPLOGNOTICE(PLAYER, fmt, args...) \
    FWLOGNOTICE("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), ##args)
#  define FWPLOGINFO(PLAYER, fmt, args...) \
    FWLOGINFO("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), ##args)
#  define FWPLOGWARNING(PLAYER, fmt, args...)                                                                     \
    FWLOGWARNING("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), \
                 ##args)
#  define FWPLOGERROR(PLAYER, fmt, args...) \
    FWLOGERROR("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), ##args)
#  define FWPLOGFATAL(PLAYER, fmt, args...) \
    FWLOGFATAL("player {}({}:{}) " fmt, (PLAYER).get_open_id(), (PLAYER).get_zone_id(), (PLAYER).get_user_id(), ##args)
#endif

ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_BEGIN
template <class CharT>
struct ATFW_UTIL_SYMBOL_VISIBLE formatter<player_cache, CharT> : formatter<basic_string_view<CharT>, CharT> {
  template <class FormatContext>
  auto format(const player_cache &user, FormatContext &ctx) const {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "player {}({}:{})", user.get_open_id(), user.get_zone_id(),
                                       user.get_user_id());
  }
};
ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_END

// Copyright 2021 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/compiler_features.h>
#include <design_pattern/noncopyable.h>

#include <utility/protobuf_mini_dumper.h>

#include <log/log_wrapper.h>

#include <config/server_frame_build_feature.h>

#include <bitset>
#include <memory>
#include <string>

#include "data/player_key_hash_helper.h"

class session;

/**
 * @brief 用户数据包装，自动标记写脏
 * @note 能够隐式转换到只读类型，手动使用get或ref函数提取数据会视为即将写脏
 */
template <typename Ty>
class player_cache_dirty_wrapper {
 public:
  using value_type = Ty;

  player_cache_dirty_wrapper() : dirty_(false) {}

  inline bool is_dirty() const { return dirty_; }

  inline void mark_dirty() { dirty_ = true; }

  inline void clear_dirty() { dirty_ = false; }

  const value_type *operator->() const noexcept { return &real_data_; }

  operator const value_type &() const noexcept { return real_data_; }

  const value_type &operator*() const noexcept { return real_data_; }

  const value_type *get() const { return &real_data_; }

  value_type *get() {
    mark_dirty();
    return &real_data_;
  }

  const value_type &ref() const { return real_data_; }

  value_type &ref() {
    mark_dirty();
    return real_data_;
  }

 private:
  value_type real_data_;
  bool dirty_;
};

class player_cache : public std::enable_shared_from_this<player_cache> {
 public:
  using ptr_t = std::shared_ptr<player_cache>;
  friend class player_manager;

 protected:
  struct fake_constructor {};

 public:
  player_cache(fake_constructor &);
  virtual ~player_cache();

  virtual bool can_be_writable() const;

  virtual bool is_writable() const;

  // 初始化，默认数据
  virtual void init(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  static ptr_t create(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  // 创建默认角色数据
  virtual void create_init(uint32_t version_type);

  // 登入读取用户数据
  virtual void login_init();

  // 是否脏（有数据变更）
  virtual bool is_dirty() const;

  // 清理脏（有数据变更）数据标记
  virtual void clear_dirty();

  // 刷新功能限制次数
  virtual void refresh_feature_limit();

  // GM操作
  virtual bool gm_init();

  // 是否GM操作
  virtual bool is_gm() const;

  // 登入事件
  virtual void on_login();

  // 登出事件
  virtual void on_logout();

  // 完成保存事件
  virtual void on_saved();

  // 更新session事件
  virtual void on_update_session(const std::shared_ptr<session> &from, const std::shared_ptr<session> &to);

  // 从table数据初始化
  virtual void init_from_table_data(const PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user &stTableplayer_cache);

  /**
   * @brief 转储数据
   * @param user 转储目标
   * @param always 是否忽略脏数据
   * @return 0或错误码
   */
  virtual int dump(PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user &user, bool always);

  /**
   * @brief 下发同步消息
   */
  virtual void send_all_syn_msg();

  /**
   * @brief 等待登出前需要结算完的任务
   */
  virtual int await_before_logout_tasks();

  /**
   * @brief 监视关联的Session
   * @param session_ptr 关联的Session
   */
  void set_session(std::shared_ptr<session> session_ptr);

  /**
   * @brief 获取关联的Session
   * @return 关联的Session
   */
  std::shared_ptr<session> get_session();

  bool has_session() const;

  inline const std::string &get_open_id() const { return openid_id_; };
  inline uint64_t get_user_id() const { return user_id_; };
  inline unsigned long long get_user_id_llu() const { return static_cast<unsigned long long>(get_user_id()); };

  const std::string &get_version() const { return version_; };
  std::string &get_version() { return version_; };
  void set_version(const std::string &version) { version_ = version; };

  /**
   * @brief 获取大区号
   */
  inline uint32_t get_zone_id() const { return zone_id_; }

  inline const PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login &get_login_info() const { return login_info_; }
  inline PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login &get_login_info() { return login_info_; }
  void load_and_move_login_info(PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login &&lg, const std::string &ver);

  inline const std::string &get_login_version() const { return login_info_version_; }
  inline std::string &get_login_version() { return login_info_version_; }

  inline const PROJECT_SERVER_FRAME_NAMESPACE_ID::account_information &get_account_info() const {
    return account_info_;
  }
  inline PROJECT_SERVER_FRAME_NAMESPACE_ID::account_information &get_account_info() { return account_info_.ref(); }

  inline const PROJECT_SERVER_FRAME_NAMESPACE_ID::player_options &get_player_options() const { return player_options_; }
  inline PROJECT_SERVER_FRAME_NAMESPACE_ID::player_options &get_player_options() { return player_options_.ref(); }

  inline const PROJECT_SERVER_FRAME_NAMESPACE_ID::player_data &get_player_data() const { return player_data_; }

  inline uint32_t get_data_version() const { return data_version_; }

  uint64_t alloc_server_sequence();

  void set_quick_save() const;

 private:
  inline PROJECT_SERVER_FRAME_NAMESPACE_ID::player_data &mutable_player_data() { return player_data_.ref(); }

 protected:
  inline void set_data_version(uint32_t ver) { data_version_ = ver; }

 private:
  std::string openid_id_;
  uint64_t user_id_;
  uint32_t zone_id_;
  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login login_info_;
  std::string login_info_version_;

  std::string version_;
  uint32_t data_version_;

  std::weak_ptr<session> session_;
  uint64_t server_sequence_;

  player_cache_dirty_wrapper<PROJECT_SERVER_FRAME_NAMESPACE_ID::account_information> account_info_;
  player_cache_dirty_wrapper<PROJECT_SERVER_FRAME_NAMESPACE_ID::player_data> player_data_;
  player_cache_dirty_wrapper<PROJECT_SERVER_FRAME_NAMESPACE_ID::player_options> player_options_;
};

// 玩家日志输出工具
#ifdef _MSC_VER
#  define WPLOGTRACE(PLAYER, fmt, ...)                                                           \
    WLOGTRACE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), __VA_ARGS__)
#  define WPLOGDEBUG(PLAYER, fmt, ...)                                                           \
    WLOGDEBUG("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), __VA_ARGS__)
#  define WPLOGNOTICE(PLAYER, fmt, ...)                                                           \
    WLOGNOTICE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
               (PLAYER).get_user_id_llu(), __VA_ARGS__)
#  define WPLOGINFO(PLAYER, fmt, ...)                                                           \
    WLOGINFO("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
             (PLAYER).get_user_id_llu(), __VA_ARGS__)
#  define WPLOGWARNING(PLAYER, fmt, ...)                                                           \
    WLOGWARNING("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
                (PLAYER).get_user_id_llu(), __VA_ARGS__)
#  define WPLOGERROR(PLAYER, fmt, ...)                                                           \
    WLOGERROR("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), __VA_ARGS__)
#  define WPLOGFATAL(PLAYER, fmt, ...)                                                           \
    WLOGFATAL("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), __VA_ARGS__)

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
#  define WPLOGTRACE(PLAYER, fmt, args...)                                                       \
    WLOGTRACE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), ##args)
#  define WPLOGDEBUG(PLAYER, fmt, args...)                                                       \
    WLOGDEBUG("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), ##args)
#  define WPLOGNOTICE(PLAYER, fmt, args...)                                                       \
    WLOGNOTICE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
               (PLAYER).get_user_id_llu(), ##args)
#  define WPLOGINFO(PLAYER, fmt, args...)                                                       \
    WLOGINFO("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
             (PLAYER).get_user_id_llu(), ##args)
#  define WPLOGWARNING(PLAYER, fmt, args...)                                                       \
    WLOGWARNING("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
                (PLAYER).get_user_id_llu(), ##args)
#  define WPLOGERROR(PLAYER, fmt, args...)                                                       \
    WLOGERROR("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), ##args)
#  define WPLOGFATAL(PLAYER, fmt, args...)                                                       \
    WLOGFATAL("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), \
              (PLAYER).get_user_id_llu(), ##args)

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

namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID {
template <class T>
struct formatter<T, typename std::enable_if<std::is_base_of<player_cache, T>::value, char>::type>
    : formatter<std::string> {
  template <class FormatContext>
  auto format(const player_cache &user, FormatContext &ctx) {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "player {}({}:{})", user.get_open_id(), user.get_zone_id(),
                                       user.get_user_id_llu());
  }
};
}  // namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID

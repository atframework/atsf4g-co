// Copyright 2021 atframework

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <config/server_frame_build_feature.h>

#include <data/player_cache.h>

#include <dispatcher/task_type_defines.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define REG_PLAYER_MGR_PTR_DEF(mgr)                       \
 private:                                                 \
  std::unique_ptr<mgr> mgr##_;                            \
                                                          \
 public:                                                  \
  inline const mgr &get_##mgr() const { return *mgr##_; } \
  inline mgr &get_##mgr() { return *mgr##_; }

class user_async_jobs_manager;

/**
 * @brief 用户数据缓存包装，析构时自动还原
 * @note 注意只能用作局部变量
 */
template <typename Ty>
class player_cache_ptr_holder : public util::design_pattern::noncopyable {
 public:
  using value_type = Ty;
  using pointer_type = value_type *;

  explicit player_cache_ptr_holder(pointer_type &holded, pointer_type ptr_addr) : ptr_addr_(nullptr) {
    if (nullptr != holded || nullptr == ptr_addr) {
      return;
    }

    ptr_addr_ = &holded;
    holded = ptr_addr;
  }

  ~player_cache_ptr_holder() noexcept {
    if (nullptr != ptr_addr_) {
      *ptr_addr_ = nullptr;
    }
  }

  bool available() const noexcept { return nullptr != ptr_addr_ && nullptr != *ptr_addr_; }

 protected:
  pointer_type *ptr_addr_;
};

class player : public player_cache {
 private:
  static constexpr const uint32_t PLAYER_DATA_LOGIC_VERSION = 1;
  struct inner_flag {
    enum type {
      EN_IFT_FEATURE_INVALID = 0,
      EN_IFT_IS_INITED,                  // 是否已初始化
      EN_IFT_NEED_PATCH_REMOTE_COMMAND,  // 是否需要启动远程命令任务
      EN_IFT_MAX
    };
  };

 public:
  using base_type = player_cache;
  using ptr_t = std::shared_ptr<player>;
  friend class task_action_player_remote_patch_jobs;

  struct heartbeat_t {
    time_t last_recv_time;        // 上一次收到心跳包时间
    size_t continue_error_times;  // 连续错误次数
    size_t sum_error_times;       // 总错误次数
  };
  /** 因为会对其进行memset，所以内部不允许出现非POD类型 **/

  struct dirty_message_container {
    std::unique_ptr<PROJECT_SERVER_FRAME_NAMESPACE_ID::SCPlayerDirtyChgSync> player_dirty;
  };

  using build_dirty_message_fn_t = std::function<void(player &, dirty_message_container &)>;
  using clear_dirty_cache_fn_t = std::function<void(player &)>;
  struct dirty_sync_handle_t {
    build_dirty_message_fn_t build_fn;
    clear_dirty_cache_fn_t clear_fn;
  };

  struct cache_t {
    time_t refresh_feature_limit_second;
    time_t refresh_feature_limit_minute;
    time_t refresh_feature_limit_hour;
    // PROJECT_SERVER_FRAME_NAMESPACE_ID::SCPlayerLevelupSyn player_level_up_syn;

    std::unordered_map<int32_t, PROJECT_SERVER_FRAME_NAMESPACE_ID::DItem> dirty_item_by_type;

    std::unordered_map<uintptr_t, dirty_sync_handle_t> dirty_handles;
  };

  struct task_queue_node {
    task_types::task_ptr_type related_task;
    bool is_waiting;

    explicit task_queue_node(const task_types::task_ptr_type &t);
  };

  /**
   * @brief 用于玩家的某些任务的排队机制。
   *        如果是正常流程，所有创建这个lock_guard的任务排队执行
   *        如果是异常流程，超时或被kill的任务不保证顺序，仍然有效的任务还是会按顺序执行
   */
  class task_queue_lock_guard {
   public:
    explicit task_queue_lock_guard(player &user);
    ~task_queue_lock_guard();

    inline operator bool() const { return false == is_exiting(); }
    bool is_exiting() const;

    UTIL_DESIGN_PATTERN_NOCOPYABLE(task_queue_lock_guard)
    UTIL_DESIGN_PATTERN_NOMOVABLE(task_queue_lock_guard)

   private:
    player *lock_target_;
    bool is_exiting_;
    std::list<task_queue_node>::iterator queue_iter_;
  };

 public:
  ptr_t shared_from_this() { return std::static_pointer_cast<player>(base_type::shared_from_this()); }

 public:
  explicit player(fake_constructor &);
  virtual ~player();

  bool can_be_writable() const override;

  bool is_writable() const override;

  // 初始化，默认数据
  void init(uint64_t user_id, uint32_t zone_id, const std::string &openid) override;

  static ptr_t create(uint64_t user_id, uint32_t zone_id, const std::string &openid);

  // 创建默认角色数据
  void create_init(uint32_t version_type) override;

  // 登入读取用户数据
  void login_init() override;

  bool is_dirty() const override;

  // 清理脏（有数据变更）数据标记
  void clear_dirty() override;

  // 刷新功能限制次数
  void refresh_feature_limit() override;

  // 登入事件
  void on_login() override;

  // 登出事件
  void on_logout() override;

  // 保存完毕事件
  void on_saved() override;

  // 更新session事件
  void on_update_session(const std::shared_ptr<session> &from, const std::shared_ptr<session> &to) override;

  // 从table数据初始化
  void init_from_table_data(const PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user &stTableplayer_cache) override;

  /**
   * @brief 转储数据
   * @param user 转储目标
   * @param always 是否忽略脏数据
   * @return 0或错误码
   */
  int dump(PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user &user, bool always) override;

  /**
   * @brief 是否完整执行过初始化
   * @note 如果完整执行了登入流程，则会走完整初始化流程。这个flag还有一个含义是玩家数据仅仅在此进程内可写。
   *       比如如果一个玩家对象是缓存，则不会走完整的登入流程，也不会被完全初始化，那么这个数据就是只读的。
   *        这时候如果登出或者移除玩家对象的时候清理就不能写数据库。
   */
  bool is_inited() const { return inner_flags_.test(inner_flag::EN_IFT_IS_INITED); }
  /**
   * @brief 标记为完全初始化，也表示在此进程中玩家数据是可写的。
   * @note 这个flag用于标记玩家实时数据必须最多唯一存在于一个进程中，其他进程的数据都是缓存。
   *       缓存可以升级为实时数据，但是不能降级。如果需要降级，则直接移除玩家对象，下一次需要的时候重新拉取缓存
   */
  void set_inited() { inner_flags_.set(inner_flag::EN_IFT_IS_INITED, true); }

  const PROJECT_SERVER_FRAME_NAMESPACE_ID::DClientDeviceInfo &get_client_info() const { return client_info_; }
  void set_client_info(const PROJECT_SERVER_FRAME_NAMESPACE_ID::DClientDeviceInfo &info) {
    client_info_.CopyFrom(info);
  }

  /**
   * @brief 获取心跳包统计数据
   * @return 心跳包统计数据
   */
  inline const heartbeat_t &get_heartbeat_data() const { return heartbeat_data_; }

  /**
   * @brief 更新心跳包统计数据
   */
  void update_heartbeat();

  /**
   * @brief 获取缓存信息
   * @return 缓存信息
   */
  inline const cache_t &get_cache_data() const { return cache_data_; }

  /**
   * @brief 获取缓存信息
   * @return 缓存信息
   */
  inline cache_t &get_cache_data() { return cache_data_; }

  /**
   * @brief 下发同步消息
   */
  void send_all_syn_msg() override;
  int await_before_logout_tasks() override;
  void clear_dirty_cache();

  PROJECT_SERVER_FRAME_NAMESPACE_ID::DItem &mutable_dirty_item(const PROJECT_SERVER_FRAME_NAMESPACE_ID::DItem &in);

  /**
   * @brief 插入脏数据handle
   *
   * @param key handle的key。可以指向关联数据的地址
   * @param create_handle_fn handle结构，仅在不存在时执行插入，已存在则忽略
   * @note
   * 所有回调函数请尽可能小，保证整个闭包在3个指针以内（成员函数占2个指针）。这样std::function会使用小对象优化
   */
  void insert_dirty_handle_if_not_exists(uintptr_t key, dirty_sync_handle_t (*create_handle_fn)(player &));

  /**
   * @brief 插入脏数据handle
   *
   * @param key handle的key。可以指向关联数据的地址
   * @param build_fn 构建脏数据时的回调函数，仅在不存在时执行插入，已存在则忽略
   * @param clear_fn 清空脏数据时的对调函数，仅在不存在时执行插入，已存在则忽略
   * @note
   * 所有回调函数请尽可能小，保证整个闭包在3个指针以内（成员函数占2个指针）。这样std::function会使用小对象优化
   */
  void insert_dirty_handle_if_not_exists(uintptr_t key, build_dirty_message_fn_t build_fn,
                                         clear_dirty_cache_fn_t clear_fn);

 private:
  mutable std::bitset<inner_flag::EN_IFT_MAX> inner_flags_;

  friend class task_queue_lock_guard;
  std::list<task_queue_node> task_lock_queue_;

  PROJECT_SERVER_FRAME_NAMESPACE_ID::DClientDeviceInfo client_info_;
  // =======================================================
  heartbeat_t heartbeat_data_;
  cache_t cache_data_;
  // -------------------------------------------------------

  REG_PLAYER_MGR_PTR_DEF(user_async_jobs_manager)
};

// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>
#include <config/compiler_features.h>

#include <log/log_wrapper.h>

#include <time/jiffies_timer.h>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>
#include <nostd/nullability.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/any.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_common_types.h>

#include <dispatcher/task_type_traits.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "data/mq_channel_wal_handle.h"

class mq_channel_manager;

using mq_channel_timer_type = atfw::util::time::jiffies_timer<>;

PROJECT_NAMESPACE_BEGIN

class table_dtmq_channel_record;

PROJECT_NAMESPACE_END

namespace rpc {
class context;
}

class mq_channel : public atfw::util::memory::enable_shared_rc_from_this<mq_channel> {
 public:
  class mq_channel_accessor {
   private:
    static void update_timer(mq_channel& mq_channel, rpc::context& ctx, bool force = false);
    static void remove_timer(mq_channel& mq_channel);
    friend class mq_channel_manager;
  };

  enum class channel_status : uint8_t {
    kNone = 0,
    kReadonly = 1,
    kWritable = 2,
  };

  struct replicate_index_set {
    uint64_t prefer_replicate_index = 0;
    std::unordered_set<uint64_t> index_set;
  };

  ATFW_UTIL_DESIGN_PATTERN_NOCOPYABLE(mq_channel)
  ATFW_UTIL_DESIGN_PATTERN_NOMOVABLE(mq_channel)

 public:
  explicit mq_channel(mq_channel_manager& manager, const atfw::dtmq::DChannelIdKey& channel_key,
                      const atfw::dtmq::DChannelConfigure& configure);
  ~mq_channel();

  void load(const atfw::dtmq::DChannelMetadata& metadata, const atfw::dtmq::DChannelRuntime& runtime);
  void load(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_dtmq_channel_record& record);
  void dump(atfw::dtmq::DChannelMetadata& metadata, bool with_configure, bool with_custom_data) const;
  void dump(atfw::dtmq::DChannelRuntime& runtime, bool with_private_data) const;
  void dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_dtmq_channel_record& record) const;
  void dump(atfw::dtmq::DChannelSnapshot& snapshot, bool with_configure, bool with_custom_data,
            bool with_private_data) const;
  void reload_configure(const atfw::dtmq::DChannelConfigure& config);

  inline const std::string& get_channel_id() const noexcept { return channel_key_.channel_id(); }
  inline const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept { return channel_key_; }
  inline const atfw::dtmq::DChannelConfigure& get_configure() const noexcept { return configure_; }

  inline const google::protobuf::Any& get_custom_data() const noexcept { return custom_data_; }
  inline int64_t get_custom_data_sequence() const noexcept { return custom_data_sequence_; }
  void set_custom_data(const google::protobuf::Any& custom_data) noexcept;
  void clear_custom_data() noexcept;

  inline const google::protobuf::Any& get_private_data() const noexcept { return private_data_; }
  inline int64_t get_private_data_sequence() const noexcept { return private_data_sequence_; }
  void set_private_data(const google::protobuf::Any& private_data) noexcept;
  void clear_private_data() noexcept;

  inline const atfw::dtmq::DChannelOptimisticLock& get_lock() const noexcept { return lock_; }
  inline int64_t get_compact_stateful_sequence() const noexcept { return compact_stateful_sequence_; }

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type writable_init(rpc::context& ctx);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type readonly_init(rpc::context& ctx, uint64_t readonly_server_index);

  void merge_subscriber(
      rpc::context& ctx,
      const ::google::protobuf::RepeatedPtrField<::atframework::dtmq::channel_subscriber>& subscribers);

  void load_snapshot(rpc::context& ctx, atfw::dtmq::channel_snapshot&&);

  void dump_snapshot(rpc::context& ctx, atfw::dtmq::channel_snapshot&);

  inline bool is_dirty() const noexcept { return is_dirty_; }

  inline bool is_readonly() const noexcept { return status_ == channel_status::kReadonly; }

  inline bool is_writable() const noexcept { return status_ == channel_status::kWritable; }

  inline atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<mq_channel_wal_object_type>>
  get_shared_wal_object() {
    return shared_wal_object_;
  }

  inline mq_channel_wal_publisher_type& get_wal_publisher() noexcept { return *wal_publisher_; }
  inline const mq_channel_wal_publisher_type& get_wal_publisher() const noexcept { return *wal_publisher_; }

  inline atfw::util::memory::strong_rc_ptr<mq_channel_wal_client_type> get_wal_client() { return wal_client_; }

  static bool should_be_writable_or_get_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                  uint64_t& writable_server_id, mq_channel* channel = nullptr) noexcept;
  bool should_be_writable() noexcept;

  static bool should_be_readonly_or_get_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                  uint64_t& readonly_server_id, uint64_t readonly_replicate_index,
                                                  mq_channel* channel = nullptr) noexcept;

  static bool should_be_readonly_or_random_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                     uint64_t& readonly_replicate_index, uint64_t& readonly_server_id,
                                                     mq_channel* channel = nullptr) noexcept;

  bool should_be_readonly(const replicate_index_set * ATFW_UTIL_MACRO_NULLABLE & readonly_replicate_index_set) noexcept;

  /**
   * @brief Get the target distribution server id
   *
   * @param replicate_index 0表示writable，>0表示readonly副本序号
   * @return uint64_t server_id
   */
  uint64_t get_target_distribution_server_id(uint64_t replicate_index) const noexcept;

  /**
   * @brief Get the target distribution replicate index
   *
   * @param server_id 服务器ID
   * @return replicate_index_set writable或未找到返回nullptr，否则返回改服务节点ID对应的副本索引集合
   */
  const replicate_index_set* ATFW_UTIL_MACRO_NULLABLE
  get_target_distribution_replicate_index(uint64_t server_id) const noexcept;

  static uint64_t calculate_transfer_target_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                      uint64_t replicate_index) noexcept;

  uint64_t get_transfer_target_server_id() const noexcept;

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_transfer(rpc::context& ctx, uint64_t& transfer_to_server_id);

  void force_refresh_distribution();

  bool need_save_db() const noexcept;

  bool is_io_task_running() const noexcept;
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_io_task(rpc::context& ctx);

  void async_start_transfer(rpc::context& ctx, uint64_t target_server_id);

  void async_save(rpc::context& ctx);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type save(rpc::context& ctx);

  void async_destroy(rpc::context& ctx, std::chrono::system_clock::time_point writable_remove_timepoint =
                                            std::chrono::system_clock::from_time_t(0));
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type destroy(
      rpc::context& ctx,
      std::chrono::system_clock::time_point writable_remove_timepoint = std::chrono::system_clock::from_time_t(0));

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type load_from_db(rpc::context& ctx);

  int32_t async_send_subscribe_to_writable(rpc::context& ctx);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_send_subscribe_to_writable(rpc::context& ctx);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_subscribe_to_writable(rpc::context& ctx);
  void set_destroy_message(rpc::context& ctx);

  int32_t subscribe(rpc::context& ctx, const atfw::dtmq::channel_subscriber& subscriber_info,
                    int64_t last_received_sequence, size_t last_received_hash_code, bool merge_mode);
  int32_t unsubscribe(rpc::context& ctx, const std::string& subscriber_key);

  int64_t alloc_message_sequence() noexcept;

  int64_t get_last_message_sequence() const noexcept;

  uint64_t get_last_hash_code() const noexcept;

  uint64_t get_client_last_hash_code() const noexcept;

  uint64_t get_main_ready_writable_server_id();

  int tick(rpc::context& ctx);

  inline bool is_loading_snapshot() const noexcept { return is_loading_snapshot_; }

  void update_lost_last_subscriber() noexcept;
  void update_last_writable_notify_time() noexcept;
  void reset_lost_last_subscriber() noexcept;

  void set_dirty() noexcept;

  void set_lock(rpc::context& ctx, const atfw::dtmq::DChannelOptimisticLock& lock, bool append_log = true);
  void clear_lock();
  bool compare_and_maybe_reset_lock(rpc::context& ctx, atfw::dtmq::channel_lock_checker& checker,
                                    bool append_log = true);

  void compact_stateful_sequence(int64_t sequence);
  void compact_sequence(int64_t sequence);
  void maybe_create_wal_client();

  inline void set_sequence_allocator(int64_t sequence) noexcept { sequence_allocator_ = sequence; }
  inline int64_t get_sequence_allocator() const noexcept { return sequence_allocator_; }

  void hash_mismatch_increase();

 private:
  void update_timer(rpc::context& ctx, bool force = false);
  void remove_timer();

  bool upgrade_to_readonly(uint64_t readonly_server_index) noexcept;
  bool upgrade_to_writable() noexcept;
  bool downgrade_to_readable(uint64_t readonly_server_index) noexcept;
  bool downgrade_to_none() noexcept;

  void send_oss(rpc::context& ctx, const std::string& action, int32_t ret = 0, uint64_t transfer_to = 0);

  void recalculate_etcd_cache();

 private:
  // channel_manager* owner_;
  atfw::dtmq::DChannelIdKey channel_key_;
  int64_t sequence_allocator_;
  int64_t compact_stateful_sequence_;
  mq_channel_timer_type::timer_wptr_t timer_handle_;
  channel_status status_;
  uint64_t readonly_replicate_index_;
  int64_t readonly_replicate_configure_count_;
  std::chrono::system_clock::time_point remove_timepoint_;
  std::chrono::system_clock::time_point last_save_timepoint_;
  std::chrono::system_clock::time_point lost_last_subscriber_timepoint_;
  std::chrono::system_clock::time_point next_notify_readonly_subscribe_timepoint_;
  std::chrono::system_clock::time_point last_writable_notify_readonly_timepoint_;
  std::chrono::system_clock::time_point next_init_subscribe_timepoint_;
  rpc::result_code_type::value_type last_result_code_;

  atfw::dtmq::DChannelConfigure configure_;
  atfw::dtmq::DChannelOptimisticLock lock_;

  int64_t custom_data_sequence_;
  int64_t private_data_sequence_;
  google::protobuf::Any custom_data_;
  google::protobuf::Any private_data_;

  bool is_loading_snapshot_;
  bool is_dirty_;

  atfw::util::memory::strong_rc_ptr<mq_channel_wal_object_type> shared_wal_object_;
  atfw::util::memory::strong_rc_ptr<mq_channel_wal_publisher_type> wal_publisher_;
  atfw::util::memory::strong_rc_ptr<mq_channel_wal_client_type> wal_client_;

  mutable task_type_trait::task_type io_task_;

  std::chrono::system_clock::time_point next_send_oss_time_;
  int64_t resolved_transfer_etcd_revision_;
  int64_t server_distribution_etcd_revision_;
  struct replicate_distribution_info {
    uint64_t writable_server_id = 0;

    // 只读服务索引: 服务ID -> 只读副本index集合
    std::unordered_map<uint64_t, replicate_index_set> readonly_server_id_to_replicate_index;
    // 只读服务索引: 只读副本index -> 服务ID
    std::unordered_map<uint64_t, uint64_t> readonly_replicate_index_to_server_id;
  };
  replicate_distribution_info ready_distribution_;
  replicate_distribution_info target_distribution_;
};

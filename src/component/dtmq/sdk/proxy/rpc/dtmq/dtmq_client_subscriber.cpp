// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/dtmq/dtmq_client_subscriber.h"

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>
#include <nostd/function_ref.h>

#include <log/log_wrapper.h>
#include <memory/rc_ptr.h>
#include <time/jiffies_timer.h>
#include <time/time_utility.h>

#include <distributed_system/wal_client.h>
#include <distributed_system/wal_common_defs.h>

#include <atframe/etcdcli/etcd_discovery.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>
#include <utility/random_engine.h>

#include <dispatcher/task_type_traits.h>
#include <rpc/dtmq/dtmqproxysvrservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_utils.h>

#include <config/excel/config_easy_api.h>
#include <config/excel_config_dtmq_index.h>
#include <config/extern_service_types.h>
#include <config/server_frame_build_feature.h>

#include <logic/logic_server_setup.h>
#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rpc/dtmq/dtmq_client_api.h"
#include "rpc/rpc_common_types.h"

#ifdef max
#  undef max
#endif

namespace rpc {
namespace dtmq {

namespace {

class ATFW_UTIL_SYMBOL_LOCAL shared_subscriber;

using mq_client_subscriber_timer_type = atfw::util::time::jiffies_timer<>;

using mq_client_subscriber_storage_type = atfw::dtmq::DChannelSnapshot;

struct ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_wal_object_context {
  std::reference_wrapper<rpc::context> context;
  std::reference_wrapper<int32_t> result_code;

  explicit mq_client_subscriber_wal_object_context(rpc::context& ctx, int32_t& output_result)
      : context(ctx), result_code(output_result) {}
};

struct ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_private_data_type {
  shared_subscriber* subscriber = nullptr;
};

struct ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_wal_log_action_getter {
  atfw::dtmq::DChannelMessageDetail::CommandCase operator()(const atfw::dtmq::DChannelMessage& log) noexcept {
    return log.detail().command_case();
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_log_action_hash_t {
  size_t operator()(const atfw::dtmq::DChannelMessageDetail::CommandCase& key) const noexcept {
    return std::hash<int>()(key);
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_log_action_equal_t {
  bool operator()(const atfw::dtmq::DChannelMessageDetail::CommandCase& l,
                  const atfw::dtmq::DChannelMessageDetail::CommandCase& r) const noexcept {
    return l == r;
  }
};

struct mq_client_subscriber_log_operator
    : public atfw::util::distributed_system::wal_log_operator<
          // NOLINTNEXTLINE(modernize-use-transparent-functors)
          int64_t, atfw::dtmq::DChannelMessage, mq_client_subscriber_wal_log_action_getter, std::less<int64_t>,
          mq_client_subscriber_log_action_hash_t, mq_client_subscriber_log_action_equal_t,
          atfw::memory::stl::allocator<atfw::dtmq::DChannelMessage>,
          atfw::util::distributed_system::wal_mt_mode::kSingleThread> {};

using mq_client_subscriber_wal_client_type = atfw::util::distributed_system::wal_client<
    mq_client_subscriber_storage_type, mq_client_subscriber_log_operator, mq_client_subscriber_wal_object_context,
    mq_client_subscriber_private_data_type, mq_client_subscriber_storage_type>;

struct ATFW_UTIL_SYMBOL_LOCAL internal_subscriber_manager;

template <class Rep, class Period>
static time_t chrono_to_timer_tick(std::chrono::duration<Rep, Period> d) {
  // 统一的精度转换，对于消息队列服务订阅来说 128ms 定时器精度足够了
  return static_cast<time_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count() / 128);
}

static time_t chrono_to_timer_tick(std::chrono::system_clock::time_point tp) {
  return chrono_to_timer_tick(tp.time_since_epoch());
}

class ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_delegate_helper;

struct ATFW_UTIL_SYMBOL_LOCAL client_subscriber_option {
  bool auto_create_channel;
  bool with_private_data;

  inline client_subscriber_option() noexcept : auto_create_channel(false), with_private_data(false) {}
  inline client_subscriber_option(bool auto_create, bool with_private) noexcept
      : auto_create_channel(auto_create), with_private_data(with_private) {}

  inline client_subscriber_option(const client_subscriber_option&) = default;
  inline client_subscriber_option(client_subscriber_option&&) = default;
  inline client_subscriber_option& operator=(const client_subscriber_option&) = default;
  inline client_subscriber_option& operator=(client_subscriber_option&&) = default;

  inline bool operator==(const client_subscriber_option& other) const noexcept {
    return auto_create_channel == other.auto_create_channel && with_private_data == other.with_private_data;
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL send_subscribe_key_t {
  uint64_t target_server_id;
  bool with_private_data;

  inline send_subscribe_key_t(uint64_t server_id, bool with_private) noexcept
      : target_server_id(server_id), with_private_data(with_private) {}
};

struct ATFW_UTIL_SYMBOL_LOCAL send_subscribe_hash_t {
  size_t operator()(const send_subscribe_key_t& key) const noexcept {
    size_t value = std::hash<uint64_t>()(key.target_server_id);
    value = std::hash<bool>()(key.with_private_data) + 0x9e3779b9 + (value << 6) + (value >> 2);
    return value;
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL send_subscribe_equal_t {
  bool operator()(const send_subscribe_key_t& l, const send_subscribe_key_t& r) const noexcept {
    return l.target_server_id == r.target_server_id && l.with_private_data == r.with_private_data;
  }
};

class ATFW_UTIL_SYMBOL_LOCAL shared_subscriber
    : public atfw::util::memory::enable_shared_rc_from_this<shared_subscriber> {
 public:
  using ptr_t = atfw::util::memory::strong_rc_ptr<shared_subscriber>;

  // NOLINTNEXTLINE(readability-enum-initial-value)
  enum class subscriber_flag : uint32_t {
    kUninitialized = 0,
    kReady = 1,
    kLockRegisteredClient = 2,
    kDestroying = 3,
    kRecentlyHeartbeatFailure = 4,
    kInCallbackReceiveEvent = 5,
    kInCallbackLoadSnapshot = 6,
    kMax,
  };

  enum class timer_action_type : uint8_t {
    kNone = 0,
    kSendHeartbeat,
    kRetryHeartbeat,
    kGc,
  };

 public:
  static ptr_t make_shared(const atfw::dtmq::DChannelIdKey& channel_key);

  void maybe_mutable_wal_client();

  static mq_client_subscriber_wal_client_type::configure_pointer create_client_configure(
      const atfw::dtmq::DChannelConfigure& configure);
  static mq_client_subscriber_wal_client_type::vtable_pointer create_client_vtable();

  // NOLINTNEXTLINE(modernize-pass-by-value)
  explicit shared_subscriber(const atfw::dtmq::DChannelIdKey& channel_key)
      : identify_key_(0),
        timer_action_(timer_action_type::kNone),
        timer_timeout_tick_(0),
        timer_gc_tick_(0),
        channel_key_(channel_key),
        // 随机订阅副本的index即可，实际的发送接口会标准化成合理值
        // 通过固定这个值，可以防止数据抖动
        readonly_replicate_index_(atfw::component::random_engine::random()),
        destroy_timepoint_(std::chrono::system_clock::from_time_t(0)),
        destroy_sequence_(0),
        create_timepoint_(std::chrono::system_clock::from_time_t(0)),
        create_sequence_(0),
        custom_data_sequence_(0),
        private_data_sequence_(0),
        last_message_hash_code_(0),
        last_message_sequence_(0) {
    static std::atomic<uint64_t> global_shared_subscriber_identify_key_allocator{1};
    identify_key_ = global_shared_subscriber_identify_key_allocator.fetch_add(1, std::memory_order_acq_rel);
    subscriber_info_.set_subscriber_server_id(logic_config::me()->get_local_server_id());

    auto channel_cfg = excel::get_dtmq_channel_configure(channel_key.channel_type());
    if (channel_cfg) {
      reload_configure(*channel_cfg);
    } else {
      excel::normalize_dtmq_channel_configure(configure_);
    }

    // 这里是共享 subscriber key
    if (!logic_config::me()->get_local_server_name().empty()) {
      subscriber_info_.set_subscriber_key(
          atfw::util::string::format("server:{}", logic_config::me()->get_local_server_name()));
    } else {
      subscriber_info_.set_subscriber_key(
          atfw::util::string::format("server:{}", subscriber_info_.subscriber_server_id()));
    }

    FWLOGINFO(
        "atframework.dtmq.shared_subscriber created, channel_key={}, subscriber_key={}, readonly_replicate_index={}",
        channel_key_.ShortDebugString(), subscriber_info_.subscriber_key(), readonly_replicate_index_);
  }

  ~shared_subscriber() {
    set_flag(subscriber_flag::kDestroying, true);

    remove_timer();

    FWLOGINFO(
        "atframework.dtmq.shared_subscriber destroyed, channel_key={}, subscriber_key={}, readonly_replicate_index={}",
        channel_key_.ShortDebugString(), subscriber_info_.subscriber_key(), readonly_replicate_index_);
  }

  void setup_timer(timer_action_type action, bool ignore_same_action = true);

  void remove_timer();

  void load_metadata(rpc::context& ctx, const atfw::dtmq::DChannelMetadata& metadata);
  void load_runtime(rpc::context& ctx, const atfw::dtmq::DChannelRuntime& runtime);
  void load_lock(rpc::context& ctx, const atfw::dtmq::DChannelOptimisticLock& lock);

  inline bool check_flag(subscriber_flag flag) const noexcept { return flags_.test(static_cast<size_t>(flag)); }

  inline void set_flag(subscriber_flag flag, bool value) noexcept { flags_.set(static_cast<size_t>(flag), value); }

  inline bool is_ready() const noexcept { return check_flag(subscriber_flag::kReady); }

  inline uint64_t get_shared_channel_identify() const noexcept { return identify_key_; }

  inline const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept { return channel_key_; }

  inline const atfw::dtmq::channel_subscriber& get_subscriber_info() const noexcept { return subscriber_info_; }

  inline const atfw::dtmq::DChannelConfigure& get_configure() const noexcept { return configure_; }

  inline const atfw::dtmq::DChannelOptimisticLock& get_lock() const noexcept { return lock_; }

  inline int64_t get_custom_data_sequence() const noexcept { return custom_data_sequence_; }

  inline const ::google::protobuf::Any& get_custom_data_content() const noexcept { return custom_data_; }

  inline int64_t get_private_data_sequence() const noexcept { return private_data_sequence_; }

  inline const ::google::protobuf::Any& get_private_data_content() const noexcept { return private_data_; }

  inline int64_t get_create_sequence() const noexcept { return create_sequence_; }

  inline std::chrono::system_clock::time_point get_create_timepoint() const noexcept { return create_timepoint_; }

  inline int64_t get_destroy_sequence() const noexcept { return destroy_sequence_; }

  inline std::chrono::system_clock::time_point get_destroy_timepoint() const noexcept { return destroy_timepoint_; }

  inline uint64_t get_readonly_replicate_index() const noexcept { return readonly_replicate_index_; }

  inline bool get_option_with_private_data() const noexcept { return !registered_client_with_private_data_.empty(); }

  inline bool has_registered_client() const noexcept {
    return !registered_client_auto_create_channel_.empty() || !registered_client_no_create_channel_.empty() ||
           !lock_registered_client_pending_add_.empty();
  }

  inline bool should_auto_create_channel() const noexcept { return !registered_client_auto_create_channel_.empty(); }

  inline bool can_be_removed() const noexcept {
    return registered_client_auto_create_channel_.empty() && registered_client_no_create_channel_.empty() &&
           lock_registered_client_pending_add_.empty();
  }

  void register_client_subscriber(client_subscriber* client, const client_subscriber_option& options);
  void unregister_client_subscriber(client_subscriber* client);
  void foreach_registered_client_subscriber(atfw::util::nostd::function_ref<void(client_subscriber&)> callback);

  int64_t get_compact_sequence() const noexcept;

  uint64_t get_last_message_hash_code() const noexcept;
  int64_t get_last_message_sequence() const noexcept;

  int64_t get_last_removed_sequence() const noexcept;

  mq_client_subscriber_wal_client_type::log_const_pointer get_message_by_sequence(int64_t sequence) const noexcept;

  bool query_message(atfw::util::nostd::function_ref<bool(const atfw::dtmq::DChannelMessage&)> fn,
                     const client_subscriber::query_options& option);

  int32_t tick(rpc::context& ctx);

  void update_last_heartbeat(int64_t log_sequence, uint64_t log_hash_code) noexcept;

  void receive_heartbeat_response(rpc::context& ctx);

  void receive_event_sync(rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& event_sync);

  void load_snapshot(rpc::context& ctx, const atfw::dtmq::DChannelSnapshot& snapshot);

  void reload_configure(const atfw::dtmq::DChannelConfigure& config);

  void set_ready(rpc::context& ctx);

  void set_destroyed(rpc::context& ctx, int64_t log_sequence, std::chrono::system_clock::time_point destroy_time);

  void update_custom_data(rpc::context& ctx, int64_t sequence, const google::protobuf::Any& custom_data);

  void update_private_data(rpc::context& ctx, int64_t sequence, const google::protobuf::Any& private_data);

  void update_optimistic_lock(rpc::context& ctx, const ::atfw::dtmq::DChannelOptimisticLock& to);

  void compact(rpc::context& ctx, int64_t compact_sequence);

  void schedule_send_heartbeat(rpc::context& ctx);

  void schedule_retry_heartbeat(rpc::context& ctx);

  static void add_cached_shared_subscriber(const shared_subscriber::ptr_t& subscriber);

  static void remove_cached_shared_subscriber(const shared_subscriber* subscriber);

 private:
  friend class ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_delegate_helper;

  uint64_t identify_key_;

  std::bitset<static_cast<size_t>(subscriber_flag::kMax)> flags_;
  mq_client_subscriber_timer_type::timer_wptr_t timer_watcher_;
  timer_action_type timer_action_;
  time_t timer_timeout_tick_;
  time_t timer_gc_tick_;

  atfw::dtmq::DChannelIdKey channel_key_;
  atfw::dtmq::channel_subscriber subscriber_info_;
  uint64_t readonly_replicate_index_;

  atfw::dtmq::DChannelConfigure configure_;
  atfw::dtmq::DChannelOptimisticLock lock_;

  std::chrono::system_clock::time_point destroy_timepoint_;
  int64_t destroy_sequence_;
  std::chrono::system_clock::time_point create_timepoint_;
  int64_t create_sequence_;

  int64_t custom_data_sequence_;
  int64_t private_data_sequence_;
  google::protobuf::Any custom_data_;
  google::protobuf::Any private_data_;

  atfw::util::memory::strong_rc_ptr<mq_client_subscriber_wal_client_type> wal_client_;

  uint64_t last_message_hash_code_;
  int64_t last_message_sequence_;

  std::unordered_set<client_subscriber*> registered_client_auto_create_channel_;
  std::unordered_set<client_subscriber*> registered_client_no_create_channel_;
  std::unordered_set<client_subscriber*> registered_client_with_private_data_;

  struct lock_registered_client_guard {
    shared_subscriber* subscriber_;

    lock_registered_client_guard(const lock_registered_client_guard&) = delete;
    lock_registered_client_guard(lock_registered_client_guard&&) = delete;
    lock_registered_client_guard& operator=(const lock_registered_client_guard&) = delete;
    lock_registered_client_guard& operator=(lock_registered_client_guard&&) = delete;

    explicit lock_registered_client_guard(shared_subscriber& subscriber) : subscriber_(nullptr) {
      if (subscriber.check_flag(subscriber_flag::kLockRegisteredClient)) {
        return;
      }

      subscriber.set_flag(subscriber_flag::kLockRegisteredClient, true);
      subscriber_ = &subscriber;
    }

    ~lock_registered_client_guard() {
      if (subscriber_ == nullptr) {
        return;
      }

      subscriber_->set_flag(subscriber_flag::kLockRegisteredClient, false);

      if (!subscriber_->lock_registered_client_pending_add_.empty()) {
        std::unordered_map<client_subscriber*, client_subscriber_option> pending_add;
        pending_add.swap(subscriber_->lock_registered_client_pending_add_);
        for (const auto& client : pending_add) {
          if (client.first != nullptr) {
            subscriber_->register_client_subscriber(client.first, client.second);
          }
        }
      }

      if (!subscriber_->lock_registered_client_pending_remove_.empty()) {
        std::unordered_set<client_subscriber*> pending_remove;
        pending_remove.swap(subscriber_->lock_registered_client_pending_remove_);
        for (const auto& client : pending_remove) {
          if (client != nullptr) {
            subscriber_->unregister_client_subscriber(client);
          }
        }

        if (subscriber_->can_be_removed()) {
          subscriber_->setup_timer(timer_action_type::kGc);
        }
      }
    }
  };

  std::unordered_map<client_subscriber*, client_subscriber_option> lock_registered_client_pending_add_;
  std::unordered_set<client_subscriber*> lock_registered_client_pending_remove_;
};

class ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_delegate_helper {
 public:
  using wal_object_type = mq_client_subscriber_wal_client_type::object_type;
  using wal_result_code = atfw::util::distributed_system::wal_result_code;
  using log_const_iterator = wal_object_type::log_const_iterator;
  using log_iterator = wal_object_type::log_iterator;
  using log_key_type = wal_object_type::log_key_type;
  using log_type = wal_object_type::log_type;

  static void setup_delegate_actions(wal_object_type::callback_log_group_map_t& actions);

  static wal_result_code destroy_channel(wal_object_type&, const wal_object_type::log_type&,
                                         wal_object_type::callback_param_type);

  static wal_result_code create_channel(wal_object_type&, const wal_object_type::log_type&,
                                        wal_object_type::callback_param_type);

  static wal_result_code reset_lock(wal_object_type&, const wal_object_type::log_type&,
                                    wal_object_type::callback_param_type);

  static wal_result_code receive_text(wal_object_type&, const wal_object_type::log_type&,
                                      wal_object_type::callback_param_type);

  static wal_result_code receive_event(wal_object_type&, const wal_object_type::log_type&,
                                       wal_object_type::callback_param_type);

  static wal_result_code common_action(wal_object_type&, const wal_object_type::log_type&,
                                       wal_object_type::callback_param_type);
};

static bool& is_internal_subscriber_manager_destroyed() {
  static bool internal_subscriber_manager_destroyed = false;
  return internal_subscriber_manager_destroyed;
}

struct ATFW_UTIL_SYMBOL_LOCAL internal_subscriber_manager {
  bool timer_running = false;
  bool destroyed = false;
  bool is_in_callback_global_receive_channel_event = false;
  bool is_in_callback_global_tick = false;
  mq_client_subscriber_timer_type timer_set;

  std::unordered_map<std::string, shared_subscriber::ptr_t> cached_subscriber_by_channel_id;
  std::unordered_map<shared_subscriber*, shared_subscriber::ptr_t> cached_subscriber_by_raw_pointer;
  std::list<std::pair<std::chrono::system_clock::time_point, atfw::util::memory::weak_rc_ptr<shared_subscriber>>>
      retry_setup_timer_list;

  std::unordered_set<shared_subscriber*> pending_heartbeat_subscriber;
  std::unordered_set<shared_subscriber*> retry_heartbeat_subscriber;
  task_type_trait::task_type running_heartbeat_task;

  std::unordered_map<std::string, shared_subscriber::ptr_t> pending_unsubscribe_subscriber;
  task_type_trait::task_type running_unsubscribe_task;

  ~internal_subscriber_manager() { is_internal_subscriber_manager_destroyed() = true; }
};

static internal_subscriber_manager& get_internal_subscriber_manager() {
  static internal_subscriber_manager ret;
  return ret;
}

static int32_t internal_subscriber_manager_do_send_heartbeat(rpc::context& ctx);
static int32_t internal_subscriber_manager_do_send_unsubscribe(rpc::context& ctx);
static void internal_subscriber_manager_do_retry_heartbeat(rpc::context& ctx);

}  // namespace

struct client_subscriber::event_callback_set_t {
  client_subscriber::event_callback_on_ready_t on_ready;
  client_subscriber::event_callback_on_destroy_t on_destroy;
  client_subscriber::event_callback_on_update_custom_data_t on_update_custom_data;
  client_subscriber::event_callback_on_update_private_data_t on_update_private_data;
  client_subscriber::event_callback_on_update_optimistic_lock_t on_update_optimistic_lock;
  client_subscriber::event_callback_on_compact_t on_compact;
  client_subscriber::event_callback_on_receive_text_t on_receive_text;
  client_subscriber::event_callback_on_receive_event_t on_receive_event;
  std::unordered_map<std::string, client_subscriber::event_callback_on_receive_event_t> on_receive_event_by_type_url;
  client_subscriber::event_callback_on_receive_raw_message_t on_receive_raw_message;
  client_subscriber::event_callback_on_receive_snapshot_t on_receive_snapshot_start;
  client_subscriber::event_callback_on_receive_snapshot_t on_receive_snapshot_finished;
};

namespace {
static const client_subscriber::event_callback_set_t& get_default_event_callback_set() {
  static client_subscriber::event_callback_set_t default_event_callback_set;
  return default_event_callback_set;
}
}  // namespace

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::subscriber_options(std::string&& input_subscriber_key)
    : subscriber_key(std::move(input_subscriber_key)), auto_create_channel(true), with_private_data(false) {}

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::subscriber_options(const std::string& input_subscriber_key)
    : subscriber_key(input_subscriber_key), auto_create_channel(true), with_private_data(false) {}

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::~subscriber_options() {}

struct client_subscriber::ctor_guard {
  std::string subscriber_key;
  shared_subscriber::ptr_t shared_instance;
  client_subscriber::event_callback_set_ptr_t event_handler;
  client_subscriber_option options;

  ctor_guard(const atfw::dtmq::DChannelIdKey& input_channel_key, const subscriber_options& input_options)
      : subscriber_key(input_options.subscriber_key),
        shared_instance(shared_subscriber::make_shared(input_channel_key)),
        event_handler(input_options.event_callback_set),
        options{input_options.auto_create_channel, input_options.with_private_data} {}
};

struct client_subscriber::subscriber_internal_data {
  std::string subscriber_key;
  client_subscriber_option options;
  atfw::util::nostd::nonnull<shared_subscriber::ptr_t> shared_instance;
  client_subscriber::event_callback_set_ptr_t shared_event_handler;
  client_subscriber::event_callback_set_ptr_t private_event_handler;
  std::vector<uintptr_t> local_private_data;

  subscriber_internal_data(std::string&& input_subscriber_key, shared_subscriber::ptr_t&& input_shared_instance,
                           event_callback_set_ptr_t&& input_event_handler, client_subscriber_option input_options)
      : subscriber_key(std::move(input_subscriber_key)),
        options(input_options),
        shared_instance(std::move(input_shared_instance)),
        shared_event_handler(std::move(input_event_handler)) {}
};

client_subscriber::client_subscriber(ctor_guard& guard)
    : internal_data_(atfw::component::memory::stl::make_strong_rc<subscriber_internal_data>(
          std::move(guard.subscriber_key), std::move(guard.shared_instance), std::move(guard.event_handler),
          guard.options)) {
  internal_data_->shared_instance->register_client_subscriber(this, guard.options);
}

DTMQ_PROXY_SDK_API client_subscriber::~client_subscriber() {
  internal_data_->shared_instance->unregister_client_subscriber(this);
}

DTMQ_PROXY_SDK_API client_subscriber::event_callback_set_ptr_t client_subscriber::create_event_callback_set() {
  return atfw::component::memory::stl::make_strong_rc<event_callback_set_t>();
}

DTMQ_PROXY_SDK_API atfw::util::nostd::nullable<client_subscriber::ptr_t> client_subscriber::create(
    const atfw::dtmq::DChannelIdKey& channel_key, const subscriber_options& options) {
  ctor_guard cg(channel_key, options);
  if (!cg.shared_instance) {
    return nullptr;
  }

  return atfw::util::nostd::nullable<ptr_t>(atfw::component::memory::stl::make_strong_rc<client_subscriber>(cg));
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::global_receive_channel_event(
    rpc::context& ctx, uint64_t from_server_id, const atfw::dtmq::SSChannelEventSync& event_sync) {
  if (is_internal_subscriber_manager_destroyed()) {
    RPC_RETURN_CODE(0);
  }

  if (!event_sync.has_channel_snapshot() && !event_sync.has_channel_metadata()) {
    FCTXLOGERROR(ctx, "channel event sync has no channel_snapshot or channel_metadata, ignore this sync");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  const atfw::dtmq::DChannelIdKey* channel_key = nullptr;
  if (event_sync.has_channel_snapshot()) {
    channel_key = &event_sync.channel_snapshot().channel_metadata().channel_key();
  } else {
    channel_key = &event_sync.channel_metadata().channel_key();
  }

  if (channel_key->channel_id().empty()) {
    FCTXLOGERROR(ctx, "channel event sync has empty channel_id, ignore this sync");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto& mgr = get_internal_subscriber_manager();
  if (mgr.is_in_callback_global_receive_channel_event) {
    RPC_RETURN_CODE(0);
  }
  mgr.is_in_callback_global_receive_channel_event = true;
  auto in_callback_guard = gsl::finally([] {
    if (is_internal_subscriber_manager_destroyed()) {
      return;
    }

    get_internal_subscriber_manager().is_in_callback_global_receive_channel_event = false;
  });

  // 先处理定时器事件，保证时序
  global_tick(ctx);

  auto iter = mgr.cached_subscriber_by_channel_id.find(channel_key->channel_id());
  if (iter == mgr.cached_subscriber_by_channel_id.end() || !iter->second) {
    FCTXLOGINFO(ctx, "channel {} receive event sync, but may be destroyed, ignore this {} sync",
                channel_key->channel_id(), (event_sync.has_channel_snapshot() ? "snapshot" : "incremental"));

    // 频道已不存在则发送反订阅
    for (const auto& subscriber_key : event_sync.subscriber_keys()) {
      rpc::context::message_holder<atfw::dtmq::SSChannelUnsubscribeReq> req_body(ctx);
      rpc::context::message_holder<google::protobuf::Empty> rsp_body(ctx);
      req_body->mutable_subscriber()->set_subscriber_server_id(logic_config::me()->get_local_server_id());
      req_body->mutable_subscriber()->set_subscriber_key(subscriber_key);
      req_body->add_channel_id(channel_key->channel_id());
      RPC_AWAIT_IGNORE_RESULT(rpc::dtmq::unsubscribe(ctx, from_server_id, *req_body, *rsp_body, true));
    }
    RPC_RETURN_CODE(0);
  }

  auto subscriber = iter->second;
  bool has_notify_current_subscriber = false;
  for (const auto& subscriber_key : event_sync.subscriber_keys()) {
    if (subscriber_key == subscriber->get_subscriber_info().subscriber_key()) {
      has_notify_current_subscriber = true;
      break;
    }
  }
  if (has_notify_current_subscriber) {
    subscriber->receive_event_sync(ctx, event_sync);
  }

  // 如果没有注册的客户端订阅者，也可以发送反订阅
  if (!has_notify_current_subscriber || !subscriber->has_registered_client()) {
    FCTXLOGINFO(ctx, "channel {} receive event sync, but has no registered client subscriber, send unsubscribe",
                channel_key->channel_id());

    for (const auto& subscriber_key : event_sync.subscriber_keys()) {
      rpc::context::message_holder<atfw::dtmq::SSChannelUnsubscribeReq> req_body(ctx);
      rpc::context::message_holder<google::protobuf::Empty> rsp_body(ctx);
      req_body->mutable_subscriber()->set_subscriber_server_id(logic_config::me()->get_local_server_id());
      req_body->mutable_subscriber()->set_subscriber_key(subscriber_key);
      req_body->add_channel_id(channel_key->channel_id());
      RPC_AWAIT_IGNORE_RESULT(rpc::dtmq::unsubscribe(ctx, from_server_id, *req_body, *rsp_body, true));
    }
  }

  if (!subscriber->has_registered_client()) {
    subscriber->setup_timer(shared_subscriber::timer_action_type::kGc);
  }

  RPC_RETURN_CODE(0);
}

DTMQ_PROXY_SDK_API int32_t client_subscriber::global_tick(rpc::context& ctx) {
  if (is_internal_subscriber_manager_destroyed()) {
    return 0;
  }

  auto& mgr = get_internal_subscriber_manager();
  if (mgr.is_in_callback_global_tick) {
    return 0;
  }
  mgr.is_in_callback_global_tick = true;
  auto in_callback_guard = gsl::finally([] {
    if (is_internal_subscriber_manager_destroyed()) {
      return;
    }

    get_internal_subscriber_manager().is_in_callback_global_tick = false;
  });

  if (!mgr.timer_running) {
    mgr.timer_set.init(chrono_to_timer_tick(atfw::util::time::time_utility::now()));
    mgr.timer_running = true;
  }

  mgr.timer_set.set_private_data(reinterpret_cast<void*>(&ctx));
  int32_t ret = 0;
  int res = mgr.timer_set.tick(chrono_to_timer_tick(atfw::util::time::time_utility::now()));
  mgr.timer_set.set_private_data(nullptr);
  if (res < 0) {
    FWLOGERROR("client_subscriber::global_tick with error, timer error code: {}", res);
  } else {
    ret += res;
  }

  // 统一批处理执行心跳发送
  if (task_type_trait::empty(mgr.running_heartbeat_task) || task_type_trait::is_exiting(mgr.running_heartbeat_task)) {
    internal_subscriber_manager_do_send_heartbeat(ctx);
    if (!task_type_trait::empty(mgr.running_heartbeat_task) &&
        !task_type_trait::is_exiting(mgr.running_heartbeat_task)) {
      ++ret;
    }
  }

  // 处理心跳重试，重设定时器
  if (!mgr.retry_heartbeat_subscriber.empty()) {
    ++ret;
  }
  internal_subscriber_manager_do_retry_heartbeat(ctx);

  // 统一处理反订阅
  if (task_type_trait::empty(mgr.running_unsubscribe_task) ||
      task_type_trait::is_exiting(mgr.running_unsubscribe_task)) {
    internal_subscriber_manager_do_send_unsubscribe(ctx);
    if (!task_type_trait::empty(mgr.running_unsubscribe_task) &&
        !task_type_trait::is_exiting(mgr.running_unsubscribe_task)) {
      ++ret;
    }
  }

  // 重试定时器
  while (!mgr.retry_setup_timer_list.empty()) {
    auto& item = mgr.retry_setup_timer_list.front();
    if (item.first > atfw::util::time::time_utility::now()) {
      break;
    }

    if (item.second.expired()) {
      mgr.retry_setup_timer_list.pop_front();
      continue;
    }

    auto subscriber = item.second.lock();
    mgr.retry_setup_timer_list.pop_front();

    if (!subscriber) {
      continue;
    }

    ++ret;

    // 已经移除则忽略定时器重设
    if (mgr.cached_subscriber_by_raw_pointer.end() == mgr.cached_subscriber_by_raw_pointer.find(subscriber.get())) {
      continue;
    }

    if (subscriber->can_be_removed()) {
      subscriber->setup_timer(shared_subscriber::timer_action_type::kGc);
    } else {
      subscriber->setup_timer(shared_subscriber::timer_action_type::kSendHeartbeat);
    }
  }

  return ret;
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::global_await_pending_heartbeat(rpc::context& ctx) {
  if (is_internal_subscriber_manager_destroyed()) {
    RPC_RETURN_CODE(0);
  }

  auto& mgr = get_internal_subscriber_manager();
  if (global_is_sending_heartbeat()) {
    auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, mgr.running_heartbeat_task));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  if (is_internal_subscriber_manager_destroyed()) {
    RPC_RETURN_CODE(0);
  }

  // 确保当前这一轮的pending心跳也发送出去了
  if (!mgr.pending_heartbeat_subscriber.empty()) {
    auto ret = internal_subscriber_manager_do_send_heartbeat(ctx);
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, mgr.running_heartbeat_task));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  RPC_RETURN_CODE(0);
}

DTMQ_PROXY_SDK_API bool client_subscriber::global_has_pending_heartbeat() noexcept {
  if (is_internal_subscriber_manager_destroyed()) {
    return false;
  }

  auto& mgr = get_internal_subscriber_manager();
  return !mgr.pending_heartbeat_subscriber.empty() || !mgr.retry_heartbeat_subscriber.empty();
}

DTMQ_PROXY_SDK_API bool client_subscriber::global_is_sending_heartbeat() noexcept {
  if (is_internal_subscriber_manager_destroyed()) {
    return false;
  }

  auto& mgr = get_internal_subscriber_manager();
  return !task_type_trait::empty(mgr.running_heartbeat_task) &&
         !task_type_trait::is_exiting(mgr.running_heartbeat_task);
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelIdKey& client_subscriber::get_channel_key() const noexcept {
  return internal_data_->shared_instance->get_channel_key();
}

DTMQ_PROXY_SDK_API const std::string& client_subscriber::get_subscriber_key() const noexcept {
  return internal_data_->subscriber_key;
}

DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point client_subscriber::get_last_heartbeat_timepoint()
    const noexcept {
  return protobuf_to_system_clock(internal_data_->shared_instance->get_subscriber_info().last_heartbeat_timepoint());
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_last_heartbeat_sequence() const noexcept {
  return internal_data_->shared_instance->get_subscriber_info().last_heartbeat_sequence();
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_custom_data_sequence() const noexcept {
  return internal_data_->shared_instance->get_custom_data_sequence();
}

DTMQ_PROXY_SDK_API const ::google::protobuf::Any& client_subscriber::get_custom_data_content() const noexcept {
  return internal_data_->shared_instance->get_custom_data_content();
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_private_data_sequence() const noexcept {
  if (!internal_data_->options.with_private_data) {
    return 0;
  }

  return internal_data_->shared_instance->get_private_data_sequence();
}

DTMQ_PROXY_SDK_API const ::google::protobuf::Any& client_subscriber::get_private_data_content() const noexcept {
  if (!internal_data_->options.with_private_data) {
    return ::google::protobuf::Any::default_instance();
  }

  return internal_data_->shared_instance->get_private_data_content();
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelConfigure& client_subscriber::get_configure() const noexcept {
  return internal_data_->shared_instance->get_configure();
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelOptimisticLock& client_subscriber::get_lock() const noexcept {
  return internal_data_->shared_instance->get_lock();
}

DTMQ_PROXY_SDK_API bool client_subscriber::is_ready() const noexcept {
  return internal_data_->shared_instance->is_ready();
}

DTMQ_PROXY_SDK_API bool client_subscriber::is_destroyed() const noexcept {
  return internal_data_->shared_instance->get_destroy_sequence() > 0 &&
         internal_data_->shared_instance->get_destroy_sequence() >=
             internal_data_->shared_instance->get_create_sequence();
}

DTMQ_PROXY_SDK_API gsl::span<const uintptr_t> client_subscriber::get_local_private_data() const noexcept {
  return {internal_data_->local_private_data.data(), internal_data_->local_private_data.size()};
}

DTMQ_PROXY_SDK_API void client_subscriber::set_local_private_data(gsl::span<uintptr_t> local_private_data) {
  internal_data_->local_private_data.assign(local_private_data.begin(), local_private_data.end());
}

DTMQ_PROXY_SDK_API void client_subscriber::append_local_private_data(uintptr_t local_private_data) {
  internal_data_->local_private_data.emplace_back(local_private_data);
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_create_sequence() const noexcept {
  return internal_data_->shared_instance->get_create_sequence();
}

DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point client_subscriber::get_create_timepoint() const noexcept {
  return internal_data_->shared_instance->get_create_timepoint();
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_destroy_sequence() const noexcept {
  return internal_data_->shared_instance->get_destroy_sequence();
}

DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point client_subscriber::get_destroy_timepoint() const noexcept {
  return internal_data_->shared_instance->get_destroy_timepoint();
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_last_message_sequence() const noexcept {
  return internal_data_->shared_instance->get_last_message_sequence();
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_last_removed_sequence() const noexcept {
  return internal_data_->shared_instance->get_last_removed_sequence();
}

DTMQ_PROXY_SDK_API bool client_subscriber::get_option_auto_create_channel() const noexcept {
  return internal_data_->options.auto_create_channel;
}

DTMQ_PROXY_SDK_API bool client_subscriber::get_option_with_private_data() const noexcept {
  return internal_data_->options.with_private_data;
}

DTMQ_PROXY_SDK_API uint64_t client_subscriber::get_shared_channel_identify() const noexcept {
  return internal_data_->shared_instance->get_shared_channel_identify();
}

DTMQ_PROXY_SDK_API void client_subscriber::set_shared_event_callback_set(
    const event_callback_set_ptr_t& event_callbacl_set) {
  internal_data_->shared_event_handler = event_callbacl_set;
}

DTMQ_PROXY_SDK_API const atfw::util::nostd::nullable<client_subscriber::event_callback_set_ptr_t>&
client_subscriber::get_shared_event_callback_set() const noexcept {
  return internal_data_->shared_event_handler;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(event_callback_on_ready_t&& on_ready) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_ready = std::move(on_ready);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(const event_callback_on_ready_t& on_ready) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_ready = on_ready;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_ready_t& client_subscriber::get_event_callback_on_ready()
    const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_ready) {
    return internal_data_->private_event_handler->on_ready;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_ready;
  }

  return get_default_event_callback_set().on_ready;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(event_callback_set_t& event_callback_set,
                                                                       event_callback_on_ready_t&& on_ready) {
  event_callback_set.on_ready = std::move(on_ready);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(event_callback_set_t& event_callback_set,
                                                                       const event_callback_on_ready_t& on_ready) {
  event_callback_set.on_ready = on_ready;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_ready_t& client_subscriber::get_event_callback_on_ready(
    const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_ready;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroyed(event_callback_on_destroy_t&& on_destroy) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_destroy = std::move(on_destroy);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroyed(
    const event_callback_on_destroy_t& on_destroy) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_destroy = on_destroy;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_destroy_t&
client_subscriber::get_event_callback_on_destroyed() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_destroy) {
    return internal_data_->private_event_handler->on_destroy;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_destroy;
  }

  return get_default_event_callback_set().on_destroy;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroyed(event_callback_set_t& event_callback_set,
                                                                           event_callback_on_destroy_t&& on_destroy) {
  event_callback_set.on_destroy = std::move(on_destroy);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroyed(
    event_callback_set_t& event_callback_set, const event_callback_on_destroy_t& on_destroy) {
  event_callback_set.on_destroy = on_destroy;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_destroy_t&
client_subscriber::get_event_callback_on_destroyed(const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_destroy;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    event_callback_on_update_custom_data_t&& on_update_custom_data) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_update_custom_data = std::move(on_update_custom_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    const event_callback_on_update_custom_data_t& on_update_custom_data) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_update_custom_data = on_update_custom_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_custom_data_t&
client_subscriber::get_event_callback_on_update_custom_data() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_update_custom_data) {
    return internal_data_->private_event_handler->on_update_custom_data;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_update_custom_data;
  }

  return get_default_event_callback_set().on_update_custom_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    event_callback_set_t& event_callback_set, event_callback_on_update_custom_data_t&& on_update_custom_data) {
  event_callback_set.on_update_custom_data = std::move(on_update_custom_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    event_callback_set_t& event_callback_set, const event_callback_on_update_custom_data_t& on_update_custom_data) {
  event_callback_set.on_update_custom_data = on_update_custom_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_custom_data_t&
client_subscriber::get_event_callback_on_update_custom_data(const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_update_custom_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    event_callback_on_update_private_data_t&& on_update_private_data) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_update_private_data = std::move(on_update_private_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    const event_callback_on_update_private_data_t& on_update_private_data) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_update_private_data = on_update_private_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_private_data_t&
client_subscriber::get_event_callback_on_update_private_data() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_update_private_data) {
    return internal_data_->private_event_handler->on_update_private_data;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_update_private_data;
  }

  return get_default_event_callback_set().on_update_private_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    event_callback_set_t& event_callback_set, event_callback_on_update_private_data_t&& on_update_private_data) {
  event_callback_set.on_update_private_data = std::move(on_update_private_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    event_callback_set_t& event_callback_set, const event_callback_on_update_private_data_t& on_update_private_data) {
  event_callback_set.on_update_private_data = on_update_private_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_private_data_t&
client_subscriber::get_event_callback_on_update_private_data(const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_update_private_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_optimistic_lock(
    event_callback_on_update_optimistic_lock_t&& on_update_optimistic_lock) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_update_optimistic_lock = std::move(on_update_optimistic_lock);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_optimistic_lock(
    const event_callback_on_update_optimistic_lock_t& on_update_optimistic_lock) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_update_optimistic_lock = on_update_optimistic_lock;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_optimistic_lock_t&
client_subscriber::get_event_callback_on_update_optimistic_lock() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_update_optimistic_lock) {
    return internal_data_->private_event_handler->on_update_optimistic_lock;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_update_optimistic_lock;
  }

  return get_default_event_callback_set().on_update_optimistic_lock;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_optimistic_lock(
    event_callback_set_t& event_callback_set, event_callback_on_update_optimistic_lock_t&& on_update_optimistic_lock) {
  event_callback_set.on_update_optimistic_lock = std::move(on_update_optimistic_lock);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_optimistic_lock(
    event_callback_set_t& event_callback_set,
    const event_callback_on_update_optimistic_lock_t& on_update_optimistic_lock) {
  event_callback_set.on_update_optimistic_lock = on_update_optimistic_lock;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_optimistic_lock_t&
client_subscriber::get_event_callback_on_update_optimistic_lock(
    const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_update_optimistic_lock;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(event_callback_on_compact_t&& on_compact) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_compact = std::move(on_compact);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(
    const event_callback_on_compact_t& on_compact) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_compact = on_compact;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_compact_t&
client_subscriber::get_event_callback_on_compact() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_compact) {
    return internal_data_->private_event_handler->on_compact;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_compact;
  }

  return get_default_event_callback_set().on_compact;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(event_callback_set_t& event_callback_set,
                                                                         event_callback_on_compact_t&& on_compact) {
  event_callback_set.on_compact = std::move(on_compact);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(
    event_callback_set_t& event_callback_set, const event_callback_on_compact_t& on_compact) {
  event_callback_set.on_compact = on_compact;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_compact_t&
client_subscriber::get_event_callback_on_compact(const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_compact;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    event_callback_on_receive_text_t&& on_receive_text) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_text = std::move(on_receive_text);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    const event_callback_on_receive_text_t& on_receive_text) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_text = on_receive_text;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_text_t&
client_subscriber::get_event_callback_on_receive_text() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_receive_text) {
    return internal_data_->private_event_handler->on_receive_text;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_receive_text;
  }

  return get_default_event_callback_set().on_receive_text;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    event_callback_set_t& event_callback_set, event_callback_on_receive_text_t&& on_receive_text) {
  event_callback_set.on_receive_text = std::move(on_receive_text);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    event_callback_set_t& event_callback_set, const event_callback_on_receive_text_t& on_receive_text) {
  event_callback_set.on_receive_text = on_receive_text;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_text_t&
client_subscriber::get_event_callback_on_receive_text(const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_receive_text;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    event_callback_on_receive_event_t&& on_receive_event) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_event = std::move(on_receive_event);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    const event_callback_on_receive_event_t& on_receive_event) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_event = on_receive_event;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_event_t&
client_subscriber::get_event_callback_on_receive_event() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_receive_event) {
    return internal_data_->private_event_handler->on_receive_event;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_receive_event;
  }

  return get_default_event_callback_set().on_receive_event;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    event_callback_set_t& event_callback_set, event_callback_on_receive_event_t&& on_receive_event) {
  event_callback_set.on_receive_event = std::move(on_receive_event);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    event_callback_set_t& event_callback_set, const event_callback_on_receive_event_t& on_receive_event) {
  event_callback_set.on_receive_event = on_receive_event;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_event_t&
client_subscriber::get_event_callback_on_receive_event(const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_receive_event;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event_by_type_url(
    const std::string& type_url, event_callback_on_receive_event_t&& on_receive_event) {
  if (!on_receive_event && !internal_data_->private_event_handler) {
    return;
  }

  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  if (!on_receive_event) {
    internal_data_->private_event_handler->on_receive_event_by_type_url.erase(type_url);
  } else {
    internal_data_->private_event_handler->on_receive_event_by_type_url[type_url] = std::move(on_receive_event);
  }
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event_by_type_url(
    const std::string& type_url, const event_callback_on_receive_event_t& on_receive_event) {
  if (!on_receive_event && !internal_data_->private_event_handler) {
    return;
  }

  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  if (!on_receive_event) {
    internal_data_->private_event_handler->on_receive_event_by_type_url.erase(type_url);
  } else {
    internal_data_->private_event_handler->on_receive_event_by_type_url[type_url] = on_receive_event;
  }
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_event_t&
client_subscriber::get_event_callback_on_receive_event_by_type_url(const std::string& type_url) const noexcept {
  if (internal_data_->private_event_handler) {
    auto iter = internal_data_->private_event_handler->on_receive_event_by_type_url.find(type_url);
    if (iter != internal_data_->private_event_handler->on_receive_event_by_type_url.end()) {
      return iter->second;
    }
  }

  if (internal_data_->shared_event_handler) {
    auto iter = internal_data_->shared_event_handler->on_receive_event_by_type_url.find(type_url);
    if (iter != internal_data_->shared_event_handler->on_receive_event_by_type_url.end()) {
      return iter->second;
    }
  }

  return get_default_event_callback_set().on_receive_event;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event_by_type_url(
    event_callback_set_t& event_callback_set, const std::string& type_url,
    event_callback_on_receive_event_t&& on_receive_event) {
  if (!on_receive_event) {
    event_callback_set.on_receive_event_by_type_url.erase(type_url);
  } else {
    event_callback_set.on_receive_event_by_type_url[type_url] = std::move(on_receive_event);
  }
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event_by_type_url(
    event_callback_set_t& event_callback_set, const std::string& type_url,
    const event_callback_on_receive_event_t& on_receive_event) {
  if (!on_receive_event) {
    event_callback_set.on_receive_event_by_type_url.erase(type_url);
  } else {
    event_callback_set.on_receive_event_by_type_url[type_url] = on_receive_event;
  }
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_event_t&
client_subscriber::get_event_callback_on_receive_event_by_type_url(const event_callback_set_t& event_callback_set,
                                                                   const std::string& type_url) noexcept {
  auto iter = event_callback_set.on_receive_event_by_type_url.find(type_url);
  if (iter != event_callback_set.on_receive_event_by_type_url.end()) {
    return iter->second;
  }

  return get_default_event_callback_set().on_receive_event;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_raw_message(
    event_callback_on_receive_raw_message_t&& on_receive_raw_message) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_raw_message = std::move(on_receive_raw_message);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_raw_message(
    const event_callback_on_receive_raw_message_t& on_receive_raw_message) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_raw_message = on_receive_raw_message;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_raw_message_t&
client_subscriber::get_event_callback_on_receive_raw_message() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_receive_raw_message) {
    return internal_data_->private_event_handler->on_receive_raw_message;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_receive_raw_message;
  }

  return get_default_event_callback_set().on_receive_raw_message;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_raw_message(
    event_callback_set_t& event_callback_set, event_callback_on_receive_raw_message_t&& on_receive_raw_message) {
  event_callback_set.on_receive_raw_message = std::move(on_receive_raw_message);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_raw_message(
    event_callback_set_t& event_callback_set, const event_callback_on_receive_raw_message_t& on_receive_raw_message) {
  event_callback_set.on_receive_raw_message = on_receive_raw_message;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_raw_message_t&
client_subscriber::get_event_callback_on_receive_raw_message(const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_receive_raw_message;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_start(
    event_callback_on_receive_snapshot_t&& on_receive_snapshot_start) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_snapshot_start = std::move(on_receive_snapshot_start);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_start(
    const event_callback_on_receive_snapshot_t& on_receive_snapshot_start) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_snapshot_start = on_receive_snapshot_start;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_snapshot_t&
client_subscriber::get_event_callback_on_receive_snapshot_start() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_receive_snapshot_start) {
    return internal_data_->private_event_handler->on_receive_snapshot_start;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_receive_snapshot_start;
  }

  return get_default_event_callback_set().on_receive_snapshot_start;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_start(
    event_callback_set_t& event_callback_set, event_callback_on_receive_snapshot_t&& on_receive_snapshot_start) {
  event_callback_set.on_receive_snapshot_start = std::move(on_receive_snapshot_start);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_start(
    event_callback_set_t& event_callback_set, const event_callback_on_receive_snapshot_t& on_receive_snapshot_start) {
  event_callback_set.on_receive_snapshot_start = on_receive_snapshot_start;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_snapshot_t&
client_subscriber::get_event_callback_on_receive_snapshot_start(
    const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_receive_snapshot_start;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_finished(
    event_callback_on_receive_snapshot_t&& on_receive_snapshot_finished) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_snapshot_finished = std::move(on_receive_snapshot_finished);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_finished(
    const event_callback_on_receive_snapshot_t& on_receive_snapshot_finished) {
  if (!internal_data_->private_event_handler) {
    internal_data_->private_event_handler = client_subscriber::create_event_callback_set();
  }

  internal_data_->private_event_handler->on_receive_snapshot_finished = on_receive_snapshot_finished;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_snapshot_t&
client_subscriber::get_event_callback_on_receive_snapshot_finished() const noexcept {
  if (internal_data_->private_event_handler && internal_data_->private_event_handler->on_receive_snapshot_finished) {
    return internal_data_->private_event_handler->on_receive_snapshot_finished;
  }

  if (internal_data_->shared_event_handler) {
    return internal_data_->shared_event_handler->on_receive_snapshot_finished;
  }

  return get_default_event_callback_set().on_receive_snapshot_finished;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_finished(
    event_callback_set_t& event_callback_set, event_callback_on_receive_snapshot_t&& on_receive_snapshot_finished) {
  event_callback_set.on_receive_snapshot_finished = std::move(on_receive_snapshot_finished);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_finished(
    event_callback_set_t& event_callback_set,
    const event_callback_on_receive_snapshot_t& on_receive_snapshot_finished) {
  event_callback_set.on_receive_snapshot_finished = on_receive_snapshot_finished;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_snapshot_t&
client_subscriber::get_event_callback_on_receive_snapshot_finished(
    const event_callback_set_t& event_callback_set) noexcept {
  return event_callback_set.on_receive_snapshot_finished;
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::send_message(
    rpc::context& ctx, atfw::dtmq::DChannelMessageDetail&& detail,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr, bool auto_create_channel,
    bool no_wait) {
  rpc::context::message_holder<atfw::dtmq::channel_subscriber> subscriber_info_holder{ctx};
  protobuf_copy_message(*subscriber_info_holder, internal_data_->shared_instance->get_subscriber_info());

  // 这里需要重新设置subscriber_key，不能用共享的subscriber_info里的subscriber_key，因为共享的subscriber_info里的subscriber_key可能是空的
  subscriber_info_holder->set_subscriber_key(get_subscriber_key());

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(
      ctx, std::move(*subscriber_info_holder), internal_data_->shared_instance->get_channel_key(), std::move(detail),
      compare_and_maybe_reset_lock_ptr, compare_and_maybe_reset_lock_rsp_ptr, auto_create_channel, no_wait)));
}

DTMQ_PROXY_SDK_API bool client_subscriber::find_cached_message(
    rpc::context& /*ctx*/, int64_t sequence,
    atfw::util::nostd::function_ref<void(const atfw::dtmq::DChannelMessage&)> fn) const noexcept {
  if (internal_data_->shared_instance->is_ready()) {
    auto log_ptr = internal_data_->shared_instance->get_message_by_sequence(sequence);
    if (!log_ptr) {
      return false;
    }
    fn(*log_ptr);
    return true;
  }

  return false;
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::find_message(rpc::context& ctx, int64_t sequence,
                                                                         atfw::dtmq::DChannelMessage& msg) {
  // 优先从本地缓存拉取
  if (internal_data_->shared_instance->is_ready()) {
    auto log_ptr = internal_data_->shared_instance->get_message_by_sequence(sequence);
    if (!log_ptr) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_MESSAGE_NOT_FOUND);
    }
    protobuf_copy_message(msg, *log_ptr);

    RPC_RETURN_CODE(0);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      rpc::dtmq::find_message(ctx, internal_data_->shared_instance->get_channel_key(),
                              internal_data_->shared_instance->get_readonly_replicate_index(), sequence, msg)));
}

DTMQ_PROXY_SDK_API bool client_subscriber::query_cached_message(
    rpc::context& /*ctx*/, atfw::util::nostd::function_ref<bool(const atfw::dtmq::DChannelMessage&)> fn,
    query_options options) const noexcept {
  if (internal_data_->shared_instance->is_ready()) {
    return internal_data_->shared_instance->query_message(fn, options);
  }

  return false;
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::page_query_message(
    rpc::context& ctx, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs) {
  if (internal_data_->shared_instance->is_ready()) {
    client_subscriber::query_options options;
    options.max_count = page_info.page_size();
    options.start_sequence = page_info.page_start_sequence();

    bool has_more = internal_data_->shared_instance->query_message(
        [&msgs](const atfw::dtmq::DChannelMessage& msg) {
          protobuf_copy_message(*msgs.Add(), msg);
          return true;
        },
        options);
    page_info.set_page_more(has_more);
    RPC_RETURN_CODE(0);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      rpc::dtmq::page_query_message(ctx, internal_data_->shared_instance->get_channel_key(),
                                    internal_data_->shared_instance->get_readonly_replicate_index(), page_info, msgs)));
}

namespace {
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static int32_t internal_subscriber_manager_do_send_heartbeat(rpc::context& ctx) {
  auto& mgr = get_internal_subscriber_manager();

  if (mgr.pending_heartbeat_subscriber.empty()) {
    return 0;
  }

  // 已经在执行，等待下一轮
  if (!task_type_trait::empty(mgr.running_heartbeat_task) && !task_type_trait::is_exiting(mgr.running_heartbeat_task)) {
    return 0;
  }

  // 尽量在一个task里处理心跳发送，这样不用占用浪费task池占用
  auto invoke_result = rpc::async_invoke(
      ctx, "atframework.dtmq.internal_subscriber_manager.send_heartbeat",
      [](rpc::context& child_ctx) -> rpc::result_code_type {
        if (is_internal_subscriber_manager_destroyed()) {
          RPC_RETURN_CODE(0);
        }

        auto& inner_mgr = get_internal_subscriber_manager();
        int32_t ret = 0;
        do {
          if (inner_mgr.pending_heartbeat_subscriber.empty()) {
            break;
          }
          std::unordered_set<shared_subscriber*> pending_subscriber;

          std::unordered_map<send_subscribe_key_t, std::list<shared_subscriber::ptr_t>, send_subscribe_hash_t,
                             send_subscribe_equal_t>
              pending_subscriber_by_target_server_id;  // 在这里续期生命周期，阻止ABA问题
          pending_subscriber.swap(inner_mgr.pending_heartbeat_subscriber);

          std::unordered_set<dispatcher_await_options> waiter_options_set;
          std::unordered_map<uint64_t, atframework::SSMsg*> waiter_messages;
          waiter_options_set.reserve(pending_subscriber.size());
          waiter_messages.reserve(pending_subscriber.size());

          // subscriber_info_ 只和当前节点有关，所以可以合批发送
          for (auto* subscriber : pending_subscriber) {
            if (subscriber == nullptr) {
              continue;
            }

            // 无订阅client则不需要心跳，等待自然淘汰即可
            if (!subscriber->has_registered_client()) {
              continue;
            }

            auto target_server_id =
                rpc::dtmq::get_target_server_id(subscriber->get_channel_key(), rpc::dtmq::replicate_type::kReadonly,
                                                subscriber->get_readonly_replicate_index());
            if (0 == target_server_id) {
              FCTXLOGWARNING(
                  child_ctx,
                  "Failed to get target server id for subscriber: {}, channel: {} and ignore to send heartbeat",
                  subscriber->get_subscriber_info().subscriber_key(), subscriber->get_channel_key().channel_id());
              subscriber->schedule_retry_heartbeat(child_ctx);
              continue;
            }

            send_subscribe_key_t send_key{target_server_id, subscriber->get_option_with_private_data()};
            pending_subscriber_by_target_server_id[send_key].emplace_back(subscriber->shared_from_this());
          }
          for (const auto& kv : pending_subscriber_by_target_server_id) {
            atfw::dtmq::SSChannelSubscribeReq* req_body = child_ctx.create<atfw::dtmq::SSChannelSubscribeReq>();
            atfw::dtmq::SSChannelSubscribeRsp* rsp_body = child_ctx.create<atfw::dtmq::SSChannelSubscribeRsp>();
            if (req_body == nullptr || rsp_body == nullptr) {
              FCTXLOGERROR(child_ctx, "Failed to create request or response body for target server: {:#x}",
                           kv.first.target_server_id);
              continue;
            }

            for (const auto& subscriber : kv.second) {
              if (is_internal_subscriber_manager_destroyed()) {
                break;
              }
              // subscriber 在await之后可能已经被销毁了，所以要检查是否还在缓存里
              if (inner_mgr.cached_subscriber_by_raw_pointer.end() ==
                  inner_mgr.cached_subscriber_by_raw_pointer.find(subscriber.get())) {
                continue;
              }
              auto* heartbeat = req_body->add_heartbeat();
              if (heartbeat == nullptr) {
                FCTXLOGERROR(child_ctx, "Failed to add heartbeat to request body for subscriber: {}, channel: {}",
                             subscriber->get_subscriber_info().subscriber_key(),
                             subscriber->get_channel_key().channel_id());
                subscriber->schedule_retry_heartbeat(child_ctx);
                continue;
              }
              subscriber->update_last_heartbeat(subscriber->get_last_message_sequence(),
                                                subscriber->get_last_message_hash_code());

              if (!req_body->has_subscriber()) {
                protobuf_copy_message(*req_body->mutable_subscriber(), subscriber->get_subscriber_info());
                req_body->mutable_subscriber()->set_with_private_data(kv.first.with_private_data);
              }
              protobuf_copy_message(*heartbeat->mutable_channel_key(), subscriber->get_channel_key());
              // 订阅者参数填充
              heartbeat->set_last_sequence(subscriber->get_last_message_sequence());
              heartbeat->set_last_hash_code(subscriber->get_last_message_hash_code());
              heartbeat->set_auto_create_channel(subscriber->should_auto_create_channel());
              heartbeat->set_readonly_index(subscriber->get_readonly_replicate_index());
            }

            dispatcher_await_options one_waiter_options = dispatcher_make_default<dispatcher_await_options>();
            rpc::result_code_type::value_type send_result = RPC_AWAIT_CODE_RESULT(rpc::dtmq::subscribe(
                child_ctx, kv.first.target_server_id, *req_body, *rsp_body, false, &one_waiter_options));

            if (send_result >= 0 && one_waiter_options.sequence > 0) {
              waiter_messages[one_waiter_options.sequence] = child_ctx.create<atframework::SSMsg>();
              waiter_options_set.insert(one_waiter_options);
            } else {
              FCTXLOGERROR(child_ctx, "try to call rpc::dtmq::subscribe to {:#x} failed, res: {}({})",
                           kv.first.target_server_id, send_result, protobuf_mini_dumper_get_error_msg(send_result));

              // 失败了要计划重试,await之后要重新检查有效性
              if (!is_internal_subscriber_manager_destroyed()) {
                for (const auto& subscriber : kv.second) {
                  if (inner_mgr.cached_subscriber_by_raw_pointer.end() !=
                      inner_mgr.cached_subscriber_by_raw_pointer.find(subscriber.get())) {
                    subscriber->schedule_retry_heartbeat(child_ctx);
                  }
                }
              }
            }
          }

          // 等待回包
          rpc::result_code_type::value_type send_result =
              RPC_AWAIT_CODE_RESULT(rpc::wait(child_ctx, waiter_options_set, waiter_messages));
          if (send_result < 0) {
            FCTXLOGERROR(child_ctx, "try to call rpc::dtmq::subscribe for {} times and wait failed, res: {}({})",
                         waiter_options_set.size(), send_result, protobuf_mini_dumper_get_error_msg(send_result));

            // manager 已销毁则不用再重试
            if (!is_internal_subscriber_manager_destroyed()) {
              for (auto* subscriber : pending_subscriber) {
                if (inner_mgr.cached_subscriber_by_raw_pointer.end() !=
                    inner_mgr.cached_subscriber_by_raw_pointer.find(subscriber)) {
                  subscriber->schedule_retry_heartbeat(child_ctx);
                }
              }
            }
            RPC_RETURN_CODE(send_result);
          }

          // manager 已销毁则不用再处理回包
          if (is_internal_subscriber_manager_destroyed()) {
            break;
          }

          // 处理回包
          rpc::foreach_received_message<atfw::dtmq::SSChannelSubscribeRsp>(
              child_ctx, waiter_messages, "rpc::dtmq::subscribe",
              [&](const atfw::SSMsgHead& /*head*/, const atfw::dtmq::SSChannelSubscribeRsp& rsp_body) {
                FCTXLOGDEBUG(
                    child_ctx,
                    "rpc::dtmq::subscribe parse message {} successfuly with {} valid channel(s) and {} not found "
                    "channel(s)",
                    atfw::dtmq::SSChannelSubscribeRsp::descriptor()->full_name(), rsp_body.subscribe_node_size(),
                    rsp_body.not_found_channel_ids_size());

                for (int i = 0; i < rsp_body.subscribe_node_size(); ++i) {
                  const auto& channel_id = rsp_body.subscribe_node(i).channel_key().channel_id();
                  auto iter = inner_mgr.cached_subscriber_by_channel_id.find(channel_id);
                  if (iter == inner_mgr.cached_subscriber_by_channel_id.end() || !iter->second) {
                    FCTXLOGINFO(child_ctx,
                                "channel {} receive heartbeat response, but may be destroyed, ignore this response",
                                channel_id);
                    continue;
                  }

                  FCTXLOGDEBUG(child_ctx, "channel {} receive heartbeat response", channel_id);
                  // 检查如果不是一开始的发起请求的subscriber，则不处理回包，避免ABA问题
                  if (pending_subscriber.find(iter->second.get()) == pending_subscriber.end()) {
                    FCTXLOGINFO(child_ctx,
                                "channel {} receive heartbeat response, but not in the pending subscriber list, ignore "
                                "this response",
                                channel_id);
                    continue;
                  }
                  iter->second->receive_heartbeat_response(child_ctx);
                }

                for (int i = 0; i < rsp_body.not_found_channel_ids_size(); ++i) {
                  const auto& channel_id = rsp_body.not_found_channel_ids(i);
                  auto iter = inner_mgr.cached_subscriber_by_channel_id.find(channel_id);
                  if (iter == inner_mgr.cached_subscriber_by_channel_id.end() || !iter->second) {
                    FCTXLOGINFO(child_ctx,
                                "channel {} receive not found response, but may be destroyed, ignore this response",
                                channel_id);
                    continue;
                  }
                  FCTXLOGWARNING(child_ctx, "channel {} receive not found response", channel_id);

                  // 检查如果不是一开始的发起请求的subscriber，则不处理回包，避免ABA问题
                  if (pending_subscriber.find(iter->second.get()) == pending_subscriber.end()) {
                    FCTXLOGINFO(child_ctx,
                                "channel {} receive heartbeat response, but not in the pending subscriber list, ignore "
                                "this response",
                                channel_id);
                    continue;
                  }
                  // 虚拟删除事件通知，以便触发监听者的销毁回调
                  iter->second->set_destroyed(child_ctx, iter->second->get_last_message_sequence(),
                                              std::chrono::system_clock::now());
                }
              });
        } while (false);

        if (!is_internal_subscriber_manager_destroyed()) {
          if (task_type_trait::get_task_id(inner_mgr.running_heartbeat_task) == child_ctx.get_task_context().task_id) {
            task_type_trait::reset_task(inner_mgr.running_heartbeat_task);
          }
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "Failed to invoke async task for sending heartbeat, result: {}({})", *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    return *invoke_result.get_error();
  }

  if (!task_type_trait::empty(*invoke_result.get_success()) &&
      !task_type_trait::is_exiting(*invoke_result.get_success())) {
    mgr.running_heartbeat_task = std::move(*invoke_result.get_success());
  }
  FCTXLOGDEBUG(ctx, "Successfully invoked async task for sending heartbeat, subscriber count: {}",
               mgr.pending_heartbeat_subscriber.size());
  return 0;
}

static int32_t internal_subscriber_manager_do_send_unsubscribe(rpc::context& ctx) {
  auto& mgr = get_internal_subscriber_manager();

  if (mgr.pending_unsubscribe_subscriber.empty()) {
    return 0;
  }

  // 已经在执行，等待下一轮
  if (!task_type_trait::empty(mgr.running_unsubscribe_task) &&
      !task_type_trait::is_exiting(mgr.running_unsubscribe_task)) {
    return 0;
  }

  // 尽量在一个task里处理心跳发送，这样不用占用浪费task池占用
  auto invoke_result = rpc::async_invoke(
      ctx, "atframework.dtmq.internal_subscriber_manager.send_unsubscribe",
      [](rpc::context& child_ctx) -> rpc::result_code_type {
        if (is_internal_subscriber_manager_destroyed()) {
          RPC_RETURN_CODE(0);
        }

        auto& inner_mgr = get_internal_subscriber_manager();
        int32_t ret = 0;
        do {
          if (inner_mgr.pending_unsubscribe_subscriber.empty()) {
            break;
          }
          std::unordered_map<std::string, shared_subscriber::ptr_t> pending_unsubscribe_subscriber;

          std::unordered_map<send_subscribe_key_t, std::list<shared_subscriber::ptr_t>, send_subscribe_hash_t,
                             send_subscribe_equal_t>
              pending_unsubscribe_subscriber_by_target_server_id;  // 在这里续期生命周期，阻止ABA问题
          pending_unsubscribe_subscriber.swap(inner_mgr.pending_unsubscribe_subscriber);

          // subscriber_info_ 只和当前节点有关，所以可以合批发送
          for (auto& kv : pending_unsubscribe_subscriber) {
            if (!kv.second) {
              continue;
            }

            auto target_server_id =
                rpc::dtmq::get_target_server_id(kv.second->get_channel_key(), rpc::dtmq::replicate_type::kReadonly,
                                                kv.second->get_readonly_replicate_index());
            if (0 == target_server_id) {
              FCTXLOGWARNING(
                  child_ctx,
                  "Failed to get target server id for subscriber: {}, channel: {} and ignore to send unsubscribe",
                  kv.second->get_subscriber_info().subscriber_key(), kv.second->get_channel_key().channel_id());
              continue;
            }

            // 反订阅不需要管 with_private_data，所以不需要分组
            send_subscribe_key_t send_key{target_server_id, false};
            pending_unsubscribe_subscriber_by_target_server_id[send_key].emplace_back(kv.second);
          }

          for (const auto& kv : pending_unsubscribe_subscriber_by_target_server_id) {
            atfw::dtmq::SSChannelUnsubscribeReq* req_body = child_ctx.create<atfw::dtmq::SSChannelUnsubscribeReq>();
            google::protobuf::Empty* rsp_body = child_ctx.create<google::protobuf::Empty>();
            if (req_body == nullptr || rsp_body == nullptr) {
              FCTXLOGERROR(child_ctx, "Failed to create request or response body for target server: {:#x}",
                           kv.first.target_server_id);
              continue;
            }

            for (const auto& subscriber : kv.second) {
              if (is_internal_subscriber_manager_destroyed()) {
                break;
              }
              // 如果await流程后又有新的subscriber注册了，则不需要再发送unsubscribe请求
              if (inner_mgr.cached_subscriber_by_channel_id.end() !=
                  inner_mgr.cached_subscriber_by_channel_id.find(subscriber->get_channel_key().channel_id())) {
                continue;
              }
              if (!req_body->has_subscriber()) {
                protobuf_copy_message(*req_body->mutable_subscriber(), subscriber->get_subscriber_info());
                req_body->mutable_subscriber()->set_with_private_data(kv.first.with_private_data);
              }
              req_body->add_channel_id(subscriber->get_channel_key().channel_id());
            }

            rpc::result_code_type::value_type send_result = RPC_AWAIT_CODE_RESULT(
                rpc::dtmq::unsubscribe(child_ctx, kv.first.target_server_id, *req_body, *rsp_body, true));

            // 如果发送打日志即可，长期淘汰和容灾逻辑后面会自动反订阅
            if (send_result < 0) {
              FCTXLOGERROR(child_ctx, "try to call rpc::dtmq::unsubscribe to {:#x} failed, res: {}({})",
                           kv.first.target_server_id, send_result, protobuf_mini_dumper_get_error_msg(send_result));
            }
          }
        } while (false);

        if (!is_internal_subscriber_manager_destroyed()) {
          if (task_type_trait::get_task_id(inner_mgr.running_unsubscribe_task) ==
              child_ctx.get_task_context().task_id) {
            task_type_trait::reset_task(inner_mgr.running_unsubscribe_task);
          }
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "Failed to invoke async task for sending unsubscribe, result: {}({})", *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    return *invoke_result.get_error();
  }

  if (!task_type_trait::empty(*invoke_result.get_success()) &&
      !task_type_trait::is_exiting(*invoke_result.get_success())) {
    mgr.running_unsubscribe_task = std::move(*invoke_result.get_success());
  }

  FCTXLOGDEBUG(ctx, "Successfully invoked async task for sending unsubscribe, subscriber count: {}",
               mgr.pending_unsubscribe_subscriber.size());
  return 0;
}

static void internal_subscriber_manager_do_retry_heartbeat(rpc::context& /*ctx*/) {
  auto& mgr = get_internal_subscriber_manager();

  if (mgr.retry_heartbeat_subscriber.empty()) {
    return;
  }

  std::unordered_set<shared_subscriber*> retry_subscriber_set;
  retry_subscriber_set.swap(mgr.retry_heartbeat_subscriber);

  for (auto* subscriber : retry_subscriber_set) {
    if (subscriber == nullptr) {
      continue;
    }

    subscriber->setup_timer(shared_subscriber::timer_action_type::kRetryHeartbeat);
  }
}

shared_subscriber::ptr_t shared_subscriber::make_shared(const atfw::dtmq::DChannelIdKey& channel_key) {
  if (channel_key.channel_id().empty()) {
    return nullptr;
  }

  if (is_internal_subscriber_manager_destroyed()) {
    return nullptr;
  }

  auto& mgr = get_internal_subscriber_manager();
  auto iter = mgr.cached_subscriber_by_channel_id.find(channel_key.channel_id());
  if (iter != mgr.cached_subscriber_by_channel_id.end() && iter->second) {
    return iter->second;
  }

  auto channel_cfg = excel::get_dtmq_channel_configure(channel_key.channel_type());
  if (!channel_cfg) {
    FWLOGWARNING("Failed to get channel config for channel type: {}", channel_key.channel_type());
    return nullptr;
  }

  auto new_shared_subscriber = atfw::component::memory::stl::make_strong_rc<shared_subscriber>(channel_key);
  add_cached_shared_subscriber(new_shared_subscriber);
  return new_shared_subscriber;
}

void shared_subscriber::maybe_mutable_wal_client() {
  if (wal_client_) {
    return;
  }

  mq_client_subscriber_private_data_type private_data;
  private_data.subscriber = this;

  wal_client_ =
      mq_client_subscriber_wal_client_type::create(atfw::util::time::time_utility::now(), create_client_vtable(),
                                                   create_client_configure(configure_), std::move(private_data));
}

mq_client_subscriber_wal_client_type::configure_pointer shared_subscriber::create_client_configure(
    const atfw::dtmq::DChannelConfigure& configure) {
  mq_client_subscriber_wal_client_type::configure_pointer ret = mq_client_subscriber_wal_client_type::make_configure();
  if (!ret) {
    return ret;
  }

  ret->subscriber_heartbeat_interval =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_interval());
  ret->subscriber_heartbeat_retry_interval =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_retry_interval());

  ret->require_snapshot = true;

  // 以下不同类型的消息队列频道配置不一样
  ret->gc_expire_duration =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.gc_expire_duration());
  ret->gc_log_size = configure.gc_log_count();
  ret->max_log_size = configure.max_log_count();

  return ret;
}

mq_client_subscriber_wal_client_type::vtable_pointer shared_subscriber::create_client_vtable() {
  using wal_client_type = mq_client_subscriber_wal_client_type;
  using wal_object_type = wal_client_type::object_type;
  using snapshot_type = mq_client_subscriber_storage_type;
  using wal_result_code = atfw::util::distributed_system::wal_result_code;

  static wal_client_type::vtable_pointer ret;
  if (ret) {
    return ret;
  }

  ret = atfw::component::memory::stl::make_strong_rc<wal_client_type::vtable_type>();
  if (!ret) {
    return ret;
  }

  // 公共算法
  rpc::dtmq::setup_common_vtable<wal_object_type>(*ret);

  // ============ callbacks for wal_object ============
  ret->load = [](wal_object_type& wal, const wal_object_type::storage_type& snapshot,
                 wal_object_type::callback_param_type param) -> wal_result_code {
    shared_subscriber* subscriber = wal.get_private_data().subscriber;
    if (nullptr == subscriber) {
      param.result_code.get() = static_cast<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
      return wal_result_code::kInitlization;
    }

    int64_t last_removed_key = snapshot.channel_runtime().last_removed_sequence();
    if (nullptr != wal.get_last_removed_key()) {
      if (last_removed_key < *wal.get_last_removed_key()) {
        last_removed_key = *wal.get_last_removed_key();
      }
    }

    // Load logs
    std::vector<wal_object_type::log_pointer> storage;
    storage.reserve(static_cast<size_t>(snapshot.messages_size()));
    for (const auto& msg : snapshot.messages()) {
      if (wal.get_log_key_compare()(msg.sequence(), last_removed_key)) {
        continue;
      }

      auto log_ptr = atfw::component::memory::stl::make_strong_rc<wal_object_type::log_type>();
      if (!log_ptr) {
        param.result_code.get() = static_cast<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        return wal_result_code::kCallbackError;
      }

      protobuf_copy_message(*log_ptr, msg);
      storage.emplace_back(std::move(log_ptr));
    }

    // Sort
    std::sort(storage.begin(), storage.end(),
              [](const wal_object_type::log_pointer& l, const wal_object_type::log_pointer& r) {
                return l->sequence() < r->sequence();
              });
    wal.assign_logs(storage);

    subscriber->load_metadata(param.context, snapshot.channel_metadata());
    subscriber->load_runtime(param.context, snapshot.channel_runtime());
    subscriber->load_lock(param.context, snapshot.lock());

    if (last_removed_key > 0) {
      subscriber->compact(param.context, last_removed_key);
    }

    return wal_result_code::kOk;
  };

  ret->dump = [](const wal_object_type& /*wal*/, wal_object_type::storage_type& /*to*/,
                 wal_object_type::callback_param_type /*param*/) -> wal_result_code {
    // 给用户侧的Client端不需要dump，直接返回ok即可
    return wal_result_code::kOk;
  };

  ret->get_meta = [](const wal_object_type&,
                     const wal_object_type::log_type& log) -> wal_object_type::meta_result_type {
    atfw::util::distributed_system::wal_time_point timepoint = protobuf_to_system_clock(log.create_timepoint());
    return wal_object_type::meta_result_type::make_success(timepoint, log.sequence(), log.detail().command_case());
  };

  ret->set_meta = [](const wal_object_type&, wal_object_type::log_type& log, const wal_object_type::meta_type& meta) {
    // log.command_case = meta.action_case; // command_case will be created by mutable_*
    *log.mutable_create_timepoint() = protobuf_from_system_clock(meta.timepoint);
    log.set_sequence(meta.log_key);
  };

  ret->get_log_key = [](const wal_object_type&, const wal_object_type::log_type& log) -> wal_object_type::log_key_type {
    return log.sequence();
  };

  ret->allocate_log_key = [](wal_object_type&, const wal_object_type::log_type& log,
                             wal_object_type::callback_param_type) -> wal_object_type::log_key_result_type {
    // 由Client端分配sequence
    return wal_object_type::log_key_result_type::make_success(log.sequence());
  };

  ret->merge_log = [](const wal_object_type&, wal_object_type::callback_param_type, wal_object_type::log_type& to,
                      const wal_object_type::log_type& from) {
    // client端更新就好
    protobuf_copy_message(to, from);
  };

  // ============ callbacks for wal_client ============
  ret->on_receive_snapshot = [](wal_client_type& wal, const snapshot_type& snapshot_data,
                                wal_client_type::callback_param_type param) -> wal_result_code {
    return wal.load(snapshot_data, param);
  };

  ret->on_receive_subscribe_response = [](wal_client_type&, wal_client_type::callback_param_type) -> wal_result_code {
    // 接收到订阅回包的不需要做任何处理
    return wal_result_code::kOk;
  };

  ret->subscribe_request = [](wal_client_type& wal, wal_client_type::callback_param_type param) -> wal_result_code {
    // 标记需要发送心跳，在最后发数据时合并channel发送
    shared_subscriber* subscriber = wal.get_private_data().subscriber;
    if (nullptr == subscriber) {
      return wal_result_code::kOk;
    }

    // Client向 readonly或writable节点发送心跳
    subscriber->schedule_send_heartbeat(param.context);

    if (subscriber->check_flag(shared_subscriber::subscriber_flag::kRecentlyHeartbeatFailure)) {
      return wal_result_code::kCallbackError;
    }
    return wal_result_code::kOk;
  };

  // 事件回调
  mq_client_subscriber_delegate_helper::setup_delegate_actions(ret->log_action_delegate);

  // Allow default delegate to allow sync package
  ret->default_delegate.action = &mq_client_subscriber_delegate_helper::common_action;

  return ret;
}

void shared_subscriber::setup_timer(timer_action_type action, bool ignore_same_action) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  // 如果定时器类型不变，那么就不需要重新设置定时器了
  if ((timer_action_ == action && ignore_same_action) || check_flag(subscriber_flag::kDestroying)) {
    return;
  }

  atfw::util::time::time_utility::raw_time_t timeout_tp = std::chrono::system_clock::from_time_t(0);
  switch (action) {
    case timer_action_type::kSendHeartbeat:
      if (wal_client_) {
        timeout_tp = wal_client_->get_next_heartbeat_timepoint();
      } else {
        timeout_tp = atfw::util::time::time_utility::now() +
                     protobuf_to_chrono_duration<std::chrono::system_clock::duration>(configure_.heartbeat_interval());
      }
      break;
    case timer_action_type::kRetryHeartbeat:
      if (wal_client_) {
        timeout_tp = wal_client_->get_next_heartbeat_timepoint();
      } else {
        timeout_tp =
            atfw::util::time::time_utility::now() +
            protobuf_to_chrono_duration<std::chrono::system_clock::duration>(configure_.heartbeat_retry_interval());
      }
      break;
    case timer_action_type::kGc:
      // 这里指删除频道的间隔，不是log的过期时间
      timeout_tp =
          atfw::util::time::time_utility::now() +
          protobuf_to_chrono_duration<std::chrono::system_clock::duration>(configure_.shared_subscriber_gc_timeout());

      break;
    default:
      return;
  }

  time_t timeout_tick = chrono_to_timer_tick(timeout_tp);
  if ((timer_action_ == timer_action_type::kNone || timer_timeout_tick_ <= 0) || timeout_tick < timer_timeout_tick_) {
    remove_timer();

    auto& mgr = get_internal_subscriber_manager();

    // 确保添加定时器时一定已经初始化
    if (!mgr.timer_running) {
      mgr.timer_set.init(chrono_to_timer_tick(atfw::util::time::time_utility::now()));
      mgr.timer_running = true;
    }

    if (timeout_tick <= mgr.timer_set.get_last_tick()) {
      timeout_tick = mgr.timer_set.get_last_tick() + 1;
    }

    atfw::util::memory::weak_rc_ptr<shared_subscriber> self_weak = shared_from_this();
    auto fn = [self_weak](time_t tick_time, const mq_client_subscriber_timer_type::timer_t& /*timer*/) {
      if (self_weak.expired()) {
        return;
      }

      auto self = self_weak.lock();
      if (!self) {
        return;
      }

      auto current_action = self->timer_action_;
      self->timer_action_ = timer_action_type::kNone;
      self->timer_timeout_tick_ = 0;

      if (is_internal_subscriber_manager_destroyed()) {
        return;
      }

      auto& inner_mgr = get_internal_subscriber_manager();

      // Tick触发心跳和日志压缩
      if (inner_mgr.timer_set.get_private_data() != nullptr) {
        self->tick(*reinterpret_cast<rpc::context*>(inner_mgr.timer_set.get_private_data()));
      } else {
        self->tick(logic_server_get_current_tick_context());
      }

      if (current_action == timer_action_type::kGc && self->can_be_removed() && self->timer_gc_tick_ != 0 &&
          tick_time >= self->timer_gc_tick_) {
        remove_cached_shared_subscriber(self.get());
        return;
      }

      // 设置下一个定时器
      if (self->can_be_removed()) {
        self->setup_timer(timer_action_type::kGc);
      } else if (self->timer_action_ != timer_action_type::kRetryHeartbeat) {
        self->setup_timer(timer_action_type::kSendHeartbeat);
      }
    };
    int timer_result =
        mgr.timer_set.add_timer(timeout_tick - mgr.timer_set.get_last_tick(), std::move(fn), nullptr, &timer_watcher_);

    if (timer_result < 0) {
      FWLOGERROR("Failed to add timer for subscriber: {}, channel: {}, action: {}, result: {}",
                 get_subscriber_info().subscriber_key(), get_channel_key().channel_id(), static_cast<int>(action),
                 timer_result);

      timer_watcher_.reset();

      timeout_tp =
          atfw::util::time::time_utility::now() +
          protobuf_to_chrono_duration<std::chrono::system_clock::duration>(configure_.heartbeat_retry_interval());
      mgr.retry_setup_timer_list.emplace_back(timeout_tp, shared_from_this());
      return;
    }

    timer_timeout_tick_ = timeout_tick;
  }

  if (action == timer_action_type::kGc) {
    if (can_be_removed()) {
      timer_action_ = action;

      if (timer_gc_tick_ == 0) {
        timer_gc_tick_ = timeout_tick;
      }
    }
  } else {
    // 从 GC 状态切到非GC状态要确认注册的client还存在
    if (timer_action_ != timer_action_type::kGc || !can_be_removed()) {
      timer_action_ = action;
      timer_gc_tick_ = 0;
    }
  }
}

void shared_subscriber::remove_timer() {
  timer_action_ = timer_action_type::kNone;
  timer_timeout_tick_ = 0;

  if (timer_watcher_.expired()) {
    return;
  }

  auto timer_ptr = timer_watcher_.lock();
  timer_watcher_.reset();
  if (!timer_ptr) {
    return;
  }

  mq_client_subscriber_timer_type::remove_timer(*timer_ptr);
}

void shared_subscriber::load_metadata(rpc::context& ctx, const atfw::dtmq::DChannelMetadata& metadata) {
  if (metadata.destroy_timepoint().seconds() > 0) {
    destroy_timepoint_ = protobuf_to_system_clock(metadata.destroy_timepoint());
    destroy_sequence_ = metadata.destroy_sequence();
  } else {
    destroy_timepoint_ = std::chrono::system_clock::from_time_t(0);
    destroy_sequence_ = 0;
  }

  if (metadata.create_timepoint().seconds() > 0) {
    create_timepoint_ = protobuf_to_system_clock(metadata.create_timepoint());
    create_sequence_ = metadata.create_sequence();
  } else {
    create_timepoint_ = std::chrono::system_clock::from_time_t(0);
    create_sequence_ = 0;
  }

  update_custom_data(ctx, metadata.custom_data_sequence(), metadata.custom_data());
}

void shared_subscriber::load_runtime(rpc::context& ctx, const atfw::dtmq::DChannelRuntime& runtime) {
  update_private_data(ctx, runtime.private_data_sequence(), runtime.private_data());
}

void shared_subscriber::load_lock(rpc::context& ctx, const atfw::dtmq::DChannelOptimisticLock& lock) {
  update_optimistic_lock(ctx, lock);
}
}  // namespace

void shared_subscriber::register_client_subscriber(client_subscriber* client, const client_subscriber_option& options) {
  if (client == nullptr) {
    return;
  }

  if (check_flag(subscriber_flag::kLockRegisteredClient)) {
    lock_registered_client_pending_add_[client] = options;
    lock_registered_client_pending_remove_.erase(client);
    return;
  }

  if (options.auto_create_channel) {
    registered_client_auto_create_channel_.insert(client);
  } else {
    registered_client_no_create_channel_.insert(client);
  }

  bool no_private_data_before = registered_client_with_private_data_.empty();
  if (options.with_private_data) {
    registered_client_with_private_data_.insert(client);
  }

  // 最初的订阅client注册，要改定时器为心跳定时器，并且立即发送一次心跳
  if (timer_action_ == timer_action_type::kGc || timer_action_ == timer_action_type::kNone) {
    setup_timer(timer_action_type::kSendHeartbeat);
  }

  size_t total_registered_clients =
      registered_client_auto_create_channel_.size() + registered_client_no_create_channel_.size();

  // 第一次追加auto_create_channel/with_private_data也要立即触发一次心跳来触发自动创建频道
  if (total_registered_clients == 1 ||
      (options.auto_create_channel && registered_client_auto_create_channel_.size() == 1) ||
      (options.with_private_data && no_private_data_before)) {
    auto& mgr = get_internal_subscriber_manager();
    mgr.pending_heartbeat_subscriber.insert(this);
  }
}

void shared_subscriber::unregister_client_subscriber(client_subscriber* client) {
  if (client == nullptr) {
    return;
  }

  if (check_flag(subscriber_flag::kLockRegisteredClient)) {
    lock_registered_client_pending_remove_.insert(client);
    lock_registered_client_pending_add_.erase(client);
    return;
  }

  bool has_remove_client = false;
  if (registered_client_auto_create_channel_.erase(client) > 0) {
    has_remove_client = true;
  }
  if (registered_client_no_create_channel_.erase(client) > 0) {
    has_remove_client = true;
  }
  registered_client_with_private_data_.erase(client);

  // 最后一个订阅client退出，要把心跳定时器改成缓存清理定时器
  if (has_remove_client && can_be_removed()) {
    setup_timer(timer_action_type::kGc);
  }
}

void shared_subscriber::foreach_registered_client_subscriber(
    atfw::util::nostd::function_ref<void(client_subscriber&)> callback) {
  if (registered_client_auto_create_channel_.empty() && registered_client_no_create_channel_.empty()) {
    return;
  }

  // 保持生命周期，确保在回调中不会导致整个实例被销毁
  auto hold_lifetime = shared_from_this();

  lock_registered_client_guard guard(*this);
  std::unordered_set<client_subscriber*>* subscriber_container[] = {&registered_client_auto_create_channel_,
                                                                    &registered_client_no_create_channel_};
  for (auto* container : subscriber_container) {
    for (const auto& client : *container) {
      if (client == nullptr) {
        continue;
      }

      // 回调中删除了，跳过迭代
      if (lock_registered_client_pending_remove_.find(client) != lock_registered_client_pending_remove_.end()) {
        continue;
      }

      if (client != nullptr) {
        callback(*client);
      }
    }
  }
}

int64_t shared_subscriber::get_compact_sequence() const noexcept {
  if (!wal_client_) {
    return 0;
  }

  const auto* last_removed_key = wal_client_->get_log_manager().get_last_removed_key();
  if (last_removed_key == nullptr) {
    return 0;
  }

  return *last_removed_key;
}

uint64_t shared_subscriber::get_last_message_hash_code() const noexcept { return last_message_hash_code_; }

int64_t shared_subscriber::get_last_message_sequence() const noexcept { return last_message_sequence_; }

int64_t shared_subscriber::get_last_removed_sequence() const noexcept {
  if (!wal_client_) {
    return 0;
  }

  const auto* last_removed_key = wal_client_->get_log_manager().get_last_removed_key();
  if (last_removed_key == nullptr) {
    return 0;
  }

  return *last_removed_key;
}

mq_client_subscriber_wal_client_type::log_const_pointer shared_subscriber::get_message_by_sequence(
    int64_t sequence) const noexcept {
  if (wal_client_ == nullptr) {
    return nullptr;
  }

  return wal_client_->find_log(sequence);
}

bool shared_subscriber::query_message(atfw::util::nostd::function_ref<bool(const atfw::dtmq::DChannelMessage&)> fn,
                                      const client_subscriber::query_options& option) {
  if (wal_client_ == nullptr) {
    return false;
  }

  auto begin_iter = option.start_sequence > 0 ? wal_client_->get_log_manager().log_lower_bound(option.start_sequence)
                                              : wal_client_->get_log_manager().log_begin();

  int64_t left_count = option.max_count > 0 ? option.max_count : std::numeric_limits<int64_t>::max();
  while (begin_iter != wal_client_->get_log_manager().log_end() && left_count > 0) {
    const auto& log = *begin_iter;
    ++begin_iter;
    if (!log) {
      continue;
    }

    if (option.end_sequence > 0 && log->sequence() >= option.end_sequence) {
      break;
    }

    if (!fn(*log)) {
      break;
    }
  }

  return begin_iter != wal_client_->get_log_manager().log_end();
}

int32_t shared_subscriber::tick(rpc::context& ctx) {
  // 如果有注册的client，需要创建wal_client_，否则无法触发发送tick
  if (!wal_client_ && has_registered_client()) {
    maybe_mutable_wal_client();
  }

  if (!wal_client_) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  int32_t result_code = 0;
  mq_client_subscriber_wal_object_context param{ctx, result_code};

  int64_t before_compact_sequence = get_compact_sequence();

  int32_t ret = static_cast<int32_t>(wal_client_->tick(atfw::util::time::time_utility::now(), param));
  if (ret < 0) {
    FWLOGERROR("shared_subscriber tick failed, ret: {}", ret);
    return ret;
  }

  int64_t after_compact_sequence = get_compact_sequence();

  // Trigger event callbacks for compact sequence change
  if (before_compact_sequence != after_compact_sequence && is_ready()) {
    foreach_registered_client_subscriber([&ctx, after_compact_sequence](client_subscriber& client) {
      const auto& fn = client.get_event_callback_on_compact();
      if (fn) {
        fn(ctx, client.shared_from_this(), after_compact_sequence);
      }
    });
  }

  return ret;
}

void shared_subscriber::update_last_heartbeat(int64_t log_sequence, uint64_t log_hash_code) noexcept {
  subscriber_info_.set_last_heartbeat_sequence(log_sequence);
  subscriber_info_.set_last_heartbeat_hash_code(log_hash_code);
  *subscriber_info_.mutable_last_heartbeat_timepoint() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now());
}

void shared_subscriber::receive_heartbeat_response(rpc::context& ctx) {
  if (!wal_client_) {
    return;
  }

  int32_t result_code = 0;
  mq_client_subscriber_wal_object_context param{ctx, result_code};

  auto wal_result = wal_client_->receive_subscribe_response(param);
  if (wal_result < atfw::util::distributed_system::wal_result_code::kOk) {
    FCTXLOGERROR(ctx, "shared_subscriber receive_heartbeat_response failed, ret: {}, {}({})",
                 static_cast<int32_t>(wal_result), result_code, protobuf_mini_dumper_get_error_msg(result_code));
    return;
  }

  if (result_code < 0) {
    FCTXLOGERROR(ctx, "shared_subscriber receive_heartbeat_response failed, ret: {}", result_code);
    return;
  }

  set_flag(subscriber_flag::kRecentlyHeartbeatFailure, false);
}

void shared_subscriber::receive_event_sync(rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& event_sync) {
  // 禁止重入
  if (check_flag(subscriber_flag::kInCallbackReceiveEvent)) {
    return;
  }
  set_flag(subscriber_flag::kInCallbackReceiveEvent, true);
  auto reset_flag_guard = gsl::finally([this]() { set_flag(subscriber_flag::kInCallbackReceiveEvent, false); });

  // Ignore events if the subscriber is not ready and the event is not a snapshot
  int64_t start_sequence = 0;
  if (!is_ready() && !event_sync.has_channel_snapshot()) {
    // 如果未就绪，且无重新创建事件，直接忽略
    for (int i = event_sync.channel_message_size() - 1; i >= 0; --i) {
      const auto& msg = event_sync.channel_message(i);
      if (msg.detail().has_create()) {
        start_sequence = msg.sequence();
        break;
      }
    }
    if (start_sequence == 0) {
      FCTXLOGINFO(ctx, "shared_subscriber is not ready and no create event found, ignoring event sync");
      return;
    }
  }

  if (event_sync.has_channel_snapshot()) {
    load_snapshot(ctx, event_sync.channel_snapshot());
    return;
  }

  maybe_mutable_wal_client();

  int64_t before_compact_sequence = get_compact_sequence();

  int64_t update_custom_data_sequence = 0;
  if (event_sync.channel_metadata().has_custom_data()) {
    update_custom_data_sequence = event_sync.channel_metadata().custom_data_sequence();
  }
  int64_t update_private_data_sequence = 0;
  if (event_sync.has_channel_runtime() && event_sync.channel_runtime().has_private_data()) {
    update_private_data_sequence = event_sync.channel_runtime().private_data_sequence();
  }

  // 增量消息
  int32_t result_code = 0;
  mq_client_subscriber_wal_object_context param{ctx, result_code};
  int64_t failure_log_sequence = 0;
  for (const auto& log_msg : event_sync.channel_message()) {
    if (log_msg.sequence() < start_sequence) {
      continue;
    }

    if (wal_client_) {
      auto log_ptr = wal_client_->get_log_manager().allocate_log(protobuf_to_system_clock(log_msg.create_timepoint()),
                                                                 log_msg.detail().command_case(), param, log_msg);
      if (log_ptr) {
        result_code = 0;
        auto log_result = wal_client_->receive_log(param, std::move(log_ptr));

        if (log_result == atfw::util::distributed_system::wal_result_code::kIgnore) {
          continue;
        }

        if (log_result < atfw::util::distributed_system::wal_result_code::kOk) {
          if (log_result == atfw::util::distributed_system::wal_result_code::kClientRequireSnapshot) {
            FCTXLOGINFO(ctx, "Required snapshot first and failed to emplace log for sequence: {}, ignore rest logs",
                        log_msg.sequence());
          } else {
            FCTXLOGERROR(ctx, "Failed to emplace log for sequence: {}, result: {}, {}({})", log_msg.sequence(),
                         static_cast<int32_t>(log_result), result_code,
                         protobuf_mini_dumper_get_error_msg(result_code));
          }

          failure_log_sequence = log_msg.sequence();
          break;
        }

        if (result_code < 0) {
          FCTXLOGERROR(ctx, "Failed to emplace log for sequence: {}, result: {}({})", log_msg.sequence(), result_code,
                       protobuf_mini_dumper_get_error_msg(result_code));
          failure_log_sequence = log_msg.sequence();
          break;
        }

        if (last_message_sequence_ < log_msg.sequence()) {
          last_message_sequence_ = log_msg.sequence();
          last_message_hash_code_ = log_msg.hash_code();
        }
      } else {
        FCTXLOGERROR(ctx, "Failed to allocate log for sequence: {}", log_msg.sequence());
        failure_log_sequence = log_msg.sequence();
        break;
      }
    }

    // 保持事件顺序
    if (update_custom_data_sequence > 0 && update_custom_data_sequence <= log_msg.sequence()) {
      update_custom_data(ctx, update_custom_data_sequence, event_sync.channel_metadata().custom_data());
      update_custom_data_sequence = 0;
    }

    if (update_private_data_sequence > 0 && update_private_data_sequence <= log_msg.sequence()) {
      update_private_data(ctx, update_private_data_sequence, event_sync.channel_runtime().private_data());
      update_private_data_sequence = 0;
    }
  }

  // 如果有错误数据立即触发心跳，触发server端下发快照覆盖错误数据
  if (failure_log_sequence != 0 && !is_internal_subscriber_manager_destroyed()) {
    get_internal_subscriber_manager().pending_heartbeat_subscriber.insert(this);
  }

  // 所有的消息都落后，也要补充触发 update_custom_data/update_private_data
  if (failure_log_sequence == 0 && update_custom_data_sequence > 0) {
    update_custom_data(ctx, update_custom_data_sequence, event_sync.channel_metadata().custom_data());
  }

  if (failure_log_sequence == 0 && update_private_data_sequence > 0) {
    update_private_data(ctx, update_private_data_sequence, event_sync.channel_runtime().private_data());
  }

  // 同步GC边界
  if (event_sync.channel_runtime().last_removed_sequence() > failure_log_sequence) {
    compact(ctx, event_sync.channel_runtime().last_removed_sequence());
  }

  // 压缩日志事件
  int64_t after_compact_sequence = get_compact_sequence();
  if (after_compact_sequence != before_compact_sequence && is_ready()) {
    foreach_registered_client_subscriber([&ctx, after_compact_sequence](client_subscriber& client) {
      const auto& fn = client.get_event_callback_on_compact();
      if (fn) {
        fn(ctx, client.shared_from_this(), after_compact_sequence);
      }
    });
  }
}

void shared_subscriber::load_snapshot(rpc::context& ctx, const atfw::dtmq::DChannelSnapshot& snapshot) {
  // 禁止重入
  if (check_flag(subscriber_flag::kInCallbackLoadSnapshot)) {
    return;
  }
  set_flag(subscriber_flag::kInCallbackLoadSnapshot, true);
  auto reset_flag_guard = gsl::finally([this]() { set_flag(subscriber_flag::kInCallbackLoadSnapshot, false); });

  // 要考虑异常情况快照回退，保证最终一致性
  if (snapshot.channel_metadata().has_channel_configure()) {
    reload_configure(snapshot.channel_metadata().channel_configure());
  }

  maybe_mutable_wal_client();
  if (!wal_client_) {
    FCTXLOGERROR(ctx, "Failed to load snapshot, wal_client_ malloc failed");
    return;
  }

  foreach_registered_client_subscriber([&ctx, &snapshot](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_receive_snapshot_start();
    if (fn) {
      fn(ctx, client.shared_from_this(), snapshot, 0);
    }
  });

  int32_t result_code = 0;
  do {
    mq_client_subscriber_wal_object_context param{ctx, result_code};
    if (wal_client_) {
      auto log_result = wal_client_->receive_snapshot(snapshot, param);
      if (log_result < atfw::util::distributed_system::wal_result_code::kOk) {
        FCTXLOGERROR(ctx, "Failed to load snapshot, result: {}, {}({})", static_cast<int32_t>(log_result), result_code,
                     protobuf_mini_dumper_get_error_msg(result_code));
        if (result_code >= 0) {
          result_code = PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
        }
        break;
      }

      if (result_code < 0) {
        FCTXLOGERROR(ctx, "Failed to load snapshot, result: {}({})", result_code,
                     protobuf_mini_dumper_get_error_msg(result_code));
        break;
      }
    }

    last_message_hash_code_ = snapshot.channel_metadata().last_hash_code();
    last_message_sequence_ = snapshot.channel_metadata().last_sequence();

    // 如果是已销毁或者尚未创建的频道，保持订阅缓存。但不能ready
    if (create_sequence_ > 0 && destroy_sequence_ < create_sequence_) {
      set_ready(ctx);
    } else if (destroy_sequence_ > 0 && destroy_sequence_ >= create_sequence_ && is_ready()) {
      set_destroyed(ctx, destroy_sequence_, destroy_timepoint_);
    }
  } while (false);

  foreach_registered_client_subscriber([&ctx, &snapshot, result_code](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_receive_snapshot_finished();
    if (fn) {
      fn(ctx, client.shared_from_this(), snapshot, result_code);
    }
  });

  if (result_code >= 0 && is_ready()) {
    // 刷新定时器,如果服务器下发的心跳间隔更短，则要缩短下一次心跳定时器间隔
    // 加载快照后配置可能发生变化，所以要不能忽略同action
    setup_timer(timer_action_type::kSendHeartbeat, false);
  }
}

void shared_subscriber::reload_configure(const atfw::dtmq::DChannelConfigure& config) {
  if (&configure_ != &config) {
    protobuf_copy_message(configure_, config);
    excel::normalize_dtmq_channel_configure(configure_);
  }

  // 确保配置有效

  if (wal_client_) {
    auto& wal_obj_conf = wal_client_->get_log_manager().get_configure();

    wal_obj_conf.gc_expire_duration =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure_.gc_expire_duration());
    wal_obj_conf.gc_log_size = configure_.gc_log_count();
    wal_obj_conf.max_log_size = configure_.max_log_count();

    auto& client_conf = wal_client_->get_configure();
    client_conf.gc_expire_duration = wal_obj_conf.gc_expire_duration;
    client_conf.gc_log_size = wal_obj_conf.gc_log_size;
    client_conf.max_log_size = wal_obj_conf.max_log_size;

    client_conf.subscriber_heartbeat_interval =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure_.heartbeat_interval());
    client_conf.subscriber_heartbeat_retry_interval =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
            configure_.heartbeat_retry_interval());
  }
}

void shared_subscriber::set_ready(rpc::context& ctx) {
  if (is_ready()) {
    return;
  }
  set_flag(subscriber_flag::kReady, true);

  foreach_registered_client_subscriber([&ctx](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_ready();
    if (fn) {
      fn(ctx, client.shared_from_this());
    }
  });
}

void shared_subscriber::set_destroyed(rpc::context& ctx, int64_t log_sequence,
                                      std::chrono::system_clock::time_point destroy_time) {
  // 合并销毁事件，取最新的。传入的 log_sequence
  // 有可能是本地占位生成的，如果后续收到同sequence的真实销毁事件，destroy_time 也要更新
  if (log_sequence >= destroy_sequence_) {
    destroy_sequence_ = log_sequence;
    destroy_timepoint_ = destroy_time;
  }

  if (!is_ready()) {
    return;
  }
  set_flag(subscriber_flag::kReady, false);

  foreach_registered_client_subscriber([&ctx, this](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_destroyed();
    if (fn) {
      fn(ctx, client.shared_from_this(), destroy_sequence_, destroy_timepoint_);
    }
  });
}

void shared_subscriber::update_custom_data(rpc::context& ctx, int64_t sequence,
                                           const google::protobuf::Any& custom_data) {
  if (custom_data_sequence_ >= sequence) {
    return;
  }

  custom_data_sequence_ = sequence;
  protobuf_copy_message(custom_data_, custom_data);

  if (!is_ready()) {
    return;
  }
  foreach_registered_client_subscriber([&ctx, this](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_update_custom_data();
    if (fn) {
      fn(ctx, client.shared_from_this(), custom_data_sequence_, custom_data_);
    }
  });
}

void shared_subscriber::update_private_data(rpc::context& ctx, int64_t sequence,
                                            const google::protobuf::Any& private_data) {
  if (private_data_sequence_ >= sequence) {
    return;
  }

  private_data_sequence_ = sequence;
  protobuf_copy_message(private_data_, private_data);

  if (!is_ready()) {
    return;
  }
  foreach_registered_client_subscriber([&ctx, this](client_subscriber& client) {
    if (!client.get_option_with_private_data()) {
      return;
    }

    const auto& fn = client.get_event_callback_on_update_private_data();
    if (fn) {
      fn(ctx, client.shared_from_this(), private_data_sequence_, private_data_);
    }
  });
}

void shared_subscriber::update_optimistic_lock(rpc::context& ctx, const ::atfw::dtmq::DChannelOptimisticLock& to) {
  if (lock_.lock_holder() == to.lock_holder() &&
      protobuf_to_system_clock(lock_.timeout()) == protobuf_to_system_clock(to.timeout())) {
    return;
  }

  ::atfw::dtmq::DChannelOptimisticLock from;
  lock_.Swap(&from);
  protobuf_copy_message(lock_, to);

  if (!is_ready()) {
    return;
  }
  foreach_registered_client_subscriber([&ctx, &from, this](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_update_optimistic_lock();
    if (fn) {
      fn(ctx, client.shared_from_this(), from, lock_);
    }
  });
}

void shared_subscriber::compact(rpc::context& /*ctx*/, int64_t compact_sequence) {
  if (compact_sequence <= get_compact_sequence()) {
    return;
  }

  if (!wal_client_) {
    return;
  }

  if (wal_client_->get_log_manager().get_all_logs().empty()) {
    return;
  }

  if (!wal_client_->get_log_manager().get_log_key_compare()(
          (*wal_client_->get_log_manager().get_all_logs().begin())->sequence(), compact_sequence)) {
    return;
  }

  // 同步GC边界
  size_t remove_count = 0;
  for (const auto& log : wal_client_->get_log_manager().get_all_logs()) {
    if (!log) {
      continue;
    }

    if (wal_client_->get_log_manager().get_log_key_compare()(log->sequence(), compact_sequence)) {
      ++remove_count;
    } else {
      break;
    }
  }

  if (remove_count > 0) {
    wal_client_->get_log_manager().remove_before(atfw::util::time::time_utility::now(), remove_count);
  }
}

void shared_subscriber::schedule_send_heartbeat(rpc::context& /*ctx*/) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  // 计划发心跳包

  auto& mgr = get_internal_subscriber_manager();

  // 不受管理的对象要跳过
  if (mgr.cached_subscriber_by_raw_pointer.end() == mgr.cached_subscriber_by_raw_pointer.find(this)) {
    return;
  }

  mgr.pending_heartbeat_subscriber.insert(this);
}

void shared_subscriber::schedule_retry_heartbeat(rpc::context& /*ctx*/) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  // 计划发心跳包

  auto& mgr = get_internal_subscriber_manager();

  // 不受管理的对象要跳过
  if (mgr.cached_subscriber_by_raw_pointer.end() == mgr.cached_subscriber_by_raw_pointer.find(this)) {
    return;
  }

  mgr.retry_heartbeat_subscriber.insert(this);

  set_flag(subscriber_flag::kRecentlyHeartbeatFailure, true);
}

void shared_subscriber::add_cached_shared_subscriber(const shared_subscriber::ptr_t& subscriber) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  if (!subscriber) {
    return;
  }

  auto& mgr = get_internal_subscriber_manager();
  mgr.cached_subscriber_by_channel_id[subscriber->get_channel_key().channel_id()] = subscriber;
  mgr.cached_subscriber_by_raw_pointer[subscriber.get()] = subscriber;
  mgr.pending_unsubscribe_subscriber.erase(subscriber->get_channel_key().channel_id());

  // 注册定时器执行订阅和垃圾回收
  subscriber->setup_timer(timer_action_type::kGc);
}

void shared_subscriber::remove_cached_shared_subscriber(const shared_subscriber* subscriber) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  if (subscriber == nullptr) {
    return;
  }

  auto& mgr = get_internal_subscriber_manager();

  auto iter = mgr.cached_subscriber_by_channel_id.find(subscriber->get_channel_key().channel_id());
  if (iter == mgr.cached_subscriber_by_channel_id.end()) {
    return;
  }

  if (iter->second.get() != subscriber) {
    return;
  }

  auto hold_lifetime = std::move(iter->second);

  mgr.cached_subscriber_by_channel_id.erase(subscriber->get_channel_key().channel_id());
  mgr.cached_subscriber_by_raw_pointer.erase(hold_lifetime.get());
  mgr.pending_unsubscribe_subscriber[subscriber->get_channel_key().channel_id()] = hold_lifetime;

  // 移除待执行任务
  mgr.pending_heartbeat_subscriber.erase(hold_lifetime.get());
  mgr.retry_heartbeat_subscriber.erase(hold_lifetime.get());

  // 移除定时器
  hold_lifetime->remove_timer();
}

void mq_client_subscriber_delegate_helper::setup_delegate_actions(wal_object_type::callback_log_group_map_t& actions) {
  // 走default_delegate即可
  actions[atfw::dtmq::DChannelMessageDetail::kDestroy].action = mq_client_subscriber_delegate_helper::destroy_channel;
  actions[atfw::dtmq::DChannelMessageDetail::kCreate].action = mq_client_subscriber_delegate_helper::create_channel;
  actions[atfw::dtmq::DChannelMessageDetail::kResetLock].action = mq_client_subscriber_delegate_helper::reset_lock;
  actions[atfw::dtmq::DChannelMessageDetail::kText].action = mq_client_subscriber_delegate_helper::receive_text;
  actions[atfw::dtmq::DChannelMessageDetail::kEvent].action = mq_client_subscriber_delegate_helper::receive_event;
}

mq_client_subscriber_delegate_helper::wal_result_code mq_client_subscriber_delegate_helper::destroy_channel(
    wal_object_type& wal, const wal_object_type::log_type& raw_message, wal_object_type::callback_param_type param) {
  shared_subscriber* subscriber = wal.get_private_data().subscriber;
  if (nullptr == subscriber) {
    return wal_result_code::kInitlization;
  }

  common_action(wal, raw_message, param);

  if (subscriber->is_ready() && raw_message.sequence() >= subscriber->create_sequence_) {
    subscriber->set_destroyed(param.context, raw_message.sequence(),
                              protobuf_to_system_clock(raw_message.detail().destroy().removed_timepoint()));
  } else {
    if (raw_message.sequence() > subscriber->destroy_sequence_) {
      subscriber->destroy_sequence_ = raw_message.sequence();
      subscriber->destroy_timepoint_ = protobuf_to_system_clock(raw_message.detail().destroy().removed_timepoint());
    }
  }

  // 频道销毁也要销毁乐观锁,重置视为所有数据清空，此时不需要重复投递锁重置事件
  if (!subscriber->is_ready()) {
    atfw::dtmq::DChannelOptimisticLock empty_lock;
    subscriber->load_lock(param.context, empty_lock);
  }
  return wal_result_code::kOk;
}

mq_client_subscriber_delegate_helper::wal_result_code mq_client_subscriber_delegate_helper::create_channel(
    wal_object_type& wal, const wal_object_type::log_type& raw_message, wal_object_type::callback_param_type param) {
  shared_subscriber* subscriber = wal.get_private_data().subscriber;
  if (nullptr == subscriber) {
    return wal_result_code::kInitlization;
  }

  common_action(wal, raw_message, param);

  if (raw_message.sequence() > subscriber->create_sequence_) {
    subscriber->create_timepoint_ = protobuf_to_system_clock(raw_message.detail().create().create_timepoint());
    subscriber->create_sequence_ = raw_message.sequence();
  }
  if (!subscriber->is_ready() && raw_message.sequence() > subscriber->destroy_sequence_) {
    subscriber->set_ready(param.context);
  }
  return wal_result_code::kOk;
}

mq_client_subscriber_delegate_helper::wal_result_code mq_client_subscriber_delegate_helper::reset_lock(
    wal_object_type& wal, const wal_object_type::log_type& raw_message, wal_object_type::callback_param_type param) {
  shared_subscriber* subscriber = wal.get_private_data().subscriber;
  if (nullptr == subscriber) {
    return wal_result_code::kInitlization;
  }

  common_action(wal, raw_message, param);

  // ready 前不用处理action消息,应该在 ready 事件中通过快照数据处理
  if (!subscriber->is_ready()) {
    return wal_result_code::kOk;
  }
  subscriber->update_optimistic_lock(param.context, raw_message.detail().reset_lock());

  return wal_result_code::kOk;
}

mq_client_subscriber_delegate_helper::wal_result_code mq_client_subscriber_delegate_helper::receive_text(
    wal_object_type& wal, const wal_object_type::log_type& raw_message, wal_object_type::callback_param_type param) {
  shared_subscriber* subscriber = wal.get_private_data().subscriber;
  if (nullptr == subscriber) {
    return wal_result_code::kInitlization;
  }

  common_action(wal, raw_message, param);

  // ready 前不用处理action消息,应该在 ready 事件中通过快照数据处理
  if (!subscriber->is_ready()) {
    return wal_result_code::kOk;
  }

  subscriber->foreach_registered_client_subscriber([&raw_message, &param](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_receive_text();
    if (fn) {
      fn(param.context, client.shared_from_this(), raw_message);
    }
  });

  return wal_result_code::kOk;
}

mq_client_subscriber_delegate_helper::wal_result_code mq_client_subscriber_delegate_helper::receive_event(
    wal_object_type& wal, const wal_object_type::log_type& raw_message, wal_object_type::callback_param_type param) {
  shared_subscriber* subscriber = wal.get_private_data().subscriber;
  if (nullptr == subscriber) {
    return wal_result_code::kInitlization;
  }

  common_action(wal, raw_message, param);

  // ready 前不用处理action消息,应该在 ready 事件中通过快照数据处理
  if (!subscriber->is_ready()) {
    return wal_result_code::kOk;
  }

  subscriber->foreach_registered_client_subscriber([&raw_message, &param](client_subscriber& client) {
    const auto& any_event_fn = client.get_event_callback_on_receive_event();
    if (any_event_fn) {
      any_event_fn(param.context, client.shared_from_this(), raw_message);
    }
    if (!raw_message.detail().event().type_url().empty()) {
      const auto& one_event_fn =
          client.get_event_callback_on_receive_event_by_type_url(raw_message.detail().event().type_url());
      if (one_event_fn) {
        one_event_fn(param.context, client.shared_from_this(), raw_message);
      }
    }
  });

  return wal_result_code::kOk;
}

mq_client_subscriber_delegate_helper::wal_result_code mq_client_subscriber_delegate_helper::common_action(
    wal_object_type& wal, const wal_object_type::log_type& raw_message, wal_object_type::callback_param_type param) {
  shared_subscriber* subscriber = wal.get_private_data().subscriber;
  if (nullptr == subscriber) {
    return wal_result_code::kInitlization;
  }

  // ready 前不用处理原始消息,应该在 ready 事件中通过快照数据处理
  if (!subscriber->is_ready()) {
    return wal_result_code::kOk;
  }

  subscriber->foreach_registered_client_subscriber([&raw_message, &param](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_receive_raw_message();
    if (fn) {
      fn(param.context, client.shared_from_this(), raw_message);
    }
  });
  return wal_result_code::kOk;
}

}  // namespace dtmq
}  // namespace rpc

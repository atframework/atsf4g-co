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

#include <utility/random_engine.h>

#include <dispatcher/task_type_traits.h>
#include <rpc/dtmq/dtmqproxysvrservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_utils.h>

#include <config/excel/config_easy_api.h>
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
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rpc/dtmq/dtmq_client_api.h"
#include "rpc/rpc_common_types.h"

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
  atfw::dtmq::DChannelMessageDetail::CommandCase operator()(const atfw::dtmq::DChannelMessage&) noexcept;
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

class ATFW_UTIL_SYMBOL_LOCAL shared_subscriber : public std::enable_shared_from_this<shared_subscriber> {
 public:
  using ptr_t = std::shared_ptr<shared_subscriber>;

  // NOLINTNEXTLINE(readability-enum-initial-value)
  enum class subscriber_flag : uint32_t {
    kUninitialized = 0,
    kReady = 1,
    kLockRegisteredClient = 2,
    kDestroying = 3,
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
      : timer_action_(timer_action_type::kNone),
        timer_timeout_tick_(0),
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
        snapshot_sequence_(0),
        last_message_hash_code_(0),
        last_message_sequence_(0) {
    subscriber_info_.set_subscriber_server_id(logic_config::me()->get_local_server_id());

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

  void setup_timer(timer_action_type action);

  void remove_timer();

  void load_metadata(rpc::context& ctx, const atfw::dtmq::DChannelMetadata& metadata);
  void load_runtime(rpc::context& ctx, const atfw::dtmq::DChannelRuntime& runtime);
  void load_lock(rpc::context& ctx, const atfw::dtmq::DChannelOptimisticLock& lock);

  inline bool check_flag(subscriber_flag flag) const noexcept { return flags_.test(static_cast<size_t>(flag)); }

  inline void set_flag(subscriber_flag flag, bool value) noexcept { flags_.set(static_cast<size_t>(flag), value); }

  inline bool is_ready() const noexcept { return check_flag(subscriber_flag::kReady); }

  inline const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept { return channel_key_; }

  inline const atfw::dtmq::channel_subscriber& get_subscriber_info() const noexcept { return subscriber_info_; }

  inline const atfw::dtmq::DChannelConfigure& get_configure() const noexcept { return configure_; }

  inline const atfw::dtmq::DChannelOptimisticLock& get_lock() const noexcept { return lock_; }

  inline uint64_t get_readonly_replicate_index() const noexcept { return readonly_replicate_index_; }

  inline bool has_registered_client() const noexcept {
    return !registered_client_auto_create_channel_.empty() || !registered_client_no_create_channel_.empty() ||
           !lock_registered_client_pending_add_.empty();
  }

  inline bool should_auto_create_channel() const noexcept { return !registered_client_auto_create_channel_.empty(); }

  inline bool can_be_removed() const noexcept {
    return registered_client_auto_create_channel_.empty() && registered_client_no_create_channel_.empty() &&
           lock_registered_client_pending_add_.empty();
  }

  void register_client_subscriber(client_subscriber* client, bool auto_create_channel);
  void unregister_client_subscriber(client_subscriber* client);
  void foreach_registered_client_subscriber(atfw::util::nostd::function_ref<void(client_subscriber&)> callback);

  int64_t get_compact_sequence() const noexcept;

  uint64_t get_last_message_hash_code() const noexcept;
  int64_t get_last_message_sequence() const noexcept;

  int32_t tick(rpc::context& ctx);

  void update_last_heartbeat(int64_t log_sequence, uint64_t log_hash_code) noexcept;

  void receive_heartbeat_response(rpc::context& ctx);

  void receive_event_sync(rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& event_sync);

  void load_snapshot(rpc::context& ctx, const atfw::dtmq::DChannelSnapshot& snapshot);

  void reload_configure(const atfw::dtmq::DChannelConfigure& config);

  void set_ready(rpc::context& ctx);

  void set_destroyed(rpc::context& ctx, int64_t log_sequence, std::chrono::system_clock::time_point destroy_time);

  void update_custom_data(rpc::context& ctx, int64_t sequence, const google::protobuf::Any& custom_data);

  void update_private_data(rpc::context& ctx, int64_t sequence, const google::protobuf::Any& custom_data);

  void update_optimistic_lock(rpc::context& ctx, const ::atfw::dtmq::DChannelOptimisticLock& to);

  void compact(rpc::context& ctx, int64_t compact_sequence);

  void schedule_send_heartbeat(rpc::context& ctx);

  void schedule_retry_heartbeat(rpc::context& ctx);

  static void add_cached_shared_subscriber(const shared_subscriber::ptr_t& subscriber);

  static void remove_cached_shared_subscriber(const shared_subscriber* subscriber);

 private:
  friend class ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_delegate_helper;

  std::bitset<static_cast<size_t>(subscriber_flag::kMax)> flags_;
  mq_client_subscriber_timer_type::timer_wptr_t timer_watcher_;
  timer_action_type timer_action_;
  time_t timer_timeout_tick_;

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
  int64_t snapshot_sequence_;

  uint64_t last_message_hash_code_;
  int64_t last_message_sequence_;

  std::unordered_set<client_subscriber*> registered_client_auto_create_channel_;
  std::unordered_set<client_subscriber*> registered_client_no_create_channel_;

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
      if (subscriber_ != nullptr) {
        subscriber_->set_flag(subscriber_flag::kLockRegisteredClient, false);

        for (const auto& client : subscriber_->lock_registered_client_pending_add_) {
          if (client.first != nullptr) {
            if (client.second) {
              subscriber_->registered_client_auto_create_channel_.insert(client.first);
            } else {
              subscriber_->registered_client_no_create_channel_.insert(client.first);
            }
          }
        }
        subscriber_->lock_registered_client_pending_add_.clear();

        for (const auto& client : subscriber_->lock_registered_client_pending_remove_) {
          if (client != nullptr) {
            subscriber_->registered_client_auto_create_channel_.erase(client);
            subscriber_->registered_client_no_create_channel_.erase(client);
          }
        }
        subscriber_->lock_registered_client_pending_remove_.clear();
      }
    }
  };

  std::unordered_map<client_subscriber*, bool> lock_registered_client_pending_add_;
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
  mq_client_subscriber_timer_type timer_set;

  std::unordered_map<std::string, shared_subscriber::ptr_t> cached_subscriber_smart_ptr;
  std::unordered_set<shared_subscriber*> cached_subscriber_raw_pointer;

  std::unordered_set<shared_subscriber*> pending_heartbeat_subscriber;
  std::unordered_set<shared_subscriber*> retry_heartbeat_subscriber;
  task_type_trait::task_type running_heartbeat_task;

  ~internal_subscriber_manager() { is_internal_subscriber_manager_destroyed() = true; }
};

static internal_subscriber_manager& get_internal_subscriber_manager() {
  static internal_subscriber_manager ret;
  return ret;
}

static void internal_subscriber_manager_do_send_heartbeat(rpc::context& ctx);
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
  client_subscriber::event_callback_on_receive_raw_message_t on_receive_raw_message;
  client_subscriber::event_callback_on_receive_snapshot_t on_receive_snapshot_start;
  client_subscriber::event_callback_on_receive_snapshot_t on_receive_snapshot_finished;
};

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::subscriber_options(std::string&& input_subscriber_key)
    : subscriber_key(std::move(input_subscriber_key)), auto_create_channel(true) {}

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::subscriber_options(const std::string& input_subscriber_key)
    : subscriber_key(input_subscriber_key), auto_create_channel(true) {}

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::~subscriber_options() {}

struct client_subscriber::ctor_guard {
  std::string subscriber_key;
  shared_subscriber::ptr_t shared_subscriber;
  client_subscriber::event_callback_set_ptr_t event_handler;
  bool auto_create_channel;

  ctor_guard(const atfw::dtmq::DChannelIdKey& input_channel_key, const subscriber_options& input_options)
      : subscriber_key(input_options.subscriber_key),
        shared_subscriber(shared_subscriber::make_shared(input_channel_key)),
        event_handler(input_options.event_callback_set),
        auto_create_channel(input_options.auto_create_channel) {
    if (!event_handler) {
      event_handler = client_subscriber::create_event_callback_set();
    }
  }
};

struct client_subscriber::subscriber_internal_data {
  std::string subscriber_key_;
  atfw::util::nostd::nonnull<shared_subscriber::ptr_t> shared_subscriber_;
  atfw::util::nostd::nonnull<event_callback_set_ptr_t> event_handler;

  explicit subscriber_internal_data(std::string&& input_subscriber_key,
                                    shared_subscriber::ptr_t&& input_shared_subscriber,
                                    event_callback_set_ptr_t&& input_event_handler)
      : subscriber_key_(std::move(input_subscriber_key)),
        shared_subscriber_(std::move(input_shared_subscriber)),
        event_handler(std::move(input_event_handler)) {}
};

client_subscriber::client_subscriber(ctor_guard& guard)
    : internal_data_(atfw::util::memory::make_strong_rc<subscriber_internal_data>(
          std::move(guard.subscriber_key), std::move(guard.shared_subscriber), std::move(guard.event_handler))) {
  internal_data_->shared_subscriber_->register_client_subscriber(this, guard.auto_create_channel);
}

DTMQ_PROXY_SDK_API client_subscriber::~client_subscriber() {
  internal_data_->shared_subscriber_->unregister_client_subscriber(this);
}

DTMQ_PROXY_SDK_API client_subscriber::event_callback_set_ptr_t client_subscriber::create_event_callback_set() {
  return std::make_shared<event_callback_set_t>();
}

DTMQ_PROXY_SDK_API atfw::util::nostd::nullable<client_subscriber::ptr_t> client_subscriber::create(
    const atfw::dtmq::DChannelIdKey& channel_key, const subscriber_options& options) {
  ctor_guard cg(channel_key, options);
  if (!cg.shared_subscriber) {
    return nullptr;
  }

  return atfw::util::nostd::nullable<ptr_t>(atfw::util::memory::make_strong_rc<client_subscriber>(cg));
}

DTMQ_PROXY_SDK_API void client_subscriber::global_receive_channel_event(
    rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& event_sync) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  if (!event_sync.has_channel_snapshot() && !event_sync.has_channel_metadata()) {
    FCTXLOGERROR(ctx, "channel event sync has no channel_snapshot or channel_metadata, ignore this sync");
    return;
  }

  // 先处理定时器事件，保证时序
  global_tick(ctx);

  auto& mgr = get_internal_subscriber_manager();
  const atfw::dtmq::DChannelIdKey* channel_key = nullptr;
  if (event_sync.has_channel_snapshot()) {
    channel_key = &event_sync.channel_snapshot().channel_metadata().channel_key();
  } else {
    channel_key = &event_sync.channel_metadata().channel_key();
  }
  auto iter = mgr.cached_subscriber_smart_ptr.find(channel_key->channel_id());
  if (iter == mgr.cached_subscriber_smart_ptr.end() || !iter->second) {
    FCTXLOGINFO(ctx, "channel {} receive event sync, but may be destroyed, ignore this {} sync",
                channel_key->channel_id(), (event_sync.has_channel_snapshot() ? "snapshot" : "incremental"));
    return;
  }

  iter->second->receive_event_sync(ctx, event_sync);
}

DTMQ_PROXY_SDK_API int32_t client_subscriber::global_tick(rpc::context& ctx) {
  if (is_internal_subscriber_manager_destroyed()) {
    return 0;
  }

  auto& mgr = get_internal_subscriber_manager();
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
    ret += static_cast<int32_t>(mgr.pending_heartbeat_subscriber.size());
    internal_subscriber_manager_do_send_heartbeat(ctx);
  }

  // 处理心跳重试，重设定时器
  ret += static_cast<int32_t>(mgr.retry_heartbeat_subscriber.size());
  internal_subscriber_manager_do_retry_heartbeat(ctx);

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
    internal_subscriber_manager_do_send_heartbeat(ctx);

    auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, mgr.running_heartbeat_task));
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
  return internal_data_->shared_subscriber_->get_channel_key();
}

DTMQ_PROXY_SDK_API const std::string& client_subscriber::get_subscriber_key() const noexcept {
  return internal_data_->subscriber_key_;
}

DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point client_subscriber::get_last_heartbeat_timepoint()
    const noexcept {
  return protobuf_to_system_clock(internal_data_->shared_subscriber_->get_subscriber_info().last_heartbeat_timepoint());
}

DTMQ_PROXY_SDK_API int64_t client_subscriber::get_last_heartbeat_sequence() const noexcept {
  return internal_data_->shared_subscriber_->get_subscriber_info().last_heartbeat_sequence();
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelConfigure& client_subscriber::get_configure() const noexcept {
  return internal_data_->shared_subscriber_->get_configure();
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelOptimisticLock& client_subscriber::get_lock() const noexcept {
  return internal_data_->shared_subscriber_->get_lock();
}

DTMQ_PROXY_SDK_API bool client_subscriber::is_ready() const noexcept {
  return internal_data_->shared_subscriber_->is_ready();
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(event_callback_on_ready_t&& on_ready) {
  internal_data_->event_handler->on_ready = std::move(on_ready);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(const event_callback_on_ready_t& on_ready) {
  internal_data_->event_handler->on_ready = on_ready;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_ready_t& client_subscriber::get_event_callback_on_ready()
    const noexcept {
  return internal_data_->event_handler->on_ready;
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
  internal_data_->event_handler->on_destroy = std::move(on_destroy);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroyed(
    const event_callback_on_destroy_t& on_destroy) {
  internal_data_->event_handler->on_destroy = on_destroy;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_destroy_t&
client_subscriber::get_event_callback_on_destroyed() const noexcept {
  return internal_data_->event_handler->on_destroy;
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
  internal_data_->event_handler->on_update_custom_data = std::move(on_update_custom_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    const event_callback_on_update_custom_data_t& on_update_custom_data) {
  internal_data_->event_handler->on_update_custom_data = on_update_custom_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_custom_data_t&
client_subscriber::get_event_callback_on_update_custom_data() const noexcept {
  return internal_data_->event_handler->on_update_custom_data;
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
  internal_data_->event_handler->on_update_private_data = std::move(on_update_private_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    const event_callback_on_update_private_data_t& on_update_private_data) {
  internal_data_->event_handler->on_update_private_data = on_update_private_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_private_data_t&
client_subscriber::get_event_callback_on_update_private_data() const noexcept {
  return internal_data_->event_handler->on_update_private_data;
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
  internal_data_->event_handler->on_update_optimistic_lock = std::move(on_update_optimistic_lock);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_optimistic_lock(
    const event_callback_on_update_optimistic_lock_t& on_update_optimistic_lock) {
  internal_data_->event_handler->on_update_optimistic_lock = on_update_optimistic_lock;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_optimistic_lock_t&
client_subscriber::get_event_callback_on_update_optimistic_lock() const noexcept {
  return internal_data_->event_handler->on_update_optimistic_lock;
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
  internal_data_->event_handler->on_compact = std::move(on_compact);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(
    const event_callback_on_compact_t& on_compact) {
  internal_data_->event_handler->on_compact = on_compact;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_compact_t&
client_subscriber::get_event_callback_on_compact() const noexcept {
  return internal_data_->event_handler->on_compact;
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
  internal_data_->event_handler->on_receive_text = std::move(on_receive_text);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    const event_callback_on_receive_text_t& on_receive_text) {
  internal_data_->event_handler->on_receive_text = on_receive_text;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_text_t&
client_subscriber::get_event_callback_on_receive_text() const noexcept {
  return internal_data_->event_handler->on_receive_text;
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
  internal_data_->event_handler->on_receive_event = std::move(on_receive_event);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    const event_callback_on_receive_event_t& on_receive_event) {
  internal_data_->event_handler->on_receive_event = on_receive_event;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_event_t&
client_subscriber::get_event_callback_on_receive_event() const noexcept {
  return internal_data_->event_handler->on_receive_event;
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

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_raw_message(
    event_callback_on_receive_raw_message_t&& on_receive_raw_message) {
  internal_data_->event_handler->on_receive_raw_message = std::move(on_receive_raw_message);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_raw_message(
    const event_callback_on_receive_raw_message_t& on_receive_raw_message) {
  internal_data_->event_handler->on_receive_raw_message = on_receive_raw_message;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_raw_message_t&
client_subscriber::get_event_callback_on_receive_raw_message() const noexcept {
  return internal_data_->event_handler->on_receive_raw_message;
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
  internal_data_->event_handler->on_receive_snapshot_start = std::move(on_receive_snapshot_start);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_start(
    const event_callback_on_receive_snapshot_t& on_receive_snapshot_start) {
  internal_data_->event_handler->on_receive_snapshot_start = on_receive_snapshot_start;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_snapshot_t&
client_subscriber::get_event_callback_on_receive_snapshot_start() const noexcept {
  return internal_data_->event_handler->on_receive_snapshot_start;
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
  internal_data_->event_handler->on_receive_snapshot_finished = std::move(on_receive_snapshot_finished);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot_finished(
    const event_callback_on_receive_snapshot_t& on_receive_snapshot_finished) {
  internal_data_->event_handler->on_receive_snapshot_finished = on_receive_snapshot_finished;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_snapshot_t&
client_subscriber::get_event_callback_on_receive_snapshot_finished() const noexcept {
  return internal_data_->event_handler->on_receive_snapshot_finished;
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
  protobuf_copy_message(*subscriber_info_holder, internal_data_->shared_subscriber_->get_subscriber_info());

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(
      ctx, std::move(*subscriber_info_holder), internal_data_->shared_subscriber_->get_channel_key(), std::move(detail),
      compare_and_maybe_reset_lock_ptr, compare_and_maybe_reset_lock_rsp_ptr, auto_create_channel, no_wait)));
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::find_message(rpc::context& ctx, int64_t sequence,
                                                                         atfw::dtmq::DChannelMessage& msg) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      rpc::dtmq::find_message(ctx, internal_data_->shared_subscriber_->get_channel_key(),
                              internal_data_->shared_subscriber_->get_readonly_replicate_index(), sequence, msg)));
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::page_query_message(
    rpc::context& ctx, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::page_query_message(
      ctx, internal_data_->shared_subscriber_->get_channel_key(),
      internal_data_->shared_subscriber_->get_readonly_replicate_index(), page_info, msgs)));
}

namespace {

static void internal_subscriber_manager_do_send_heartbeat(rpc::context& ctx) {
  auto& mgr = get_internal_subscriber_manager();

  if (mgr.pending_heartbeat_subscriber.empty()) {
    return;
  }

  // 已经在执行，等待下一轮
  if (!task_type_trait::empty(mgr.running_heartbeat_task) && !task_type_trait::is_exiting(mgr.running_heartbeat_task)) {
    return;
  }

  // 尽量在一个task里处理星跳发送，这样不用占用浪费task池占用
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
          std::unordered_map<uint64_t, std::list<shared_subscriber*>> pending_subscriber_by_target_server_id;
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
              continue;
            }

            pending_subscriber_by_target_server_id[target_server_id].push_back(subscriber);
          }
          for (const auto& kv : pending_subscriber_by_target_server_id) {
            atfw::dtmq::SSChannelSubscribeReq* req_body = child_ctx.create<atfw::dtmq::SSChannelSubscribeReq>();
            atfw::dtmq::SSChannelSubscribeRsp* rsp_body = child_ctx.create<atfw::dtmq::SSChannelSubscribeRsp>();
            if (req_body == nullptr || rsp_body == nullptr) {
              FCTXLOGERROR(child_ctx, "Failed to create request or response body for target server: {:#x}", kv.first);
              continue;
            }

            for (auto* subscriber : kv.second) {
              auto* heartbeat = req_body->add_heartbeat();
              if (heartbeat == nullptr) {
                FCTXLOGERROR(child_ctx, "Failed to add heartbeat to request body for subscriber: {}, channel: {}",
                             subscriber->get_subscriber_info().subscriber_key(),
                             subscriber->get_channel_key().channel_id());
                continue;
              }
              subscriber->update_last_heartbeat(subscriber->get_last_message_sequence(),
                                                subscriber->get_last_message_hash_code());

              protobuf_copy_message(*req_body->mutable_subscriber(), subscriber->get_subscriber_info());
              protobuf_copy_message(*heartbeat->mutable_channel_key(), subscriber->get_channel_key());
              // 订阅者参数填充
              heartbeat->set_last_sequence(subscriber->get_last_message_sequence());
              heartbeat->set_last_hash_code(subscriber->get_last_message_hash_code());
              heartbeat->set_auto_create_channel(subscriber->should_auto_create_channel());
              heartbeat->set_readonly_index(subscriber->get_readonly_replicate_index());
            }

            dispatcher_await_options one_waiter_options = dispatcher_make_default<dispatcher_await_options>();
            rpc::result_code_type::value_type send_result = RPC_AWAIT_CODE_RESULT(
                rpc::dtmq::subscribe(child_ctx, kv.first, *req_body, *rsp_body, false, &one_waiter_options));

            if (send_result >= 0 && one_waiter_options.sequence > 0) {
              waiter_messages[one_waiter_options.sequence] = child_ctx.create<atframework::SSMsg>();
              waiter_options_set.insert(one_waiter_options);
            } else {
              FWLOGERROR("try to call rpc::dtmq::subscribe to {:#x} failed, res: {}({})", kv.first, send_result,
                         protobuf_mini_dumper_get_error_msg(send_result));

              // 失败了要计划重试,await之后要重新检查有效性
              if (!is_internal_subscriber_manager_destroyed()) {
                for (auto* subscriber : kv.second) {
                  if (inner_mgr.cached_subscriber_raw_pointer.end() !=
                      inner_mgr.cached_subscriber_raw_pointer.find(subscriber)) {
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
                if (inner_mgr.cached_subscriber_raw_pointer.end() !=
                    inner_mgr.cached_subscriber_raw_pointer.find(subscriber)) {
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
                  auto iter = inner_mgr.cached_subscriber_smart_ptr.find(channel_id);
                  if (iter == inner_mgr.cached_subscriber_smart_ptr.end() || !iter->second) {
                    FCTXLOGINFO(child_ctx,
                                "channel {} receive heartbeat response, but may be destroyed, ignore this response",
                                channel_id);
                    continue;
                  }

                  FCTXLOGDEBUG(child_ctx, "channel {} receive heartbeat response", channel_id);
                  iter->second->receive_heartbeat_response(child_ctx);
                }

                for (int i = 0; i < rsp_body.not_found_channel_ids_size(); ++i) {
                  const auto& channel_id = rsp_body.not_found_channel_ids(i);
                  auto iter = inner_mgr.cached_subscriber_smart_ptr.find(channel_id);
                  if (iter == inner_mgr.cached_subscriber_smart_ptr.end() || !iter->second) {
                    FCTXLOGINFO(child_ctx,
                                "channel {} receive not found response, but may be destroyed, ignore this response",
                                channel_id);
                    continue;
                  }
                  FCTXLOGWARNING(child_ctx, "channel {} receive not found response", channel_id);

                  // 虚拟删除事件通知，以便触发监听者的销毁回调
                  iter->second->set_destroyed(child_ctx, iter->second->get_last_message_sequence() + 1,
                                              std::chrono::system_clock::now());
                }
              });
        } while (false);

        if (task_type_trait::get_task_id(inner_mgr.running_heartbeat_task) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(inner_mgr.running_heartbeat_task);
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "Failed to invoke async task for sending heartbeat, result: {}({})", *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  } else {
    mgr.running_heartbeat_task = std::move(*invoke_result.get_success());
    FCTXLOGDEBUG(ctx, "Successfully invoked async task for sending heartbeat, subscriber count: {}",
                 mgr.pending_heartbeat_subscriber.size());
  }
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
  auto iter = mgr.cached_subscriber_smart_ptr.find(channel_key.channel_id());
  if (iter != mgr.cached_subscriber_smart_ptr.end() && iter->second) {
    return iter->second;
  }

  auto channel_cfg = excel::get_ExcelDtmqChannelType_by_channel_type(channel_key.channel_type());
  if (!channel_cfg) {
    FWLOGWARNING("Failed to get channel config for channel type: {}", channel_key.channel_type());
    return nullptr;
  }

  auto new_shared_subscriber = std::make_shared<shared_subscriber>(channel_key);
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

  if (configure.heartbeat_interval().seconds() > 0) {
    ret->subscriber_heartbeat_interval =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_interval());
  } else {
    ret->subscriber_heartbeat_interval =
        std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{300});
  }
  if (configure.heartbeat_retry_interval().seconds() > 0) {
    ret->subscriber_heartbeat_retry_interval =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_retry_interval());
  } else {
    ret->subscriber_heartbeat_retry_interval =
        std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{60});
  }

  ret->require_snapshot = true;

  // 以下不同类型的聊天频道配置不一样
  if (configure.gc_expire_duration().seconds() <= 0) {
    ret->gc_expire_duration =
        std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::hours{3650 * 24});
  } else {
    ret->gc_expire_duration =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.gc_expire_duration());
  }
  ret->gc_log_size = configure.gc_log_count();
  if (ret->gc_log_size <= 0) {
    ret->gc_log_size = 30;
  }

  ret->max_log_size = configure.max_log_count();
  if (ret->max_log_size <= 0) {
    ret->max_log_size = 300;
  }
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

  ret = atfw::memory::stl::make_strong_rc<wal_client_type::vtable_type>();
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
      return wal_result_code::kInitlization;
    }

    int64_t last_removed_key = snapshot.channel_runtime().last_removed_sequence();
    if (nullptr != wal.get_last_removed_key()) {
      last_removed_key = *wal.get_last_removed_key();
    }

    // Load logs
    std::vector<wal_object_type::log_pointer> storage;
    storage.reserve(static_cast<size_t>(snapshot.messages_size()));
    for (const auto& msg : snapshot.messages()) {
      if (wal.get_log_key_compare()(msg.sequence(), last_removed_key)) {
        continue;
      }

      auto log_ptr = atfw::memory::stl::make_strong_rc<wal_object_type::log_type>();
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
    return wal_result_code::kOk;
  };

  // 事件回调
  mq_client_subscriber_delegate_helper::setup_delegate_actions(ret->log_action_delegate);

  // Allow default delegate to allow sync package
  ret->default_delegate.action = &mq_client_subscriber_delegate_helper::common_action;

  return ret;
}

void shared_subscriber::setup_timer(timer_action_type action) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  // 如果定时器类型不变，那么就不需要重新设置定时器了
  if (timer_action_ == action || check_flag(subscriber_flag::kDestroying)) {
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
      timeout_tp = atfw::util::time::time_utility::now() +
                   protobuf_to_chrono_duration<std::chrono::system_clock::duration>(configure_.gc_expire_duration());
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

    std::weak_ptr<shared_subscriber> self_weak = shared_from_this();
    auto fn = [self_weak](time_t /*tick_time*/, const mq_client_subscriber_timer_type::timer_t& /*timer*/) {
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
        rpc::context child_ctx = rpc::context::create_without_task();
        self->tick(child_ctx);
      }

      if (current_action == timer_action_type::kGc && self->can_be_removed()) {
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
      return;
    }

    timer_timeout_tick_ = timeout_tick;
  }

  timer_action_ = action;
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

  // load阶段如果尚未ready，不需要触发custom data的变化通知
  if (is_ready()) {
    update_custom_data(ctx, metadata.custom_data_sequence(), metadata.custom_data());
  } else {
    if (metadata.custom_data_sequence() > custom_data_sequence_) {
      custom_data_sequence_ = metadata.custom_data_sequence();
      protobuf_copy_message(custom_data_, metadata.custom_data());
    }
  }
}

void shared_subscriber::load_runtime(rpc::context& ctx, const atfw::dtmq::DChannelRuntime& runtime) {
  // load阶段如果尚未ready，不需要触发private的变化通知
  if (is_ready()) {
    update_private_data(ctx, runtime.private_data_sequence(), runtime.private_data());
  } else {
    if (runtime.private_data_sequence() > private_data_sequence_) {
      private_data_sequence_ = runtime.private_data_sequence();
      protobuf_copy_message(private_data_, runtime.private_data());
    }
  }
}

void shared_subscriber::load_lock(rpc::context& ctx, const atfw::dtmq::DChannelOptimisticLock& lock) {
  // load阶段如果尚未ready，不需要触发乐观锁的变化通知
  if (is_ready()) {
    update_optimistic_lock(ctx, lock);
  } else {
    protobuf_copy_message(lock_, lock);
  }
}
}  // namespace

void shared_subscriber::register_client_subscriber(client_subscriber* client, bool auto_create_channel) {
  if (client == nullptr) {
    return;
  }

  if (check_flag(subscriber_flag::kLockRegisteredClient)) {
    lock_registered_client_pending_add_[client] = auto_create_channel;
    lock_registered_client_pending_remove_.erase(client);
    return;
  }

  if (auto_create_channel) {
    registered_client_auto_create_channel_.insert(client);
  } else {
    registered_client_no_create_channel_.insert(client);
  }

  // 最初的订阅client注册，要改定时器为心跳定时器，并且立即发送一次心跳
  if (timer_action_ == timer_action_type::kGc || timer_action_ == timer_action_type::kNone) {
    setup_timer(timer_action_type::kSendHeartbeat);

    size_t total_registered_clients =
        registered_client_auto_create_channel_.size() + registered_client_no_create_channel_.size();

    if (total_registered_clients == 1) {
      auto& mgr = get_internal_subscriber_manager();
      mgr.pending_heartbeat_subscriber.insert(this);
    }
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

int32_t shared_subscriber::tick(rpc::context& ctx) {
  if (!wal_client_) {
    return 0;
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
  if (before_compact_sequence != after_compact_sequence) {
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
}

void shared_subscriber::receive_event_sync(rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& event_sync) {
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
      FCTXLOGWARNING(ctx, "shared_subscriber is not ready and no create event found, ignoring event sync");
      return;
    }
  }

  if (event_sync.has_channel_snapshot()) {
    load_snapshot(ctx, event_sync.channel_snapshot());
    return;
  }

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
  for (const auto& log_msg : event_sync.channel_message()) {
    if (log_msg.sequence() < start_sequence) {
      continue;
    }

    if (wal_client_) {
      auto log_ptr = wal_client_->get_log_manager().allocate_log(protobuf_to_system_clock(log_msg.create_timepoint()),
                                                                 log_msg.detail().command_case(), param, log_msg);
      if (log_ptr) {
        auto log_result = wal_client_->get_log_manager().emplace_back(std::move(log_ptr), param);
        if (log_result < atfw::util::distributed_system::wal_result_code::kOk) {
          FCTXLOGERROR(ctx, "Failed to emplace log for sequence: {}, result: {}, {}({})", log_msg.sequence(),
                       static_cast<int32_t>(log_result), result_code, protobuf_mini_dumper_get_error_msg(result_code));
        } else if (result_code < 0) {
          FCTXLOGERROR(ctx, "Failed to emplace log for sequence: {}, result: {}({})", log_msg.sequence(), result_code,
                       protobuf_mini_dumper_get_error_msg(result_code));
        } else {
          if (last_message_sequence_ < log_msg.sequence()) {
            last_message_sequence_ = log_msg.sequence();
            last_message_hash_code_ = log_msg.hash_code();
          }
        }
      } else {
        FCTXLOGERROR(ctx, "Failed to allocate log for sequence: {}", log_msg.sequence());
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

  // 所有的消息都落后，也要补充触发 update_custom_data/update_private_data
  if (update_custom_data_sequence > 0) {
    update_custom_data(ctx, update_custom_data_sequence, event_sync.channel_metadata().custom_data());
  }

  if (update_private_data_sequence > 0) {
    update_private_data(ctx, update_private_data_sequence, event_sync.channel_runtime().private_data());
  }

  // 同步GC边界
  if (event_sync.channel_runtime().last_removed_sequence() > 0) {
    compact(ctx, event_sync.channel_runtime().last_removed_sequence());
  }

  // 压缩日志事件
  int64_t after_compact_sequence = get_compact_sequence();
  if (after_compact_sequence != before_compact_sequence) {
    foreach_registered_client_subscriber([&ctx, after_compact_sequence](client_subscriber& client) {
      const auto& fn = client.get_event_callback_on_compact();
      if (fn) {
        fn(ctx, client.shared_from_this(), after_compact_sequence);
      }
    });
  }
}

void shared_subscriber::load_snapshot(rpc::context& ctx, const atfw::dtmq::DChannelSnapshot& snapshot) {
  // 忽略重复信息和滞后的快照
  if (snapshot_sequence_ >= snapshot.channel_metadata().last_sequence() &&
      snapshot.channel_runtime().last_removed_sequence() < snapshot_sequence_) {
    return;
  }

  if (snapshot.channel_metadata().has_channel_configure()) {
    reload_configure(snapshot.channel_metadata().channel_configure());
  }

  maybe_mutable_wal_client();

  foreach_registered_client_subscriber([&ctx, &snapshot](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_receive_snapshot_start();
    if (fn) {
      fn(ctx, client.shared_from_this(), snapshot);
    }
  });

  int32_t result_code = 0;
  mq_client_subscriber_wal_object_context param{ctx, result_code};
  if (wal_client_) {
    auto log_result = wal_client_->receive_snapshot(snapshot, param);
    if (log_result < atfw::util::distributed_system::wal_result_code::kOk) {
      FCTXLOGERROR(ctx, "Failed to load snapshot, result: {}, {}({})", static_cast<int32_t>(log_result), result_code,
                   protobuf_mini_dumper_get_error_msg(result_code));
      return;
    }

    if (result_code < 0) {
      FCTXLOGERROR(ctx, "Failed to load snapshot, result: {}({})", result_code,
                   protobuf_mini_dumper_get_error_msg(result_code));
      return;
    }
  }

  snapshot_sequence_ = snapshot.channel_metadata().last_sequence();
  last_message_hash_code_ = snapshot.channel_metadata().last_hash_code();
  last_message_sequence_ = snapshot.channel_metadata().last_sequence();
  foreach_registered_client_subscriber([&ctx, &snapshot](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_receive_snapshot_finished();
    if (fn) {
      fn(ctx, client.shared_from_this(), snapshot);
    }
  });

  // 如果是已销毁或者尚未创建的频道，保持订阅缓存。但不能ready
  if (create_sequence_ > 0 && destroy_sequence_ < create_sequence_) {
    set_ready(ctx);
  } else if (destroy_sequence_ > 0 && destroy_sequence_ >= create_sequence_ && is_ready()) {
    set_destroyed(ctx, destroy_sequence_, destroy_timepoint_);
  }
}

void shared_subscriber::reload_configure(const atfw::dtmq::DChannelConfigure& config) {
  protobuf_copy_message(configure_, config);

  if (wal_client_) {
    auto& wal_obj_conf = wal_client_->get_log_manager().get_configure();

    // 未配置则用默认值
    if (configure_.gc_expire_duration().seconds() > 0) {
      wal_obj_conf.gc_expire_duration =
          protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure_.gc_expire_duration());
    }

    if (configure_.gc_log_count() > 0) {
      wal_obj_conf.gc_log_size = configure_.gc_log_count();
    }

    if (configure_.max_log_count() > 0) {
      wal_obj_conf.max_log_size = configure_.max_log_count();
    }

    auto& client_conf = wal_client_->get_configure();
    client_conf.gc_expire_duration = wal_obj_conf.gc_expire_duration;
    client_conf.gc_log_size = wal_obj_conf.gc_log_size;
    client_conf.max_log_size = wal_obj_conf.max_log_size;

    if (configure_.heartbeat_interval().seconds() > 0) {
      client_conf.subscriber_heartbeat_interval =
          protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure_.heartbeat_interval());
    } else {
      client_conf.subscriber_heartbeat_interval =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{300});
    }

    if (configure_.heartbeat_retry_interval().seconds() > 0) {
      client_conf.subscriber_heartbeat_retry_interval =
          protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
              configure_.heartbeat_retry_interval());
    } else {
      client_conf.subscriber_heartbeat_retry_interval =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{60});
    }
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
  if (!is_ready()) {
    return;
  }
  set_flag(subscriber_flag::kReady, false);

  foreach_registered_client_subscriber([&ctx, log_sequence, destroy_time](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_destroyed();
    if (fn) {
      fn(ctx, client.shared_from_this(), log_sequence, destroy_time);
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

  foreach_registered_client_subscriber([&ctx, this](client_subscriber& client) {
    const auto& fn = client.get_event_callback_on_update_custom_data();
    if (fn) {
      fn(ctx, client.shared_from_this(), custom_data_sequence_, custom_data_);
    }
  });
}

void shared_subscriber::update_private_data(rpc::context& ctx, int64_t sequence,
                                            const google::protobuf::Any& custom_data) {
  if (private_data_sequence_ >= sequence) {
    return;
  }

  private_data_sequence_ = sequence;
  protobuf_copy_message(private_data_, custom_data);

  foreach_registered_client_subscriber([&ctx, this](client_subscriber& client) {
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
  auto remove_timepoint = atfw::util::time::time_utility::now();
  size_t remove_count = 0;
  for (const auto& log : wal_client_->get_log_manager().get_all_logs()) {
    if (!log) {
      continue;
    }

    if (wal_client_->get_log_manager().get_log_key_compare()(log->sequence(), compact_sequence)) {
      ++remove_count;
    } else {
      remove_timepoint = protobuf_to_system_clock(log->create_timepoint());
      break;
    }
  }
  wal_client_->get_log_manager().remove_before(remove_timepoint, remove_count);
}

void shared_subscriber::schedule_send_heartbeat(rpc::context& /*ctx*/) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  // 计划发心跳包

  auto& mgr = get_internal_subscriber_manager();

  // 不受管理的对象要跳过
  if (mgr.cached_subscriber_raw_pointer.end() == mgr.cached_subscriber_raw_pointer.find(this)) {
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
  if (mgr.cached_subscriber_raw_pointer.end() == mgr.cached_subscriber_raw_pointer.find(this)) {
    return;
  }

  mgr.retry_heartbeat_subscriber.insert(this);
}

void shared_subscriber::add_cached_shared_subscriber(const shared_subscriber::ptr_t& subscriber) {
  if (is_internal_subscriber_manager_destroyed()) {
    return;
  }

  if (!subscriber) {
    return;
  }

  auto& mgr = get_internal_subscriber_manager();
  mgr.cached_subscriber_smart_ptr[subscriber->get_channel_key().channel_id()] = subscriber;
  mgr.cached_subscriber_raw_pointer.insert(subscriber.get());

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

  auto iter = mgr.cached_subscriber_smart_ptr.find(subscriber->get_channel_key().channel_id());
  if (iter == mgr.cached_subscriber_smart_ptr.end()) {
    return;
  }

  if (iter->second.get() != subscriber) {
    return;
  }

  auto hold_lifetime = std::move(iter->second);

  mgr.cached_subscriber_smart_ptr.erase(subscriber->get_channel_key().channel_id());
  mgr.cached_subscriber_raw_pointer.erase(hold_lifetime.get());

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

  if (raw_message.sequence() > subscriber->destroy_sequence_) {
    subscriber->destroy_timepoint_ = protobuf_to_system_clock(raw_message.detail().destroy().removed_timepoint());
    subscriber->destroy_sequence_ = raw_message.sequence();
  }
  if (subscriber->is_ready() && raw_message.sequence() >= subscriber->create_sequence_) {
    subscriber->set_destroyed(param.context, subscriber->destroy_sequence_, subscriber->destroy_timepoint_);
  }

  // 频道销毁也要销毁乐观锁
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
      fn(param.context, client.shared_from_this(), raw_message.sequence(), raw_message.detail().text());
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
    const auto& fn = client.get_event_callback_on_receive_event();
    if (fn) {
      fn(param.context, client.shared_from_this(), raw_message.sequence(), raw_message.detail().event());
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

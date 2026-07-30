// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/dtmq/dtmq_client_subscriber.h"

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>
#include <nostd/function_ref.h>

#include <log/log_wrapper.h>
#include <memory/rc_ptr.h>

#include <distributed_system/wal_client.h>

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

#include <rpc/dtmq/dtmqproxysvrservice.atfw.gen.h>
#include <rpc/rpc_context.h>

#include <config/excel/config_easy_api.h>
#include <config/extern_service_types.h>
#include <config/server_frame_build_feature.h>

#include <logic/logic_server_setup.h>
#include <utility/protobuf_mini_dumper.h>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rpc {
namespace dtmq {

namespace {

class ATFW_UTIL_SYMBOL_LOCAL shared_subscriber;

using mq_client_subscriber_storage_type = atfw::dtmq::DChannelSnapshot;

struct ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_wal_object_context {
  std::reference_wrapper<rpc::context> context;
  std::reference_wrapper<int32_t> result_code;

  explicit mq_client_subscriber_wal_object_context(rpc::context& ctx, int32_t& output_result)
      : context(ctx), result_code(output_result) {}
};

struct ATFW_UTIL_SYMBOL_LOCAL mq_client_subscriber_wal_object_private_data_type {
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
    mq_client_subscriber_wal_object_private_data_type, mq_client_subscriber_storage_type>;

class ATFW_UTIL_SYMBOL_LOCAL shared_subscriber {
 public:
  using ptr_t = std::shared_ptr<shared_subscriber>;

  // NOLINTNEXTLINE(readability-enum-initial-value)
  enum class subscriber_flag : uint32_t {
    kUninitialized = 0,
    kReady = 1,
    kLockRegisteredClient = 2,
    kMax,
  };

 public:
  static ptr_t make_shared(const atfw::dtmq::DChannelIdKey& channel_key);

  // NOLINTNEXTLINE(modernize-pass-by-value)
  shared_subscriber(const atfw::dtmq::DChannelIdKey& channel_key)
      : channel_key_(channel_key),
        readonly_replicate_index_(atfw::component::random_engine::random()),
        custom_data_sequence_(0),
        private_data_sequence_(0) {
    subscriber_info_.set_subscriber_server_id(logic_config::me()->get_local_server_id());

    // 这里是共享 subscriber key
    if (!logic_config::me()->get_local_server_name().empty()) {
      subscriber_info_.set_subscriber_key(
          atfw::util::string::format("server:{}", logic_config::me()->get_local_server_name()));
    } else {
      subscriber_info_.set_subscriber_key(
          atfw::util::string::format("server:{}", subscriber_info_.subscriber_server_id()));
    }
  }

  inline bool check_flag(subscriber_flag flag) const noexcept { return flags_.test(static_cast<size_t>(flag)); }

  inline void set_flag(subscriber_flag flag, bool value) noexcept { flags_.set(static_cast<size_t>(flag), value); }

  inline const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept { return channel_key_; }

  inline const atfw::dtmq::channel_subscriber& get_subscriber_info() const noexcept { return subscriber_info_; }

  inline const atfw::dtmq::DChannelConfigure& get_configure() const noexcept { return configure_; }

  inline const atfw::dtmq::DChannelOptimisticLock& get_lock() const noexcept { return lock_; }

  inline uint64_t get_readonly_replicate_index() const noexcept { return readonly_replicate_index_; }

  void register_client_subscriber(client_subscriber* client);
  void unregister_client_subscriber(client_subscriber* client);
  void foreach_registered_client_subscriber(atfw::util::nostd::function_ref<void(client_subscriber&)> callback);

  int64_t get_compact_sequence() const noexcept;

  int32_t tick(rpc::context& ctx);

  void receive_event_sync(rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& event_sync);

  void load_snapshot(rpc::context& ctx, const atfw::dtmq::DChannelSnapshot& snapshot);

  void set_ready(rpc::context& ctx);

  void set_destroyed(rpc::context& ctx, int64_t log_sequence, std::chrono::system_clock::time_point destroy_time);

  void update_custom_data(rpc::context& ctx, int64_t sequence, const google::protobuf::Any& custom_data);

  void update_private_data(rpc::context& ctx, int64_t sequence, const google::protobuf::Any& custom_data);

  void compact(rpc::context& ctx, int64_t compact_sequence);

  static std::unordered_map<std::string, shared_subscriber::ptr_t>& get_cached_shared_subscriber() {
    static std::unordered_map<std::string, shared_subscriber::ptr_t> ret;
    return ret;
  }

  static void add_cached_shared_subscriber(const shared_subscriber::ptr_t& subscriber);

 private:
  std::bitset<static_cast<size_t>(subscriber_flag::kMax)> flags_;
  atfw::dtmq::DChannelIdKey channel_key_;
  atfw::dtmq::channel_subscriber subscriber_info_;
  uint64_t readonly_replicate_index_;

  atfw::dtmq::DChannelConfigure configure_;
  atfw::dtmq::DChannelOptimisticLock lock_;

  int64_t custom_data_sequence_;
  int64_t private_data_sequence_;
  google::protobuf::Any custom_data_;
  google::protobuf::Any private_data_;

  atfw::util::memory::strong_rc_ptr<mq_client_subscriber_wal_client_type> wal_client_;

  std::unordered_set<client_subscriber*> registered_client_;

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
          if (client != nullptr) {
            subscriber_->registered_client_.insert(client);
          }
        }
        subscriber_->lock_registered_client_pending_add_.clear();

        for (const auto& client : subscriber_->lock_registered_client_pending_remove_) {
          if (client != nullptr) {
            subscriber_->registered_client_.erase(client);
          }
        }
        subscriber_->lock_registered_client_pending_remove_.clear();
      }
    }
  };

  std::unordered_set<client_subscriber*> lock_registered_client_pending_add_;
  std::unordered_set<client_subscriber*> lock_registered_client_pending_remove_;
};

struct ATFW_UTIL_SYMBOL_LOCAL subscriber_event_handler_set {
  client_subscriber::event_callback_on_ready_t on_ready;
  client_subscriber::event_callback_on_destroy_t on_destroy;
  client_subscriber::event_callback_on_update_custom_data_t on_update_custom_data;
  client_subscriber::event_callback_on_update_private_data_t on_update_private_data;
  client_subscriber::event_callback_on_compact_t on_compact;
  client_subscriber::event_callback_on_receive_text_t on_receive_text;
  client_subscriber::event_callback_on_receive_event_t on_receive_event;
  client_subscriber::event_callback_on_receive_snapshot_t on_receive_snapshot;
};

}  // namespace

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::subscriber_options(std::string&& input_subscriber_key)
    : subscriber_key(std::move(input_subscriber_key)) {}

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::subscriber_options(const std::string& input_subscriber_key)
    : subscriber_key(input_subscriber_key) {}

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::~subscriber_options() {}

struct client_subscriber::ctor_guard {
  std::string subscriber_key_;
  shared_subscriber::ptr_t shared_subscriber_;

  ctor_guard(const atfw::dtmq::DChannelIdKey& input_channel_key, const subscriber_options& input_options)
      : subscriber_key_(input_options.subscriber_key),
        shared_subscriber_(shared_subscriber::make_shared(input_channel_key)) {}
};

struct client_subscriber::subscriber_internal_data {
  std::string subscriber_key_;
  atfw::util::nostd::nonnull<shared_subscriber::ptr_t> shared_subscriber_;

  subscriber_event_handler_set event_handler;

  explicit subscriber_internal_data(std::string&& input_subscriber_key,
                                    shared_subscriber::ptr_t&& input_shared_subscriber)
      : subscriber_key_(std::move(input_subscriber_key)), shared_subscriber_(std::move(input_shared_subscriber)) {}
};

client_subscriber::client_subscriber(ctor_guard& guard)
    : internal_data_(atfw::util::memory::make_strong_rc<subscriber_internal_data>(
          std::move(guard.subscriber_key_), std::move(guard.shared_subscriber_))) {
  internal_data_->shared_subscriber_->register_client_subscriber(this);
}

DTMQ_PROXY_SDK_API client_subscriber::~client_subscriber() {
  internal_data_->shared_subscriber_->unregister_client_subscriber(this);
}

DTMQ_PROXY_SDK_API atfw::util::nostd::nullable<client_subscriber::ptr_t> client_subscriber::create(
    const atfw::dtmq::DChannelIdKey& channel_key, const subscriber_options& options) {
  ctor_guard cg(channel_key, options);
  if (!cg.shared_subscriber_) {
    return nullptr;
  }

  return atfw::util::nostd::nullable<ptr_t>(atfw::util::memory::make_strong_rc<client_subscriber>(cg));
}

DTMQ_PROXY_SDK_API void client_subscriber::global_receive_channel_event(
    rpc::context& /*ctx*/, const atfw::dtmq::SSChannelEventSync& /*event_sync*/) {
  // TODO(owent): implement the logic to handle received channel events
}

DTMQ_PROXY_SDK_API int32_t client_subscriber::global_tick(rpc::context& /*ctx*/) {
  // TODO(owent): implement the logic for periodic tick handling
  return 0;  // Return 0 to indicate no timer events triggered
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

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(rpc::context& ctx,
                                                                       event_callback_on_ready_t&& on_ready) {
  internal_data_->event_handler.on_ready = std::move(on_ready);

  if (internal_data_->event_handler.on_ready &&
      internal_data_->shared_subscriber_->check_flag(shared_subscriber::subscriber_flag::kReady)) {
    internal_data_->event_handler.on_ready(ctx, shared_from_this());
  }
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(rpc::context& ctx,
                                                                       const event_callback_on_ready_t& on_ready) {
  internal_data_->event_handler.on_ready = on_ready;

  if (internal_data_->event_handler.on_ready &&
      internal_data_->shared_subscriber_->check_flag(shared_subscriber::subscriber_flag::kReady)) {
    internal_data_->event_handler.on_ready(ctx, shared_from_this());
  }
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_ready_t& client_subscriber::get_event_callback_on_ready()
    const noexcept {
  return internal_data_->event_handler.on_ready;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroyed(event_callback_on_destroy_t&& on_destroy) {
  internal_data_->event_handler.on_destroy = std::move(on_destroy);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroyed(
    const event_callback_on_destroy_t& on_destroy) {
  internal_data_->event_handler.on_destroy = on_destroy;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_destroy_t&
client_subscriber::get_event_callback_on_destroyed() const noexcept {
  return internal_data_->event_handler.on_destroy;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    event_callback_on_update_custom_data_t&& on_update_custom_data) {
  internal_data_->event_handler.on_update_custom_data = std::move(on_update_custom_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    const event_callback_on_update_custom_data_t& on_update_custom_data) {
  internal_data_->event_handler.on_update_custom_data = on_update_custom_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_custom_data_t&
client_subscriber::get_event_callback_on_update_custom_data() const noexcept {
  return internal_data_->event_handler.on_update_custom_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    event_callback_on_update_private_data_t&& on_update_private_data) {
  internal_data_->event_handler.on_update_private_data = std::move(on_update_private_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    const event_callback_on_update_private_data_t& on_update_private_data) {
  internal_data_->event_handler.on_update_private_data = on_update_private_data;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_update_private_data_t&
client_subscriber::get_event_callback_on_update_private_data() const noexcept {
  return internal_data_->event_handler.on_update_private_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(event_callback_on_compact_t&& on_compact) {
  internal_data_->event_handler.on_compact = std::move(on_compact);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(
    const event_callback_on_compact_t& on_compact) {
  internal_data_->event_handler.on_compact = on_compact;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_compact_t&
client_subscriber::get_event_callback_on_compact() const noexcept {
  return internal_data_->event_handler.on_compact;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    event_callback_on_receive_text_t&& on_receive_text) {
  internal_data_->event_handler.on_receive_text = std::move(on_receive_text);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    const event_callback_on_receive_text_t& on_receive_text) {
  internal_data_->event_handler.on_receive_text = on_receive_text;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_text_t&
client_subscriber::get_event_callback_on_receive_text() const noexcept {
  return internal_data_->event_handler.on_receive_text;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    event_callback_on_receive_event_t&& on_receive_event) {
  internal_data_->event_handler.on_receive_event = std::move(on_receive_event);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    const event_callback_on_receive_event_t& on_receive_event) {
  internal_data_->event_handler.on_receive_event = on_receive_event;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_event_t&
client_subscriber::get_event_callback_on_receive_event() const noexcept {
  return internal_data_->event_handler.on_receive_event;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot(
    event_callback_on_receive_snapshot_t&& on_receive_snapshot) {
  internal_data_->event_handler.on_receive_snapshot = std::move(on_receive_snapshot);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot(
    const event_callback_on_receive_snapshot_t& on_receive_snapshot) {
  internal_data_->event_handler.on_receive_snapshot = on_receive_snapshot;
}

DTMQ_PROXY_SDK_API const client_subscriber::event_callback_on_receive_snapshot_t&
client_subscriber::get_event_callback_on_receive_snapshot() const noexcept {
  return internal_data_->event_handler.on_receive_snapshot;
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
shared_subscriber::ptr_t shared_subscriber::make_shared(const atfw::dtmq::DChannelIdKey& channel_key) {
  if (channel_key.channel_id().empty()) {
    return nullptr;
  }

  auto iter = get_cached_shared_subscriber().find(channel_key.channel_id());
  if (iter != get_cached_shared_subscriber().end() && iter->second) {
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

void shared_subscriber::register_client_subscriber(client_subscriber* client) {
  if (client == nullptr) {
    return;
  }

  if (check_flag(subscriber_flag::kLockRegisteredClient)) {
    lock_registered_client_pending_add_.insert(client);
    lock_registered_client_pending_remove_.erase(client);
    return;
  }

  registered_client_.insert(client);
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

  registered_client_.erase(client);
}

void shared_subscriber::foreach_registered_client_subscriber(
    atfw::util::nostd::function_ref<void(client_subscriber&)> callback) {
  if (registered_client_.empty()) {
    return;
  }

  lock_registered_client_guard guard(*this);
  for (const auto& client : registered_client_) {
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
      if (client.get_event_callback_on_compact()) {
        client.get_event_callback_on_compact()(ctx, client.shared_from_this(), after_compact_sequence);
      }
    });
  }

  return ret;
}

void shared_subscriber::receive_event_sync(rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& event_sync) {
  // Ignore events if the subscriber is not ready and the event is not a snapshot
  int64_t start_sequence = 0;
  if (!check_flag(subscriber_flag::kReady) && !event_sync.has_channel_snapshot()) {
    // 如果未就绪，且无重新创建事件，直接忽略
    for (int i = event_sync.channel_message_size() - 1; i >= 0; --i) {
      const auto& msg = event_sync.channel_message(i);
      if (msg.detail().has_create()) {
        start_sequence = msg.sequence();
        break;
      }
    }
    if (start_sequence == 0) {
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
        wal_client_->get_log_manager().emplace_back(std::move(log_ptr), param);
      } else {
        FCTXLOGERROR(ctx, "Failed to allocate log for sequence: {}", log_msg.sequence());
      }
    }

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
      if (client.get_event_callback_on_compact()) {
        client.get_event_callback_on_compact()(ctx, client.shared_from_this(), after_compact_sequence);
      }
    });
  }
}

void shared_subscriber::load_snapshot(rpc::context& ctx, const atfw::dtmq::DChannelSnapshot& /*snapshot*/) {
  // TODO(owent): Implement the logic to load snapshot data into the shared subscriber

  set_ready(ctx);
}

void shared_subscriber::set_ready(rpc::context& ctx) {
  if (check_flag(subscriber_flag::kReady)) {
    return;
  }
  set_flag(subscriber_flag::kReady, true);

  foreach_registered_client_subscriber([&ctx](client_subscriber& client) {
    if (client.get_event_callback_on_ready()) {
      client.get_event_callback_on_ready()(ctx, client.shared_from_this());
    }
  });
}

void shared_subscriber::set_destroyed(rpc::context& ctx, int64_t log_sequence,
                                      std::chrono::system_clock::time_point destroy_time) {
  if (!check_flag(subscriber_flag::kReady)) {
    return;
  }
  set_flag(subscriber_flag::kReady, false);

  foreach_registered_client_subscriber([&ctx, log_sequence, destroy_time](client_subscriber& client) {
    if (client.get_event_callback_on_destroyed()) {
      client.get_event_callback_on_destroyed()(ctx, client.shared_from_this(), log_sequence, destroy_time);
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
    if (client.get_event_callback_on_update_custom_data()) {
      client.get_event_callback_on_update_custom_data()(ctx, client.shared_from_this(), custom_data_sequence_,
                                                        custom_data_);
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
    if (client.get_event_callback_on_update_private_data()) {
      client.get_event_callback_on_update_private_data()(ctx, client.shared_from_this(), private_data_sequence_,
                                                         private_data_);
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

void shared_subscriber::add_cached_shared_subscriber(const shared_subscriber::ptr_t& subscriber) {
  if (!subscriber) {
    return;
  }

  auto& cache = get_cached_shared_subscriber();
  cache[subscriber->get_channel_key().channel_id()] = subscriber;

  // TODO(owent): 注册定时器执行订阅和垃圾回收
}

}  // namespace
}  // namespace dtmq
}  // namespace rpc

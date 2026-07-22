// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/dtmq/dtmq_client_subscriber.h"

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

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

using mq_client_subscriber_wal_client_type = atfw::util::distributed_system::wal_client<
    mq_client_subscriber_storage_type, mq_client_subscriber_wal_log_action_getter,
    mq_client_subscriber_wal_object_context, mq_client_subscriber_wal_object_private_data_type,
    mq_client_subscriber_storage_type>;

class ATFW_UTIL_SYMBOL_LOCAL shared_subscriber {
 public:
  using ptr_t = std::shared_ptr<shared_subscriber>;

 public:
  static ptr_t make_shared(const atfw::dtmq::DChannelIdKey& channel_key,
                           const client_subscriber::subscriber_options& options);

  // NOLINTNEXTLINE(modernize-pass-by-value)
  shared_subscriber(const atfw::dtmq::DChannelIdKey& channel_key, const client_subscriber::subscriber_options& options)
      : channel_key_(channel_key), readonly_replicate_index_(atfw::component::random_engine::random()) {
    subscriber_info_.set_subscriber_server_id(logic_config::me()->get_local_server_id());
    subscriber_info_.set_subscriber_key(options.subscriber_key);
  }

  inline const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept { return channel_key_; }

  inline const atfw::dtmq::channel_subscriber& get_subscriber_info() const noexcept { return subscriber_info_; }

  inline const atfw::dtmq::DChannelConfigure& get_configure() const noexcept { return configure_; }

  inline const atfw::dtmq::DChannelOptimisticLock& get_lock() const noexcept { return lock_; }

  uint64_t get_readonly_replicate_index() const noexcept { return readonly_replicate_index_; }

  void register_client_subscriber(client_subscriber* client) { registered_client_.insert(client); }
  void unregister_client_subscriber(client_subscriber* client) { registered_client_.erase(client); }

  static std::unordered_map<std::string, shared_subscriber::ptr_t>& get_cached_shared_subscriber() {
    static std::unordered_map<std::string, shared_subscriber::ptr_t> ret;
    return ret;
  }

  static void add_cached_shared_subscriber(const shared_subscriber::ptr_t& subscriber);

 private:
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

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::subscriber_options(std::string input_subscriber_key)
    : subscriber_key(input_subscriber_key) {}

DTMQ_PROXY_SDK_API client_subscriber::subscriber_options::~subscriber_options() {}

struct client_subscriber::ctor_guard {
  shared_subscriber::ptr_t shared_subscriber;

  ctor_guard(const atfw::dtmq::DChannelIdKey& input_channel_key, const subscriber_options& input_options)
      : shared_subscriber(shared_subscriber::make_shared(input_channel_key, input_options)) {}
};

struct client_subscriber::subscriber_internal_data {
  atfw::util::nostd::nonnull<shared_subscriber::ptr_t> shared_subscriber;

  subscriber_event_handler_set event_handler;

  explicit subscriber_internal_data(shared_subscriber::ptr_t&& input_shared_subscriber)
      : shared_subscriber(std::move(input_shared_subscriber)) {}
};

client_subscriber::client_subscriber(ctor_guard& guard)
    : internal_data_(atfw::util::memory::make_strong_rc<subscriber_internal_data>(std::move(guard.shared_subscriber))) {
  internal_data_->shared_subscriber->register_client_subscriber(this);
}

DTMQ_PROXY_SDK_API client_subscriber::~client_subscriber() {
  internal_data_->shared_subscriber->unregister_client_subscriber(this);
}

DTMQ_PROXY_SDK_API atfw::util::nostd::nullable<client_subscriber::ptr_t> client_subscriber::create(
    const atfw::dtmq::DChannelIdKey& channel_key, const subscriber_options& options) {
  ctor_guard cg(channel_key, options);
  if (!cg.shared_subscriber) {
    return nullptr;
  }

  return atfw::util::nostd::nullable<ptr_t>(std::make_shared<client_subscriber>(cg));
}

DTMQ_PROXY_SDK_API void client_subscriber::global_receive_channel_event(
    rpc::context& /*ctx*/, const atfw::dtmq::SSChannelEventSync /*event_sync*/) {
  // TODO(owent): implement the logic to handle received channel events
}

DTMQ_PROXY_SDK_API int32_t client_subscriber::global_tick(rpc::context& /*ctx*/) {
  // TODO(owent): implement the logic for periodic tick handling
  return 0;  // Return 0 to indicate no timer events triggered
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelIdKey& client_subscriber::get_channel_key() const noexcept {
  return internal_data_->shared_subscriber->get_channel_key();
}

DTMQ_PROXY_SDK_API const atfw::dtmq::channel_subscriber& client_subscriber::get_subscriber_info() const noexcept {
  return internal_data_->shared_subscriber->get_subscriber_info();
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelConfigure& client_subscriber::get_configure() const noexcept {
  return internal_data_->shared_subscriber->get_configure();
}

DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelOptimisticLock& client_subscriber::get_lock() const noexcept {
  return internal_data_->shared_subscriber->get_lock();
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(event_callback_on_ready_t&& on_ready) {
  internal_data_->event_handler.on_ready = std::move(on_ready);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_ready(const event_callback_on_ready_t& on_ready) {
  internal_data_->event_handler.on_ready = on_ready;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroy(event_callback_on_destroy_t&& on_destroy) {
  internal_data_->event_handler.on_destroy = std::move(on_destroy);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_destroy(
    const event_callback_on_destroy_t& on_destroy) {
  internal_data_->event_handler.on_destroy = on_destroy;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    event_callback_on_update_custom_data_t&& on_update_custom_data) {
  internal_data_->event_handler.on_update_custom_data = std::move(on_update_custom_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_custom_data(
    const event_callback_on_update_custom_data_t& on_update_custom_data) {
  internal_data_->event_handler.on_update_custom_data = on_update_custom_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    event_callback_on_update_private_data_t&& on_update_private_data) {
  internal_data_->event_handler.on_update_private_data = std::move(on_update_private_data);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_update_private_data(
    const event_callback_on_update_private_data_t& on_update_private_data) {
  internal_data_->event_handler.on_update_private_data = on_update_private_data;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(event_callback_on_compact_t&& on_compact) {
  internal_data_->event_handler.on_compact = std::move(on_compact);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_compact(
    const event_callback_on_compact_t& on_compact) {
  internal_data_->event_handler.on_compact = on_compact;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    event_callback_on_receive_text_t&& on_receive_text) {
  internal_data_->event_handler.on_receive_text = std::move(on_receive_text);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_text(
    const event_callback_on_receive_text_t& on_receive_text) {
  internal_data_->event_handler.on_receive_text = on_receive_text;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    event_callback_on_receive_event_t&& on_receive_event) {
  internal_data_->event_handler.on_receive_event = std::move(on_receive_event);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_event(
    const event_callback_on_receive_event_t& on_receive_event) {
  internal_data_->event_handler.on_receive_event = on_receive_event;
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot(
    event_callback_on_receive_snapshot_t&& on_receive_snapshot) {
  internal_data_->event_handler.on_receive_snapshot = std::move(on_receive_snapshot);
}

DTMQ_PROXY_SDK_API void client_subscriber::set_event_callback_on_receive_snapshot(
    const event_callback_on_receive_snapshot_t& on_receive_snapshot) {
  internal_data_->event_handler.on_receive_snapshot = on_receive_snapshot;
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::send_message(
    rpc::context& ctx, atfw::dtmq::DChannelMessageDetail&& detail,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr, bool auto_create_channel,
    bool no_wait) {
  rpc::context::message_holder<atfw::dtmq::channel_subscriber> subscriber_info_holder{ctx};
  protobuf_copy_message(*subscriber_info_holder, internal_data_->shared_subscriber->get_subscriber_info());

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(
      ctx, std::move(*subscriber_info_holder), internal_data_->shared_subscriber->get_channel_key(), std::move(detail),
      compare_and_maybe_reset_lock_ptr, compare_and_maybe_reset_lock_rsp_ptr, auto_create_channel, no_wait)));
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::find_message(rpc::context& ctx, int64_t sequence,
                                                                         atfw::dtmq::DChannelMessage& msg) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      rpc::dtmq::find_message(ctx, internal_data_->shared_subscriber->get_channel_key(),
                              internal_data_->shared_subscriber->get_readonly_replicate_index(), sequence, msg)));
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::page_query_message(
    rpc::context& ctx, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::page_query_message(
      ctx, internal_data_->shared_subscriber->get_channel_key(),
      internal_data_->shared_subscriber->get_readonly_replicate_index(), page_info, msgs)));
}

namespace {
shared_subscriber::ptr_t shared_subscriber::make_shared(const atfw::dtmq::DChannelIdKey& channel_key,
                                                        const client_subscriber::subscriber_options& options) {
  if (channel_key.channel_id().empty() || options.subscriber_key.empty()) {
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

  auto new_shared_subscriber = std::make_shared<shared_subscriber>(channel_key, options);
  add_cached_shared_subscriber(new_shared_subscriber);
  return new_shared_subscriber;
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

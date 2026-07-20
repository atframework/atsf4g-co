// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/dtmq/dtmq_client_subscriber.h"

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

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

#include "log/log_wrapper.h"
#include "memory/rc_ptr.h"

namespace rpc {
namespace dtmq {

namespace {
class shared_subscriber {
 public:
  using ptr_t = std::shared_ptr<shared_subscriber>;

 public:
  static ptr_t make_shared(const atfw::dtmq::DChannelIdKey& channel_key,
                           const client_subscriber::subscriber_options& options);

  // NOLINTNEXTLINE(modernize-pass-by-value)
  shared_subscriber(const atfw::dtmq::DChannelIdKey& channel_key, const client_subscriber::subscriber_options& options)
      : channel_key_(channel_key) {
    subscriber_info_.set_subscriber_server_id(logic_config::me()->get_local_server_id());
    subscriber_info_.set_subscriber_key(options.subscriber_key);
  }

  inline const atfw::dtmq::DChannelIdKey& get_channel_key() const { return channel_key_; }
  inline const atfw::dtmq::channel_subscriber& get_subscriber_info() const { return subscriber_info_; }

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

  std::unordered_set<client_subscriber*> registered_client_;
};
}  // namespace

struct client_subscriber::ctor_guard {
  shared_subscriber::ptr_t shared_subscriber;

  ctor_guard(const atfw::dtmq::DChannelIdKey& input_channel_key, const subscriber_options& input_options)
      : shared_subscriber(shared_subscriber::make_shared(input_channel_key, input_options)) {}
};

struct client_subscriber::subscriber_internal_data {
  atfw::util::nostd::nonnull<shared_subscriber::ptr_t> shared_subscriber;

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
      rpc::dtmq::find_message(ctx, internal_data_->shared_subscriber->get_channel_key(), sequence, msg)));
}

DTMQ_PROXY_SDK_API rpc::result_code_type client_subscriber::page_query_message(
    rpc::context& ctx, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      rpc::dtmq::page_query_message(ctx, internal_data_->shared_subscriber->get_channel_key(), page_info, msgs)));
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

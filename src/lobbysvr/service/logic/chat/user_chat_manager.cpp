// Copyright 2026 atframework

#include "logic/chat/user_chat_manager.h"

#include <string/string_format.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.dtmq.common.pb.h>
#include <protocol/config/lobbysvr_config.pb.h>
#include <protocol/pbdesc/com.struct.chat.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include <memory/object_allocator.h>

#include <rpc/dtmq/dtmq_algorithm.h>
#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>

#include <utility/protobuf_mini_dumper.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "data/user.h"
#include "data/session.h"
#include "rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h"

namespace {

struct global_chat_pending_channel_data;
using global_chat_pending_channel_ptr_t = atfw::util::memory::strong_rc_ptr<global_chat_pending_channel_data>;
struct global_chat_pending_session_data {
  std::weak_ptr<session> sess;
  global_chat_pending_channel_ptr_t sync_data;

  inline global_chat_pending_session_data(std::weak_ptr<session> sess_watcher,
                                          global_chat_pending_channel_ptr_t input_sync_data) noexcept
      : sess(std::move(sess_watcher)), sync_data(std::move(input_sync_data)) {}
};

struct global_chat_manager_private_data {
  time_t last_push_message_tick = 0;

  std::unordered_map<uint64_t, global_chat_pending_channel_ptr_t> reuse_pending_incremental_message;
  std::unordered_map<uint64_t, global_chat_pending_channel_ptr_t> reuse_pending_snapshot_message;
  std::list<global_chat_pending_session_data> pending_sync_message_queue;
};

struct global_chat_pending_channel_data {
  bool ignored;
  uint64_t shared_channel_identify = 0;
  atfw::chat::SCChatChannelSync sync_message;

  std::unordered_map<const session*, std::weak_ptr<session>> already_registered_sessions;

  inline explicit global_chat_pending_channel_data(uint64_t channel_identify)
      : ignored(false), shared_channel_identify(channel_identify) {}
};

static global_chat_manager_private_data& get_global_chat_manager_private_data() {
  static global_chat_manager_private_data ret;
  return ret;
}

static global_chat_pending_channel_ptr_t global_chat_manager_append_incremental_event_message(
    rpc::context& ctx, const rpc::dtmq::client_subscriber& channel, const std::shared_ptr<session>& sess) {
  if (!sess) {
    return nullptr;
  }
  auto& mgr = get_global_chat_manager_private_data();
  auto& reuse_cache_set = mgr.reuse_pending_incremental_message[channel.get_shared_channel_identify()];
  if (!reuse_cache_set) {
    reuse_cache_set = atfw::component::memory::stl::make_strong_rc<global_chat_pending_channel_data>(
        channel.get_shared_channel_identify());
    if (!reuse_cache_set) {
      FCTXLOGERROR(ctx, "Failed to allocate global_chat_pending_channel_data for channel: {}",
                   channel.get_shared_channel_identify());
      return nullptr;
    }

    auto* chat_channel = reuse_cache_set->sync_message.add_chat_channel();
    if (chat_channel != nullptr) {
      user_chat_manager::dump_dtmq_to_chat_channel_metadata(ctx, channel, *chat_channel->mutable_metadata(), false);
    }
  }

  if (reuse_cache_set->already_registered_sessions.find(sess.get()) ==
      reuse_cache_set->already_registered_sessions.end()) {
    reuse_cache_set->already_registered_sessions[sess.get()] = sess;
    // 添加一次待发送队列就行了
    mgr.pending_sync_message_queue.emplace_back(sess, reuse_cache_set);
  }

  return reuse_cache_set;
}

static global_chat_pending_channel_ptr_t global_chat_manager_append_snapshot_event_message(
    rpc::context& ctx, const rpc::dtmq::client_subscriber& channel, const std::shared_ptr<session>& sess) {
  if (!sess) {
    return nullptr;
  }
  auto& mgr = get_global_chat_manager_private_data();

  // 追加snapshot消息时要先移除掉增量消息
  auto iter_incremental = mgr.reuse_pending_incremental_message.find(channel.get_shared_channel_identify());
  if (iter_incremental != mgr.reuse_pending_incremental_message.end()) {
    // 后续推进到这个增量消息，可以忽略发送
    if (iter_incremental->second) {
      iter_incremental->second->ignored = true;
    }
    mgr.reuse_pending_incremental_message.erase(iter_incremental);
  }

  auto& reuse_cache_set = mgr.reuse_pending_snapshot_message[channel.get_shared_channel_identify()];
  if (!reuse_cache_set) {
    reuse_cache_set = atfw::component::memory::stl::make_strong_rc<global_chat_pending_channel_data>(
        channel.get_shared_channel_identify());
    if (!reuse_cache_set) {
      FCTXLOGERROR(ctx, "Failed to allocate global_chat_pending_channel_data for channel: {}",
                   channel.get_shared_channel_identify());
      return nullptr;
    }

    auto* chat_channel = reuse_cache_set->sync_message.add_chat_channel();
    if (chat_channel != nullptr) {
      user_chat_manager::dump_dtmq_to_chat_channel_metadata(ctx, channel, *chat_channel->mutable_metadata(), true);
    }
  }

  if (reuse_cache_set->already_registered_sessions.find(sess.get()) ==
      reuse_cache_set->already_registered_sessions.end()) {
    reuse_cache_set->already_registered_sessions[sess.get()] = sess;
    // 添加一次待发送队列就行了
    mgr.pending_sync_message_queue.emplace_back(sess, reuse_cache_set);
  }

  return reuse_cache_set;
}

static void global_chat_manager_remove_cached_reuse_event_message_by_send(uint64_t shared_channel_identify) {
  auto& mgr = get_global_chat_manager_private_data();
  auto iter_incr = mgr.reuse_pending_incremental_message.find(shared_channel_identify);
  if (iter_incr != mgr.reuse_pending_incremental_message.end()) {
    mgr.reuse_pending_incremental_message.erase(iter_incr);
  }

  auto iter_snap = mgr.reuse_pending_snapshot_message.find(shared_channel_identify);
  if (iter_snap != mgr.reuse_pending_snapshot_message.end()) {
    mgr.reuse_pending_snapshot_message.erase(iter_snap);
  }
}

static void push_pending_message_once(
    rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
    atfw::util::nostd::function_ref<void(rpc::context&, const rpc::dtmq::client_subscriber::ptr_t&,
                                         const session::ptr_t&, user_chat_manager&)>
        func) {
  if (!subscriber) {
    return;
  }
  auto local_private_data = subscriber->get_local_private_data();
  if (local_private_data.empty()) {
    FWLOGERROR("local private data is empty for subscriber: {}, channel: {}", subscriber->get_subscriber_key(),
               subscriber->get_channel_key().channel_id());
    return;
  }
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  user_chat_manager* chat_mgr = reinterpret_cast<user_chat_manager*>(local_private_data[0]);
  if (nullptr == chat_mgr) {
    FWLOGERROR("local private data is null for subscriber: {}, channel: {}", subscriber->get_subscriber_key(),
               subscriber->get_channel_key().channel_id());
    return;
  }

  // if session is removed, then the user is removed, so no need to send sync messages
  auto sess = chat_mgr->get_owner().get_session();
  if (!sess) {
    return;
  }

  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  int64_t pending_message_buffer_max_count = server_cfg.chat().push_message_buffer_max_count();
  if (pending_message_buffer_max_count <= 0) {
    pending_message_buffer_max_count = 100000;
  }
  // Too many pending messages, just drop it, and let the client to re-sync
  if (get_global_chat_manager_private_data().pending_sync_message_queue.size() >=
      static_cast<size_t>(pending_message_buffer_max_count)) {
    return;
  }

  func(ctx, subscriber, sess, *chat_mgr);
}

static void user_chat_callback_receive_raw_message_fn(rpc::context& ctx,
                                                      const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                                      const ::atfw::dtmq::DChannelMessage& data) {
  push_pending_message_once(
      ctx, subscriber,
      // =====================================================================================================================
      [&data](rpc::context& child_ctx, const rpc::dtmq::client_subscriber::ptr_t& sub_subscriber,
              const session::ptr_t& sess, user_chat_manager& /*chat_mgr*/) {
        global_chat_pending_channel_ptr_t sync_data =
            global_chat_manager_append_incremental_event_message(child_ctx, *sub_subscriber, sess);

        if (!sync_data) {
          return;
        }

        ::atfw::chat::DChatChannelData* chat_channel = nullptr;
        if (sync_data->sync_message.chat_channel_size() <= 0) {
          chat_channel = sync_data->sync_message.add_chat_channel();
        } else {
          chat_channel = sync_data->sync_message.mutable_chat_channel(0);
        }
        if (chat_channel == nullptr) {
          return;
        }

        // 如果已经添加过了，则不用重复添加。（一个频道可能同时下发到多个订阅者，事件连续触发多次）
        if (chat_channel->mutable_incremental()->message_list_size() > 0) {
          const auto& last_message = chat_channel->mutable_incremental()->message_list(
              chat_channel->mutable_incremental()->message_list_size() - 1);
          if (last_message.sequence() == data.sequence()) {
            return;
          }
        }

        protobuf_copy_message(*chat_channel->mutable_incremental()->add_message_list(), data);
        if (data.sequence() > chat_channel->metadata().last_message_sequence()) {
          chat_channel->mutable_metadata()->set_last_message_sequence(data.sequence());
        }
      });
}

static void user_chat_callback_receive_ready_or_snapshot_fn(rpc::context& ctx,
                                                            const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                                            int64_t snapshot_sequence) {
  push_pending_message_once(
      ctx, subscriber,
      // =====================================================================================================================
      [&snapshot_sequence](rpc::context& child_ctx, const rpc::dtmq::client_subscriber::ptr_t& sub_subscriber,
                           const session::ptr_t& sess, user_chat_manager& /*chat_mgr*/) {
        global_chat_pending_channel_ptr_t sync_data =
            global_chat_manager_append_snapshot_event_message(child_ctx, *sub_subscriber, sess);

        if (!sync_data) {
          return;
        }

        ::atfw::chat::DChatChannelData* chat_channel = nullptr;
        if (sync_data->sync_message.chat_channel_size() <= 0) {
          chat_channel = sync_data->sync_message.add_chat_channel();
        } else {
          chat_channel = sync_data->sync_message.mutable_chat_channel(0);
        }
        if (chat_channel == nullptr) {
          return;
        }

        // 如果已经添加过了，则不用重复添加。（一个频道可能同时下发到多个订阅者，事件触发多次）
        if (chat_channel->has_snapshot() && chat_channel->metadata().last_message_sequence() == snapshot_sequence &&
            chat_channel->metadata().custom_data_sequence() == sub_subscriber->get_custom_data_sequence() &&
            chat_channel->metadata().private_data_sequence() == sub_subscriber->get_private_data_sequence()) {
          return;
        }

        user_chat_manager::dump_dtmq_to_chat_channel_snapshot(
            child_ctx, *sub_subscriber, *chat_channel->mutable_metadata(), *chat_channel->mutable_snapshot());
      });
}

static rpc::dtmq::client_subscriber::event_callback_set_ptr_t build_shared_chat_channel_event_callback_set() {
  rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
      rpc::dtmq::client_subscriber::create_event_callback_set();

  rpc::dtmq::client_subscriber::set_event_callback_on_ready(*ret, [](rpc::context& ctx,
                                                                     const rpc::dtmq::client_subscriber::ptr_t&
                                                                         subscriber) {
    push_pending_message_once(
        ctx, subscriber,
        // =====================================================================================================================
        [](rpc::context& child_ctx, const rpc::dtmq::client_subscriber::ptr_t& sub_subscriber,
           const session::ptr_t& /*sess*/, user_chat_manager& /*chat_mgr*/) {
          user_chat_callback_receive_ready_or_snapshot_fn(child_ctx, sub_subscriber,
                                                          sub_subscriber->get_last_message_sequence());
        });
  });

  rpc::dtmq::client_subscriber::set_event_callback_on_receive_snapshot_finished(*ret, [](rpc::context& ctx,
                                                                                         const rpc::dtmq::
                                                                                             client_subscriber::ptr_t&
                                                                                                 subscriber,
                                                                                         const ::atfw::dtmq::
                                                                                             DChannelSnapshot& /*data*/,
                                                                                         int32_t result_code) {
    if (result_code < 0) {
      FCTXLOGERROR(ctx, "Failed to receive snapshot finished: {}", result_code);
      return;
    }

    push_pending_message_once(
        ctx, subscriber,
        // =====================================================================================================================
        [](rpc::context& child_ctx, const rpc::dtmq::client_subscriber::ptr_t& sub_subscriber,
           const session::ptr_t& /*sess*/, user_chat_manager& /*chat_mgr*/) {
          user_chat_callback_receive_ready_or_snapshot_fn(child_ctx, sub_subscriber,
                                                          sub_subscriber->get_last_message_sequence());
        });
  });

  rpc::dtmq::client_subscriber::set_event_callback_on_receive_raw_message(*ret,
                                                                          user_chat_callback_receive_raw_message_fn);
  return ret;
}

static rpc::dtmq::client_subscriber::event_callback_set_ptr_t& get_shared_chat_channel_event_callback_set() {
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret = build_shared_chat_channel_event_callback_set();
  return ret;
}

static void global_chat_manager_send_pending_message_once(rpc::context& ctx, int64_t max_count) {
  auto invoke_result = rpc::async_invoke(
      ctx, "user_chat_manager.push_channel_loop_once", [max_count](rpc::context& child_ctx) -> rpc::result_code_type {
        auto& mgr = get_global_chat_manager_private_data();
        auto loop_message_count = max_count;
        int32_t skip_action_count = 100;
        while (loop_message_count > 0 && !mgr.pending_sync_message_queue.empty()) {
          global_chat_pending_session_data current_data{std::move(mgr.pending_sync_message_queue.front())};
          mgr.pending_sync_message_queue.pop_front();

          if (!current_data.sync_data) {
            // 多次空操作消耗一次发送操作，防止空操作过多消费过多CPU
            if (--skip_action_count <= 0) {
              --loop_message_count;
              skip_action_count = 100;
            }
            continue;
          }

          // 一旦数据开始发送，就不允许修改，不允许再被复用修改
          global_chat_manager_remove_cached_reuse_event_message_by_send(
              current_data.sync_data->shared_channel_identify);

          // 无数据或老数据可以忽略则不用再发送了
          if (current_data.sync_data->ignored || current_data.sync_data->sync_message.chat_channel_size() <= 0) {
            // 多次空操作消耗一次发送操作，防止空操作过多消费过多CPU
            if (--skip_action_count <= 0) {
              --loop_message_count;
              skip_action_count = 100;
            }
            continue;
          }

          // 会话已退出或无频道数据则不用再发送了
          if (current_data.sess.expired()) {
            // 多次空操作消耗一次发送操作，防止空操作过多消费过多CPU
            if (--skip_action_count <= 0) {
              --loop_message_count;
              skip_action_count = 100;
            }
            continue;
          }

          auto sess = current_data.sess.lock();
          if (!sess) {
            // 多次空操作消耗一次发送操作，防止空操作过多消费过多CPU
            if (--skip_action_count <= 0) {
              --loop_message_count;
              skip_action_count = 100;
            }
            continue;
          }

          int32_t result_code = RPC_AWAIT_CODE_RESULT(rpc::lobbysvrclientservice::send_chat_channel_sync(
              child_ctx, current_data.sync_data->sync_message, *sess));
          if (result_code < 0) {
            FCTXLOGERROR(child_ctx, "Failed to send_chat_channel_sync to {}: {}({})", *sess, result_code,
                         protobuf_mini_dumper_get_error_msg(result_code));
          }
          --loop_message_count;
        }
        RPC_RETURN_CODE(0);
      });

  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "Failed to invoke push_channel_loop_once: {}({})", *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

}  // namespace

int32_t user_chat_manager::global_tick(rpc::context& ctx) {
  global_chat_manager_private_data& global_data = get_global_chat_manager_private_data();
  time_t now_tick =
      (atfw::util::time::time_utility::get_now() * 10) + (atfw::util::time::time_utility::get_now_usec() / 100000);
  if (global_data.last_push_message_tick == now_tick) {
    return 0;
  }
  global_data.last_push_message_tick = now_tick;

  if (global_data.pending_sync_message_queue.empty()) {
    return 0;
  }

  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>();
  int64_t max_push_message_per_tick = server_cfg.chat().push_message_per_second() / 10;
  if (max_push_message_per_tick <= 0) {
    max_push_message_per_tick = 200;
  }
  global_chat_manager_send_pending_message_once(ctx, max_push_message_per_tick);

  // leak checking, if pending_sync_message_queue is empty, then reuse_pending_incremental_message and
  // reuse_pending_snapshot_message should be empty too
  if (global_data.pending_sync_message_queue.empty()) {
    if (!global_data.reuse_pending_incremental_message.empty() || !global_data.reuse_pending_snapshot_message.empty()) {
      FCTXLOGERROR(ctx,
                   "pending_sync_message_queue is empty, but reuse_pending_incremental_message or "
                   "reuse_pending_snapshot_message is not empty, this should not happen");
      global_data.reuse_pending_snapshot_message.clear();
      global_data.reuse_pending_incremental_message.clear();
    }
  }

  return 0;
}

user_chat_manager::user_chat_manager(user& owner)
    : owner_(&owner), last_send_to_world_channel_timepoint_unix_sec_(0) {}

user_chat_manager::~user_chat_manager() {}

rpc::result_code_type user_chat_manager::login_init(rpc::context& ctx) {
  subscriber_key_ = atfw::util::string::format("user:{}:{}", owner_->get_zone_id(), owner_->get_user_id());
  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(this)};

  // 换session时，大部分情况下客户端已经拿到了最新消息，如果拿不到，下一次心跳也会触发修正
  // 如果客户端没有缓存，后面仍然会通过一次 get_snapshot 拉取最新数据
  // 所以不需要立即补发，以便减少额外的负载

  // 创建聊天频道
  if (!world_chat_channel_) {
    // TODO(ANY): 如果以后世界频道要分片，这里添加分片逻辑
    uint64_t partition_id = 0;
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC));
    channel_key.set_channel_id(rpc::dtmq::make_world_partition_channel_id(
        channel_key.channel_type(), logic_config::me()->get_local_world_id(), partition_id));
    world_chat_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    if (world_chat_channel_) {
      world_chat_channel_->set_local_private_data(local_private_data);
    } else {
      FCTXLOGERROR(ctx, "Failed to create world chat channel {}:{}, maybe configure is missing.",
                   channel_key.channel_type(), channel_key.channel_id());
    }
  }

  if (!private_chat_channel_) {
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE));
    channel_key.set_channel_id(
        rpc::dtmq::make_unicast_channel_id(channel_key.channel_type(), owner_->get_zone_id(), owner_->get_user_id()));
    private_chat_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    if (private_chat_channel_) {
      private_chat_channel_->set_local_private_data(local_private_data);
    } else {
      FCTXLOGERROR(ctx, "Failed to create private chat channel {}:{}, maybe configure is missing.",
                   channel_key.channel_type(), channel_key.channel_id());
    }
  }

  // 创建系统通知 Channel(生命周期短)
  if (!sys_notification_channel_) {
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_NOTIFICATION));
    channel_key.set_channel_id(rpc::dtmq::make_world_broadcast_channel_id(channel_key.channel_type(),
                                                                          logic_config::me()->get_local_world_id()));
    sys_notification_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    if (sys_notification_channel_) {
      sys_notification_channel_->set_local_private_data(local_private_data);
    } else {
      FCTXLOGERROR(ctx, "Failed to create system notification channel {}:{}, maybe configure is missing.",
                   channel_key.channel_type(), channel_key.channel_id());
    }
  }

  // 创建系统公告 Channel(生命周期长)
  if (!sys_announcement_channel_) {
    rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
    atfw::dtmq::DChannelIdKey channel_key;
    channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_ANNOUNCEMENT));
    channel_key.set_channel_id(rpc::dtmq::make_world_broadcast_channel_id(channel_key.channel_type(),
                                                                          logic_config::me()->get_local_world_id()));
    sys_announcement_channel_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
    if (sys_announcement_channel_) {
      sys_announcement_channel_->set_local_private_data(local_private_data);
    } else {
      FCTXLOGERROR(ctx, "Failed to create system announcement channel {}:{}, maybe configure is missing.",
                   channel_key.channel_type(), channel_key.channel_id());
    }
  }

  RPC_RETURN_CODE(0);
}

void user_chat_manager::foreach_channel(
    atfw::util::nostd::function_ref<bool(const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&)>
        callback) const {
  const rpc::dtmq::client_subscriber::ptr_t* channels[] = {&world_chat_channel_, &private_chat_channel_,
                                                           &sys_notification_channel_, &sys_announcement_channel_};
  for (const auto* channel_ptr : channels) {
    if (*channel_ptr) {
      if (!callback(*channel_ptr)) {
        break;
      }
    }
  }
}

int32_t user_chat_manager::check_writable(rpc::context& /*ctx*/, const atfw::dtmq::DChannelIdKey& channel_key) const {
  if (subscriber_key_.empty()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED;
  }

  if (sys_notification_channel_ &&
      (channel_key.channel_type() == static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_NOTIFICATION) ||
       channel_key.channel_id() == sys_notification_channel_->get_channel_key().channel_id())) {
    return PROJECT_NAMESPACE_ID::EN_ERR_CHAT_ACCESS_DENY_FOR_WRITE;
  }

  if (sys_announcement_channel_ &&
      (channel_key.channel_type() == static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_ANNOUNCEMENT) ||
       channel_key.channel_id() == sys_announcement_channel_->get_channel_key().channel_id())) {
    return PROJECT_NAMESPACE_ID::EN_ERR_CHAT_ACCESS_DENY_FOR_WRITE;
  }

  // TODO(ANY): 细化世界频道的限频
  if (world_chat_channel_ && channel_key.channel_type() == world_chat_channel_->get_channel_key().channel_type()) {
    if (last_send_to_world_channel_timepoint_unix_sec_ == atfw::util::time::time_utility::get_now()) {
      return PROJECT_NAMESPACE_ID::EN_ERR_CHAT_WRITE_TOO_FREQUENT;
    }
  }

  switch (channel_key.channel_type()) {
    case atfw::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC:
    case atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE:
      return 0;
    default:
      return PROJECT_NAMESPACE_ID::EN_ERR_CHAT_ACCESS_DENY_FOR_WRITE;
  }
}

rpc::result_code_type user_chat_manager::send_text_message(rpc::context& ctx,
                                                           const atfw::dtmq::DChannelIdKey& channel_key,
                                                           gsl::string_view text) {
  uint32_t channel_type = parse_channel_type_from_channel_id(channel_key);
  if (channel_type == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  int32_t ret = check_writable(ctx, channel_key);
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  rpc::context::message_holder<atfw::dtmq::channel_subscriber> sender_info{ctx};
  rpc::context::message_holder<atfw::dtmq::DChannelMessageDetail> message_detail{ctx};

  sender_info->set_subscriber_key(subscriber_key_);
  message_detail->set_text(text.data(), text.size());

  ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(ctx, std::move(*sender_info), channel_key,
                                                      std::move(*message_detail), nullptr, nullptr, true));

  if (ret >= 0) {
    // TODO(ANY): 细化世界频道的限频
    if (world_chat_channel_ && channel_type == world_chat_channel_->get_channel_key().channel_type()) {
      last_send_to_world_channel_timepoint_unix_sec_ = atfw::util::time::time_utility::get_now();
    }
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type user_chat_manager::send_event_message(rpc::context& ctx,
                                                            const atfw::dtmq::DChannelIdKey& channel_key,
                                                            google::protobuf::Any&& event_data) {
  uint32_t channel_type = parse_channel_type_from_channel_id(channel_key);
  if (channel_type == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  int32_t ret = check_writable(ctx, channel_key);
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  rpc::context::message_holder<atfw::dtmq::channel_subscriber> sender_info{ctx};
  rpc::context::message_holder<atfw::dtmq::DChannelMessageDetail> message_detail{ctx};

  sender_info->set_subscriber_key(subscriber_key_);
  protobuf_move_message(*message_detail->mutable_event(), std::move(event_data));

  ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(ctx, std::move(*sender_info), channel_key,
                                                      std::move(*message_detail), nullptr, nullptr, true));

  if (ret >= 0) {
    // TODO(ANY): 细化世界频道的限频
    if (world_chat_channel_ && channel_type == world_chat_channel_->get_channel_key().channel_type()) {
      last_send_to_world_channel_timepoint_unix_sec_ = atfw::util::time::time_utility::get_now();
    }
  }
  RPC_RETURN_CODE(ret);
}

int32_t user_chat_manager::get_snapshot(rpc::context& ctx, gsl::string_view channel_id,
                                        atfw::chat::DChatChannelData& data) {
  int32_t ret = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND;
  foreach_channel(
      [&ret, &ctx, &channel_id, &data](const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>& channel) {
        if (channel_id != channel->get_channel_key().channel_id()) {
          return true;
        }

        dump_dtmq_to_chat_channel_snapshot(ctx, *channel, *data.mutable_metadata(), *data.mutable_snapshot());

        // 我们要求客户端拉取一次之后才会主动推送频道通知
        // 如果没设置回调函数组，说明之前没拉过，设置回调函数组也能触发后续的主动通知
        if (!channel->get_shared_event_callback_set()) {
          setup_subscriber_callback(channel);
        }

        ret = 0;
        return false;
      });

  return ret;
}

int32_t user_chat_manager::receive_heartbeat(rpc::context& ctx, const atfw::dtmq::DChannelSyncPoint& sync_point,
                                             atfw::chat::SCChatChannelSync& sync_msg) {
  auto channel_ptr = get_channel_by_key(sync_point.channel_key());
  if (!channel_ptr) {
    return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND;
  }

  rpc::dtmq::client_subscriber::query_options options{};
  ::atfw::chat::DChatChannelData* sync_channel = nullptr;
  options.start_sequence = sync_point.last_sequence();
  bool need_snapshot = channel_ptr->get_last_removed_sequence() > sync_point.last_sequence();
  if (!need_snapshot) {
    channel_ptr->query_cached_message(
        ctx,
        atfw::util::nostd::function_ref<bool(const atfw::dtmq::DChannelMessage&)>(
            [&ctx, &need_snapshot, &sync_point, &sync_channel, &sync_msg](const atfw::dtmq::DChannelMessage& msg) {
              if (msg.sequence() == sync_point.last_sequence() && sync_point.last_hash_code() != 0 &&
                  msg.hash_code() != sync_point.last_hash_code()) {
                need_snapshot = true;
                return false;
              }

              // 跳过已有消息
              if (msg.sequence() == sync_point.last_sequence()) {
                return true;
              }

              if (sync_channel == nullptr) {
                sync_channel = sync_msg.add_chat_channel();
              }
              if (sync_channel == nullptr) {
                FCTXLOGERROR(ctx, "malloc chat_channel is failed");
                return true;
              }

              protobuf_copy_message(*sync_channel->mutable_incremental()->add_message_list(), msg);
              return true;
            }),
        options);
  }

  if (need_snapshot) {
    if (sync_channel == nullptr) {
      sync_channel = sync_msg.add_chat_channel();
    }
    if (sync_channel == nullptr) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
    }
    dump_dtmq_to_chat_channel_snapshot(ctx, *channel_ptr, *sync_channel->mutable_metadata(),
                                       *sync_channel->mutable_snapshot());
    return 0;
  }

  if (sync_channel != nullptr && sync_channel->has_incremental()) {
    dump_dtmq_to_chat_channel_metadata(ctx, *channel_ptr, *sync_channel->mutable_metadata(), false);
  }
  return 0;
}

int32_t user_chat_manager::build_dtmq_channel_key_from_chat_channel_key(
    const atfw::chat::DChatChannelKey& chat_channel_key, atfw::dtmq::DChannelIdKey& dtmq_channel_key) {
  switch (chat_channel_key.key_type_case()) {
    case atfw::chat::DChatChannelKey::KeyTypeCase::kWorldPartitionChannel: {
      // TODO(ANY): 限制partition_id的范围，防止客户端恶意构造
      uint64_t partition_id = chat_channel_key.world_partition_channel();
      if (partition_id > 0) {
        return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL;
      }
      dtmq_channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC));
      dtmq_channel_key.set_channel_id(
          rpc::dtmq::make_world_partition_channel_id(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC),
                                                     logic_config::me()->get_local_world_id(), partition_id));
      return 0;
    }
    case atfw::chat::DChatChannelKey::KeyTypeCase::kPrivateChannel:
      dtmq_channel_key.set_channel_type(static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE));
      dtmq_channel_key.set_channel_id(rpc::dtmq::make_unicast_channel_id(dtmq_channel_key.channel_type(),
                                                                         chat_channel_key.private_channel().zone_id(),
                                                                         chat_channel_key.private_channel().user_id()));
      return 0;
    default:
      return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL;
  }
}

void user_chat_manager::dump_dtmq_to_chat_channel_metadata(rpc::context& /*ctx*/,
                                                           const rpc::dtmq::client_subscriber& channel,
                                                           atfw::chat::DChatChannelMeta& metadata,
                                                           bool with_configure) {
  protobuf_copy_message(*metadata.mutable_channel_key(), channel.get_channel_key());

  if (with_configure) {
    protobuf_copy_message(*metadata.mutable_channel_configure(), channel.get_configure());
  }
  metadata.set_last_message_sequence(channel.get_last_message_sequence());
  metadata.set_custom_data_sequence(channel.get_custom_data_sequence());
  metadata.set_private_data_sequence(channel.get_private_data_sequence());

  if (channel.is_ready()) {
    metadata.set_create_sequence(channel.get_create_sequence());
    *metadata.mutable_create_timepoint() = protobuf_from_system_clock(channel.get_create_timepoint());
  }
  if (channel.is_destroyed()) {
    metadata.set_destroy_sequence(channel.get_destroy_sequence());
    *metadata.mutable_destroy_timepoint() = protobuf_from_system_clock(channel.get_destroy_timepoint());
  }

  metadata.set_last_removed_sequence(channel.get_last_removed_sequence());
}

void user_chat_manager::dump_dtmq_to_chat_channel_snapshot(rpc::context& ctx,
                                                           const rpc::dtmq::client_subscriber& channel,
                                                           atfw::chat::DChatChannelMeta& metadata,
                                                           atfw::chat::DChatChannelSnapshot& snapshot) {
  dump_dtmq_to_chat_channel_metadata(ctx, channel, metadata, true);

  snapshot.clear_message_list();
  channel.query_cached_message(ctx, [&snapshot](const atfw::dtmq::DChannelMessage& msg) {
    protobuf_copy_message(*snapshot.add_message_list(), msg);
    return true;
  });
}

uint32_t user_chat_manager::parse_channel_type_from_channel_id(const atfw::dtmq::DChannelIdKey& channel_key) {
  switch (channel_key.channel_type()) {
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC):
      return rpc::dtmq::parse_world_partition_channel_type_from_channel_id(channel_key.channel_id());
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE):
      return rpc::dtmq::parse_unicast_channel_type_from_channel_id(channel_key.channel_id());
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_NOTIFICATION):
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_ANNOUNCEMENT):
      return rpc::dtmq::parse_world_broadcast_channel_type_from_channel_id(channel_key.channel_id());
    default:
      return 0;
  }
}

rpc::dtmq::client_subscriber::ptr_t user_chat_manager::get_channel_by_key(
    const atfw::dtmq::DChannelIdKey& channel_key) const {
  switch (channel_key.channel_type()) {
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC):
      if (world_chat_channel_ && world_chat_channel_->get_channel_key().channel_id() == channel_key.channel_id()) {
        return world_chat_channel_;
      } else {
        return nullptr;
      }
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE):
      if (private_chat_channel_ && private_chat_channel_->get_channel_key().channel_id() == channel_key.channel_id()) {
        return private_chat_channel_;
      } else {
        return nullptr;
      }
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_NOTIFICATION):
      if (sys_notification_channel_ &&
          sys_notification_channel_->get_channel_key().channel_id() == channel_key.channel_id()) {
        return sys_notification_channel_;
      } else {
        return nullptr;
      }
    case static_cast<uint32_t>(atfw::chat::EN_CHAT_CHANNEL_TYPE_SYS_ANNOUNCEMENT):
      if (sys_announcement_channel_ &&
          sys_announcement_channel_->get_channel_key().channel_id() == channel_key.channel_id()) {
        return sys_announcement_channel_;
      } else {
        return nullptr;
      }
    default:
      return nullptr;
  }
}

void user_chat_manager::setup_subscriber_callback(const rpc::dtmq::client_subscriber::ptr_t& channel) {
  if (!channel) {
    return;
  }

  channel->set_shared_event_callback_set(get_shared_chat_channel_event_callback_set());
}

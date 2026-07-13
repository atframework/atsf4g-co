// Copyright 2026 atframework
// @brief Created by owent

#include "data/mq_channel.h"

#include <utility/random_engine.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel_config_dtmq_index.h>
#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <logic/logic_server_setup.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/task_type_traits.h>
#include <rpc/db/local_db_interface.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>

#include <rpc/dtmq/dtmq_algorithm.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "data/mq_channel_wal_handle.h"
#include "logic/mq_channel_manager.h"

#ifdef max
#  undef max
#endif

namespace {
static atfw::util::distributed_system::wal_duration get_mq_channel_ttl() {
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
  auto timeout =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(dtmq_proxysvr_cfg.remove_ttl());
  if (timeout < std::chrono::seconds{1}) {
    timeout = std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{1814400});
  }

  return timeout;
}
}  // namespace

void mq_channel::mq_channel_accessor::update_timer(mq_channel& mq_channel, rpc::context& ctx, bool force) {
  mq_channel.update_timer(ctx, force);
}

void mq_channel::mq_channel_accessor::remove_timer(mq_channel& mq_channel) { mq_channel.remove_timer(); }

mq_channel::mq_channel(mq_channel_manager& /*manager*/, const atfw::dtmq::DChannelIdKey& channel_key,
                       const atfw::dtmq::DChannelConfigure& configure)
    :  // owner_(&manager),
      sequence_allocator_(0),
      compact_stateful_sequence_(0),
      status_(channel_status::kNone),
      readonly_replicate_index_(0),
      readonly_replicate_configure_count_(0),
      remove_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_save_timepoint_{std::chrono::system_clock::from_time_t(0)},
      lost_last_subscriber_timepoint_{std::chrono::system_clock::from_time_t(0)},
      next_notify_readonly_subscribe_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_writable_notify_readonly_timepoint_{std::chrono::system_clock::from_time_t(0)},
      next_init_subscribe_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_result_code_(PROJECT_NAMESPACE_ID::err::EN_SUCCESS),
      custom_data_sequence_(0),
      private_data_sequence_(0),
      is_loading_snapshot_(false),
      is_dirty_(false),
      resolved_transfer_etcd_revision_(0),
      server_distribution_etcd_revision_(0) {
  protobuf_copy_message(channel_key_, channel_key);
  protobuf_copy_message(configure_, configure);

  shared_wal_object_ = create_mq_channel_object(*this, configure);
  wal_publisher_ = create_mq_channel_publisher(*this, configure);

  next_send_oss_time_ = atfw::util::time::time_utility::now();

  FWLOGINFO("channel {}({}) constructed.", get_channel_id(), reinterpret_cast<const void*>(this));
}

mq_channel::~mq_channel() {
  // Remove timer
  remove_timer();

  FWLOGINFO("channel {}({}) destructed.", get_channel_id(), reinterpret_cast<const void*>(this));
}

void mq_channel::load(const atfw::dtmq::DChannelMetadata& metadata, const atfw::dtmq::DChannelRuntime& runtime) {
  remove_timepoint_ = protobuf_to_system_clock(metadata.destroy_timepoint());
  if (remove_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    downgrade_to_readable(readonly_replicate_index_);
  }

  // 后面所有数据走合并逻辑，以防调用到老的load导致meta和runtime回退
  if (sequence_allocator_ < metadata.last_sequence()) {
    sequence_allocator_ = metadata.last_sequence();
  }

  // custom data 和 private data 可能比较大，如果未变化会被压缩，不会下发
  if (metadata.custom_data_sequence() > custom_data_sequence_) {
    protobuf_copy_message(custom_data_, metadata.custom_data());
    custom_data_sequence_ = metadata.custom_data_sequence();
  }

  if (runtime.private_data_sequence() > private_data_sequence_) {
    protobuf_copy_message(private_data_, runtime.private_data());
    private_data_sequence_ = runtime.private_data_sequence();
  }
  if (protobuf_to_system_clock(runtime.lost_last_subscriber_timepoint()) > lost_last_subscriber_timepoint_) {
    lost_last_subscriber_timepoint_ = protobuf_to_system_clock(runtime.lost_last_subscriber_timepoint());
  }
  if (runtime.compact_stateful_sequence() > compact_stateful_sequence_) {
    compact_stateful_sequence_ = runtime.compact_stateful_sequence();
  }

  if (runtime.last_removed_sequence() > 0) {
    compact_sequence(runtime.last_removed_sequence());
  }
}

void mq_channel::load(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_dtmq_channel_record& record) {
  load(record.channel_metadata(), record.runtime_data());

  if (shared_wal_object_) {
    mq_channel_wal_publisher_type::log_container_type container;
    int64_t last_removed_key = 0;
    if (nullptr != shared_wal_object_->get_last_removed_key()) {
      last_removed_key = *shared_wal_object_->get_last_removed_key();
    }
    for (const auto& log : record.record_set().record()) {
      if (log.sequence() <= compact_stateful_sequence_ || log.sequence() <= last_removed_key) {
        continue;
      }

      container.emplace_back(atfw::memory::stl::make_strong_rc<atfw::dtmq::DChannelMessage>(log));
    }
    shared_wal_object_->assign_logs(container);
  }

  protobuf_copy_message(lock_, record.lock());

  // load订阅者缓存(转移时恢复订阅)
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
  auto now = atfw::util::time::time_utility::now();
  auto subscriber_timeout_conf =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(dtmq_proxysvr_cfg.subscriber_timeout());
  if (subscriber_timeout_conf < std::chrono::seconds{1}) {
    subscriber_timeout_conf =
        std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(subscriber_timeout_conf);
  }

  auto subscriber_cache = rpc::make_shared_message<atfw::dtmq::channel_subscriber_cache>(ctx);
  if (false == record.subscriber_cache().UnpackTo(&(*subscriber_cache))) {
    FCTXLOGERROR(ctx, "mq channel {} package subscribers failed. message: {}", get_channel_id(),
                 subscriber_cache->InitializationErrorString());
  } else {
    for (const auto& subscriber_info : subscriber_cache->subscriber()) {
      if (protobuf_to_system_clock(subscriber_info.last_heartbeat_timepoint()) + subscriber_timeout_conf < now) {
        continue;
      }

      // 合并订阅信息，内存数据优先
      subscribe(ctx, subscriber_info, subscriber_info.last_heartbeat_sequence(), 0, true);
    }
  }

  // 有可能await后，负载发生变化。本节点被迁出 writable
  if (should_be_writable()) {
    upgrade_to_writable();
  } else if (!is_readonly()) {
    upgrade_to_readonly(target_distribution_.current_readonly_server_index);
  }
}

void mq_channel::dump(atfw::dtmq::DChannelMetadata& metadata, bool with_configure, bool with_custom_data) const {
  protobuf_copy_message(*metadata.mutable_channel_key(), get_channel_key());

  if (remove_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    *metadata.mutable_destroy_timepoint() = protobuf_from_system_clock(remove_timepoint_);
  } else {
    metadata.clear_destroy_timepoint();
  }
  metadata.set_last_sequence(get_last_message_sequence());
  metadata.set_last_hash_code(get_last_hash_code());

  if (with_custom_data) {
    if (!custom_data_.type_url().empty() || custom_data_sequence_ > 0) {
      protobuf_copy_message(*metadata.mutable_custom_data(), custom_data_);
    }
    metadata.set_custom_data_sequence(custom_data_sequence_);
  }

  if (with_configure) {
    protobuf_copy_message(*metadata.mutable_channel_configure(), configure_);
  }
}

void mq_channel::dump(atfw::dtmq::DChannelRuntime& runtime, bool with_private_data) const {
  runtime.set_compact_stateful_sequence(compact_stateful_sequence_);
  *runtime.mutable_lost_last_subscriber_timepoint() = protobuf_from_system_clock(lost_last_subscriber_timepoint_);

  if (with_private_data) {
    if (!private_data_.type_url().empty() || private_data_sequence_ > 0) {
      protobuf_copy_message(*runtime.mutable_private_data(), private_data_);
    }
    runtime.set_private_data_sequence(private_data_sequence_);
  }

  if (wal_publisher_) {
    if (nullptr != wal_publisher_->get_log_manager().get_last_removed_key()) {
      runtime.set_last_removed_sequence(*wal_publisher_->get_log_manager().get_last_removed_key());
    }
  }
}

void mq_channel::dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_dtmq_channel_record& record) const {
  record.set_channel_id(get_channel_id());

  dump(*record.mutable_channel_metadata(), false, true);
  dump(*record.mutable_runtime_data(), true);

  if (wal_publisher_) {
    auto* record_set = record.mutable_record_set();
    for (const auto& log : wal_publisher_->get_log_manager().get_all_logs()) {
      if (!log) {
        continue;
      }

      protobuf_copy_message(*record_set->add_record(), *log);
    }

    // dump订阅者缓存(转移时恢复订阅)
    const auto& dtmq_proxysvr_cfg =
        logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
    auto now = atfw::util::time::time_utility::now();
    auto subscriber_timeout_conf = protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
        dtmq_proxysvr_cfg.subscriber_timeout());
    if (subscriber_timeout_conf < std::chrono::seconds{1}) {
      subscriber_timeout_conf =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(subscriber_timeout_conf);
    }

    auto subscriber_cache = rpc::make_shared_message<atfw::dtmq::channel_subscriber_cache>(ctx);

    auto all_subscribers = wal_publisher_->get_subscribe_manager().all_range();
    int subscriber_count = 0;
    for (auto iter = all_subscribers.first; iter != all_subscribers.second; ++iter) {
      ++subscriber_count;
    }
    subscriber_cache->mutable_subscriber()->Reserve(subscriber_count);
    for (auto iter = all_subscribers.first; iter != all_subscribers.second; ++iter) {
      if (protobuf_to_system_clock(iter->second->get_private_data().last_heartbeat_timepoint()) +
              subscriber_timeout_conf <
          now) {
        continue;
      }

      auto* copy_subscriber = subscriber_cache->add_subscriber();
      if (nullptr == copy_subscriber) {
        FCTXLOGERROR(ctx, "mq channel {} transfer: malloc subscriber {} data failed.", get_channel_id(),
                     iter->second->get_key());
        continue;
      }

      protobuf_copy_message(*copy_subscriber, iter->second->get_private_data());
    }

    if (false == record.mutable_subscriber_cache()->PackFrom(*subscriber_cache)) {
      FCTXLOGERROR(ctx, "mq channel {} package subscribers failed. message: {}", get_channel_id(),
                   subscriber_cache->InitializationErrorString());
    }
  }

  if (!lock_.lock_holder().empty()) {
    protobuf_copy_message(*record.mutable_lock(), lock_);
  }
}

void mq_channel::dump(atfw::dtmq::DChannelSnapshot& snapshot, bool with_configure, bool with_custom_data,
                      bool with_private_data) const {
  dump(*snapshot.mutable_channel_metadata(), with_configure, with_custom_data);
  dump(*snapshot.mutable_channel_runtime(), with_private_data);

  if (shared_wal_object_) {
    snapshot.mutable_messages()->Reserve(static_cast<int>(shared_wal_object_->get_all_logs().size()));
    for (const auto& msg : shared_wal_object_->get_all_logs()) {
      if (!msg) {
        continue;
      }

      // dump snapshot 时，跳过带状态的message
      if (compact_stateful_sequence_ >= msg->sequence()) {
        continue;
      }

      protobuf_copy_message(*snapshot.add_messages(), *msg);
    }
  }

  if (!lock_.lock_holder().empty()) {
    protobuf_copy_message(*snapshot.mutable_lock(), lock_);
  }
}

void mq_channel::reload_configure(const atfw::dtmq::DChannelConfigure& config) {
  protobuf_copy_message(configure_, config);

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  if (readonly_replicate_index_ > 0) {
    if (dtmq_proxysvr_cfg.readonly_replicate_count() <= 0) {
      readonly_replicate_index_ = 0;
    } else if (readonly_replicate_index_ > static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) {
      readonly_replicate_index_ =
          (readonly_replicate_index_ % static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) + 1;
    }
  }

  if (readonly_replicate_configure_count_ != dtmq_proxysvr_cfg.readonly_replicate_count()) {
    readonly_replicate_configure_count_ = dtmq_proxysvr_cfg.readonly_replicate_count();

    // 重算分布
    server_distribution_etcd_revision_ = 0;

    // 重算分布后可以自动触发迁移逻辑,新增的只读副本会按需初始化加载，不需要主动通知
  }
}

void mq_channel::set_custom_data(const google::protobuf::Any& custom_data) noexcept {
  if (custom_data_.type_url().empty() && custom_data.type_url().empty()) {
    return;
  }

  protobuf_copy_message(custom_data_, custom_data);
  custom_data_sequence_ = get_last_message_sequence();
}

void mq_channel::clear_custom_data() noexcept {
  if (custom_data_.type_url().empty()) {
    return;
  }

  custom_data_.Clear();
  custom_data_sequence_ = get_last_message_sequence();
}

void mq_channel::set_private_data(const google::protobuf::Any& private_data) noexcept {
  if (private_data.type_url().empty() && private_data.type_url().empty()) {
    return;
  }

  protobuf_copy_message(private_data_, private_data);
  private_data_sequence_ = get_last_message_sequence();
}

void mq_channel::clear_private_data() noexcept {
  if (private_data_.type_url().empty()) {
    return;
  }

  private_data_.Clear();
  private_data_sequence_ = get_last_message_sequence();
}

rpc::result_code_type mq_channel::writable_init(rpc::context& ctx) {
  if (shared_wal_object_) {
    shared_wal_object_->set_last_removed_key(alloc_message_sequence());
  }
  if (is_io_task_running()) {
    auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
    if (ret != 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  if (is_writable()) {
    RPC_RETURN_CODE(0);
  }

  if (configure_.memory_only()) {
    upgrade_to_writable();
    RPC_RETURN_CODE(0);
  }

  // Writable节点从DB拉取
  auto self_ptr = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, "mq_channel.writable_init", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        FWLOGINFO("create_init mq channel {}", self_ptr->get_channel_id());

        auto ret = RPC_AWAIT_CODE_RESULT(self_ptr->load_from_db(child_ctx));

        if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->io_task_);
        }

        self_ptr->last_result_code_ = ret;
        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} transfer: create task failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    RPC_RETURN_CODE(*invoke_result.get_error());
  }
  if (invoke_result.is_success() && !task_type_trait::is_exiting(*invoke_result.get_success())) {
    io_task_ = *invoke_result.get_success();
  }

  if (is_io_task_running()) {
    RPC_AWAIT_IGNORE_RESULT(rpc::wait_task(ctx, io_task_));
  }

  RPC_RETURN_CODE(task_type_trait::get_result(*invoke_result.get_success()));
}

rpc::result_code_type mq_channel::readonly_init(rpc::context& ctx, uint64_t readonly_server_index) {
  if (shared_wal_object_) {
    shared_wal_object_->set_last_removed_key(alloc_message_sequence());
  }
  if (is_io_task_running()) {
    auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
    if (ret != 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  if (is_writable()) {
    readonly_replicate_index_ = 0;
    RPC_RETURN_CODE(0);
  }

  readonly_replicate_index_ = readonly_server_index;
  if (is_readonly()) {
    RPC_RETURN_CODE(0);
  }

  // 冷静窗口
  auto now = atfw::util::time::time_utility::now();
  if (next_init_subscribe_timepoint_ > now) {
    FWLOGWARNING("await_send_subscribe_to_writable too frequent, now {} next time {}", now,
                 next_init_subscribe_timepoint_);
    RPC_RETURN_CODE(last_result_code_);
  }
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  next_init_subscribe_timepoint_ = now + protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
                                             dtmq_proxysvr_cfg.channel_initialize_subscribe_timepoint());

  auto self_ptr = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, "mq_channel.readonly_init", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto ret = RPC_AWAIT_CODE_RESULT(self_ptr->send_subscribe_to_writable(child_ctx));

        self_ptr->last_result_code_ = ret;

        if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->io_task_);
        }

        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} transfer: create task failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    RPC_RETURN_CODE(*invoke_result.get_error());
  }
  if (invoke_result.is_success() && !task_type_trait::is_exiting(*invoke_result.get_success())) {
    io_task_ = *invoke_result.get_success();
  }

  if (is_io_task_running()) {
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  RPC_RETURN_CODE(task_type_trait::get_result(*invoke_result.get_success()));
}

void mq_channel::merge_subscriber(
    rpc::context& ctx,
    const ::google::protobuf::RepeatedPtrField<::atframework::dtmq::channel_subscriber>& subscribers) {
  // 合并订阅者信息, 只读副本时，每个节点分别维护自己的订阅者。不需要继承可写节点的订阅者。
  for (const auto& subscriber : subscribers) {
    auto sub_inst = wal_publisher_->get_subscribe_manager().find(subscriber.subscriber_key());
    if (sub_inst && sub_inst->get_private_data().last_heartbeat_sequence() >= subscriber.last_heartbeat_sequence()) {
      continue;
    }
    subscribe(ctx, subscriber, subscriber.last_heartbeat_sequence(), 0, true);
  }
}

void mq_channel::load_snapshot(rpc::context& ctx, atfw::dtmq::channel_snapshot&& snapshot) {
  if (is_loading_snapshot_) {
    FWLOGERROR("mq channel {} recursive load snapshot is not allowed", get_channel_id());
    return;
  }
  is_loading_snapshot_ = true;
  auto loading_guard = gsl::finally([this]() { this->is_loading_snapshot_ = false; });

  if (is_writable() && snapshot.replicate_index() > 0) {
    FWLOGINFO("mq channel {} ignore load snapshot, channel is writable but received a readonly snapshot",
              get_channel_id());

    // 订阅者总是要合并的
    merge_subscriber(ctx, snapshot.subscriber());
    return;
  }

  if (is_readonly() && snapshot.replicate_index() > 0 &&
      get_last_message_sequence() >= snapshot.channel_data().channel_metadata().last_sequence()) {
    FWLOGINFO("mq channel {} ignore load snapshot, channel is readonly and received an older readonly snapshot",
              get_channel_id());

    // 订阅者总是要合并的
    merge_subscriber(ctx, snapshot.subscriber());
    return;
  }

  FWLOGINFO("mq channel {} load snapshot", get_channel_id());

  last_save_timepoint_ = atfw::util::time::time_utility::now();
  // 如果当前节点是从节点，client也要处理快照
  maybe_create_wal_client();

  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};

  // 加载全量数据会导致广播边界被重置。如果有订阅者也需要重新恢复广播边界以便触发广播。
  auto subscriber_range = wal_publisher_->get_subscribe_manager().all_range();
  bool restore_broadcast_boundary = subscriber_range.first != subscriber_range.second;
  mq_channel_wal_object_type::log_key_type restore_broadcast_key{};
  if (restore_broadcast_boundary && wal_publisher_->get_broadcast_key_bound() != nullptr) {
    restore_broadcast_key = *wal_publisher_->get_broadcast_key_bound();
  } else if (wal_publisher_->get_log_manager().get_all_logs().empty()) {
    restore_broadcast_key = 0;
  } else {
    restore_broadcast_key = (*wal_publisher_->get_log_manager().get_all_logs().begin())->sequence() - 1;
  }
  // wal_client_ 和 wal_publisher_ 共享WAL层，其中一个加载即可。
  if (wal_client_) {
    wal_client_->receive_snapshot(snapshot.channel_data(), params);
  } else {
    wal_publisher_->load(snapshot.channel_data(), params);
  }
  if (result < 0) {
    FWLOGERROR("mq channel {} load snapshot failed, res: {}({})", get_channel_id(), result,
               protobuf_mini_dumper_get_error_msg(result));
  }

  // lock
  if (snapshot.channel_data().has_lock()) {
    set_lock(ctx, snapshot.channel_data().lock(), false);
  } else {
    clear_lock();
  }

  if (0 == snapshot.replicate_index()) {
    // 如果时transfer过来的数据，需要以来源的配置为准
    protobuf_copy_message(configure_, snapshot.channel_data().channel_metadata().channel_configure());
  }

  if (shared_wal_object_) {
    if (shared_wal_object_->get_all_logs().empty()) {
      shared_wal_object_->set_last_removed_key(alloc_message_sequence());
    } else {
      shared_wal_object_->set_last_removed_key((*shared_wal_object_->get_all_logs().begin())->sequence());
    }
  }
  is_dirty_ = false;

  // 恢复广播边界,以便如果加载snapshot后有订阅者，能够触发增量广播
  if (restore_broadcast_boundary) {
    wal_publisher_->set_broadcast_key_bound(restore_broadcast_key);
  }

  if (snapshot.replicate_index() > 0) {
    upgrade_to_readonly(snapshot.replicate_index());
  } else {
    upgrade_to_writable();
  }

  // 订阅者要在数据加载后再合并，否则会触发广播，导致订阅者收到不完整数据
  merge_subscriber(ctx, snapshot.subscriber());

  FWLOGINFO("channel {}({}) load_snapshot finished.", get_channel_id(), reinterpret_cast<const void*>(this));
}

void mq_channel::dump_snapshot(rpc::context& ctx, atfw::dtmq::channel_snapshot& output) {
  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};

  if (is_writable()) {
    output.set_replicate_index(0);
  } else {
    output.set_replicate_index(readonly_replicate_index_);
  }

  if (wal_publisher_) {
    wal_publisher_->dump(*output.mutable_channel_data(), params);

    auto now = atfw::util::time::time_utility::now();
    const auto& dtmq_proxysvr_cfg =
        logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

    auto subscriber_timeout_conf = protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
        dtmq_proxysvr_cfg.subscriber_timeout());
    if (subscriber_timeout_conf < std::chrono::seconds{1}) {
      subscriber_timeout_conf =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{1800});
    }

    auto all_subscribers = wal_publisher_->get_subscribe_manager().all_range();
    int subscriber_count = 0;
    for (auto iter = all_subscribers.first; iter != all_subscribers.second; ++iter) {
      ++subscriber_count;
    }
    output.mutable_subscriber()->Reserve(subscriber_count);
    for (auto iter = all_subscribers.first; iter != all_subscribers.second; ++iter) {
      if (protobuf_to_system_clock(iter->second->get_private_data().last_heartbeat_timepoint()) +
              subscriber_timeout_conf <
          now) {
        continue;
      }

      auto* copy_subscriber = output.add_subscriber();
      if (nullptr == copy_subscriber) {
        FCTXLOGERROR(ctx, "mq channel {} transfer: malloc subscriber {} data failed.", get_channel_id(),
                     iter->second->get_key());
        continue;
      }

      protobuf_copy_message(*copy_subscriber, iter->second->get_private_data());
    }
  }

  if (!lock_.lock_holder().empty()) {
    protobuf_copy_message(*output.mutable_channel_data()->mutable_lock(), lock_);
  }
}

bool mq_channel::should_be_writable_or_get_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                     uint64_t& writable_server_id, mq_channel* channel) noexcept {
  bool can_be_writable = true;
  do {
    if (mq_channel_manager::is_instance_destroyed()) {
      can_be_writable = false;
      break;
    }

    if (mq_channel_manager::me()->is_stoping()) {
      can_be_writable = false;
      break;
    }

    if (channel == nullptr) {
      break;
    }

    // 先调用 should_be_writable, 会触发分布计算，避免后续重复计算
    bool ret = channel->should_be_writable();
    writable_server_id = channel->target_distribution_.writable_server_id;
    return ret;
  } while (false);

  writable_server_id = rpc::dtmq::get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable, 0,
                                                       logic_hpa_discovery_select_mode::kTarget);
  return can_be_writable && logic_config::me()->get_local_server_id() == writable_server_id;
}

bool mq_channel::should_be_writable() noexcept {
  if (server_distribution_etcd_revision_ < mq_channel_manager::me()->get_latest_server_etcd_revision()) {
    recalculate_etcd_cache();
  }

  return target_distribution_.writable_server_id == logic_config::me()->get_local_server_id();
}

bool mq_channel::should_be_readonly_or_get_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                     uint64_t& readonly_server_id, uint64_t readonly_replicate_index,
                                                     mq_channel* channel) noexcept {
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
  if (0 == readonly_replicate_index || dtmq_proxysvr_cfg.readonly_replicate_count() <= 0) {
    return should_be_writable_or_get_server_id(channel_key, readonly_server_id, channel);
  }

  if (readonly_replicate_index > static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) {
    readonly_replicate_index =
        (readonly_replicate_index % static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) + 1;
  }

  bool can_be_readonly = true;
  do {
    if (mq_channel_manager::is_instance_destroyed()) {
      can_be_readonly = false;
      break;
    }

    if (mq_channel_manager::me()->is_stoping()) {
      can_be_readonly = false;
      break;
    }

    if (channel == nullptr) {
      break;
    }

    // 先调用 should_be_writable, 会触发分布计算，避免后续重复计算
    uint64_t local_readonly_replicate_index = 0;
    bool ret = channel->should_be_readonly(local_readonly_replicate_index);

    ret = ret && (local_readonly_replicate_index == readonly_replicate_index);
    if (!ret) {
      readonly_server_id = channel->get_target_distribution_server_id(readonly_replicate_index);
      if (0 != readonly_server_id) {
        return false;
      }
      break;
    }

    readonly_server_id = logic_config::me()->get_local_server_id();
    return ret;
  } while (false);

  readonly_server_id =
      rpc::dtmq::get_target_server_id(channel_key, rpc::dtmq::replicate_type::kReadonly, readonly_replicate_index,
                                      logic_hpa_discovery_select_mode::kTarget);
  return can_be_readonly && readonly_server_id == logic_config::me()->get_local_server_id();
}

bool mq_channel::should_be_readonly_or_random_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                        uint64_t& readonly_replicate_index,
                                                        uint64_t& readonly_server_id, mq_channel* channel) noexcept {
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  if (dtmq_proxysvr_cfg.readonly_replicate_count() <= 0) {
    readonly_replicate_index = 0;
    should_be_writable_or_get_server_id(channel_key, readonly_server_id, channel);
    return false;
  }

  bool can_be_readonly = true;
  uint64_t local_server_id = logic_config::me()->get_local_server_id();
  do {
    if (mq_channel_manager::is_instance_destroyed()) {
      can_be_readonly = false;
      break;
    }

    if (mq_channel_manager::me()->is_stoping()) {
      can_be_readonly = false;
      break;
    }

    if (channel == nullptr) {
      break;
    }

    if (channel->server_distribution_etcd_revision_ < mq_channel_manager::me()->get_latest_server_etcd_revision()) {
      channel->recalculate_etcd_cache();
    }

    auto iter = channel->target_distribution_.readonly_server_id_to_replicate_index.find(local_server_id);
    if (channel->target_distribution_.readonly_server_id_to_replicate_index.end() == iter) {
      // 如果本机无法成为只读副本，则随机选取一个只读节点用于后续转发RPC
      std::vector<std::pair<uint64_t, uint64_t>> readonly_servers;
      readonly_servers.reserve(
          static_cast<size_t>(channel->target_distribution_.readonly_server_id_to_replicate_index.size() + 1));
      if (channel->target_distribution_.writable_server_id != local_server_id) {
        readonly_servers.emplace_back(channel->target_distribution_.writable_server_id, 0);
      }

      for (const auto& readonly_server : channel->target_distribution_.readonly_server_id_to_replicate_index) {
        readonly_servers.emplace_back(readonly_server.first, readonly_server.second);
      }

      // 实在没有则返回本机
      if (readonly_servers.empty()) {
        readonly_replicate_index = 0;
        readonly_server_id = local_server_id;
      } else if (readonly_servers.size() == 1) {
        readonly_replicate_index = readonly_servers[0].second;
        readonly_server_id = readonly_servers[0].first;
      } else {
        size_t idx = atfw::component::random_engine::random_between<size_t>(0, readonly_servers.size());
        readonly_replicate_index = readonly_servers[idx].second;
        readonly_server_id = readonly_servers[idx].first;
      }
      return false;
    }

    readonly_replicate_index = iter->second;
    readonly_server_id = local_server_id;
    return true;
  } while (false);

  std::vector<std::pair<uint64_t, uint64_t>> readonly_servers;
  readonly_servers.reserve(static_cast<size_t>(dtmq_proxysvr_cfg.readonly_replicate_count() + 1));
  uint64_t writable_server_id = rpc::dtmq::get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable, 0,
                                                                logic_hpa_discovery_select_mode::kTarget);
  if (writable_server_id != local_server_id) {
    readonly_servers.emplace_back(writable_server_id, 0);
  }

  for (size_t i = 1; i <= static_cast<size_t>(dtmq_proxysvr_cfg.readonly_replicate_count()); i++) {
    readonly_server_id = rpc::dtmq::get_target_server_id(channel_key, rpc::dtmq::replicate_type::kReadonly, i,
                                                         logic_hpa_discovery_select_mode::kTarget);
    // 本机可以成为只读副本的情况
    if (local_server_id == readonly_server_id && can_be_readonly) {
      readonly_replicate_index = static_cast<uint64_t>(i);
      readonly_server_id = local_server_id;
      return true;
    }
    readonly_servers.emplace_back(readonly_server_id, static_cast<uint64_t>(i));
  }

  // 实在没有则返回本机
  if (readonly_servers.empty()) {
    readonly_replicate_index = 0;
    readonly_server_id = local_server_id;
  } else if (readonly_servers.size() == 1) {
    readonly_replicate_index = readonly_servers[0].second;
    readonly_server_id = readonly_servers[0].first;
  } else {
    size_t idx = atfw::component::random_engine::random_between<size_t>(0, readonly_servers.size());
    readonly_replicate_index = readonly_servers[idx].second;
    readonly_server_id = readonly_servers[idx].first;
  }
  return false;
}

bool mq_channel::should_be_readonly(uint64_t& readonly_replicate_index) noexcept {
  if (server_distribution_etcd_revision_ < mq_channel_manager::me()->get_latest_server_etcd_revision()) {
    recalculate_etcd_cache();
  }

  // 可写节点重合，以可写节点为准
  if (should_be_writable()) {
    readonly_replicate_index = 0;
    return false;
  }

  auto iter =
      target_distribution_.readonly_server_id_to_replicate_index.find(logic_config::me()->get_local_server_id());
  if (target_distribution_.readonly_server_id_to_replicate_index.end() == iter) {
    readonly_replicate_index = 0;
    return false;
  }

  readonly_replicate_index = iter->second;
  return true;
}

uint64_t mq_channel::get_target_distribution_server_id(uint64_t replicate_index) const noexcept {
  if (replicate_index == 0) {
    return target_distribution_.writable_server_id;
  }

  auto iter = target_distribution_.readonly_replicate_index_to_server_id.find(replicate_index);
  if (iter != target_distribution_.readonly_replicate_index_to_server_id.end()) {
    return iter->second;
  }

  return 0;
}

uint64_t mq_channel::get_target_distribution_replicate_index(uint64_t server_id) const noexcept {
  if (target_distribution_.writable_server_id == server_id) {
    return 0;
  }

  auto iter = target_distribution_.readonly_server_id_to_replicate_index.find(server_id);
  if (iter != target_distribution_.readonly_server_id_to_replicate_index.end()) {
    return iter->second;
  }

  return std::numeric_limits<uint64_t>::max();
}

uint64_t mq_channel::calculate_transfer_target_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                         uint64_t replicate_index) noexcept {
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  if (dtmq_proxysvr_cfg.readonly_replicate_count() <= 0) {
    replicate_index = 0;
  } else if (replicate_index > static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) {
    replicate_index = (replicate_index % static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) + 1;
  }

  rpc::dtmq::replicate_type rt =
      replicate_index > 0 ? rpc::dtmq::replicate_type::kReadonly : rpc::dtmq::replicate_type::kWritable;
  // 检查分布，确认是否需要启动转移流程
  auto local_server_id = logic_config::me()->get_local_server_id();
  uint64_t target_server_id = rpc::dtmq::get_target_server_id(channel_key, rt, static_cast<size_t>(replicate_index),
                                                              logic_hpa_discovery_select_mode::kTarget);

  if (local_server_id == target_server_id) {
    return 0;
  }

  return target_server_id;
}

uint64_t mq_channel::get_transfer_target_server_id() const noexcept {
  auto local_server_id = logic_config::me()->get_local_server_id();
  if (is_writable()) {
    if (target_distribution_.writable_server_id == local_server_id) {
      return 0;
    }

    // 如果连续多次节点负载变化和冷静窗口等待，如果已处理数据迁移的版本号小于等于最后一次的等待边界。
    // 说明当前频道上一次的负载变化尚未触发转移，就不需要再等待。
    // 可写节点在停机时如果有冷静窗口可以保存数据到DB，后续目标节点从DB拉取数据即可。
    if (mq_channel_manager::me()->is_waiting_transfer() &&
        resolved_transfer_etcd_revision_ >= mq_channel_manager::me()->get_transfer_server_etcd_revision()) {
      return 0;
    }

    return target_distribution_.writable_server_id;
  }

  if (is_readonly()) {
    if (target_distribution_.writable_server_id == local_server_id) {
      return 0;
    }

    if (target_distribution_.readonly_server_id_to_replicate_index.end() !=
        target_distribution_.readonly_server_id_to_replicate_index.find(local_server_id)) {
      return 0;
    }

    // 如果连续多次节点负载变化和冷静窗口等待，如果已处理数据迁移的版本号小于等于最后一次的等待边界。
    // 说明当前频道上一次的负载变化尚未触发转移，就不需要再等待。
    // 只读节点在停机时应该立即转移，否则会丢失订阅信息
    if (mq_channel_manager::me()->is_waiting_transfer() && !mq_channel_manager::me()->is_stoping() &&
        resolved_transfer_etcd_revision_ >= mq_channel_manager::me()->get_transfer_server_etcd_revision()) {
      return 0;
    }

    // 没有只读副本则全部按可写副本计算
    if (readonly_replicate_index_ == 0) {
      return target_distribution_.writable_server_id;
    }

    return rpc::dtmq::get_target_server_id(get_channel_key(), rpc::dtmq::replicate_type::kReadonly,
                                           static_cast<size_t>(readonly_replicate_index_),
                                           logic_hpa_discovery_select_mode::kTarget);
  }

  return 0;
}

ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type mq_channel::await_transfer(rpc::context& ctx,
                                                                              uint64_t& transfer_to_server_id) {
  transfer_to_server_id = 0;
  if (is_io_task_running()) {
    rpc::result_code_type::value_type result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  if (!is_writable() && !is_readonly()) {
    RPC_RETURN_CODE(0);
  }

  // 刷新分布计算
  if (server_distribution_etcd_revision_ < mq_channel_manager::me()->get_latest_server_etcd_revision()) {
    recalculate_etcd_cache();
  }

  transfer_to_server_id = get_transfer_target_server_id();
  if (transfer_to_server_id == 0) {
    // 如果不在等待，可以标记已处理的转移版本号为当前最新的分布版本号，确保下一次从负载变化才能让冷静窗口等待生效。
    if (resolved_transfer_etcd_revision_ != server_distribution_etcd_revision_ &&
        !mq_channel_manager::me()->is_waiting_transfer()) {
      resolved_transfer_etcd_revision_ = server_distribution_etcd_revision_;
    }
    RPC_RETURN_CODE(0);
  }

  async_start_transfer(ctx, transfer_to_server_id);

  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
  RPC_RETURN_CODE(ret);
}

void mq_channel::force_refresh_distribution() {
  // 刷新分布计算
  if (server_distribution_etcd_revision_ < mq_channel_manager::me()->get_latest_server_etcd_revision()) {
    recalculate_etcd_cache();
  }
}

bool mq_channel::need_save_db() const noexcept {
  if (!is_writable()) {
    return false;
  }

  return is_dirty_ && get_transfer_target_server_id() == 0;
}

bool mq_channel::is_io_task_running() const noexcept {
  // 正在转移或读取
  if (task_type_trait::empty(io_task_)) {
    return false;
  }

  if (!task_type_trait::is_exiting(io_task_)) {
    return true;
  }

  task_type_trait::reset_task(io_task_);
  return false;
}

rpc::result_code_type mq_channel::await_io_task(rpc::context& ctx) {
  // 正在转移或读取
  if (!task_type_trait::empty(io_task_)) {
    if (task_type_trait::is_exiting(io_task_)) {
      task_type_trait::reset_task(io_task_);
      RPC_RETURN_CODE(0);
    }
  } else {
    RPC_RETURN_CODE(0);
  }

  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
  if (!task_type_trait::empty(io_task_) && task_type_trait::is_exiting(io_task_)) {
    task_type_trait::reset_task(io_task_);
  }
  RPC_RETURN_CODE(ret);
}

void mq_channel::async_start_transfer(rpc::context& ctx, uint64_t target_server_id) {
  if (is_io_task_running()) {
    return;
  }

  if (0 == target_server_id) {
    return;
  }

  // tick会触发一次数据下发和清理，减少无效数据量
  tick(ctx);

  auto self_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "mq_channel.start_transfer", [self_ptr, target_server_id](rpc::context& child_ctx) -> rpc::result_code_type {
        rpc::context::message_holder<atfw::dtmq::SSChannelTransferChannelReq> req_body{child_ctx};
        rpc::context::message_holder<atfw::dtmq::SSChannelTransferChannelRsp> rsp_body{child_ctx};
        int32_t result = 0;
        mq_channel_wal_object_context params{child_ctx, result};

        auto* snapshot_data = req_body->add_snapshot();
        if (nullptr == snapshot_data) {
          FWLOGERROR("mq channel {} transfer: malloc snapshot data failed.", self_ptr->get_channel_id());
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }

        self_ptr->dump_snapshot(child_ctx, *snapshot_data);
        FWLOGINFO("start transfer mq channel {} to {:#x}", self_ptr->get_channel_id(), target_server_id);

        // 转移数据
        result = RPC_AWAIT_CODE_RESULT(rpc::dtmq::transfer_channel(child_ctx, target_server_id, *req_body, *rsp_body));

        self_ptr->send_oss(child_ctx, "transfer", result, target_server_id);

        if (result >= 0) {
          // 如果IO后又要迁回可写节点，当前无需操作，后续会触发upgrade
          uint64_t readonly_replicate_index = 0;
          if (self_ptr->should_be_readonly(readonly_replicate_index)) {
            self_ptr->downgrade_to_readable(readonly_replicate_index);
          } else if (!self_ptr->should_be_writable()) {
            self_ptr->downgrade_to_none();
          }
        }

        if (!mq_channel_manager::is_instance_destroyed()) {
          mq_channel_manager::me()->set_more_transfer();
        }

        if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->io_task_);
        }

        RPC_RETURN_CODE(result);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} transfer: create task failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    return;
  }
  if (invoke_result.is_success() && !task_type_trait::is_exiting(*invoke_result.get_success())) {
    io_task_ = *invoke_result.get_success();
  }
}

void mq_channel::async_save(rpc::context& ctx) {
  if (is_io_task_running()) {
    return;
  }

  if (!is_writable()) {
    is_dirty_ = false;
    return;
  }

  if (configure_.memory_only()) {
    uint64_t readonly_replicate_index = 0;
    if (should_be_readonly(readonly_replicate_index)) {
      downgrade_to_readable(readonly_replicate_index);
    }

    is_dirty_ = false;
    return;
  }

  auto self_ptr = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, "mq_channel.save", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        self_ptr->tick(child_ctx);

        auto data = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>(child_ctx);
        self_ptr->dump(child_ctx, *data);

        // rpc::db::
        auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::replace(child_ctx, std::move(data)));
        if (ret != 0) {
          FWLOGERROR("rpc::db::dtmq_channel_record::replace faild, channel_id:{}, ret:{}({})",
                     self_ptr->get_channel_id(), ret, protobuf_mini_dumper_get_error_msg(ret));
        }
        FWLOGDEBUG("rpc::db::dtmq_channel_record::replace channel:{}, ret:{}({})", self_ptr->get_channel_id(), ret,
                   protobuf_mini_dumper_get_error_msg(ret));

        // 如果已移除，则重置删除TTL
        if (self_ptr->remove_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
          auto ttl_seconds =
              std::chrono::duration_cast<std::chrono::seconds>(self_ptr->remove_timepoint_ + get_mq_channel_ttl() -
                                                               atfw::util::time::time_utility::now())
                  .count();
          if (ttl_seconds > 0) {
            RPC_AWAIT_IGNORE_RESULT(rpc::db::dtmq_channel_record::set_ttl(child_ctx, self_ptr->get_channel_id(),
                                                                          static_cast<uint64_t>(ttl_seconds)));
          } else {
            RPC_AWAIT_IGNORE_RESULT(rpc::db::dtmq_channel_record::remove_all(child_ctx, self_ptr->get_channel_id()));
          }
        }

        uint64_t readonly_replicate_index = 0;
        if (self_ptr->should_be_readonly(readonly_replicate_index)) {
          self_ptr->downgrade_to_readable(readonly_replicate_index);
        } else if (!self_ptr->should_be_writable()) {
          self_ptr->downgrade_to_none();
        }
        self_ptr->is_dirty_ = false;

        if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->io_task_);
        }
        RPC_RETURN_CODE(0);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} transfer: create task failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    return;
  }
  if (invoke_result.is_success() && !task_type_trait::is_exiting(*invoke_result.get_success())) {
    io_task_ = *invoke_result.get_success();
  }
}

rpc::result_code_type mq_channel::save(rpc::context& ctx) {
  if (is_io_task_running()) {
    RPC_AWAIT_IGNORE_RESULT(rpc::wait_task(ctx, io_task_));
    task_type_trait::reset_task(io_task_);
  }

  async_save(ctx);

  if (is_io_task_running()) {
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  RPC_RETURN_CODE(0);
}

void mq_channel::async_destroy(rpc::context& ctx, std::chrono::system_clock::time_point writable_remove_timepoint) {
  // 频道已经销毁，无需重复执行
  if (remove_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    return;
  }
  auto self_ptr = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, "mq_channel.async_destroy",
                        [self_ptr, writable_remove_timepoint](rpc::context& child_ctx) -> rpc::result_code_type {
                          // 离线数据删除，使用TTL
                          auto ret = RPC_AWAIT_CODE_RESULT(self_ptr->destroy(child_ctx, writable_remove_timepoint));

                          RPC_RETURN_CODE(ret);
                        });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} async_destroy: failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

rpc::result_code_type mq_channel::destroy(rpc::context& ctx,
                                          std::chrono::system_clock::time_point writable_remove_timepoint) {
  // 频道已经销毁，无需重复执行
  const auto time_zero = std::chrono::system_clock::from_time_t(0);
  if (remove_timepoint_ > time_zero) {
    RPC_RETURN_CODE(0);
  }

  if (is_io_task_running()) {
    RPC_AWAIT_IGNORE_RESULT(rpc::wait_task(ctx, io_task_));
    task_type_trait::reset_task(io_task_);
  }

  // channel销毁时间先以主节点为准
  if (writable_remove_timepoint > time_zero && !is_writable()) {
    remove_timepoint_ = writable_remove_timepoint;
  } else {
    remove_timepoint_ = atfw::util::time::time_utility::now();
  }

  if (!is_writable()) {
    RPC_RETURN_CODE(0);
  }

  if (get_configure().memory_only()) {
    downgrade_to_none();
    RPC_RETURN_CODE(0);
  }

  auto self_ptr = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, "mq_channel.destroy", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        // 离线数据删除，使用TTL
        FWLOGINFO("destroy mq channel {}", self_ptr->get_channel_id());

        auto ttl_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(self_ptr->remove_timepoint_ + get_mq_channel_ttl() -
                                                             atfw::util::time::time_utility::now())
                .count();
        if (ttl_seconds > 0) {
          RPC_AWAIT_IGNORE_RESULT(rpc::db::dtmq_channel_record::set_ttl(child_ctx, self_ptr->get_channel_id(),
                                                                        static_cast<uint64_t>(ttl_seconds)));
        } else {
          RPC_AWAIT_IGNORE_RESULT(rpc::db::dtmq_channel_record::remove_all(child_ctx, self_ptr->get_channel_id()));
        }

        self_ptr->downgrade_to_none();
        self_ptr->send_oss(child_ctx, "destroy");

        if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->io_task_);
        }
        RPC_RETURN_CODE(0);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} transfer: create task failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    RPC_RETURN_CODE(*invoke_result.get_error());
  }
  if (invoke_result.is_success() && !task_type_trait::is_exiting(*invoke_result.get_success())) {
    io_task_ = *invoke_result.get_success();
  }

  if (is_io_task_running()) {
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }
  // 主节点通知从节点频道销毁
  set_destroy_message(ctx);
  RPC_RETURN_CODE(task_type_trait::get_result(*invoke_result.get_success()));
}

rpc::result_code_type mq_channel::load_from_db(rpc::context& ctx) {
  auto record = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>(ctx);
  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::get_all(ctx, get_channel_id(), record));
  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    FWLOGINFO("rpc::db::dtmq_channel_record::get_all mq channel:{} record not found", get_channel_id());
    ret = 0;
  }

  if (ret == 0) {
    if (should_be_writable()) {
      FWLOGDEBUG("rpc::db::dtmq_channel_record::get_all mq channel: {}, record_size: {}", get_channel_id(),
                 record->record_set().record().size());
      load(ctx, *record);
      send_oss(ctx, "create_init", ret);

      FWLOGINFO("channel {}({}) load_from_db success.", get_channel_id(), reinterpret_cast<const void*>(this));
    } else {
      FWLOGINFO("channel {}({}) load_from_db ignored, maybe transfer to another server node.", get_channel_id(),
                reinterpret_cast<const void*>(this));
    }
  } else {
    FWLOGERROR("rpc::db::dtmq_channel_record::get failed mq channel: {}, result: {}({})", get_channel_id(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
  }
  RPC_RETURN_CODE(ret);
}

int32_t mq_channel::async_send_subscribe_to_writable(rpc::context& ctx) {
  uint64_t readonly_replicate_index = 0;
  if (!should_be_readonly(readonly_replicate_index)) {
    wal_client_.reset();
    return 0;
  }
  maybe_create_wal_client();
  auto self_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "mq_channel.async_send_subscribe_to_writable", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto ret = RPC_AWAIT_CODE_RESULT(self_ptr->send_subscribe_to_writable(child_ctx));

        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("send_subscribe_to_writable {} : create task failed.res: {}({})", channel_key_.channel_id(),
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    return *invoke_result.get_error();
  }

  if ((task_type_trait::empty(io_task_) || task_type_trait::is_exiting(io_task_)) &&
      !task_type_trait::is_exiting(io_task_)) {
    io_task_ = *invoke_result.get_success();
  }

  return 0;
}

rpc::result_code_type mq_channel::await_send_subscribe_to_writable(rpc::context& ctx) {
  if (is_io_task_running()) {
    auto result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  uint64_t readonly_replicate_index = 0;
  if (!should_be_readonly(readonly_replicate_index)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
  }
  auto now = atfw::util::time::time_utility::now();
  if (next_init_subscribe_timepoint_ > now) {
    FWLOGWARNING("await_send_subscribe_to_writable too frequent, now {} next time {}", now,
                 next_init_subscribe_timepoint_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
  }
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
  next_init_subscribe_timepoint_ = now + protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
                                             dtmq_proxysvr_cfg.channel_initialize_subscribe_timepoint());

  int res = async_send_subscribe_to_writable(ctx);
  if (res < 0) {
    FWLOGERROR("await_send_subscribe_to_writable too frequent, now {} next time {}", now,
               next_init_subscribe_timepoint_);
    RPC_RETURN_CODE(res);
  }

  if (is_io_task_running()) {
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }
  RPC_RETURN_CODE(task_type_trait::get_result(io_task_));
}

rpc::result_code_type mq_channel::send_subscribe_to_writable(rpc::context& ctx) {
  uint64_t dtmq_proxysvr_id = get_main_ready_writable_server_id();
  if (dtmq_proxysvr_id == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  rpc::context::message_holder<atfw::dtmq::SSChannelSubscribeReq> req_body{ctx};
  rpc::context::message_holder<atfw::dtmq::SSChannelSubscribeRsp> rsp_body{ctx};
  auto* channel_data = req_body->mutable_heartbeat()->Add();
  protobuf_copy_message(*channel_data->mutable_channel_key(), channel_key_);

  auto wal_client = get_wal_client();
  if (!wal_client) {
    FWLOGERROR("wal_client is not init! channel id: {}", channel_key_.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_WAL_CLIENT_NOT_INIT);
  }
  // 必须以最后一条log为准
  if (!wal_client->get_log_manager().get_all_logs().empty()) {
    auto last_iter = wal_client->get_log_manager().get_all_logs().rbegin();
    if (*last_iter) {
      channel_data->set_last_sequence((*last_iter)->sequence());
      channel_data->set_last_hash_code((*last_iter)->hash_code());
    }

    const auto& dtmq_proxysvr_cfg =
        logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

    auto& hash_mismatch_data = wal_client->get_private_data().hash_mismatch_data;
    if (hash_mismatch_data.log_key == (*last_iter)->sequence() &&
        hash_mismatch_data.times >= dtmq_proxysvr_cfg.channel_wal_hash_mismatch_need_snapshot_times() &&
        hash_mismatch_data.next_need_snapshot_timestamp <= atfw::util::time::time_utility::now()) {
      channel_data->set_last_sequence(0);
      channel_data->set_last_hash_code(0);
      hash_mismatch_data.next_need_snapshot_timestamp =
          atfw::util::time::time_utility::now() +
          protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
              dtmq_proxysvr_cfg.channel_wal_hash_mismatch_need_snapshot_interval());
      FWLOGERROR("hash mismatch, channel_id: {}, log key: {}", get_channel_id(), hash_mismatch_data.log_key);
    }
  }

  req_body->mutable_subscriber()->set_subscriber_server_id(logic_config::me()->get_local_server_id());

  auto ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::subscribe(ctx, dtmq_proxysvr_id, *req_body, *rsp_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  bool not_found = false;
  for (const auto& not_found_channel : rsp_body->not_found_channel_ids()) {
    if (not_found_channel == channel_key_.channel_id()) {
      not_found = true;
      break;
    }
  }
  if (not_found) {
    if (!should_be_writable() && !is_writable()) {
      // 确保IO task之后任然不是writable节点，说明频道已经被writable端删除，直接销毁频道
      if (is_io_task_running()) {
        RPC_AWAIT_IGNORE_RESULT(rpc::wait_task(ctx, io_task_));
        task_type_trait::reset_task(io_task_);
      }
      if (!is_writable()) {
        RPC_AWAIT_IGNORE_RESULT(destroy(ctx));
      }
    }

  } else {
    update_last_writable_notify_time();
  }
  RPC_RETURN_CODE(0);
}

void mq_channel::set_destroy_message(rpc::context& ctx) {
  // 频道无法被destroy多次
  if (remove_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    FWLOGERROR("channel {} set_destroy_message but remove_timepoint is {}", get_channel_id(), remove_timepoint_);
    return;
  }

  if (!wal_publisher_) {
    FWLOGERROR("channel {} set_destroy_message but wal_publisher_ is null", get_channel_id());
    return;
  }
  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};
  auto message = wal_publisher_->allocate_log(atfw::util::time::time_utility::now(),
                                              atfw::dtmq::DChannelMessageDetail::kDestroy, params);
  if (message) {
    *message->mutable_detail()->mutable_destroy()->mutable_removed_timepoint() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now());
    if (wal_client_) {
      wal_client_->receive_hole_log(params, std::move(message));
    } else {
      wal_publisher_->emplace_back_log(std::move(message), params);
    }
  } else {
    FWLOGERROR("malloc wal log for mq channel {} to destroy failed", get_channel_id());
  }
}

int32_t mq_channel::subscribe(rpc::context& ctx, const atfw::dtmq::channel_subscriber& subscriber_info,
                              int64_t last_received_sequence, size_t last_received_hash_code, bool merge_mode) {
  if (subscriber_info.subscriber_server_id() == 0) {
    return 0;
  }

  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  auto now = atfw::util::time::time_utility::now();
  auto subscriber_timeout_conf =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(dtmq_proxysvr_cfg.subscriber_timeout());
  if (subscriber_timeout_conf < std::chrono::seconds{1}) {
    subscriber_timeout_conf =
        std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{1800});
  }

  std::string subscriber_key = make_subscriber_key(subscriber_info);
  auto subscriber = wal_publisher_->find_subscriber(subscriber_key, params);
  auto last_heartbeat_timepoint = protobuf_to_system_clock(subscriber_info.last_heartbeat_timepoint());
  if (subscriber) {
    // 收到老的心跳直接忽略
    if (merge_mode && (last_heartbeat_timepoint <=
                           protobuf_to_system_clock(subscriber->get_private_data().last_heartbeat_timepoint()) ||
                       last_heartbeat_timepoint + subscriber_timeout_conf < now)) {
      return 0;
    }
    subscriber->get_private_data().set_subscriber_server_id(subscriber_info.subscriber_server_id());

    if (merge_mode) {
      *subscriber->get_private_data().mutable_last_heartbeat_timepoint() = subscriber_info.last_heartbeat_timepoint();
      if (subscriber->get_private_data().last_heartbeat_sequence() > last_received_sequence) {
        if (0 == last_received_hash_code) {
          wal_publisher_->receive_subscribe_request(subscriber_key,
                                                    subscriber->get_private_data().last_heartbeat_sequence(),
                                                    atfw::util::time::time_utility::now(), params);
        } else {
          wal_publisher_->receive_subscribe_request(
              subscriber_key, subscriber->get_private_data().last_heartbeat_sequence(), last_received_hash_code,
              atfw::util::time::time_utility::now(), params);
        }
      } else {
        if (0 == last_received_hash_code) {
          wal_publisher_->receive_subscribe_request(subscriber_key, last_received_sequence,
                                                    atfw::util::time::time_utility::now(), params);
        } else {
          wal_publisher_->receive_subscribe_request(subscriber_key, last_received_sequence, last_received_hash_code,
                                                    atfw::util::time::time_utility::now(), params);
        }
        subscriber->get_private_data().set_last_heartbeat_sequence(last_received_sequence);
        subscriber->get_private_data().set_last_heartbeat_hash_code(last_received_hash_code);
      }
    } else {
      *subscriber->get_private_data().mutable_last_heartbeat_timepoint() =
          protobuf_from_system_clock(atfw::util::time::time_utility::now());
      if (0 == last_received_hash_code) {
        wal_publisher_->receive_subscribe_request(subscriber_key, last_received_sequence,
                                                  atfw::util::time::time_utility::now(), params);
      } else {
        wal_publisher_->receive_subscribe_request(subscriber_key, last_received_sequence, last_received_hash_code,
                                                  atfw::util::time::time_utility::now(), params);
      }
      subscriber->get_private_data().set_last_heartbeat_sequence(last_received_sequence);
      subscriber->get_private_data().set_last_heartbeat_hash_code(last_received_hash_code);
    }
  } else {
    if (merge_mode && last_heartbeat_timepoint + subscriber_timeout_conf < now) {
      return 0;
    }

    if (0 == last_received_hash_code) {
      subscriber = wal_publisher_->create_subscriber(subscriber_key, atfw::util::time::time_utility::now(),
                                                     last_received_sequence, params, subscriber_info);
    } else {
      subscriber = wal_publisher_->create_subscriber(subscriber_key, atfw::util::time::time_utility::now(),
                                                     std::make_pair(last_received_sequence, last_received_hash_code),
                                                     params, subscriber_info);
    }

    if (subscriber) {
      if (merge_mode) {
        *subscriber->get_private_data().mutable_last_heartbeat_timepoint() = subscriber_info.last_heartbeat_timepoint();
      } else {
        *subscriber->get_private_data().mutable_last_heartbeat_timepoint() =
            protobuf_from_system_clock(atfw::util::time::time_utility::now());
      }
      subscriber->get_private_data().set_last_heartbeat_sequence(last_received_sequence);
      subscriber->get_private_data().set_last_heartbeat_hash_code(last_received_hash_code);
    }
  }

  FWLOGDEBUG("add subscriber_info success. subscriber_info:({}), subscriber.private_data:({})",
             protobuf_mini_dumper_get_readable(subscriber_info),
             protobuf_mini_dumper_get_readable(subscriber->get_private_data()));
  return 0;
}

int32_t mq_channel::unsubscribe(rpc::context& ctx, const std::string& subscriber_key) {
  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};

  wal_publisher_->remove_subscriber(subscriber_key, util::distributed_system::wal_unsubscribe_reason::kClientRequest,
                                    params);
  return result;
}

int64_t mq_channel::alloc_message_sequence() noexcept {
  if (shared_wal_object_ && !shared_wal_object_->get_all_logs().empty()) {
    auto last_sequence = (*shared_wal_object_->get_all_logs().rbegin())->sequence();
    if (last_sequence >= sequence_allocator_) {
      sequence_allocator_ = last_sequence;
    }
  }

  if (0 == sequence_allocator_) {
    sequence_allocator_ =
        (atfw::util::time::time_utility::get_sys_now() * 1000000) + util::time::time_utility::get_now_usec();
  }

  return ++sequence_allocator_;
}

int64_t mq_channel::get_last_message_sequence() const noexcept {
  if (sequence_allocator_ != 0) {
    return sequence_allocator_;
  }

  if (shared_wal_object_ && !shared_wal_object_->get_all_logs().empty()) {
    return (*shared_wal_object_->get_all_logs().rbegin())->sequence();
  }

  return const_cast<mq_channel*>(this)->alloc_message_sequence();
}

uint64_t mq_channel::get_last_hash_code() const noexcept {
  if (shared_wal_object_ && !shared_wal_object_->get_all_logs().empty()) {
    return (*shared_wal_object_->get_all_logs().rbegin())->hash_code();
  }

  return 0;
}

uint64_t mq_channel::get_client_last_hash_code() const noexcept {
  if (wal_client_ && !wal_client_->get_log_manager().get_all_logs().empty()) {
    return (*wal_client_->get_log_manager().get_all_logs().rbegin())->hash_code();
  }

  return 0;
}

uint64_t mq_channel::get_main_ready_writable_server_id() { return ready_distribution_.writable_server_id; }

int mq_channel::tick(rpc::context& ctx) {
  auto now = atfw::util::time::time_utility::now();
  if (next_send_oss_time_ < now) {
    send_oss(ctx, "heartbeat");
    next_send_oss_time_ =
        now + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds{300});
  }

  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  if (wal_client_) {
    wal_client_->tick(atfw::util::time::time_utility::now(), params, dtmq_proxysvr_cfg.max_events_per_tick());
  }
  // 清理过期订阅者
  // 清理过期Log
  wal_publisher_->tick(atfw::util::time::time_utility::now(), params, dtmq_proxysvr_cfg.max_events_per_tick());

  // 单播数据下发
  wal_publisher_->broadcast(params);

  return 0;
}

void mq_channel::update_lost_last_subscriber() noexcept {
  lost_last_subscriber_timepoint_ = atfw::util::time::time_utility::now();
}

void mq_channel::update_last_writable_notify_time() noexcept {
  last_writable_notify_readonly_timepoint_ = atfw::util::time::time_utility::now();
}

void mq_channel::reset_lost_last_subscriber() noexcept {
  lost_last_subscriber_timepoint_ = std::chrono::system_clock::from_time_t(0);
}

void mq_channel::set_dirty() noexcept {
  // 仅仅writable需要保存
  if (!is_writable()) {
    return;
  }

  is_dirty_ = true;
}

void mq_channel::set_lock(rpc::context& ctx, const atfw::dtmq::DChannelOptimisticLock& lock, bool append_log) {
  // Ignore if unchanged
  if (atfw::atapp::protobuf_equal(lock_, lock)) {
    return;
  }

  protobuf_copy_message(lock_, lock);

  FWLOGDEBUG("channel {} lock updated.", get_channel_id());
  if (!append_log || !shared_wal_object_) {
    return;
  }

  if (!is_writable() && !(is_readonly() && wal_client_)) {
    return;
  }

  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};
  auto message = shared_wal_object_->allocate_log(atfw::util::time::time_utility::now(),
                                                  atfw::dtmq::DChannelMessageDetail::kResetLock, params);
  if (message) {
    protobuf_copy_message(*message->mutable_detail()->mutable_reset_lock(), lock_);
    if (wal_client_) {
      wal_client_->receive_hole_log(params, std::move(message));
    } else {
      wal_publisher_->emplace_back_log(std::move(message), params);
    }
  } else {
    FWLOGERROR("malloc wal log for mq channel {} to reset lock failed", get_channel_id());
  }
}

void mq_channel::clear_lock() { lock_.Clear(); }

bool mq_channel::compare_and_maybe_reset_lock(rpc::context& ctx, atfw::dtmq::channel_lock_checker& checker,
                                              bool append_log) {
  if (!checker.ignore_expect_value()) {
    bool lock_timeout = false;
    if (lock_.has_timeout() && lock_.timeout().seconds() != 0) {
      if (atfw::util::time::time_utility::now() > protobuf_to_system_clock(lock_.timeout())) {
        lock_timeout = true;
      }
    }
    if (!lock_timeout && !(lock_.lock_holder().empty() && checker.allow_empty_real_value()) &&
        lock_.lock_holder() != checker.expect_value().lock_holder()) {
      protobuf_copy_message(*checker.mutable_real_value(), lock_);
      return false;
    }
  }

  if (checker.has_reset_value()) {
    set_lock(ctx, checker.reset_value(), append_log);
  }
  return true;
}

void mq_channel::compact_stateful_sequence(int64_t sequence) {
  if (sequence > compact_stateful_sequence_) {
    compact_stateful_sequence_ = sequence;
  }
}

void mq_channel::compact_sequence(int64_t sequence) {
  if (!shared_wal_object_) {
    return;
  }

  if (nullptr == shared_wal_object_->get_last_removed_key() || sequence > *shared_wal_object_->get_last_removed_key()) {
    shared_wal_object_->set_last_removed_key(sequence);
  }

  size_t remove_count = 0;
  for (auto iter = shared_wal_object_->log_begin(); iter != shared_wal_object_->log_end(); ++iter) {
    if (!*shared_wal_object_->log_begin()) {
      ++remove_count;
      continue;
    }

    if ((*shared_wal_object_->log_begin())->sequence() <= sequence) {
      ++remove_count;
    }
  }

  if (remove_count > 0) {
    shared_wal_object_->remove_before(atfw::util::time::time_utility::now(), remove_count);
  }
}

void mq_channel::maybe_create_wal_client() {
  uint64_t readonly_replicate_index = 0;
  if (should_be_readonly(readonly_replicate_index)) {
    if (!wal_client_) {
      auto configure = excel::get_dtmq_channel_configure(get_channel_key().channel_type());
      if (configure) {
        wal_client_ = create_mq_channel_client(*this, *configure);
      }
    }
  }
}

void mq_channel::hash_mismatch_increase() {
  if (!wal_client_) {
    return;
  }

  if (wal_client_->get_log_manager().get_all_logs().empty()) {
    return;
  }
  auto last_iter = wal_client_->get_log_manager().get_all_logs().rbegin();
  auto& hash_mismatch_data = wal_client_->get_private_data().hash_mismatch_data;
  if (*last_iter && hash_mismatch_data.log_key == (*last_iter)->sequence()) {
    if (atfw::util::time::time_utility::now() > hash_mismatch_data.next_need_snapshot_timestamp) {
      hash_mismatch_data.next_need_snapshot_timestamp = atfw::util::time::time_utility::now();
    }
    hash_mismatch_data.times++;
    return;
  }
  hash_mismatch_data = rpc::dtmq::hash_mismatch_subscribe<int64_t>(channel_key_.channel_id(), (*last_iter)->sequence(),
                                                                   atfw::util::time::time_utility::now());
}

void mq_channel::update_timer(rpc::context& ctx, bool force) {
  FWLOGDEBUG("channel({}) update_timer", get_channel_id());

  if (!is_writable() && !is_readonly()) {
    mq_channel_manager::me()->remove_channel(get_channel_id(), this);
    return;
  }

  auto now = atfw::util::time::time_utility::now();

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  // 定期保存
  auto save_interval =
      protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.save_interval());
  if (configure_.memory_only() || is_readonly()) {
    is_dirty_ = false;
  } else if (now > last_save_timepoint_ + save_interval || force) {
    if (!is_io_task_running()) {
      last_save_timepoint_ = now;
      auto channel_ptr = shared_from_this();
      channel_ptr->async_save(ctx);
    }
  }

  // 检查订阅者有效性和淘汰过期数据
  tick(ctx);

  // 保护性移除或插入新定时器
  remove_timer();

  // 清理无订阅者的频道
  do {
    auto subscribes = get_wal_publisher().subscriber_all_range();
    if (subscribes.first != subscribes.second) {
      break;
    }
    if (lost_last_subscriber_timepoint_ <= std::chrono::system_clock::from_time_t(0)) {
      update_lost_last_subscriber();
      break;
    }

    if (is_io_task_running()) {
      break;
    }

    // 如果还有数据未保存不能移除
    if (!is_readonly() && is_dirty_) {
      break;
    }

    // 如果从节点近期和主节点通信过，不移除
    if (now <=
        last_writable_notify_readonly_timepoint_ + protobuf_to_chrono_duration<std::chrono::system_clock::duration>(
                                                       dtmq_proxysvr_cfg.channel_notify_readonly_subscribe_interval()) *
                                                       3) {
      break;
    }

    bool allow_gc =
        now >
        lost_last_subscriber_timepoint_ +
            protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.cache_expire_timeout()) +
            protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.subscriber_timeout()) +
            std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(1));

    if (allow_gc) {
      FWLOGDEBUG("remove_channel ({}) in gc. no subscribes", get_channel_id());
      mq_channel_manager::me()->remove_channel(get_channel_id(), this);
      return;
    }
  } while (false);

  std::chrono::system_clock::duration timer_interval;
  if (is_readonly()) {
    timer_interval =
        protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.cache_expire_timeout());
  } else {
    timer_interval =
        protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.subscriber_timeout());
    if (timer_interval < std::chrono::seconds{1}) {
      timer_interval = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds{900});
    }

    if (!configure_.memory_only() && save_interval > std::chrono::seconds(0) && save_interval < timer_interval) {
      timer_interval = save_interval;
    }
  }

  mq_channel_manager::me()->update_timer(*this, timer_handle_, timer_interval);
}

void mq_channel::remove_timer() {
  if (timer_handle_.expired()) {
    return;
  }

  auto timer_ptr = timer_handle_.lock();
  if (timer_ptr) {
    mq_channel_timer_type::remove_timer(*timer_ptr);
  }

  timer_handle_.reset();
}

bool mq_channel::upgrade_to_readonly(uint64_t readonly_server_index) noexcept {
  if (status_ != channel_status::kWritable && status_ != channel_status::kReadonly) {
    status_ = channel_status::kReadonly;
    readonly_replicate_index_ = readonly_server_index;
    return true;
  }

  return false;
}

bool mq_channel::upgrade_to_writable() noexcept {
  if (status_ == channel_status::kWritable) {
    return false;
  }

  status_ = channel_status::kWritable;
  wal_client_.reset();

  return true;
}

bool mq_channel::downgrade_to_readable(uint64_t readonly_server_index) noexcept {
  if (status_ == channel_status::kWritable) {
    status_ = channel_status::kReadonly;
    readonly_replicate_index_ = readonly_server_index;
    return true;
  }

  return false;
}

bool mq_channel::downgrade_to_none() noexcept {
  if (status_ != channel_status::kNone) {
    status_ = channel_status::kNone;
    wal_client_.reset();
    return true;
  }

  return false;
}

void mq_channel::send_oss(rpc::context& /*ctx*/, const std::string& /*action*/, int32_t /*ret*/,
                          uint64_t /*transfer_to*/) {
  // FIXME: 这里需要发送OSS日志，暂时注释掉
  // telemetry_oss_user_information user;
  // user.zone_id = logic_config::me()->get_local_zone_id();
  //
  // rpc::context::message_holder<PROJECT_NAMESPACE_ID::oss::DtmqChannel> oss_log{ctx};
  // oss_log->set_typeid_(static_cast<int32_t>(channel_key_.channel_type()));
  // oss_log->set_id(channel_key_.channel_id());
  // oss_log->set_zone(static_cast<int32_t>(channel_key_.channel_zone_id()));
  // oss_log->set_action(action);
  // oss_log->set_ret(ret);
  // oss_log->set_transferfrom(static_cast<int64_t>(logic_config::me()->get_local_server_id()));
  // oss_log->set_transferto(static_cast<int64_t>(transfer_to));
  // telemetry::oss::send_dtmq_channel(ctx, user, std::move(*oss_log));
}

void mq_channel::recalculate_etcd_cache() {
  int64_t latest_server_etcd_revision = mq_channel_manager::me()->get_latest_server_etcd_revision();
  if (server_distribution_etcd_revision_ == latest_server_etcd_revision) {
    return;
  }

  server_distribution_etcd_revision_ = latest_server_etcd_revision;
  std::pair<logic_hpa_discovery_select_mode, replicate_distribution_info*> calc_set[] = {
      {logic_hpa_discovery_select_mode::kReady, &ready_distribution_},
      {logic_hpa_discovery_select_mode::kTarget, &target_distribution_},
  };

  uint64_t local_server_id = logic_config::me()->get_local_server_id();
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  for (auto& calc_data : calc_set) {
    calc_data.second->writable_server_id =
        rpc::dtmq::get_target_server_id(get_channel_key(), rpc::dtmq::replicate_type::kWritable, 0, calc_data.first);

    calc_data.second->current_readonly_server_index = 0;
    calc_data.second->readonly_server_id_to_replicate_index.clear();
    calc_data.second->readonly_replicate_index_to_server_id.clear();
    if (dtmq_proxysvr_cfg.readonly_replicate_count() > 0) {
      calc_data.second->readonly_server_id_to_replicate_index.reserve(
          static_cast<size_t>(dtmq_proxysvr_cfg.readonly_replicate_count()));
      calc_data.second->readonly_replicate_index_to_server_id.reserve(
          static_cast<size_t>(dtmq_proxysvr_cfg.readonly_replicate_count()));
      for (size_t i = 1; i <= static_cast<size_t>(dtmq_proxysvr_cfg.readonly_replicate_count()); i++) {
        uint64_t readonly_server_id = rpc::dtmq::get_target_server_id(
            get_channel_key(), rpc::dtmq::replicate_type::kReadonly, i, calc_data.first);

        // 如果多个readonly节点分布在同一台服务器上，索引最小的一个。这样如果配置缩容readonly数量，可以尽可能保证readonly节点数据不需要迁移
        auto iter = calc_data.second->readonly_server_id_to_replicate_index.find(readonly_server_id);
        if (iter == calc_data.second->readonly_server_id_to_replicate_index.end()) {
          calc_data.second->readonly_server_id_to_replicate_index[readonly_server_id] = static_cast<uint64_t>(i);
        }
        // replicate_index 到 server_id 的索引必须全保留。
        calc_data.second->readonly_replicate_index_to_server_id[static_cast<uint64_t>(i)] = readonly_server_id;
        if (readonly_server_id == local_server_id && readonly_server_id != calc_data.second->writable_server_id) {
          calc_data.second->current_readonly_server_index = i;
        }
      }
    }

    // etcd版本号更新，可能导致从节点分布变化，重置时间立刻通知更新一次
    next_notify_readonly_subscribe_timepoint_ = std::chrono::system_clock::from_time_t(0);
  }
}

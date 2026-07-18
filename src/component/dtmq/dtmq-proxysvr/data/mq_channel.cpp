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
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "atframe/atapp_conf.h"
#include "data/mq_channel_wal_handle.h"
#include "logic/mq_channel_manager.h"
#include "time/time_utility.h"

#ifdef max
#  undef max
#endif

namespace {
constexpr const int32_t kMaxIoTaskContinueFailed = 5;

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

inline static uint64_t normalize_replicate_index(uint64_t replicate_index) {
  if (replicate_index == 0) {
    return 0;
  }

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
  if (dtmq_proxysvr_cfg.readonly_replicate_count() <= 0) {
    return 0;
  }

  if (replicate_index <= static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) {
    return replicate_index;
  }

  return ((replicate_index - 1) % static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count())) + 1;
}
}  // namespace

void mq_channel::mq_channel_accessor::update_timer(mq_channel& mq_channel, rpc::context& ctx, bool force) {
  mq_channel.update_timer(ctx, force);
}

void mq_channel::mq_channel_accessor::remove_timer(mq_channel& mq_channel) { mq_channel.remove_timer(); }

mq_channel::mq_channel(mq_channel_manager& /*manager*/, const atfw::dtmq::DChannelIdKey& channel_key,
                       const atfw::dtmq::DChannelConfigure& configure)
    : sequence_allocator_(0),
      compact_stateful_sequence_(0),
      status_(channel_status::kNone),
      readonly_replicate_index_(0),
      readonly_replicate_configure_count_(0),
      destroy_timepoint_{std::chrono::system_clock::from_time_t(0)},
      destroy_sequence_(0),
      create_timepoint_{std::chrono::system_clock::from_time_t(0)},
      create_sequence_(0),
      last_save_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_status_change_timepoint_{atfw::util::time::time_utility::now()},
      lost_last_subscriber_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_writable_notify_readonly_timepoint_{std::chrono::system_clock::from_time_t(0)},
      next_init_subscribe_timepoint_{std::chrono::system_clock::from_time_t(0)},
      last_result_code_(PROJECT_NAMESPACE_ID::err::EN_SUCCESS),
      custom_data_sequence_(0),
      private_data_sequence_(0),
      need_remove_ttl_(false),
      is_loading_snapshot_(false),
      dirty_version_(0),
      saved_version_(0),
      saved_sequence_(0),
      io_task_continue_failed_(0),
      resolved_transfer_etcd_revision_(0),
      server_distribution_etcd_revision_(0) {
  protobuf_copy_message(channel_key_, channel_key);

  shared_wal_object_ = create_mq_channel_object(*this, configure);
  wal_publisher_ = create_mq_channel_publisher(*this, configure);

  reload_configure(configure);
  next_send_oss_time_ = atfw::util::time::time_utility::now();

  FWLOGINFO("channel {}({}) constructed.", get_channel_id(), reinterpret_cast<const void*>(this));
}

mq_channel::~mq_channel() {
  // Remove timer
  remove_timer();

  if (!mq_channel_manager::is_instance_destroyed()) {
    mq_channel_manager::remove_running_io_channel(this);
  }

  FWLOGINFO("channel {}({}) destructed.", get_channel_id(), reinterpret_cast<const void*>(this));
}

void mq_channel::load(rpc::context& ctx, const atfw::dtmq::DChannelMetadata& metadata,
                      const atfw::dtmq::DChannelRuntime& runtime) {
  merge_destroy_timepoint_and_sequence(ctx, protobuf_to_system_clock(metadata.destroy_timepoint()),
                                       metadata.destroy_sequence());
  merge_created_timepoint_and_sequence(ctx, protobuf_to_system_clock(metadata.create_timepoint()),
                                       metadata.create_sequence());
  if (!is_destroyed()) {
    destroy_timepoint_ = std::chrono::system_clock::from_time_t(0);
    destroy_sequence_ = 0;
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
  do {
    // 如果并发处理时，数据库拉到的数据已经晚于本地transfer过来的快照，以本地位置为准，避免覆盖本地数据
    if ((is_readonly() || is_writable()) && record.channel_metadata().last_sequence() < get_last_message_sequence()) {
      FWLOGINFO("channel {} ignore to load table because local snapshot is newer", get_channel_id());
      break;
    }

    load(ctx, record.channel_metadata(), record.runtime_data());

    mq_channel_wal_publisher_type::log_container_type container;
    int64_t last_removed_key = 0;
    if (nullptr != get_shared_wal_object()->get_last_removed_key()) {
      last_removed_key = *get_shared_wal_object()->get_last_removed_key();
    }
    for (const auto& log : record.record_set().record()) {
      if (get_shared_wal_object()->get_log_key_compare()(log.sequence(), compact_stateful_sequence_) ||
          get_shared_wal_object()->get_log_key_compare()(log.sequence(), last_removed_key)) {
        continue;
      }

      auto log_ptr = atfw::memory::stl::make_strong_rc<mq_channel_wal_publisher_type::log_type>(log);
      if (!log_ptr) {
        FCTXLOGERROR(ctx, "channel {} load log failed, malloc failed.", get_channel_id());
        continue;
      }

      container.emplace_back(std::move(log_ptr));
    }
    get_shared_wal_object()->assign_logs(container);

    protobuf_copy_message(lock_, record.lock());

    // load订阅者缓存(转移时恢复订阅)
    const auto& dtmq_proxysvr_cfg =
        logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
    auto now = atfw::util::time::time_utility::now();
    auto subscriber_timeout_conf = protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
        dtmq_proxysvr_cfg.subscriber_timeout());
    if (subscriber_timeout_conf < std::chrono::seconds{1}) {
      subscriber_timeout_conf =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{1800});
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

    if (get_shared_wal_object()->get_all_logs().empty()) {
      if (nullptr == get_shared_wal_object()->get_last_removed_key() ||
          *get_shared_wal_object()->get_last_removed_key() <= 0) {
        get_shared_wal_object()->set_last_removed_key(get_last_message_sequence());
      }
    } else {
      get_shared_wal_object()->set_last_removed_key((*get_shared_wal_object()->get_all_logs().begin())->sequence());
    }
  } while (false);

  ++dirty_version_;
  saved_version_ = dirty_version_;
  saved_sequence_ = get_last_message_sequence();

  // 有可能await后，负载发生变化。本节点被迁出 writable

  if (should_be_writable()) {
    upgrade_to_writable();
    return;
  }

  const replicate_index_set* ris = nullptr;
  if (should_be_readonly(ris) && ris != nullptr && ris->prefer_replicate_index > 0) {
    if (is_writable()) {
      downgrade_to_readable(ris->prefer_replicate_index);
    } else {
      // 已经加载全量数据，不需要再获取快照了
      maybe_create_wal_client();
      if (wal_client_) {
        wal_client_->set_received_snapshot(true);
      }
      upgrade_to_readonly(ris->prefer_replicate_index);
    }

    return;
  }

  downgrade_to_none();
}

void mq_channel::dump(atfw::dtmq::DChannelMetadata& metadata, bool with_configure, bool with_custom_data) const {
  protobuf_copy_message(*metadata.mutable_channel_key(), get_channel_key());

  if (is_destroyed()) {
    *metadata.mutable_destroy_timepoint() = protobuf_from_system_clock(destroy_timepoint_);
    metadata.set_destroy_sequence(destroy_sequence_);
  } else {
    metadata.clear_destroy_timepoint();
    metadata.set_destroy_sequence(0);
  }
  *metadata.mutable_create_timepoint() = protobuf_from_system_clock(create_timepoint_);
  metadata.set_create_sequence(create_sequence_);

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
    dump_private_data(runtime);
  }

  if (wal_publisher_) {
    if (nullptr != wal_publisher_->get_log_manager().get_last_removed_key()) {
      runtime.set_last_removed_sequence(*wal_publisher_->get_log_manager().get_last_removed_key());
    }
  }
}

void mq_channel::dump_private_data(atfw::dtmq::DChannelRuntime& runtime) const {
  if (!private_data_.type_url().empty() || private_data_sequence_ > 0) {
    protobuf_copy_message(*runtime.mutable_private_data(), private_data_);
  }
  runtime.set_private_data_sequence(private_data_sequence_);
}

void mq_channel::dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_dtmq_channel_record& record) const {
  record.set_channel_id(get_channel_id());

  dump(*record.mutable_channel_metadata(), false, true);

  // dump db一定附带private data
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
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{1800});
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

  snapshot.mutable_messages()->Reserve(static_cast<int>(get_shared_wal_object()->get_all_logs().size()));
  for (const auto& msg : get_shared_wal_object()->get_all_logs()) {
    if (!msg) {
      continue;
    }

    // dump snapshot 时，跳过带状态的message
    if (get_shared_wal_object()->get_log_key_compare()(msg->sequence(), compact_stateful_sequence_)) {
      continue;
    }

    protobuf_copy_message(*snapshot.add_messages(), *msg);
  }

  if (!lock_.lock_holder().empty()) {
    protobuf_copy_message(*snapshot.mutable_lock(), lock_);
  }
}

void mq_channel::reload_configure(const atfw::dtmq::DChannelConfigure& config) {
  protobuf_copy_message(configure_, config);

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  {
    auto& wal_obj_conf = get_shared_wal_object()->get_configure();

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
  }

  {
    auto& wal_obj_conf = get_shared_wal_object()->get_configure();
    auto& publisher_conf = get_wal_publisher().get_configure();

    publisher_conf.gc_expire_duration = wal_obj_conf.gc_expire_duration;
    publisher_conf.gc_log_size = wal_obj_conf.gc_log_size;
    publisher_conf.max_log_size = wal_obj_conf.max_log_size;

    publisher_conf.subscriber_timeout = protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
        dtmq_proxysvr_cfg.subscriber_timeout());
    if (publisher_conf.subscriber_timeout < std::chrono::seconds{1}) {
      publisher_conf.subscriber_timeout =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{1800});
    }
  }

  if (wal_client_) {
    auto& wal_obj_conf = get_shared_wal_object()->get_configure();
    auto& client_conf = wal_client_->get_configure();

    client_conf.gc_expire_duration = wal_obj_conf.gc_expire_duration;
    client_conf.gc_log_size = wal_obj_conf.gc_log_size;
    client_conf.max_log_size = wal_obj_conf.max_log_size;

    client_conf.subscriber_heartbeat_interval =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
            dtmq_proxysvr_cfg.channel_heartbeat_interval());
    if (client_conf.subscriber_heartbeat_interval < std::chrono::seconds{1}) {
      client_conf.subscriber_heartbeat_interval =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{300});
    }
    client_conf.subscriber_heartbeat_retry_interval =
        protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
            dtmq_proxysvr_cfg.channel_heartbeat_retry_interval());
    if (client_conf.subscriber_heartbeat_retry_interval < std::chrono::seconds{1}) {
      client_conf.subscriber_heartbeat_retry_interval =
          std::chrono::duration_cast<atfw::util::distributed_system::wal_duration>(std::chrono::seconds{60});
    }
    if (configure_.heartbeat_interval().seconds() > 0) {
      client_conf.subscriber_heartbeat_interval =
          protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure_.heartbeat_interval());
    }
    if (configure_.heartbeat_retry_interval().seconds() > 0) {
      client_conf.subscriber_heartbeat_retry_interval =
          protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
              configure_.heartbeat_retry_interval());
    }
  }

  if (readonly_replicate_index_ > 0) {
    readonly_replicate_index_ = normalize_replicate_index(readonly_replicate_index_);
  }

  if (readonly_replicate_configure_count_ != dtmq_proxysvr_cfg.readonly_replicate_count()) {
    readonly_replicate_configure_count_ = dtmq_proxysvr_cfg.readonly_replicate_count();

    // 重算分布
    server_distribution_etcd_revision_ = 0;

    // 重算分布后可以自动触发迁移逻辑,新增的只读副本会按需初始化加载，不需要主动通知
  }
}

bool mq_channel::set_custom_data(const google::protobuf::Any& custom_data) noexcept {
  if (atfw::atapp::protobuf_equal(custom_data_, custom_data)) {
    return false;
  }

  protobuf_copy_message(custom_data_, custom_data);
  custom_data_sequence_ = get_last_message_sequence();
  set_dirty();
  return true;
}

bool mq_channel::clear_custom_data() noexcept {
  if (custom_data_.type_url().empty() && custom_data_.value().empty()) {
    return false;
  }

  custom_data_.Clear();
  custom_data_sequence_ = get_last_message_sequence();
  set_dirty();
  return true;
}

void mq_channel::reset_custom_data_sequence() noexcept { custom_data_sequence_ = get_last_message_sequence(); }

bool mq_channel::set_private_data(const google::protobuf::Any& private_data) noexcept {
  if (atfw::atapp::protobuf_equal(private_data_, private_data)) {
    return false;
  }

  protobuf_copy_message(private_data_, private_data);
  private_data_sequence_ = get_last_message_sequence();
  set_dirty();
  return true;
}

bool mq_channel::clear_private_data() noexcept {
  if (private_data_.type_url().empty() && private_data_.value().empty()) {
    return false;
  }

  private_data_.Clear();
  private_data_sequence_ = get_last_message_sequence();
  set_dirty();
  return true;
}

void mq_channel::reset_private_data_sequence() noexcept { private_data_sequence_ = get_last_message_sequence(); }

rpc::result_code_type mq_channel::writable_init(rpc::context& ctx) {
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
          mq_channel_manager::remove_running_io_channel(self_ptr.get());
        }

        self_ptr->last_result_code_ = ret;
        if (ret >= 0) {
          self_ptr->io_task_continue_failed_ = 0;
        } else {
          ++self_ptr->io_task_continue_failed_;
        }
        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} writable_init: create task failed. result: {}({})", get_channel_id(),
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    RPC_RETURN_CODE(*invoke_result.get_error());
  }

  rpc::result_code_type::value_type ret = 0;
  if (invoke_result.is_success()) {
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = *invoke_result.get_success();
      mq_channel_manager::insert_running_io_channel(this);
    } else {
      ret = task_type_trait::get_result(*invoke_result.get_success());
    }
  }

  if (is_io_task_running()) {
    RPC_AWAIT_IGNORE_RESULT(rpc::wait_task(ctx, io_task_));
  }

  if (invoke_result.is_success()) {
    ret = task_type_trait::get_result(*invoke_result.get_success());
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type mq_channel::readonly_init(rpc::context& ctx, uint64_t readonly_server_index) {
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
          mq_channel_manager::remove_running_io_channel(self_ptr.get());
        }

        if (task_type_trait::get_task_id(self_ptr->subscribe_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->subscribe_task_);
        }

        if (ret >= 0) {
          self_ptr->io_task_continue_failed_ = 0;
        } else {
          ++self_ptr->io_task_continue_failed_;
        }
        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} readonly_init: create task failed. result: {}({})", get_channel_id(),
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    RPC_RETURN_CODE(*invoke_result.get_error());
  }

  rpc::result_code_type::value_type ret = 0;
  if (invoke_result.is_success()) {
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = *invoke_result.get_success();
      subscribe_task_ = *invoke_result.get_success();
      mq_channel_manager::insert_running_io_channel(this);
    } else {
      ret = task_type_trait::get_result(*invoke_result.get_success());
    }
  }

  if (!task_type_trait::empty(subscribe_task_) && !task_type_trait::is_exiting(subscribe_task_)) {
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, subscribe_task_));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  if (invoke_result.is_success()) {
    ret = task_type_trait::get_result(*invoke_result.get_success());
  }
  RPC_RETURN_CODE(ret);
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

bool mq_channel::load_snapshot(rpc::context& ctx, atfw::dtmq::channel_snapshot&& snapshot) {
  // 错误的调用导致递归调用，以最上层为准
  if (is_loading_snapshot_) {
    FWLOGERROR("mq channel {} recursive load snapshot is not allowed", get_channel_id());
    return false;
  }
  is_loading_snapshot_ = true;
  auto loading_guard = gsl::finally([this]() { this->is_loading_snapshot_ = false; });

  auto compact_fn = [&]() {
    // 合并日志压缩
    const auto& snapshot_runtime = snapshot.channel_data().channel_runtime();
    if (compact_stateful_sequence_ < snapshot_runtime.compact_stateful_sequence()) {
      compact_stateful_sequence(snapshot_runtime.compact_stateful_sequence());
    }
    if (snapshot_runtime.last_removed_sequence() > 0) {
      if (nullptr == get_shared_wal_object()->get_last_removed_key() ||
          *get_shared_wal_object()->get_last_removed_key() < snapshot_runtime.last_removed_sequence()) {
        compact_sequence(snapshot_runtime.last_removed_sequence());
      }
    }
  };

  if (is_writable()) {
    if (snapshot.replicate_index() > 0) {
      FWLOGINFO("mq channel {} ignore load snapshot, channel is writable but received a readonly snapshot",
                get_channel_id());

      // 订阅者总是要合并的
      merge_subscriber(ctx, snapshot.subscriber());
      return true;
    }

    if (get_last_message_sequence() > snapshot.channel_data().channel_metadata().last_sequence() ||
        (get_last_message_sequence() == snapshot.channel_data().channel_metadata().last_sequence() &&
         get_last_hash_code() == snapshot.channel_data().channel_metadata().last_hash_code())) {
      FWLOGINFO("mq channel {} ignore load snapshot, channel is writable but received a older writable snapshot",
                get_channel_id());

      // 订阅者总是要合并的
      merge_subscriber(ctx, snapshot.subscriber());

      // 合并日志压缩选项
      compact_fn();
      return true;
    }
  }

  if (is_readonly() && snapshot.replicate_index() > 0 &&
      get_last_message_sequence() >= snapshot.channel_data().channel_metadata().last_sequence()) {
    if (get_last_message_sequence() > snapshot.channel_data().channel_metadata().last_sequence() ||
        (get_last_message_sequence() == snapshot.channel_data().channel_metadata().last_sequence() &&
         get_last_hash_code() == snapshot.channel_data().channel_metadata().last_hash_code())) {
      FWLOGINFO("mq channel {} ignore load snapshot, channel is readonly and received an older readonly snapshot",
                get_channel_id());

      // 订阅者总是要合并的
      merge_subscriber(ctx, snapshot.subscriber());
      return true;
    }
  }

  // 其他情况则是加载快照
  //   Case 1: channel是readonly，收到的snapshot是writable的快照，说明本节点被迁移到writable节点了，需要加载快照
  //   Case 2: channel是readonly，收到的snapshot是readonly的快照，说明其他更新的readonly节点迁移到这里
  //   Case 3:
  //   channel是writable，收到的snapshot是writable的快照，说明数据分布出现短暂不一致，或者先从数据库加载，后收到其他节点迁移过来的最新数据，需要加载快照

  FWLOGINFO("mq channel {} load snapshot", get_channel_id());

  last_save_timepoint_ = atfw::util::time::time_utility::now();

  // 仅仅非可写节点才需要创建wal_client_，可写节点只需要wal_publisher_即可
  if (!is_writable() && !should_be_writable()) {
    // snapshot就是全量数据，不需要client再做snapshot检查
    maybe_create_wal_client();
  }

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
    return false;
  }

  // lock
  if (snapshot.channel_data().has_lock()) {
    set_lock(ctx, snapshot.channel_data().lock(), false);
  } else {
    clear_lock();
  }

  if (0 == snapshot.replicate_index()) {
    // 如果时transfer过来的数据，需要以来源的配置为准
    reload_configure(snapshot.channel_data().channel_metadata().channel_configure());
  }

  // 合并日志压缩选项
  compact_fn();

  // 保底设置last_removed_key，这样之前的订阅者如果又滞后的数据能够触发快照下发
  if (get_shared_wal_object()->get_all_logs().empty()) {
    if (nullptr == get_shared_wal_object()->get_last_removed_key() ||
        *get_shared_wal_object()->get_last_removed_key() <= 0) {
      get_shared_wal_object()->set_last_removed_key(get_last_message_sequence());
    }
  } else {
    get_shared_wal_object()->set_last_removed_key((*get_shared_wal_object()->get_all_logs().begin())->sequence());
  }

  // 恢复广播边界,以便如果加载snapshot后有订阅者，能够触发增量广播
  if (restore_broadcast_boundary) {
    wal_publisher_->set_broadcast_key_bound(restore_broadcast_key);
  }

  ++dirty_version_;
  if (snapshot.replicate_index() > 0) {
    upgrade_to_readonly(snapshot.replicate_index());

    saved_version_ = dirty_version_;
    saved_sequence_ = get_last_message_sequence();
  } else if (!is_writable() && should_be_writable()) {
    upgrade_to_writable();
    // 加载快照时，原数据可能未落地保存。所以不能赋值 saved_version_ ，下一次定时器到期要正常保存
  }

  // 订阅者要在数据加载后再合并，否则会触发广播，导致订阅者收到不完整数据
  merge_subscriber(ctx, snapshot.subscriber());

  FWLOGINFO("channel {}({}) load_snapshot finished.", get_channel_id(), reinterpret_cast<const void*>(this));
  return true;
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
    // dump snapshot用于主从同步，一定附带 private data
    dump_private_data(*output.mutable_channel_data()->mutable_channel_runtime());

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
                                                     uint64_t& writable_server_id, mq_channel* channel) {
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

bool mq_channel::should_be_writable() {
  if (server_distribution_etcd_revision_ < mq_channel_manager::me()->get_latest_server_etcd_revision()) {
    recalculate_etcd_cache();
  }

  return target_distribution_.writable_server_id == logic_config::me()->get_local_server_id();
}

bool mq_channel::should_be_readonly_or_get_server_id(const atfw::dtmq::DChannelIdKey& channel_key,
                                                     uint64_t& readonly_server_id, uint64_t readonly_replicate_index,
                                                     mq_channel* channel) {
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
  if (0 == readonly_replicate_index || dtmq_proxysvr_cfg.readonly_replicate_count() <= 0) {
    return should_be_writable_or_get_server_id(channel_key, readonly_server_id, channel);
  }

  readonly_replicate_index = normalize_replicate_index(readonly_replicate_index);

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
    const replicate_index_set* local_readonly_replicate_index_set = nullptr;
    bool ret = channel->should_be_readonly(local_readonly_replicate_index_set);

    ret = ret && nullptr != local_readonly_replicate_index_set &&
          local_readonly_replicate_index_set->index_set.count(readonly_replicate_index) > 0;
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
                                                        uint64_t& readonly_server_id, mq_channel* channel) {
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
          static_cast<size_t>(channel->target_distribution_.readonly_replicate_index_to_server_id.size() + 1));
      if (channel->target_distribution_.writable_server_id != local_server_id) {
        readonly_servers.emplace_back(channel->target_distribution_.writable_server_id, 0);
      }

      for (const auto& readonly_server : channel->target_distribution_.readonly_replicate_index_to_server_id) {
        readonly_servers.emplace_back(readonly_server.second, readonly_server.first);
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

    readonly_replicate_index = iter->second.prefer_replicate_index;
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

  for (uint64_t i = 1; i <= static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count()); i++) {
    readonly_server_id = rpc::dtmq::get_target_server_id(channel_key, rpc::dtmq::replicate_type::kReadonly, i,
                                                         logic_hpa_discovery_select_mode::kTarget);
    // 本机可以成为只读副本的情况
    if (local_server_id == readonly_server_id && can_be_readonly) {
      readonly_replicate_index = i;
      readonly_server_id = local_server_id;
      return true;
    }
    readonly_servers.emplace_back(readonly_server_id, i);
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

bool mq_channel::should_be_readonly(const replicate_index_set * ATFW_UTIL_MACRO_NULLABLE &
                                    readonly_replicate_index_set) {
  if (server_distribution_etcd_revision_ < mq_channel_manager::me()->get_latest_server_etcd_revision()) {
    recalculate_etcd_cache();
  }

  // 可写节点重合，以可写节点为准
  if (should_be_writable()) {
    readonly_replicate_index_set = nullptr;
    return false;
  }

  auto iter =
      target_distribution_.readonly_server_id_to_replicate_index.find(logic_config::me()->get_local_server_id());
  if (target_distribution_.readonly_server_id_to_replicate_index.end() == iter) {
    readonly_replicate_index_set = nullptr;
    return false;
  }

  readonly_replicate_index_set = &iter->second;
  return true;
}

uint64_t mq_channel::get_target_distribution_server_id(uint64_t replicate_index) const noexcept {
  replicate_index = normalize_replicate_index(replicate_index);

  if (replicate_index == 0) {
    return target_distribution_.writable_server_id;
  }

  auto iter = target_distribution_.readonly_replicate_index_to_server_id.find(replicate_index);
  if (iter != target_distribution_.readonly_replicate_index_to_server_id.end()) {
    return iter->second;
  }

  return 0;
}

const mq_channel::replicate_index_set* ATFW_UTIL_MACRO_NULLABLE
mq_channel::get_target_distribution_replicate_index(uint64_t server_id) const noexcept {
  if (target_distribution_.writable_server_id == server_id) {
    return nullptr;
  }

  auto iter = target_distribution_.readonly_server_id_to_replicate_index.find(server_id);
  if (iter == target_distribution_.readonly_server_id_to_replicate_index.end()) {
    return nullptr;
  }

  if (iter->second.index_set.empty()) {
    return nullptr;
  }

  return &iter->second;
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
                                           readonly_replicate_index_, logic_hpa_discovery_select_mode::kTarget);
  }

  return 0;
}

ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type mq_channel::await_transfer(rpc::context& ctx,
                                                                              uint64_t& transfer_to_server_id) {
  transfer_to_server_id = 0;
  while (is_io_task_running()) {
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

  // 如果被销毁，要等到 destroy_sequence_ 被保存后才不需要保存DB ，否则错误重试机制无法保存 destroy 信息
  return is_dirty() && (is_available() || destroy_sequence_ > saved_sequence_);
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
  mq_channel_manager::remove_running_io_channel(this);

  return false;
}

rpc::result_code_type mq_channel::await_io_task(rpc::context& ctx, int32_t* task_result) {
  // 正在转移或读取
  if (!task_type_trait::empty(io_task_)) {
    if (task_type_trait::is_exiting(io_task_)) {
      if (task_result != nullptr) {
        *task_result = task_type_trait::get_result(io_task_);
      }

      task_type_trait::reset_task(io_task_);
      mq_channel_manager::remove_running_io_channel(this);

      RPC_RETURN_CODE(0);
    }
  } else {
    if (task_result != nullptr) {
      *task_result = 0;
    }

    RPC_RETURN_CODE(0);
  }

  auto io_task = io_task_;
  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task));

  if (task_result != nullptr) {
    *task_result = task_type_trait::get_result(io_task);
  }

  // 保底清理，有可能在其他位置被清理又被重新拉起
  if (!task_type_trait::empty(io_task_) && task_type_trait::equal(io_task_, io_task) &&
      task_type_trait::is_exiting(io_task_)) {
    task_type_trait::reset_task(io_task_);
    mq_channel_manager::remove_running_io_channel(this);
  }
  RPC_RETURN_CODE(ret);
}

bool mq_channel::is_io_task_too_many_continue_failed() const noexcept {
  return io_task_continue_failed_ >= kMaxIoTaskContinueFailed;
}

int32_t mq_channel::async_start_transfer(rpc::context& ctx, uint64_t target_server_id) {
  if (is_io_task_running()) {
    return 0;
  }

  if (0 == target_server_id || target_server_id == logic_config::me()->get_local_server_id()) {
    return 0;
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

        bool is_transfer_success = true;
        for (const auto& transfer_failed : rsp_body->failed_channel_key()) {
          if (transfer_failed.channel_id() == self_ptr->get_channel_id()) {
            is_transfer_success = false;
            FWLOGERROR("mq channel {} transfer: transfer to server {:#x} failed", self_ptr->get_channel_id(),
                       target_server_id);
          }
        }
        if (result >= 0 && is_transfer_success) {
          // 如果IO后又要迁回可写节点，当前无需操作，后续会触发upgrade
          const replicate_index_set* readonly_replicate_index_set = nullptr;
          if (self_ptr->should_be_readonly(readonly_replicate_index_set) && readonly_replicate_index_set != nullptr) {
            self_ptr->downgrade_to_readable(readonly_replicate_index_set->prefer_replicate_index);
          } else if (!self_ptr->should_be_writable()) {
            self_ptr->downgrade_to_none();
          }
        }

        if (!mq_channel_manager::is_instance_destroyed()) {
          mq_channel_manager::me()->set_more_transfer();
        }

        if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->io_task_);
          mq_channel_manager::remove_running_io_channel(self_ptr.get());
        }

        if (result >= 0) {
          self_ptr->io_task_continue_failed_ = 0;
        } else {
          ++self_ptr->io_task_continue_failed_;
        }

        RPC_RETURN_CODE(result);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} transfer: create task failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    ++io_task_continue_failed_;
    return *invoke_result.get_error();
  }

  if (invoke_result.is_success()) {
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = *invoke_result.get_success();
      mq_channel_manager::insert_running_io_channel(this);
    } else {
      // 直接结束了就返回任务的结果
      int32_t result = task_type_trait::get_result(*invoke_result.get_success());
      if (result < 0) {
        ++io_task_continue_failed_;
      }
      return result;
    }
  }

  return 0;
}

int32_t mq_channel::async_save(rpc::context& ctx) {
  if (is_io_task_running()) {
    return 0;
  }

  if (!is_writable()) {
    saved_version_ = dirty_version_;
    saved_sequence_ = get_last_message_sequence();
    return 0;
  }

  if (configure_.memory_only()) {
    const replicate_index_set* readonly_replicate_index_set = nullptr;
    if (should_be_readonly(readonly_replicate_index_set) && readonly_replicate_index_set != nullptr) {
      downgrade_to_readable(readonly_replicate_index_set->prefer_replicate_index);
    }

    saved_version_ = dirty_version_;
    saved_sequence_ = get_last_message_sequence();
    return 0;
  }

  auto self_ptr = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, "mq_channel.save", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        if (self_ptr->need_remove_ttl_) {
          auto ret =
              RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::remove_ttl(child_ctx, self_ptr->get_channel_id()));
          if (ret < 0) {
            FWLOGERROR("rpc::db::dtmq_channel_record::remove_ttl faild, channel_id:{}, ret:{}({})",
                       self_ptr->get_channel_id(), ret, protobuf_mini_dumper_get_error_msg(ret));
            ++self_ptr->io_task_continue_failed_;
            RPC_RETURN_CODE(ret);
          }
          self_ptr->need_remove_ttl_ = false;
        }

        self_ptr->tick(child_ctx);

        auto data = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>(child_ctx);
        self_ptr->dump(child_ctx, *data);

        // rpc::db::
        uint64_t saved_version = self_ptr->dirty_version_;
        int64_t saved_sequence = self_ptr->get_last_message_sequence();
        auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::replace(child_ctx, std::move(data)));
        if (ret < 0) {
          FWLOGERROR("rpc::db::dtmq_channel_record::replace faild, channel_id:{}, ret:{}({})",
                     self_ptr->get_channel_id(), ret, protobuf_mini_dumper_get_error_msg(ret));
          ++self_ptr->io_task_continue_failed_;
          RPC_RETURN_CODE(ret);
        }

        self_ptr->saved_version_ = saved_version;
        self_ptr->saved_sequence_ = saved_sequence;
        self_ptr->io_task_continue_failed_ = 0;

        FWLOGDEBUG("rpc::db::dtmq_channel_record::replace channel:{}, ret:{}({})", self_ptr->get_channel_id(), ret,
                   protobuf_mini_dumper_get_error_msg(ret));

        // 如果已移除，则重置删除TTL
        if (self_ptr->is_destroyed()) {
          auto ttl_seconds =
              std::chrono::duration_cast<std::chrono::seconds>(self_ptr->destroy_timepoint_ + get_mq_channel_ttl() -
                                                               atfw::util::time::time_utility::now())
                  .count();
          if (ttl_seconds > 0) {
            RPC_AWAIT_IGNORE_RESULT(rpc::db::dtmq_channel_record::set_ttl(child_ctx, self_ptr->get_channel_id(),
                                                                          static_cast<uint64_t>(ttl_seconds)));
          } else {
            RPC_AWAIT_IGNORE_RESULT(rpc::db::dtmq_channel_record::remove_all(child_ctx, self_ptr->get_channel_id()));
          }
        }

        // 数据transfer
        bool downgrade_after_transfer = false;
        if (self_ptr->is_writable() && !self_ptr->should_be_writable()) {
          if (self_ptr->target_distribution_.writable_server_id != 0 &&
              self_ptr->target_distribution_.writable_server_id != logic_config::me()->get_local_server_id()) {
            // 先重置io_task_，否则async_start_transfer会被忽略
            if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
              task_type_trait::reset_task(self_ptr->io_task_);
              mq_channel_manager::remove_running_io_channel(self_ptr.get());
            }

            self_ptr->async_start_transfer(child_ctx, self_ptr->target_distribution_.writable_server_id);
            downgrade_after_transfer = self_ptr->is_io_task_running();
          }
        }

        if (!downgrade_after_transfer) {
          const replicate_index_set* readonly_replicate_index_set = nullptr;
          if (self_ptr->should_be_readonly(readonly_replicate_index_set) && readonly_replicate_index_set != nullptr) {
            self_ptr->downgrade_to_readable(readonly_replicate_index_set->prefer_replicate_index);
          } else if (!self_ptr->should_be_writable()) {
            self_ptr->downgrade_to_none();
          }

          if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
            task_type_trait::reset_task(self_ptr->io_task_);
            mq_channel_manager::remove_running_io_channel(self_ptr.get());
          }
        }
        RPC_RETURN_CODE(0);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} save: create task failed.result: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    ++io_task_continue_failed_;
    return *invoke_result.get_error();
  }

  if (invoke_result.is_success()) {
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = *invoke_result.get_success();
      mq_channel_manager::insert_running_io_channel(this);
    } else {
      // 直接结束了就返回任务的结果
      int32_t result = task_type_trait::get_result(*invoke_result.get_success());
      if (result < 0) {
        ++io_task_continue_failed_;
      }
      return result;
    }
  }

  return 0;
}

rpc::result_code_type mq_channel::save(rpc::context& ctx) {
  while (is_io_task_running()) {
    rpc::result_code_type::value_type result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  int32_t ret = async_save(ctx);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  if (is_io_task_running()) {
    auto io_task = io_task_;
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }

    RPC_RETURN_CODE(task_type_trait::get_result(io_task));
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type mq_channel::destroy(rpc::context& ctx, std::chrono::system_clock::time_point destroy_timepoint,
                                          int64_t destroy_sequence) {
  // 频道已经销毁，无需重复执行, 但要合并销毁信息
  if (is_destroyed()) {
    merge_destroy_timepoint_and_sequence(ctx, destroy_timepoint, destroy_sequence);
    RPC_RETURN_CODE(0);
  }

  while (is_io_task_running()) {
    rpc::result_code_type::value_type result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  if (is_destroyed()) {
    RPC_RETURN_CODE(0);
  }

  // 主节点通知从节点频道销毁
  set_destroyed(ctx, destroy_timepoint, destroy_sequence);

  if (!is_writable()) {
    RPC_RETURN_CODE(0);
  }

  // 销毁频道不能降级，要让后续的查询能够正确返回销毁信息
  if (get_configure().memory_only()) {
    RPC_RETURN_CODE(0);
  }

  // 如果销毁信息已保存，则无需再等待保存结果，直接返回成功
  if (saved_sequence_ >= destroy_sequence_) {
    RPC_RETURN_CODE(0);
  }

  // 保存一次，下载如果再加载，要重新恢复删除流程
  int32_t res = async_save(ctx);
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  while (is_io_task_running()) {
    int32_t task_result = 0;
    rpc::result_code_type::value_type result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, &task_result));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }

    if (task_result < 0) {
      RPC_RETURN_CODE(task_result);
    }
  }

  // IO操作后如果重新创建了频道，则无需再执行销毁操作
  if (!is_destroyed()) {
    RPC_RETURN_CODE(0);
  }

  auto self_ptr = shared_from_this();
  auto invoke_result =
      rpc::async_invoke(ctx, "mq_channel.destroy", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        // 异步任务启动后如果重新创建了频道，则无需再执行销毁操作
        if (!self_ptr->is_destroyed()) {
          RPC_RETURN_CODE(0);
        }
        // 离线数据删除，使用TTL
        FWLOGINFO("destroy mq channel {}", self_ptr->get_channel_id());

        auto ttl_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(self_ptr->destroy_timepoint_ + get_mq_channel_ttl() -
                                                             atfw::util::time::time_utility::now())
                .count();

        int32_t result = 0;
        if (ttl_seconds > 0) {
          result = RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::set_ttl(child_ctx, self_ptr->get_channel_id(),
                                                                               static_cast<uint64_t>(ttl_seconds)));
        } else {
          result =
              RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::remove_all(child_ctx, self_ptr->get_channel_id()));
        }
        if (result == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
          result = 0;
        }

        // 销毁频道不能降级，要让后续的查询能够正确返回销毁信息
        self_ptr->send_oss(child_ctx, "destroy");

        if (task_type_trait::get_task_id(self_ptr->io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->io_task_);
          mq_channel_manager::remove_running_io_channel(self_ptr.get());
        }

        if (result >= 0) {
          self_ptr->io_task_continue_failed_ = 0;
        } else {
          ++self_ptr->io_task_continue_failed_;
        }
        RPC_RETURN_CODE(0);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("mq channel {} destroy: create task failed.res: {}({})", get_channel_id(), *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    RPC_RETURN_CODE(*invoke_result.get_error());
  }

  // 只要保存成功了，总是视为频道已销毁，下次加载时会重新执行 set_ttl/remove_all
  if (invoke_result.is_success() && !task_type_trait::is_exiting(*invoke_result.get_success())) {
    io_task_ = *invoke_result.get_success();
    mq_channel_manager::insert_running_io_channel(this);
  }

  if (is_io_task_running()) {
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (result < 0) {
      FWLOGERROR("mq channel {} destroy: wait task failed.res: {}({})", get_channel_id(), result,
                 protobuf_mini_dumper_get_error_msg(result));
      RPC_RETURN_CODE(0);
    }
  }

  RPC_RETURN_CODE(0);
}

void mq_channel::ensure_recreate_after_destroyed(rpc::context& ctx) {
  if (!is_writable()) {
    return;
  }

  if (is_available()) {
    return;
  }

  // 如果被销毁了且保存过才需要移除TTL
  need_remove_ttl_ = is_destroyed() && saved_sequence_ > 0;

  size_t old_log_count = get_shared_wal_object()->get_all_logs().size();
  set_created(ctx, std::chrono::system_clock::from_time_t(0), 0);

  if (old_log_count > 0) {
    get_shared_wal_object()->remove_before(atfw::util::time::time_utility::now(), old_log_count);
  }
}

rpc::result_code_type mq_channel::load_from_db(rpc::context& ctx) {
  auto record = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>(ctx);
  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::dtmq_channel_record::get_all(ctx, get_channel_id(), record));
  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    FWLOGINFO("rpc::db::dtmq_channel_record::get_all mq channel:{} record not found", get_channel_id());
    ret = 0;
  }

  // 读取到请清除的频道数据，直接清空
  if (record->channel_metadata().destroy_timepoint().seconds() > 0 &&
      atfw::util::time::time_utility::now() >=
          protobuf_to_system_clock(record->channel_metadata().destroy_timepoint())) {
    record->Clear();
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
  if (!task_type_trait::empty(subscribe_task_) && !task_type_trait::is_exiting(subscribe_task_)) {
    return 0;
  }

  const replicate_index_set* readonly_replicate_index_set = nullptr;
  if (is_writable() || !should_be_readonly(readonly_replicate_index_set)) {
    wal_client_.reset();
    return 0;
  }

  auto self_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "mq_channel.async_send_subscribe_to_writable", [self_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto ret = RPC_AWAIT_CODE_RESULT(self_ptr->send_subscribe_to_writable(child_ctx));

        if (task_type_trait::get_task_id(self_ptr->subscribe_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self_ptr->subscribe_task_);
        }

        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("send_subscribe_to_writable {} : create task failed.res: {}({})", channel_key_.channel_id(),
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    return *invoke_result.get_error();
  }

  if (!task_type_trait::empty(*invoke_result.get_success()) &&
      !task_type_trait::is_exiting(*invoke_result.get_success())) {
    subscribe_task_ = *invoke_result.get_success();
  }

  return 0;
}

rpc::result_code_type mq_channel::await_send_subscribe_to_writable(rpc::context& ctx) {
  while (is_io_task_running()) {
    auto result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  const replicate_index_set* readonly_replicate_index_set = nullptr;
  if (!should_be_readonly(readonly_replicate_index_set)) {
    next_init_subscribe_timepoint_ = std::chrono::system_clock::from_time_t(0);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
  }
  auto now = atfw::util::time::time_utility::now();
  if (next_init_subscribe_timepoint_ > now) {
    FWLOGDEBUG("await_send_subscribe_to_writable too frequent, now {} next time {}", now,
               next_init_subscribe_timepoint_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
  }
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();
  next_init_subscribe_timepoint_ = now + protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(
                                             dtmq_proxysvr_cfg.channel_initialize_subscribe_timepoint());

  int res = async_send_subscribe_to_writable(ctx);
  if (res < 0) {
    FWLOGERROR("async_send_subscribe_to_writable failed, result: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }

  if (!task_type_trait::empty(subscribe_task_) && !task_type_trait::is_exiting(subscribe_task_)) {
    auto result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, subscribe_task_));
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
  }

  if (!task_type_trait::empty(subscribe_task_)) {
    RPC_RETURN_CODE(task_type_trait::get_result(subscribe_task_));
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type mq_channel::send_subscribe_to_writable(rpc::context& ctx) {
  uint64_t dtmq_proxysvr_id = get_ready_distribution_writable_server_id();
  if (dtmq_proxysvr_id == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  rpc::context::message_holder<atfw::dtmq::SSChannelSubscribeReq> req_body{ctx};
  rpc::context::message_holder<atfw::dtmq::SSChannelSubscribeRsp> rsp_body{ctx};
  auto* channel_data = req_body->mutable_heartbeat()->Add();

  if (channel_data == nullptr) {
    FWLOGERROR("send_subscribe_to_writable {} : malloc channel_data failed", channel_key_.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  protobuf_copy_message(*channel_data->mutable_channel_key(), channel_key_);

  if (is_writable()) {
    RPC_RETURN_CODE(0);
  }

  maybe_create_wal_client();
  auto wal_client = get_wal_client();
  if (!wal_client) {
    FWLOGERROR("wal_client is not init! channel id: {}", channel_key_.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_WAL_CLIENT_NOT_INIT);
  }
  // 必须以最后一条log为准
  if (!wal_client->get_log_manager().get_all_logs().empty()) {
    auto last_iter = wal_client->get_log_manager().get_all_logs().rbegin();
    int64_t last_sequence = 0;
    if (last_iter != wal_client->get_log_manager().get_all_logs().rend() && *last_iter) {
      last_sequence = (*last_iter)->sequence();
      channel_data->set_last_sequence((*last_iter)->sequence());
      channel_data->set_last_hash_code((*last_iter)->hash_code());
    }

    const auto& dtmq_proxysvr_cfg =
        logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

    auto& hash_mismatch_data = wal_client->get_private_data().hash_mismatch_data;
    if (hash_mismatch_data.log_key == last_sequence &&
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

  auto* subscriber = req_body->mutable_subscriber();
  subscriber->set_subscriber_server_id(logic_config::me()->get_local_server_id());
  // 主从同步场景必须实时同步 private_data，否则从节点无法获取到订阅信息
  subscriber->set_with_private_data(true);
  *subscriber->mutable_last_heartbeat_timepoint() =
      protobuf_from_system_clock(last_writable_notify_readonly_timepoint_);

  auto ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::subscribe(ctx, dtmq_proxysvr_id, *req_body, *rsp_body));
  // 不需要再模拟删除了，删除频道的消息和其他消息并无不同会正常下发
  if (ret >= 0) {
    update_last_writable_notify_time();
  }
  RPC_RETURN_CODE(ret);
}

void mq_channel::set_destroyed(rpc::context& ctx, std::chrono::system_clock::time_point destroy_timepoint,
                               int64_t destroy_sequence) {
  // 频道不需要被destroy多次,但是需要Merge销毁信息
  if (is_destroyed()) {
    merge_destroy_timepoint_and_sequence(ctx, destroy_timepoint, destroy_sequence);
    return;
  }

  // writable主节自己创建和分配 destroy_timepoint 和 destroy_sequence
  if (!is_writable()) {
    merge_destroy_timepoint_and_sequence(ctx, destroy_timepoint, destroy_sequence);
    return;
  }

  need_remove_ttl_ = false;
  last_status_change_timepoint_ = atfw::util::time::time_utility::now();

  if (!wal_publisher_) {
    FWLOGERROR("channel {} set_destroyed but wal_publisher_ is null", get_channel_id());
    return;
  }
  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};
  auto message = wal_publisher_->allocate_log(atfw::util::time::time_utility::now(),
                                              atfw::dtmq::DChannelMessageDetail::kDestroy, params);
  if (message) {
    *message->mutable_detail()->mutable_destroy()->mutable_removed_timepoint() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now());
    wal_publisher_->emplace_back_log(std::move(message), params);
    FWLOGINFO("mq channel {} set_destroyed, destroy_timepoint: {}, destroy_sequence: {}", get_channel_id(),
              std::chrono::duration_cast<std::chrono::seconds>(destroy_timepoint_.time_since_epoch()).count(),
              destroy_sequence_);

    set_dirty();
  } else {
    FWLOGERROR("malloc wal log for mq channel {} to destroy failed", get_channel_id());
  }
}

void mq_channel::merge_destroy_timepoint_and_sequence(rpc::context& /*ctx*/,
                                                      std::chrono::system_clock::time_point destroy_timepoint,
                                                      int64_t destroy_sequence) noexcept {
  bool make_dirty = false;

  if (destroy_timepoint > destroy_timepoint_) {
    destroy_timepoint_ = destroy_timepoint;
    make_dirty = true;
  }

  if (destroy_sequence > destroy_sequence_) {
    destroy_sequence_ = destroy_sequence;
    make_dirty = true;
  }

  if (make_dirty && !is_dirty()) {
    set_dirty();
  }
}

void mq_channel::set_created(rpc::context& ctx, std::chrono::system_clock::time_point create_timepoint,
                             int64_t create_sequence) {
  // 不需要再创建了，已经创建过了,但是需要Merge创建信息
  if (is_available()) {
    merge_created_timepoint_and_sequence(ctx, create_timepoint, create_sequence);
    return;
  }

  // writable主节自己创建和分配 create_timepoint 和 create_sequence
  if (!is_writable()) {
    merge_created_timepoint_and_sequence(ctx, create_timepoint, create_sequence);
    return;
  }

  if (!wal_publisher_) {
    FWLOGERROR("channel {} set_created but wal_publisher_ is null", get_channel_id());
    return;
  }
  last_status_change_timepoint_ = atfw::util::time::time_utility::now();

  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};
  auto message = wal_publisher_->allocate_log(atfw::util::time::time_utility::now(),
                                              atfw::dtmq::DChannelMessageDetail::kCreate, params);
  if (message) {
    *message->mutable_detail()->mutable_create()->mutable_create_timepoint() =
        protobuf_from_system_clock(atfw::util::time::time_utility::now());
    wal_publisher_->emplace_back_log(std::move(message), params);
    FWLOGINFO("mq channel {} set_created, create_timepoint: {}, create_sequence: {}", get_channel_id(),
              std::chrono::duration_cast<std::chrono::seconds>(create_timepoint_.time_since_epoch()).count(),
              create_sequence_);

    set_dirty();
  } else {
    FWLOGERROR("malloc wal log for mq channel {} to create failed", get_channel_id());
  }
}

void mq_channel::merge_created_timepoint_and_sequence(rpc::context& /*ctx*/,
                                                      std::chrono::system_clock::time_point create_timepoint,
                                                      int64_t create_sequence) noexcept {
  bool make_dirty = false;

  if (create_timepoint > create_timepoint_) {
    create_timepoint_ = create_timepoint;
    make_dirty = true;
  }

  if (create_sequence > create_sequence_) {
    create_sequence_ = create_sequence;
    make_dirty = true;
  }

  if (make_dirty && !is_dirty()) {
    set_dirty();
  }
}

int32_t mq_channel::subscribe(rpc::context& ctx, const atfw::dtmq::channel_subscriber& subscriber_info,
                              int64_t last_received_sequence, uint64_t last_received_hash_code, bool merge_mode) {
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
    subscriber->get_private_data().set_with_private_data(subscriber_info.with_private_data());

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

  if (subscriber) {
    FWLOGDEBUG("add subscriber_info success. subscriber_info:({}), subscriber.private_data:({})",
               protobuf_mini_dumper_get_readable(subscriber_info),
               protobuf_mini_dumper_get_readable(subscriber->get_private_data()));
  } else {
    FWLOGDEBUG("add subscriber_info failed. subscriber_info:({}), reson: {}",
               protobuf_mini_dumper_get_readable(subscriber_info), "create_subscriber failed");
  }
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
  if (!get_shared_wal_object()->get_all_logs().empty()) {
    auto last_sequence = (*get_shared_wal_object()->get_all_logs().rbegin())->sequence();
    if (last_sequence >= sequence_allocator_) {
      sequence_allocator_ = last_sequence;
    }
  }

  if (sequence_allocator_ <= 1) {
    sequence_allocator_ =
        (atfw::util::time::time_utility::get_sys_now() * 1000000) + util::time::time_utility::get_now_usec();
  }

  return ++sequence_allocator_;
}

int64_t mq_channel::get_last_message_sequence() const noexcept {
  if (sequence_allocator_ != 0) {
    return sequence_allocator_;
  }

  if (!get_shared_wal_object()->get_all_logs().empty()) {
    return (*get_shared_wal_object()->get_all_logs().rbegin())->sequence();
  }

  // 初始默认是1，这样没订阅过的client第一条消息带0上来可以触发快照下发
  return 1;
}

uint64_t mq_channel::get_last_hash_code() const noexcept {
  if (!get_shared_wal_object()->get_all_logs().empty()) {
    return (*get_shared_wal_object()->get_all_logs().rbegin())->hash_code();
  }

  return 0;
}

uint64_t mq_channel::get_client_last_hash_code() const noexcept {
  if (wal_client_ && !wal_client_->get_log_manager().get_all_logs().empty()) {
    return (*wal_client_->get_log_manager().get_all_logs().rbegin())->hash_code();
  }

  return 0;
}

uint64_t mq_channel::get_ready_distribution_writable_server_id() { return ready_distribution_.writable_server_id; }

uint64_t mq_channel::get_target_distribution_writable_server_id() { return target_distribution_.writable_server_id; }

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

  if (is_readonly()) {
    if (!wal_client_) {
      maybe_create_wal_client();
    }
  }

  // 引用生命周期，防止tick过程中被析构
  auto wal_client = wal_client_;
  if (wal_client) {
    wal_client->tick(atfw::util::time::time_utility::now(), params, dtmq_proxysvr_cfg.max_events_per_tick());
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

  // IO任务不在运行，且已经标脏了，则不需要重复标记
  if (!is_io_task_running() && dirty_version_ != saved_version_) {
    return;
  }

  ++dirty_version_;
}

void mq_channel::set_lock(rpc::context& ctx, const atfw::dtmq::DChannelOptimisticLock& lock, bool append_log) {
  // Ignore if unchanged
  if (atfw::atapp::protobuf_equal(lock_, lock)) {
    return;
  }

  protobuf_copy_message(lock_, lock);
  set_dirty();

  FWLOGDEBUG("channel {} lock updated.", get_channel_id());
  if (!append_log || !shared_wal_object_) {
    return;
  }

  if (!is_writable() && !(is_readonly() && wal_client_)) {
    return;
  }

  int32_t result = 0;
  mq_channel_wal_object_context params{ctx, result};
  auto message = get_shared_wal_object()->allocate_log(atfw::util::time::time_utility::now(),
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
  if (sequence > get_last_message_sequence()) {
    sequence = get_last_message_sequence();
  }

  if (sequence > compact_stateful_sequence_) {
    compact_stateful_sequence_ = sequence;

    set_dirty();
  }
}

void mq_channel::compact_sequence(int64_t sequence) {
  if (sequence > get_last_message_sequence()) {
    sequence = get_last_message_sequence();
  }

  bool need_make_dirty = false;
  if (nullptr == get_shared_wal_object()->get_last_removed_key() ||
      sequence > *get_shared_wal_object()->get_last_removed_key()) {
    get_shared_wal_object()->set_last_removed_key(sequence);

    need_make_dirty = true;
  }

  size_t remove_count = 0;
  for (auto iter = get_shared_wal_object()->log_begin(); iter != get_shared_wal_object()->log_end(); ++iter) {
    if (!*iter) {
      ++remove_count;
      continue;
    }

    if (get_shared_wal_object()->get_log_key_compare()((*iter)->sequence(), sequence)) {
      ++remove_count;
    } else {
      break;
    }
  }

  if (remove_count > 0) {
    get_shared_wal_object()->remove_before(atfw::util::time::time_utility::now(), remove_count);
    need_make_dirty = true;
  }

  if (need_make_dirty) {
    set_dirty();
  }
}

void mq_channel::maybe_create_wal_client() {
  if (wal_client_) {
    return;
  }

  const replicate_index_set* readonly_replicate_index_set = nullptr;
  if (!should_be_readonly(readonly_replicate_index_set)) {
    return;
  }

  auto configure = excel::get_dtmq_channel_configure(get_channel_key().channel_type());
  if (configure) {
    wal_client_ = create_mq_channel_client(*this, *configure);
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
  if (!(*last_iter)) {
    return;
  }
  auto& hash_mismatch_data = wal_client_->get_private_data().hash_mismatch_data;
  if (hash_mismatch_data.log_key == (*last_iter)->sequence()) {
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

  auto now = atfw::util::time::time_utility::now();
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  // 降级或由 !auto_create 创建的临时频道，都要在过期时间到后清理掉
  // 如果时第一次destroy保存失败，这里也要保存成功后才能清理
  if ((!is_writable() && !is_readonly()) ||
      (!is_available() && (!is_writable() || saved_sequence_ >= destroy_sequence_))) {
    std::chrono::system_clock::duration cache_expire_interval =
        protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.cache_expire_timeout());
    cache_expire_interval += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(1));

    bool allow_gc = now > last_status_change_timepoint_ + cache_expire_interval;
    if (allow_gc && !is_io_task_running()) {
      FWLOGDEBUG("remove_channel ({}) in gc. not readonly or writable replicate", get_channel_id());
      mq_channel_manager::me()->remove_channel(get_channel_id(), this);
    } else {
      // 保护性移除或插入新定时器
      remove_timer();

      mq_channel_manager::me()->update_timer(*this, timer_handle_, cache_expire_interval);
    }

    return;
  }

  // 定期保存
  auto save_interval =
      protobuf_to_chrono_duration<std::chrono::system_clock::duration>(dtmq_proxysvr_cfg.save_interval());
  if (configure_.memory_only() || is_readonly()) {
    saved_version_ = dirty_version_;
    saved_sequence_ = get_last_message_sequence();
  } else if (is_dirty() && (now > last_save_timepoint_ + save_interval || force)) {
    if (!is_io_task_running()) {
      last_save_timepoint_ = now;
      auto channel_ptr = shared_from_this();
      if (mq_channel_manager::me()->is_running_io_busy()) {
        mq_channel_manager::me()->add_pending_io_channel(channel_ptr);
      } else {
        channel_ptr->async_save(ctx);
      }
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
    if (!is_readonly() && is_dirty()) {
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
    last_status_change_timepoint_ = atfw::util::time::time_utility::now();
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

  last_status_change_timepoint_ = atfw::util::time::time_utility::now();

  // 如果由Readonly提升上来，可能来源的transfer未保存，需要标记脏下次定时器触发保存
  if (status_ == channel_status::kReadonly) {
    ++dirty_version_;
  }
  status_ = channel_status::kWritable;
  wal_client_.reset();

  return true;
}

bool mq_channel::downgrade_to_readable(uint64_t readonly_server_index) noexcept {
  if (status_ == channel_status::kWritable) {
    last_status_change_timepoint_ = atfw::util::time::time_utility::now();
    status_ = channel_status::kReadonly;
    readonly_replicate_index_ = readonly_server_index;
    return true;
  }

  return false;
}

bool mq_channel::downgrade_to_none() noexcept {
  if (status_ != channel_status::kNone) {
    last_status_change_timepoint_ = atfw::util::time::time_utility::now();
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

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  std::vector<uint64_t> server_id_set;
  for (auto& calc_data : calc_set) {
    server_id_set.clear();
    rpc::dtmq::get_target_server_ids(server_id_set, get_channel_key(),
                                     static_cast<uint64_t>(dtmq_proxysvr_cfg.readonly_replicate_count()),
                                     calc_data.first);
    if (!server_id_set.empty()) {
      calc_data.second->writable_server_id = server_id_set[0];
    } else {
      calc_data.second->writable_server_id = 0;
    }

    calc_data.second->readonly_server_id_to_replicate_index.clear();
    calc_data.second->readonly_replicate_index_to_server_id.clear();
    if (dtmq_proxysvr_cfg.readonly_replicate_count() > 0) {
      calc_data.second->readonly_server_id_to_replicate_index.reserve(
          static_cast<size_t>(dtmq_proxysvr_cfg.readonly_replicate_count()));
      calc_data.second->readonly_replicate_index_to_server_id.reserve(
          static_cast<size_t>(dtmq_proxysvr_cfg.readonly_replicate_count()));
      for (size_t i = 1; i < server_id_set.size(); i++) {
        uint64_t readonly_server_id = server_id_set[i];

        auto& ris = calc_data.second->readonly_server_id_to_replicate_index[readonly_server_id];
        ris.index_set.insert(static_cast<uint64_t>(i));
        // 优先使用最小的索引作为当前readonly节点索引，避免多个readonly节点分布在同一台服务器上时，导致readonly节点索引不稳定
        if (readonly_server_id != calc_data.second->writable_server_id &&
            (ris.prefer_replicate_index == 0 || static_cast<uint64_t>(i) < ris.prefer_replicate_index)) {
          ris.prefer_replicate_index = static_cast<uint64_t>(i);
        }
        // replicate_index 到 server_id 的索引必须全保留。
        calc_data.second->readonly_replicate_index_to_server_id[static_cast<uint64_t>(i)] = readonly_server_id;
      }
    }
  }
}

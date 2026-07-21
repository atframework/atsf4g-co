// Copyright 2026 atframework
// @brief Created by owent

#include "logic/mq_channel_manager.h"

#include <atframe/modules/service_discovery_module.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.struct.dtmq.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel/config_easy_api.h>
#include <config/excel_config_dtmq_index.h>
#include <config/extern_service_types.h>

#include <logic/logic_server_setup.h>

#include <rpc/rpc_context.h>

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>

#include "data/mq_channel.h"
#include "log/log_wrapper.h"

#if defined(_LIBCPP_VERSION)
#  define ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI 0
#elif !defined(__GLIBCXX__)
#  define ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI 0
#elif defined(__cplusplus) && (__cplusplus < 201103L)
#  define ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI 1
#elif !defined(_GLIBCXX_USE_CXX11_ABI)
#  define ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI 0
#elif defined(_GLIBCXX_USE_CXX11_ABI) && (_GLIBCXX_USE_CXX11_ABI == 0)
#  define ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI 1
#endif

#ifndef ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI
#  define ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI 0
#endif

namespace {

constexpr const int32_t kPageQueryDefaultSize = 10;

template <class Rep, class Period>
static time_t chrono_to_timer_tick(std::chrono::duration<Rep, Period> d) {
  return static_cast<time_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count() / 100);
}

static time_t chrono_to_timer_tick(std::chrono::system_clock::time_point tp) {
  return chrono_to_timer_tick(tp.time_since_epoch());
}
}  // namespace

mq_channel_manager::mq_channel_manager()
    : last_tick_timepoint_(std::chrono::system_clock::from_time_t(0)),
      dtmq_server_distribute_etcd_revision_(0),
      iterating_pending_io_channels_(false),
      more_transfer_now_(false),
      is_stoping_(false),
      is_pre_stoping_(false),
      report_channel_qty_time_(std::chrono::system_clock::from_time_t(0)),
      configure_concurrency_io_task_count_(0) {}

mq_channel_manager::~mq_channel_manager() {}

int mq_channel_manager::init() {
  auto now = atfw::util::time::time_utility::now();
  timers_.init(chrono_to_timer_tick(now));

  logic_server_common_module* common_mod = logic_server_last_common_module();
  if (nullptr == common_mod) {
    FWLOGERROR("can not find {}", "logic_server_common_module");
    return -1;
  }

  auto service_discovery_mod = common_mod->get_service_discovery_module();
  if (!service_discovery_mod) {
    FWLOGERROR("service_discovery module not found");
    return -1;
  }
  dtmq_server_distribute_etcd_revision_ = service_discovery_mod->get_last_etcd_event_discovery_header().revision;

  service_discovery_mod->add_on_node_discovery_event(
      [](atfw::atapp::etcd_discovery_action_t /*action*/, const atfw::atapp::etcd_discovery_node::ptr_t& discovery) {
        if (!discovery) {
          return;
        }

        if (discovery->get_discovery_info().type_id() !=
            static_cast<uint64_t>(atfw::component::logic_service_type::kDtMqProxySvr)) {
          return;
        }

        if (mq_channel_manager::is_instance_destroyed()) {
          return;
        }

        const auto& inner_dtmq_proxysvr_cfg =
            logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

        FWLOGDEBUG("dtmq_proxysvr discovery_info has update, begin to channel_distribution");
        int64_t discovery_version = discovery->get_version().modify_revision;
        if (discovery_version > mq_channel_manager::me()->dtmq_server_distribute_etcd_revision_) {
          mq_channel_manager::me()->dtmq_server_distribute_etcd_revision_ = discovery_version;
          FWLOGINFO("dtmq_proxysvr distribute changed, dtmq_server_distribute_etcd_revision_  {}", discovery_version);
        }

        // 设置冷静窗口，应该要小于HPA模块的replicate_period配置
        if (mq_channel_manager::me()->pending_transfer_.start_time == std::chrono::system_clock::from_time_t(0)) {
          mq_channel_manager::me()->pending_transfer_.start_time =
              atfw::util::time::time_utility::sys_now() +
              protobuf_to_chrono_duration<std::chrono::system_clock::duration>(
                  inner_dtmq_proxysvr_cfg.channel_transfer_stabilization_window());
        }

        mq_channel_manager::me()->tick();
      });

  return 0;
}

int mq_channel_manager::reload() {
  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  configure_concurrency_io_task_count_ = dtmq_proxysvr_cfg.concurrency_io_task_count();
  if (configure_concurrency_io_task_count_ <= 0) {
    configure_concurrency_io_task_count_ = 4096;
  }

  for (const auto& channel : channels_) {
    auto channel_configure = excel::get_dtmq_channel_configure(channel.second->get_channel_key().channel_type());
    if (!channel_configure) {
      FWLOGWARNING("reload channel {} configure of type {} not found.", channel.second->get_channel_key().channel_id(),
                   channel.second->get_channel_key().channel_type());
      continue;
    }
    channel.second->reload_configure(*channel_configure);
  }
  FWLOGINFO("reload channel configure done.");
  return 0;
}

int mq_channel_manager::tick() {
  auto now = util::time::time_utility::now();
  int ret = 0;

  auto last_tick = chrono_to_timer_tick(last_tick_timepoint_);
  auto now_tick = chrono_to_timer_tick(now);
  if (last_tick == now_tick) {
    if (more_transfer_now_) {
      resolve_channel_io();
    }
    return ret;
  }

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  if (now > report_channel_qty_time_) {
    report_channel_qty_time_ = now + protobuf_to_chrono_duration<std::chrono::system_clock::duration>(
                                         dtmq_proxysvr_cfg.report_channel_qty_tick());
    report_channel_qty_oss();
  }

  if (last_tick / (10 * util::time::time_utility::MINITE_SECONDS) !=
      now_tick / (10 * util::time::time_utility::MINITE_SECONDS)) {
    FWLOGINFO("[STATICS]: mq channel count: {}, timer count: {}", channels_.size(), timers_.size());
  }
  last_tick_timepoint_ = now;
  int res = timers_.tick(now_tick);
  if (res < 0) {
    FWLOGERROR("jiffies_timer tick error, res: {}", res);
  } else {
    ret += res;
  }

  // 处理节点预下线的逻辑
  if (is_pre_stoping_ && !is_stoping_) {
    stop();
  }

  resolve_channel_io();

  // 如果Hpa模块不处于target集群，则需要触发pre_stoping逻辑，确保在节点下线前数据迁移到其他节点或触发保存
  // 用 !channels_.empty() 确保如果收到老数据，节点未就绪前不会触发pre_stoping逻辑。channel未空时也不需要转移或保存数据
  if (!channels_.empty() && !logic_hpa_current_node_is_in_target()) {
    pre_stoping();
  }

  // libstdc++ 在非 C++ 11 ABI下，std::list的size()复杂度为O(n)，在 add_pending_io_channel 中调用size()会导致性能问题。
  // 这里走fallack逻辑，仅在tick时低频率检查清理。
#if ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI != 0
  // 迭代过程中不能做清理操作
  if (!iterating_pending_io_channels_ && pending_io_channels_.size() >= channels_.size() * 4) {
    compact_pending_io_channels();
  }
#endif
  return ret;
}

void mq_channel_manager::pre_stoping() noexcept { is_pre_stoping_ = true; }

int mq_channel_manager::stop() {
  if (!is_stoping_) {
    // 节点已下线
    FWLOGINFO("[channel_stop] mq_channel_manager begin to stop");

    // 重置冷静窗口，立即执行迁移
    pending_transfer_.start_time = atfw::util::time::time_utility::sys_now();

    is_stoping_ = true;

    std::unordered_set<mq_channel*> pending_io_channel_cache;
    for (const auto& channel : pending_io_channels_) {
      pending_io_channel_cache.insert(channel.get());
    }

    for (const auto& channel : channels_) {
      // 直接刷新分布计算，避免在停机阶段数据转移到滞后的节点从而重复转移数据
      channel.second->force_refresh_distribution();

      if (pending_io_channel_cache.end() != pending_io_channel_cache.find(channel.second.get())) {
        continue;
      }

      if (0 != channel.second->get_transfer_target_server_id() || channel.second->need_save_db()) {
        reactive_io_channels_.erase(channel.second.get());
        pending_io_channels_.push_back(channel.second);
        FWLOGDEBUG("[channel_stop] pending_io_channels_ add channel({})", channel.second->get_channel_id());
      }
    }
  }

  return is_can_stopped() ? 0 : 1;
}

bool mq_channel_manager::is_stoping() const noexcept { return is_stoping_; }

bool mq_channel_manager::is_can_stopped() const noexcept {
  return is_stoping_ && pending_io_channels_.empty() && running_io_channels_.empty();
}

void mq_channel_manager::update_timer(mq_channel& channel, mq_channel_timer_type::timer_wptr_t& output_handle,
                                      std::chrono::system_clock::duration timeout) {
  FWLOGDEBUG("channel({}) update_timer({})", channel.get_channel_id(), timeout);

  atfw::util::memory::weak_rc_ptr<mq_channel> watcher = channel.shared_from_this();
  timers_.add_timer(
      chrono_to_timer_tick(timeout),
      [watcher](time_t /*tick*/, const mq_channel_timer_type::timer_t&) {
        auto channel_ptr = watcher.lock();
        if (channel_ptr) {
          rpc::context ctx{rpc::context::create_without_task()};
          mq_channel::mq_channel_accessor::update_timer(*channel_ptr, ctx);
        }
      },
      &output_handle);
}

rpc::result_code_type mq_channel_manager::create_channel(rpc::context& ctx, mq_channel_ptr_type& channel,
                                                         const atfw::dtmq::DChannelIdKey& channel_key,
                                                         const atfw::dtmq::DChannelConfigure& configure) {
  if (channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  channel = get_channel(channel_key.channel_id());
  if (!channel) {
    channel = atfw::memory::stl::make_strong_rc<mq_channel>(*this, channel_key, configure);
    if (!channel) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    add_channel(ctx, channel);
  }

  // 刷新分布计算
  if (channel) {
    channel->force_refresh_distribution();
  }

  // 拉取数据托管给, writable_init/readonly_init
  RPC_RETURN_CODE(0);
}

void mq_channel_manager::add_channel(rpc::context& ctx, const mq_channel_ptr_type& channel) {
  if (!channel) {
    return;
  }

  // 停服阶段，直接transfer
  if (is_stoping()) {
    set_more_transfer();

    reactive_io_channels_.erase(channel.get());
    pending_io_channels_.push_back(channel);
    return;
  }

  auto iter = channels_.find(channel->get_channel_id());
  if (iter != channels_.end()) {
    if (iter->second == channel) {
      return;
    }

    // 替换
    remove_channel(channel->get_channel_id(), iter->second.get());
  }

  // 特殊索引世界广播频道和大区
  channels_[channel->get_channel_id()] = channel;

  // 刷新定时器
  mq_channel::mq_channel_accessor::update_timer(*channel, ctx);
}

void mq_channel_manager::remove_channel(const std::string& channel_id, const mq_channel* except) {
  auto iter = channels_.find(channel_id);
  if (iter == channels_.end()) {
    return;
  }

  // 检查可能 RPC 后发生实例替换
  if (nullptr != except && iter->second.get() != except) {
    return;
  }

  mq_channel_ptr_type channel = iter->second;

  // 移除定时器
  mq_channel::mq_channel_accessor::remove_timer(*channel);

  // 移除索引
  channels_.erase(iter);
}

rpc::result_code_type mq_channel_manager::make_writable_channel(rpc::context& ctx, mq_channel_ptr_type& channel_ptr,
                                                                uint64_t& forward_server_id,
                                                                const atfw::dtmq::DChannelIdKey& channel_key,
                                                                bool auto_create) {
  if (!(channel_ptr && channel_ptr->get_channel_id() == channel_key.channel_id())) {
    channel_ptr = get_channel(channel_key.channel_id());
  }
  forward_server_id = 0;

  // 已有数据
  if (channel_ptr) {
    auto result = RPC_AWAIT_CODE_RESULT(channel_ptr->await_transfer(ctx, forward_server_id));
    if (result < 0) {
      FCTXLOGERROR(ctx, "channel {} await transfer failed. result: {}({})", channel_key.channel_id(), result,
                   protobuf_mini_dumper_get_error_msg(result));
      RPC_RETURN_CODE(result);
    }

    if (channel_ptr->is_writable()) {
      FCTXLOGDEBUG(ctx, "channel {} select existed writable channel", channel_key.channel_id());
      forward_server_id = 0;

      if (auto_create) {
        channel_ptr->ensure_recreate_after_destroyed(ctx);
      }
      RPC_RETURN_CODE(0);
    }

    channel_ptr.reset();
    if (0 != forward_server_id) {
      FCTXLOGDEBUG(ctx, "channel {} should transfer writable message to server {:#x}", channel_key.channel_id(),
                   forward_server_id);
      RPC_RETURN_CODE(0);
    }
  }

  uint64_t local_server_id = logic_config::me()->get_local_server_id();

  // 如果本地无缓存或不是writable，且本节点可以提升writable，则走提升为writable流程
  if (!mq_channel::should_be_writable_or_get_server_id(channel_key, forward_server_id)) {
    // 判定writable副本为本机，但本机又不能成为writable副本。说明本机正在被关闭
    if (forward_server_id == local_server_id || forward_server_id == 0) {
      FCTXLOGWARNING(ctx, "channel {} server {:#x} is under maintenance, and no more available node now.",
                     channel_key.channel_id(), forward_server_id);
      forward_server_id = 0;
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAINTENANCE);
    }

    FCTXLOGDEBUG(ctx, "channel {} should transfer writable message to server {:#x}", channel_key.channel_id(),
                 forward_server_id);
    RPC_RETURN_CODE(0);
  }

  auto channel_configure = excel::get_dtmq_channel_configure(channel_key.channel_type());
  if (!channel_configure) {
    FCTXLOGWARNING(ctx, "channel {} configure of type {} not found.", channel_key.channel_id(),
                   channel_key.channel_type());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  auto result = RPC_AWAIT_CODE_RESULT(create_channel(ctx, channel_ptr, channel_key, *channel_configure));
  if (result < 0) {
    FCTXLOGERROR(ctx, "channel {} create failed with result {}({}).", channel_key.channel_id(), result,
                 protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (!channel_ptr) {
    FCTXLOGERROR(ctx, "channel {} create failed with unknown reason.", channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  // 如果有到channel，但是不是writable，需要提升为writable
  if (!channel_ptr->is_writable()) {
    result = RPC_AWAIT_CODE_RESULT(channel_ptr->writable_init(ctx));
    if (result < 0) {
      if (auto_create || result != PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND) {
        FCTXLOGERROR(ctx, "channel {} writable init failed with result {}({}).", channel_key.channel_id(), result,
                     protobuf_mini_dumper_get_error_msg(result));
      }
      RPC_RETURN_CODE(result);
    }
  }

  if (!channel_ptr->is_writable() || channel_ptr != get_channel(channel_key.channel_id())) {
    FCTXLOGERROR(ctx, "channel {} writable init failed with unknown reason. maybe concurrency conflict",
                 channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  FCTXLOGDEBUG(ctx, "channel {} is created and writable inited successfully", channel_key.channel_id());
  if (auto_create) {
    channel_ptr->ensure_recreate_after_destroyed(ctx);
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type mq_channel_manager::make_readable_channel(rpc::context& ctx, mq_channel_ptr_type& channel_ptr,
                                                                uint64_t& forward_server_id,
                                                                const atfw::dtmq::DChannelIdKey& channel_key,
                                                                bool auto_create) {
  if (!(channel_ptr && channel_ptr->get_channel_id() == channel_key.channel_id())) {
    channel_ptr = get_channel(channel_key.channel_id());
  }
  forward_server_id = 0;

  // 已有数据
  if (channel_ptr) {
    auto result = RPC_AWAIT_CODE_RESULT(channel_ptr->await_transfer(ctx, forward_server_id));
    if (result < 0) {
      FCTXLOGERROR(ctx, "channel {} await transfer failed. result: {}({})", channel_key.channel_id(), result,
                   protobuf_mini_dumper_get_error_msg(result));
      RPC_RETURN_CODE(result);
    }

    if (channel_ptr->is_readonly() || channel_ptr->is_writable()) {
      FCTXLOGDEBUG(ctx, "channel {} select existed readonly/writable channel", channel_key.channel_id());
      forward_server_id = 0;
      RPC_RETURN_CODE(0);
    }

    channel_ptr.reset();
    if (0 != forward_server_id) {
      FCTXLOGDEBUG(ctx, "channel {} should transfer readonly message to server {:#x}", channel_key.channel_id(),
                   forward_server_id);
      RPC_RETURN_CODE(0);
    }
  }

  // 如果本地无缓存或不是writable，且本节点可以提升writable，则走提升为writable流程
  if (mq_channel::should_be_writable_or_get_server_id(channel_key, forward_server_id)) {
    RPC_RETURN_CODE(
        RPC_AWAIT_CODE_RESULT(make_writable_channel(ctx, channel_ptr, forward_server_id, channel_key, auto_create)));
  }

  uint64_t local_server_id = logic_config::me()->get_local_server_id();
  auto channel_cfg = excel::get_ExcelDtmqChannelType_by_channel_type(channel_key.channel_type());
  if (!channel_cfg || channel_cfg->readonly_replicate_count() <= 0) {
    // 判定无只读副本，可写副本为本机，但本机又不能成为可写副本。说明本机正在被关闭
    if (forward_server_id == local_server_id || forward_server_id == 0) {
      FCTXLOGWARNING(ctx, "channel {} server {:#x} is under maintenance, and no more available node now.",
                     channel_key.channel_id(), forward_server_id);
      forward_server_id = 0;
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAINTENANCE);
    }

    FCTXLOGDEBUG(ctx, "channel {} should transfer readonly message to server {:#x}", channel_key.channel_id(),
                 forward_server_id);
    RPC_RETURN_CODE(0);
  }

  uint64_t readonly_replicate_index = 0;
  if (!mq_channel::should_be_readonly_or_random_server_id(channel_key, readonly_replicate_index, forward_server_id)) {
    // 判定只读副本为本机，但本机又不能成为只读副本。说明本机正在被关闭
    if (forward_server_id == local_server_id || forward_server_id == 0) {
      FCTXLOGWARNING(ctx, "channel {} server {:#x} is under maintenance, and no more available node now.",
                     channel_key.channel_id(), forward_server_id);
      forward_server_id = 0;
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAINTENANCE);
    }

    FCTXLOGDEBUG(ctx, "channel {} should transfer readonly message to server {:#x}", channel_key.channel_id(),
                 forward_server_id);
    RPC_RETURN_CODE(0);
  }

  // 创建频道
  auto channel_configure = excel::get_dtmq_channel_configure(channel_key.channel_type());
  if (!channel_configure) {
    FCTXLOGERROR(ctx, "channel {} configure of type {} not found.", channel_key.channel_id(),
                 channel_key.channel_type());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  auto result = RPC_AWAIT_CODE_RESULT(create_channel(ctx, channel_ptr, channel_key, *channel_configure));
  if (result < 0) {
    FCTXLOGERROR(ctx, "channel {} create failed with result {}({}).", channel_key.channel_id(), result,
                 protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (!channel_ptr) {
    FCTXLOGERROR(ctx, "channel {} create failed with unknown reason.", channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  // 已拉取完数据，直接返回
  if (channel_ptr->is_readonly() || channel_ptr->is_writable()) {
    FCTXLOGDEBUG(ctx, "channel {} is created and inited successfully", channel_key.channel_id());
    RPC_RETURN_CODE(0);
  }

  // 尝试提升为可读
  result = RPC_AWAIT_CODE_RESULT(channel_ptr->readonly_init(ctx, readonly_replicate_index));
  if (result < 0) {
    FCTXLOGERROR(ctx, "channel {} readonly init failed with result {}({}).", channel_key.channel_id(), result,
                 protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }

  // 只读频道数据短暂不一致也没关系，顶多稍微落后一点，订阅后续也会自动恢复
  if (!channel_ptr->is_writable() && !channel_ptr->is_readonly()) {
    FCTXLOGERROR(ctx, "channel {} readonly init failed with unknown reason. maybe concurrency conflict",
                 channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  FCTXLOGDEBUG(ctx, "channel {} is created and readonly inited successfully", channel_key.channel_id());
  RPC_RETURN_CODE(0);
}

rpc::result_code_type mq_channel_manager::make_readable_channel_with_replicate_index(
    rpc::context& ctx, mq_channel_ptr_type& channel_ptr, uint64_t& forward_server_id, uint64_t replicate_index,
    const atfw::dtmq::DChannelIdKey& channel_key, bool auto_create) {
  if (0 == replicate_index) {
    RPC_RETURN_CODE(
        RPC_AWAIT_CODE_RESULT(make_writable_channel(ctx, channel_ptr, forward_server_id, channel_key, auto_create)));
  }

  if (!(channel_ptr && channel_ptr->get_channel_id() == channel_key.channel_id())) {
    channel_ptr = get_channel(channel_key.channel_id());
  }
  forward_server_id = 0;

  const uint64_t local_server_id = logic_config::me()->get_local_server_id();
  // 已有数据
  do {
    if (!channel_ptr) {
      break;
    }

    auto result = RPC_AWAIT_CODE_RESULT(channel_ptr->await_transfer(ctx, forward_server_id));
    if (result < 0) {
      FCTXLOGERROR(ctx, "channel {} await transfer failed. result: {}({})", channel_key.channel_id(), result,
                   protobuf_mini_dumper_get_error_msg(result));
      RPC_RETURN_CODE(result);
    }

    // 如果writable节点和replicate_index指向的节点一致，则channel会是writable，判定replicate_index匹配即可
    // 这里的作用是获取replicate_index对应的可以用于只读数据的节点，writable节点也可以作为只读节点的
    forward_server_id = channel_ptr->get_target_distribution_server_id(replicate_index);
    if ((channel_ptr->is_readonly() || channel_ptr->is_writable()) && local_server_id == forward_server_id) {
      FCTXLOGDEBUG(ctx, "channel {} select existed readonly channel with replicate_index {}", channel_key.channel_id(),
                   replicate_index);
      forward_server_id = 0;
      RPC_RETURN_CODE(0);
    }

    channel_ptr.reset();
    if (forward_server_id != local_server_id && forward_server_id != 0) {
      FCTXLOGDEBUG(ctx, "channel {} should transfer readonly message to server {:#x}", channel_key.channel_id(),
                   forward_server_id);
      RPC_RETURN_CODE(0);
    }
  } while (false);

  if (!mq_channel::should_be_readonly_or_get_server_id(channel_key, forward_server_id, replicate_index)) {
    // 判定只读副本为本机，但本机又不能成为只读副本。说明本机正在被关闭
    if (forward_server_id == local_server_id || forward_server_id == 0) {
      forward_server_id = 0;
      FCTXLOGWARNING(ctx, "channel {} server {:#x} is under maintenance, and no more available node now.",
                     channel_key.channel_id(), forward_server_id);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAINTENANCE);
    }

    FCTXLOGDEBUG(ctx, "channel {} should transfer readonly message to server {:#x}", channel_key.channel_id(),
                 forward_server_id);
    RPC_RETURN_CODE(0);
  }

  // 创建频道
  auto channel_configure = excel::get_dtmq_channel_configure(channel_key.channel_type());
  if (!channel_configure) {
    FCTXLOGERROR(ctx, "channel {} configure of type {} not found.", channel_key.channel_id(),
                 channel_key.channel_type());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  auto result = RPC_AWAIT_CODE_RESULT(create_channel(ctx, channel_ptr, channel_key, *channel_configure));
  if (result < 0) {
    FCTXLOGERROR(ctx, "channel {} create failed with result {}({}).", channel_key.channel_id(), result,
                 protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }
  if (!channel_ptr) {
    FCTXLOGERROR(ctx, "channel {} create failed with unknown reason.", channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  // 已拉取完数据，直接返回
  if (channel_ptr->is_readonly() || channel_ptr->is_writable()) {
    FCTXLOGDEBUG(ctx, "channel {} is created and inited successfully", channel_key.channel_id());
    RPC_RETURN_CODE(0);
  }

  // 尝试提升为可读
  result = RPC_AWAIT_CODE_RESULT(channel_ptr->readonly_init(ctx, replicate_index));
  if (result < 0) {
    FCTXLOGERROR(ctx, "channel {} readonly init failed with result {}({}).", channel_key.channel_id(), result,
                 protobuf_mini_dumper_get_error_msg(result));
    RPC_RETURN_CODE(result);
  }

  // 只读频道数据短暂不一致也没关系，顶多稍微落后一点，订阅后续也会自动恢复
  if (!channel_ptr->is_writable() && !channel_ptr->is_readonly()) {
    FCTXLOGERROR(ctx, "channel {} readonly init failed with unknown reason. maybe concurrency conflict",
                 channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  FCTXLOGDEBUG(ctx, "channel {} is created and readonly inited successfully", channel_key.channel_id());
  RPC_RETURN_CODE(0);
}

mq_channel_manager::mq_channel_ptr_type mq_channel_manager::get_channel(const std::string& channel_id) const noexcept {
  auto iter = channels_.find(channel_id);
  if (iter == channels_.end()) {
    return nullptr;
  }

  return iter->second;
}

void mq_channel_manager::set_more_transfer() noexcept { more_transfer_now_ = true; }

rpc::result_code_type mq_channel_manager::find_message(rpc::context& /*ctx*/, const mq_channel_ptr_type& channel,
                                                       int64_t sequence, atfw::dtmq::DChannelMessage& msg) {
  if (!channel) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  auto log_ptr = channel->get_shared_wal_object()->find_log(sequence);
  if (!log_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_MESSAGE_NOT_FOUND);
  }

  protobuf_copy_message(msg, *log_ptr);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
}

rpc::result_code_type mq_channel_manager::page_query_message(
    rpc::context& /*ctx*/, const mq_channel_ptr_type& channel, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs) {
  if (!channel) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
  }

  const auto& dtmq_proxysvr_cfg =
      logic_config::me()->get_server_instance_config<atfw::dtmq::config::dtmq_proxysvr_cfg>();

  if (page_info.page_size() <= 0) {
    page_info.set_page_size(kPageQueryDefaultSize);
  }

  if (page_info.page_size() > dtmq_proxysvr_cfg.page_query_max_size()) {
    page_info.set_page_size(dtmq_proxysvr_cfg.page_query_max_size());
  }

  auto& log_mgr = channel->get_wal_publisher().get_log_manager();
  auto iter = log_mgr.log_lower_bound(page_info.page_start_sequence());
  while (iter != log_mgr.log_end() && msgs.size() < page_info.page_size()) {
    if (*iter) {
      protobuf_copy_message(*msgs.Add(), **iter);
    }
    ++iter;
  }

  // 跳过末尾的空洞(已被移除的日志)，避免在无更多真实消息时误报 page_more
  while (iter != log_mgr.log_end() && !*iter) {
    ++iter;
  }

  page_info.set_page_more(iter != log_mgr.log_end());

  RPC_RETURN_CODE(0);
}

void mq_channel_manager::insert_running_io_channel(const mq_channel* channel) {
  if (nullptr == channel || mq_channel_manager::is_instance_destroyed()) {
    return;
  }

  mq_channel_manager::me()->running_io_channels_.insert(channel);
}

void mq_channel_manager::remove_running_io_channel(const mq_channel* channel) noexcept {
  if (nullptr == channel || mq_channel_manager::is_instance_destroyed()) {
    return;
  }

  auto inst = mq_channel_manager::me();
  inst->running_io_channels_.erase(channel);

  auto reactive_iter = inst->reactive_io_channels_.find(channel);
  if (reactive_iter != inst->reactive_io_channels_.end()) {
    auto channel_ptr = inst->get_channel(channel->get_channel_id());

    // 重新激活IO
    if (channel_ptr && channel_ptr.get() == channel) {
      // 仅仅保存操作或正在进程退出需要立刻执行，其他的延后执行也没关系。
      // 失败太多次则强制中断，防止雪崩。
      if (channel_ptr->is_writable() && (channel_ptr->need_save_db() || inst->is_stoping_) &&
          !channel_ptr->is_io_task_too_many_continue_failed()) {
        inst->pending_io_channels_.push_back(channel_ptr);
      }
    }

    inst->reactive_io_channels_.erase(reactive_iter);
  }

  if (inst->configure_concurrency_io_task_count_ > 0 && !inst->pending_io_channels_.empty() &&
      inst->running_io_channels_.size() < inst->configure_concurrency_io_task_count_) {
    inst->set_more_transfer();
  }
}

bool mq_channel_manager::is_running_io_busy() const noexcept {
  return configure_concurrency_io_task_count_ > 0 &&
         running_io_channels_.size() >= configure_concurrency_io_task_count_;
}

void mq_channel_manager::add_pending_io_channel(const mq_channel_ptr_type& channel) {
  if (!channel || mq_channel_manager::is_instance_destroyed()) {
    return;
  }

  pending_io_channels_.push_back(channel);

  // libstdc++ 在非 C++ 11 ABI下，std::list的size()复杂度为O(n)，在 add_pending_io_channel 中调用size()会导致性能问题。
  // 这种情况下避免每次都调用size()，而转到tick时执行压缩。
#if ATFW_DTMQ_STD_LIBSTDCXX_LEGACY_LIST_ABI == 0
  // 迭代过程中不能做清理操作
  if (!iterating_pending_io_channels_ && pending_io_channels_.size() >= channels_.size() * 4) {
    compact_pending_io_channels();
  }
#endif
}

void mq_channel_manager::compact_pending_io_channels() {
  // 压缩pending_io_channels_中的重复记录
  std::unordered_set<mq_channel*> pending_io_channel_cache;
  pending_io_channel_cache.reserve(channels_.size());
  std::list<mq_channel_ptr_type> new_pending_io_channels;
  for (const auto& channel : pending_io_channels_) {
    if (pending_io_channel_cache.insert(channel.get()).second) {
      new_pending_io_channels.push_back(channel);
    }
  }
  pending_io_channels_.swap(new_pending_io_channels);
}

void mq_channel_manager::resolve_channel_io() {
  // 检查所有的channel，转移数据
  if (!is_stoping_ && pending_transfer_.start_time != std::chrono::system_clock::from_time_t(0) &&
      util::time::time_utility::sys_now() >= pending_transfer_.start_time) {
    pending_transfer_.start_time = std::chrono::system_clock::from_time_t(0);
    pending_transfer_.resolved_etcd_revision = dtmq_server_distribute_etcd_revision_;

    std::unordered_set<mq_channel*> pending_io_channel_cache;
    for (const auto& channel : pending_io_channels_) {
      pending_io_channel_cache.insert(channel.get());
    }

    for (auto& channel : channels_) {
      if (0 == channel.second->get_transfer_target_server_id()) {
        continue;
      }

      if (pending_io_channel_cache.end() != pending_io_channel_cache.find(channel.second.get())) {
        continue;
      }

      pending_io_channels_.push_back(channel.second);
    }
  }

  if (pending_io_channels_.empty()) {
    return;
  }

  auto iter = pending_io_channels_.begin();

  rpc::context ctx{rpc::context::create_without_task()};

  // IO频率控制，不需要很精确，只要不要太密集即可
  iterating_pending_io_channels_ = true;
  uint64_t local_server_id = logic_config::me()->get_local_server_id();
  while (iter != pending_io_channels_.end() && (configure_concurrency_io_task_count_ <= 0 ||
                                                running_io_channels_.size() < configure_concurrency_io_task_count_)) {
    if ((*iter)->is_io_task_running()) {
      if (running_io_channels_.count((*iter).get()) > 0) {
        reactive_io_channels_.insert((*iter).get());
        iter = pending_io_channels_.erase(iter);
      } else {
        ++iter;
      }
      continue;
    }

    // 正在退出时，优先保存数据到DB，避免数据丢失。其他情况仅仅定时保存，这里transfer即可。
    bool need_retry = false;
    if (is_stoping_ && (*iter)->need_save_db() && !(*iter)->get_configure().memory_only()) {
      FCTXLOGDEBUG(ctx, "channel({}) async_save when is_stoping_", (*iter)->get_channel_id());
      need_retry = (*iter)->async_save(ctx) < 0;
    } else {
      FCTXLOGDEBUG(ctx, "channel({}) async_start_transfer, is_stoping: {}", (*iter)->get_channel_id(), is_stoping_);
      // 只读频道也需要转移数据，不过失败也没关系。下次拉取会从writable节点恢复数据，
      // 最多订阅者通知要等下次心跳恢复。即故障时不影响数据正确，只是会造成延迟。
      bool transfer_failed = (*iter)->async_start_transfer(ctx, (*iter)->get_transfer_target_server_id()) < 0;
      // 停服时要尽快重试，其他情况下次定时器触发重试即可
      if (transfer_failed && is_stoping_) {
        need_retry = true;
      }
    }

    if ((*iter)->is_io_task_running()) {
      uint64_t target_server_id = (*iter)->get_transfer_target_server_id();
      bool need_transfer = target_server_id != 0 && target_server_id != local_server_id;
      // 停服时和需要转移时可以尽快再检查一次，这样失败了能够重试。
      // 失败太多次则强制中断，防止雪崩。
      if ((is_stoping_ || need_transfer) && !(*iter)->is_io_task_too_many_continue_failed()) {
        reactive_io_channels_.insert((*iter).get());
      }
      iter = pending_io_channels_.erase(iter);
      continue;
    }

    if (need_retry) {
      ++iter;
    } else {
      iter = pending_io_channels_.erase(iter);
    }
  }
  iterating_pending_io_channels_ = false;

  // more_transfer_now_ 表示要立即处理剩下的pending中的频道
  // 这里设置成false，下一帧会继续处理
  more_transfer_now_ = false;
}

void mq_channel_manager::report_channel_qty_oss() {
  // FIXME: 这里需要发送OSS日志，暂时注释掉
  // telemetry_oss_user_information user;
  // user.zone_id = logic_config::me()->get_local_zone_id();
  // rpc::context ctx{rpc::context::create_without_task()};
  // rpc::context::message_holder<PROJECT_NAMESPACE_ID::oss::DtmqChannelQty> oss_log{ctx};
  // oss_log->set_total_qty(static_cast<int32_t>(channels_.size()));
  // oss_log->set_penddind_io_qty(static_cast<int32_t>(pending_io_channels_.size()));
  // oss_log->set_running_io_qty(static_cast<int32_t>(running_io_channels_.size()));
  // telemetry::oss::send_dtmq_channel_qty(ctx, user, std::move(*oss_log));
}

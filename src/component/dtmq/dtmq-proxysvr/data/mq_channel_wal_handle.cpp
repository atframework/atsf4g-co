// Copyright 2026 atframework
// @brief Created by owent

#include "data/mq_channel_wal_handle.h"

#include <log/log_wrapper.h>
#include <string/string_format.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/timestamp.pb.h>
#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/dtmq_proxy.config.pb.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include <rpc/dtmq/dtmq_algorithm.h>
#include <rpc/dtmq/dtmqproxysvrnotifyservice.atfw.gen.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>

#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <chrono>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/mq_channel.h"
#include "distributed_system/wal_common_defs.h"

namespace {

using wal_result_code = atfw::util::distributed_system::wal_result_code;

struct mq_wal_delegate_helper {
  using wal_object_type = mq_channel_wal_object_type;
  using wal_publisher_type = mq_channel_wal_publisher_type;
  using log_const_iterator = wal_object_type::log_const_iterator;
  using log_iterator = wal_object_type::log_iterator;
  using log_key_type = wal_object_type::log_key_type;
  using log_type = wal_object_type::log_type;

  static wal_result_code do_nothing(wal_object_type&, const wal_object_type::log_type&,
                                    wal_object_type::callback_param_type) {
    return wal_result_code::kOk;
  }

  static wal_result_code destroy_channel(wal_object_type& wal, const wal_object_type::log_type& log,
                                         wal_object_type::callback_param_type param) {
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel) {
      return wal_result_code::kOk;
    }

    auto tp = protobuf_to_system_clock(log.detail().destroy().removed_timepoint());
    channel->merge_destroy_timepoint_and_sequence(param.context, tp, log.sequence());

    // 频道销毁后也要清空乐观锁
    if (channel->is_destroyed()) {
      channel->clear_lock();
    }
    return wal_result_code::kOk;
  }

  static wal_result_code create_channel(wal_object_type& wal, const wal_object_type::log_type& log,
                                        wal_object_type::callback_param_type param) {
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel) {
      return wal_result_code::kOk;
    }

    auto tp = protobuf_to_system_clock(log.detail().create().create_timepoint());
    channel->merge_created_timepoint_and_sequence(param.context, tp, log.sequence());
    return wal_result_code::kOk;
  }

  static wal_result_code reset_lock(wal_object_type& wal, const wal_object_type::log_type& log,
                                    wal_object_type::callback_param_type param) {
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel) {
      return wal_result_code::kOk;
    }

    // Writable频道由 set_lock 接口处理,仅副本通过log同步状态
    if (channel->is_writable()) {
      return wal_result_code::kOk;
    }

    channel->set_lock(param.context, log.detail().reset_lock(), false);
    return wal_result_code::kOk;
  }

  static void setup_delegate_actions(mq_channel_wal_object_type::callback_log_group_map_t& actions) {
    // 走default_delegate即可
    actions[atfw::dtmq::DChannelMessageDetail::kDestroy].action = mq_wal_delegate_helper::destroy_channel;
    actions[atfw::dtmq::DChannelMessageDetail::kCreate].action = mq_wal_delegate_helper::create_channel;
    actions[atfw::dtmq::DChannelMessageDetail::kResetLock].action = mq_wal_delegate_helper::reset_lock;
  }
};

static mq_channel_wal_object_type::vtable_pointer create_mq_channel_shared_object_vtable() {
  using wal_object_type = mq_channel_wal_object_type;

  static wal_object_type::vtable_pointer ret;
  if (ret) {
    return ret;
  }

  ret = atfw::component::memory::stl::make_strong_rc<wal_object_type::vtable_type>();
  if (!ret) {
    return ret;
  }

  rpc::dtmq::setup_common_vtable<wal_object_type>(*ret);

  // callbacks for wal_object
  ret->load = [](wal_object_type& wal, const wal_object_type::storage_type& from,
                 wal_object_type::callback_param_type param) -> wal_result_code {
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel) {
      return wal_result_code::kInitlization;
    }

    int64_t last_removed_key = 0;
    if (nullptr != wal.get_last_removed_key()) {
      last_removed_key = *wal.get_last_removed_key();
    }

    // Load logs
    std::vector<wal_object_type::log_pointer> storage;
    storage.reserve(static_cast<size_t>(from.messages_size()));
    for (const auto& msg : from.messages()) {
      if (wal.get_log_key_compare()(msg.sequence(), channel->get_compact_stateful_sequence()) ||
          wal.get_log_key_compare()(msg.sequence(), last_removed_key)) {
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

    // All load operation flush broadcast bound
    if (!storage.empty() && wal.get_private_data().channel) {
      wal.get_private_data().channel->get_wal_publisher().set_broadcast_key_bound((*storage.rbegin())->sequence());
    }

    wal.get_private_data().channel->load(param.context, from.channel_metadata(), from.channel_runtime());
    return wal_result_code::kOk;
  };

  ret->dump = [](const wal_object_type& wal, wal_object_type::storage_type& to,
                 wal_object_type::callback_param_type /*param*/) -> wal_result_code {
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel) {
      return wal_result_code::kOk;
    }

    channel->dump(to, true, true, false);
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
    // client端更新就好，下发交给server端做
    protobuf_copy_message(to, from);
  };

  ret->allocate_log_key = [](wal_object_type& wal, const wal_object_type::log_type& log,
                             wal_object_type::callback_param_type) -> wal_object_type::log_key_result_type {
    if (log.sequence() > 0) {
      return wal_object_type::log_key_result_type::make_success(log.sequence());
    }
    if (nullptr != wal.get_private_data().channel) {
      return wal_object_type::log_key_result_type::make_success(
          wal.get_private_data().channel->alloc_message_sequence());
    }

    return wal_object_type::log_key_result_type::make_error(wal_result_code::kInitlization);
  };

  ret->on_log_added = [](wal_object_type& wal, const wal_object_type::log_pointer& log) {
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel || !log) {
      return;
    }

    // 刷新最后收到的sequence
    if (log->sequence() > channel->get_sequence_allocator()) {
      channel->set_sequence_allocator(log->sequence());
    }

    channel->set_dirty();

    FWLOGDEBUG("mq channel {} add log, sequence: {}, command_case: {}, timepoint: {}",
               channel->get_channel_key().channel_id(), log->sequence(),
               static_cast<int32_t>(log->detail().command_case()), log->create_timepoint().seconds());
    return;
  };

  ret->on_log_removed = [](wal_object_type& wal, const wal_object_type::log_pointer& log) {
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel || !log) {
      return;
    }

    FWLOGDEBUG("mq channel {} remove log, sequence: {}, command_case: {}, timepoint: {}",
               channel->get_channel_key().channel_id(), log->sequence(),
               static_cast<int32_t>(log->detail().command_case()), log->create_timepoint().seconds());

    channel->set_dirty();
  };

  mq_wal_delegate_helper::setup_delegate_actions(ret->log_action_delegate);

  // Allow default delegate to allow sync package
  ret->default_delegate.action = &mq_wal_delegate_helper::do_nothing;

  return ret;
}

static mq_channel_wal_object_type::configure_pointer create_mq_channel_shared_object_configure(
    const atfw::dtmq::DChannelConfigure& configure) {
  mq_channel_wal_object_type::configure_pointer ret =
      util::memory::make_strong_rc<mq_channel_wal_object_type::configure_type>();
  if (!ret) {
    return ret;
  }
  mq_channel_wal_object_type::default_configure(*ret);

  // 以下不同类型的聊天频道配置不一样
  ret->gc_expire_duration =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.gc_expire_duration());
  ret->gc_log_size = configure.gc_log_count();
  ret->max_log_size = configure.max_log_count();

  return ret;
}

static mq_channel_wal_client_type::vtable_pointer create_mq_channel_client_vtable() {
  // using wal_object_type = mq_channel_wal_client_type::object_type;
  using wal_client_type = mq_channel_wal_client_type;
  using snapshot_type = mq_channel_storage_type;

  static wal_client_type::vtable_pointer ret;
  if (ret) {
    return ret;
  }

  ret = atfw::component::memory::stl::make_strong_rc<wal_client_type::vtable_type>();
  if (!ret) {
    return ret;
  }

  // ============ callbacks for wal_client ============
  ret->on_receive_snapshot = [](wal_client_type& wal, const snapshot_type& snapshot_data,
                                wal_client_type::callback_param_type param) -> wal_result_code {
    return wal.load(snapshot_data, param);
  };

  ret->on_receive_subscribe_response = [](wal_client_type&, wal_client_type::callback_param_type) -> wal_result_code {
    // 接收到订阅回包的处理留空即可
    return wal_result_code::kOk;
  };

  ret->subscribe_request = [](wal_client_type& wal, wal_client_type::callback_param_type param) -> wal_result_code {
    // 标记需要发送心跳，在最后发数据时合并channel发送
    mq_channel* channel = wal.get_private_data().channel;
    if (nullptr == channel) {
      return wal_result_code::kOk;
    }

    // 从节点向主节点发送订阅
    channel->async_send_subscribe_to_writable(param.context);
    return wal_result_code::kOk;
  };

  return ret;
}

static wal_result_code publisher_send_snapshot(
    mq_channel_wal_publisher_type& publisher, mq_channel_wal_publisher_type::subscriber_iterator begin,
    mq_channel_wal_publisher_type::subscriber_iterator end,  // NOLINT(performance-unnecessary-value-param)
    mq_channel_wal_publisher_type::callback_param_type param) {
  mq_channel* channel = publisher.get_private_data().channel;
  if (nullptr == channel) {
    return wal_result_code::kOk;
  }

  if (channel->is_loading_snapshot()) {
    return wal_result_code::kOk;
  }

  if (begin == end) {
    return wal_result_code::kOk;
  }

  auto notify_msg = rpc::make_shared_message<atfw::dtmq::SSChannelEventSync>(param.context);
  protobuf_copy_message(*notify_msg->mutable_channel_metadata()->mutable_channel_key(), channel->get_channel_key());
  auto* snapshot_data = notify_msg->mutable_channel_snapshot();
  if (nullptr == snapshot_data) {
    FCTXLOGERROR(param.context.get(), "malloc {} failed", "channel_snapshot");
    return wal_result_code::kCallbackError;
  }
  channel->dump(*snapshot_data, true, true, false);

  // Collect subscribers by server id
  std::unordered_map<uint64_t, std::list<mq_channel_wal_publisher_type::subscriber_pointer>> svrs_without_private_data;
  std::unordered_map<uint64_t, std::list<mq_channel_wal_publisher_type::subscriber_pointer>> svrs_with_private_data;
  for (; begin != end; ++begin) {
    if (!begin->second) {
      continue;
    }

    if (begin->second->is_offline(util::time::time_utility::now())) {
      continue;
    }

    uint64_t svr_id = begin->second->get_private_data().subscriber_server_id();
    if (0 != svr_id) {
      if (begin->second->get_private_data().with_private_data()) {
        svrs_with_private_data[svr_id].push_back(begin->second);
      } else {
        svrs_without_private_data[svr_id].push_back(begin->second);
      }
    }
  }

  if (svrs_without_private_data.empty() && svrs_with_private_data.empty()) {
    FCTXLOGDEBUG(param.context.get(), "channel_snapshot, svrs is empty. channel:({})",
                 protobuf_mini_dumper_get_readable(channel->get_channel_key()));
    return wal_result_code::kOk;
  }

  auto send_fn =
      [&](const std::unordered_map<uint64_t, std::list<mq_channel_wal_publisher_type::subscriber_pointer>>& svrs) {
        for (const auto& target : svrs) {
          notify_msg->clear_subscriber_keys();
          notify_msg->mutable_subscriber_keys()->Reserve(static_cast<int>(target.second.size()));
          for (const auto& key : target.second) {
            notify_msg->add_subscriber_keys(key->get_private_data().subscriber_key());
          }

          int32_t res = rpc::dtmq::channel_event_sync(param.context, target.first, *notify_msg).unwrap();
          if (0 != res) {
            if (PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND == res ||
                PROJECT_NAMESPACE_ID::err::EN_ATBUS_ERR_ATNODE_NOT_FOUND == res) {
              // 服务器节点离线，可能是短暂不可用
              FCTXLOGWARNING(param.context.get(),
                             "mq channel {} send broadcast_event_logs to server {:#x} failed, result: {}({})",
                             channel->get_channel_id(), target.first, res, protobuf_mini_dumper_get_error_msg(res));
            } else {
              FCTXLOGERROR(param.context.get(),
                           "mq channel {} send broadcast_event_logs to server {:#x} failed, result: {}({})",
                           channel->get_channel_id(), target.first, res, protobuf_mini_dumper_get_error_msg(res));
              param.result_code.get() = res;
            }
          }
        }
      };

  // Send without private data
  send_fn(svrs_without_private_data);

  if (svrs_with_private_data.empty()) {
    return wal_result_code::kOk;
  }
  channel->dump_private_data(*snapshot_data->mutable_channel_runtime());

  // Send with private data
  send_fn(svrs_with_private_data);

  return wal_result_code::kOk;
}

static wal_result_code publisher_send_logs(
    mq_channel_wal_publisher_type& publisher, mq_channel_wal_publisher_type::log_const_iterator log_begin,
    mq_channel_wal_publisher_type::log_const_iterator log_end,  // NOLINT(performance-unnecessary-value-param)
    mq_channel_wal_publisher_type::subscriber_iterator subscriber_begin,
    mq_channel_wal_publisher_type::subscriber_iterator subscriber_end,  // NOLINT(performance-unnecessary-value-param)
    mq_channel_wal_publisher_type::callback_param_type param) {
  mq_channel* channel = publisher.get_private_data().channel;
  if (nullptr == channel) {
    return wal_result_code::kOk;
  }

  if (channel->is_loading_snapshot()) {
    return wal_result_code::kOk;
  }

  if (log_begin == log_end || subscriber_begin == subscriber_end) {
    return wal_result_code::kOk;
  }

  int64_t first_log_sequence = 0;
  for (auto iter = log_begin; 0 == first_log_sequence && iter != log_end; ++iter) {
    if (!(*iter)) {
      continue;
    }

    first_log_sequence = (*iter)->sequence();
  }

  auto notify_msg = rpc::make_shared_message<atfw::dtmq::SSChannelEventSync>(param.context);
  channel->dump(*notify_msg->mutable_channel_metadata(), false,
                channel->get_custom_data_sequence() >= first_log_sequence);
  bool has_private_data = channel->get_private_data_sequence() >= first_log_sequence;
  channel->dump(*notify_msg->mutable_channel_runtime(), false);

  // Pack log sync message
  for (; log_begin != log_end; ++log_begin) {
    if (!(*log_begin)) {
      continue;
    }

    protobuf_copy_message(*notify_msg->add_channel_message(), **log_begin);
  }

  if (0 == notify_msg->channel_message_size()) {
    return wal_result_code::kOk;
  }

  // Collect subscribers by server id
  std::unordered_map<uint64_t, std::list<mq_channel_wal_publisher_type::subscriber_pointer>> svrs_without_private_data;
  std::unordered_map<uint64_t, std::list<mq_channel_wal_publisher_type::subscriber_pointer>> svrs_with_private_data;
  for (; subscriber_begin != subscriber_end; ++subscriber_begin) {
    if (!subscriber_begin->second) {
      continue;
    }

    if (subscriber_begin->second->is_offline(util::time::time_utility::now())) {
      continue;
    }

    uint64_t svr_id = subscriber_begin->second->get_private_data().subscriber_server_id();
    if (0 != svr_id) {
      // 如果不需要下发private data，还是可以合并下发数据
      if (has_private_data && subscriber_begin->second->get_private_data().with_private_data()) {
        svrs_with_private_data[svr_id].push_back(subscriber_begin->second);
      } else {
        svrs_without_private_data[svr_id].push_back(subscriber_begin->second);
      }
    }
  }

  if (svrs_without_private_data.empty() && svrs_with_private_data.empty()) {
    FCTXLOGDEBUG(param.context.get(), "send_logs, svrs is empty. channel:({})",
                 protobuf_mini_dumper_get_readable(channel->get_channel_key()));
    return wal_result_code::kOk;
  }

  auto send_fn =
      [&](const std::unordered_map<uint64_t, std::list<mq_channel_wal_publisher_type::subscriber_pointer>>& svrs) {
        for (const auto& target : svrs) {
          notify_msg->clear_subscriber_keys();
          notify_msg->mutable_subscriber_keys()->Reserve(static_cast<int>(target.second.size()));
          for (const auto& key : target.second) {
            notify_msg->add_subscriber_keys(key->get_private_data().subscriber_key());
          }

          int32_t res = rpc::dtmq::channel_event_sync(param.context, target.first, *notify_msg).unwrap();
          if (0 != res) {
            if (PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND == res ||
                PROJECT_NAMESPACE_ID::err::EN_ATBUS_ERR_ATNODE_NOT_FOUND == res) {
              // 服务器节点离线，可能是短暂不可用
              FCTXLOGWARNING(param.context.get(), "mq channel {} send send_logs to server {:#x} failed, result: {}({})",
                             channel->get_channel_id(), target.first, res, protobuf_mini_dumper_get_error_msg(res));
            } else {
              FCTXLOGERROR(param.context.get(), "mq channel {} send send_logs to server {:#x} failed, result: {}({})",
                           channel->get_channel_id(), target.first, res, protobuf_mini_dumper_get_error_msg(res));
              param.result_code.get() = res;
            }
          }
        }
      };

  // Send without private data
  send_fn(svrs_without_private_data);

  if (svrs_with_private_data.empty()) {
    return wal_result_code::kOk;
  }
  channel->dump_private_data(*notify_msg->mutable_channel_runtime());

  // Send with private data
  send_fn(svrs_with_private_data);

  return wal_result_code::kOk;
}

static mq_channel_wal_publisher_type::vtable_pointer create_mq_channel_publisher_vtable() {
  // using wal_object_type = mq_channel_wal_publisher_type::object_type;
  using wal_publisher_type = mq_channel_wal_publisher_type;

  static wal_publisher_type::vtable_pointer ret;
  if (ret) {
    return ret;
  }

  ret = atfw::component::memory::stl::make_strong_rc<wal_publisher_type::vtable_type>();
  if (!ret) {
    return ret;
  }

  // ============ callbacks for wal_publisher ============
  ret->send_snapshot = publisher_send_snapshot;
  ret->send_logs = publisher_send_logs;

  ret->check_subscriber = [](wal_publisher_type&, const wal_publisher_type::subscriber_pointer& subscriber,
                             wal_publisher_type::callback_param_type) -> bool {
    if (!subscriber) {
      return false;
    }

    return 0 != subscriber->get_private_data().subscriber_server_id();
  };

  ret->on_subscriber_removed =
      [](wal_publisher_type& publisher, const wal_publisher_type::subscriber_pointer& subscriber,
         util::distributed_system::wal_unsubscribe_reason, wal_publisher_type::callback_param_type param) {
        mq_channel* channel = publisher.get_private_data().channel;
        if (nullptr == channel) {
          return;
        }
        auto all_subscribers = publisher.subscriber_all_range();
        if (all_subscribers.first == all_subscribers.second) {
          channel->update_lost_last_subscriber();
        }

        if (subscriber) {
          FCTXLOGDEBUG(param.context.get(), "mq channel {} remove subscriber {}", channel->get_channel_id(),
                       subscriber->get_key());
        }
      };

  ret->on_subscriber_added = [](wal_publisher_type& publisher, const wal_publisher_type::subscriber_pointer& subscriber,
                                wal_publisher_type::callback_param_type param) {
    mq_channel* channel = publisher.get_private_data().channel;
    if (nullptr == channel) {
      return;
    }

    channel->reset_lost_last_subscriber();

    if (subscriber) {
      FCTXLOGDEBUG(param.context.get(), "mq channel {} add subscriber {}", channel->get_channel_id(),
                   subscriber->get_key());
    }
  };

  return ret;
}

static mq_channel_wal_client_type::configure_pointer create_mq_channel_client_configure(
    const atfw::dtmq::DChannelConfigure& configure) {
  mq_channel_wal_client_type::configure_pointer ret = mq_channel_wal_client_type::make_configure();
  if (!ret) {
    return ret;
  }

  ret->subscriber_heartbeat_interval =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_interval());
  ret->subscriber_heartbeat_retry_interval =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_retry_interval());

  ret->require_snapshot = true;

  // 以下不同类型的聊天频道配置不一样
  ret->gc_expire_duration =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.gc_expire_duration());
  ret->gc_log_size = configure.gc_log_count();
  ret->max_log_size = configure.max_log_count();

  return ret;
}

static mq_channel_wal_publisher_type::configure_pointer create_mq_channel_publisher_configure(
    const atfw::dtmq::DChannelConfigure& configure) {
  mq_channel_wal_publisher_type::configure_pointer ret = mq_channel_wal_publisher_type::make_configure();
  if (!ret) {
    return ret;
  }
  // 无需通知移除订阅
  // ret->enable_last_broadcast_for_removed_subscriber = true;

  ret->gc_expire_duration =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.gc_expire_duration());
  ret->gc_log_size = configure.gc_log_count();
  ret->max_log_size = configure.max_log_count();

  ret->enable_hole_log = true;

  ret->subscriber_timeout = get_mq_channel_subscriber_timeout(configure);

  return ret;
}
}  // namespace

mq_channel_wal_object_context::mq_channel_wal_object_context(rpc::context& ctx, int32_t& output_result)
    : context(std::ref(ctx)), result_code(std::ref(output_result)) {}

std::string make_subscriber_key(const mq_channel_wal_subscriber_private_data& subscriber_data) {
  if (0 == subscriber_data.subscriber_server_id()) {
    return "";
  }

  if (!subscriber_data.subscriber_key().empty()) {
    return subscriber_data.subscriber_key();
  }

  return atfw::util::string::format("server:{}", subscriber_data.subscriber_server_id());
}

atfw::util::distributed_system::wal_duration get_mq_channel_subscriber_timeout(
    const atfw::dtmq::DChannelConfigure& configure) {
  if (configure.subscriber_timeout().seconds() > 0) {
    return protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.subscriber_timeout());
  }

  atfw::util::distributed_system::wal_duration subscriber_heartbeat_interval =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_interval());
  atfw::util::distributed_system::wal_duration subscriber_heartbeat_retry_interval =
      protobuf_to_chrono_duration<atfw::util::distributed_system::wal_duration>(configure.heartbeat_retry_interval());

  return subscriber_heartbeat_interval + subscriber_heartbeat_interval + subscriber_heartbeat_retry_interval;
}

atfw::util::memory::strong_rc_ptr<mq_channel_wal_object_type> create_mq_channel_object(
    mq_channel& mq_channel, const atfw::dtmq::DChannelConfigure& configure) {
  mq_wal_object_private_data_type private_data;
  private_data.channel = &mq_channel;
  private_data.hash_mismatch_data = rpc::dtmq::hash_mismatch_subscribe<int64_t>(mq_channel.get_channel_id(), -1,
                                                                                atfw::util::time::time_utility::now());
  return mq_channel_wal_object_type::create(create_mq_channel_shared_object_vtable(),
                                            create_mq_channel_shared_object_configure(configure),
                                            std::move(private_data));
}

atfw::util::memory::strong_rc_ptr<mq_channel_wal_publisher_type> create_mq_channel_publisher(
    mq_channel& channel, const atfw::dtmq::DChannelConfigure& configure) {
  mq_wal_object_private_data_type private_data;
  // private_data 不用初始化，会使用 channel.get_shared_wal_object() 里的版本
  return mq_channel_wal_publisher_type::create(channel.get_shared_wal_object(), create_mq_channel_publisher_vtable(),
                                               create_mq_channel_publisher_configure(configure),
                                               std::move(private_data));
}

atfw::util::memory::strong_rc_ptr<mq_channel_wal_client_type> create_mq_channel_client(
    mq_channel& channel, const atfw::dtmq::DChannelConfigure& configure) {
  mq_wal_object_private_data_type private_data;
  // private_data 不用初始化，会使用 channel.get_shared_wal_object() 里的版本
  return mq_channel_wal_client_type::create(atfw::util::time::time_utility::now(), channel.get_shared_wal_object(),
                                            create_mq_channel_client_vtable(),
                                            create_mq_channel_client_configure(configure), std::move(private_data));
}

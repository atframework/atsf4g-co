// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <design_pattern/singleton.h>
#include <memory/rc_ptr.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <dispatcher/task_type_traits.h>

#include <rpc/rpc_common_types.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "data/mq_channel.h"

namespace rpc {
class context;
}

class mq_channel_manager : public atfw::util::design_pattern::singleton<mq_channel_manager> {
 public:
  using mq_channel_ptr_type = atfw::util::memory::strong_rc_ptr<mq_channel>;

 protected:
  mq_channel_manager();
  ~mq_channel_manager();

 public:
  int init();
  int reload();
  int tick();

  void pre_stoping() noexcept;
  int stop();
  bool is_stoping() const noexcept;
  bool is_can_stopped() const noexcept;

  void update_timer(mq_channel& mq_channel, mq_channel_timer_type::timer_wptr_t& output_handle,
                    std::chrono::system_clock::duration timeout);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type create_channel(rpc::context& ctx, mq_channel_ptr_type& channel,
                                                                    const atfw::dtmq::DChannelIdKey& channel_key,
                                                                    const atfw::dtmq::DChannelConfigure& configure);

  void add_channel(rpc::context& ctx, const mq_channel_ptr_type& channel);

  void remove_channel(const std::string& channel_id, const mq_channel* except);

  /**
   * @brief 获取可写的channel，如果不存在或不可写则走提升写流程
   *
   * @param ctx RPC上下文
   * @param channel_ptr 输出有效的channel智能指针
   * @param forward_server_id 如果此消息应该被转发，则输出转发的server_id
   * @param channel_key mq_channel_key
   * @param auto_create 不存在是否允许自动创建
   * @return 0或RPC错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type make_writable_channel(rpc::context& ctx,
                                                                           mq_channel_ptr_type& channel_ptr,
                                                                           uint64_t& forward_server_id,
                                                                           const atfw::dtmq::DChannelIdKey& channel_key,
                                                                           bool auto_create);

  /**
   * @brief 获取可读的channel，如果不存在或不可写则走提升读流程
   * @note readonly 和 writable 都视为读
   *
   * @param ctx RPC上下文
   * @param channel_ptr 输出有效的channel智能指针
   * @param forward_server_id 如果此消息应该被转发，则输出转发的server_id
   * @param channel_key mq_channel_key
   * @param auto_create 不存在是否允许自动创建
   * @return 0或RPC错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type make_readable_channel(rpc::context& ctx,
                                                                           mq_channel_ptr_type& channel_ptr,
                                                                           uint64_t& forward_server_id,
                                                                           const atfw::dtmq::DChannelIdKey& channel_key,
                                                                           bool auto_create);

  /**
   * @brief 指定副本序号获取可读的channel，如果不存在或不可写则走提升读流程
   * @note readonly 和 writable 都视为读
   *
   * @param ctx RPC上下文
   * @param channel_ptr 输出有效的channel智能指针
   * @param forward_server_id 如果此消息应该被转发，则输出转发的server_id
   * @param replicate_index 指定的副本序号
   * @param channel_key mq_channel_key
   * @param auto_create 不存在是否允许自动创建
   * @return 0或RPC错误码
   */
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type make_readable_channel_with_replicate_index(
      rpc::context& ctx, mq_channel_ptr_type& channel_ptr, uint64_t& forward_server_id, uint64_t replicate_index,
      const atfw::dtmq::DChannelIdKey& channel_key, bool auto_create);

  mq_channel_ptr_type get_channel(const std::string& channel_id) const noexcept;

  void set_more_transfer() noexcept;

  inline int64_t get_latest_server_etcd_revision() const noexcept { return dtmq_server_distribute_etcd_revision_; }

  inline int64_t get_transfer_server_etcd_revision() const noexcept { return pending_transfer_.resolved_etcd_revision; }

  inline bool is_waiting_transfer() const noexcept {
    return pending_transfer_.start_time > std::chrono::system_clock::from_time_t(0);
  }

  ATFW_EXPLICIT_NODISCARD_ATTR static rpc::result_code_type find_message(rpc::context& ctx,
                                                                         const mq_channel_ptr_type& channel,
                                                                         int64_t sequence,
                                                                         atfw::dtmq::DChannelMessage& msg);
  ATFW_EXPLICIT_NODISCARD_ATTR static rpc::result_code_type page_query_message(
      rpc::context& ctx, const mq_channel_ptr_type& channel, atfw::dtmq::channel_page_info& page_info,
      google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs);

  static void insert_running_io_channel(const mq_channel* channel);

  static void remove_running_io_channel(const mq_channel* channel) noexcept;

  bool is_running_io_busy() const noexcept;

  void add_pending_io_channel(const mq_channel_ptr_type& channel);

 private:
  void compact_pending_io_channels();

  // 持久化dirty_channel
  void aysnc_save_dirty_channel();
  // 重新balance聊天频道分布
  void resolve_channel_io();

  void report_channel_qty_oss();

 private:
  struct pending_transfer_info {
    int64_t resolved_etcd_revision = 0;
    std::chrono::system_clock::time_point start_time = std::chrono::system_clock::from_time_t(0);
  };
  pending_transfer_info pending_transfer_;
  std::chrono::system_clock::time_point last_tick_timepoint_;
  int64_t dtmq_server_distribute_etcd_revision_;
  mq_channel_timer_type timers_;
  std::unordered_map<std::string, mq_channel_ptr_type> channels_;
  std::unordered_map<uint32_t, atfw::dtmq::DChannelConfigure> channel_configure_;
  std::list<mq_channel_ptr_type> pending_io_channels_;
  std::unordered_set<const mq_channel*> running_io_channels_;
  std::unordered_set<const mq_channel*> reactive_io_channels_;

  bool iterating_pending_io_channels_;
  bool more_transfer_now_;
  bool is_stoping_;
  bool is_pre_stoping_;

  std::chrono::system_clock::time_point report_channel_qty_time_;

  size_t configure_concurrency_io_task_count_;
};

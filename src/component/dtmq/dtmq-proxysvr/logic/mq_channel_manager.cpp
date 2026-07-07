// Copyright 2026 atframework
// @brief Created by owent

#include "logic/mq_channel_manager.h"

#include <string>

mq_channel_manager::mq_channel_manager()
    : resolve_channel_distribution_timepoint_(std::chrono::system_clock::from_time_t(0)),
      last_tick_timepoint_(std::chrono::system_clock::from_time_t(0)),
      dtmq_proxysvr_distribute_etcd_revision_(0),
      more_transfer_now_(false),
      is_stoping_(false),
      is_pre_stoping_(false),
      is_self_stateful_active_(false),
      report_mq_channel_qty_time_(0) {}

mq_channel_manager::~mq_channel_manager() {}

int mq_channel_manager::init() { return 0; }

int mq_channel_manager::reload() { return 0; }

int mq_channel_manager::tick() { return 0; }

void mq_channel_manager::pre_stoping() noexcept { is_pre_stoping_ = true; }

int mq_channel_manager::stop() { return 0; }

bool mq_channel_manager::is_stoping() const noexcept { return is_stoping_; }

bool mq_channel_manager::is_can_stopped() const noexcept {
  return is_stoping_ && pending_io_channels_.empty() && pending_save_channels_.empty();
}

bool mq_channel_manager::is_self_stateful_active() const noexcept { return is_self_stateful_active_; }

void mq_channel_manager::update_timer(mq_channel& /*mq_channel*/,
                                      mq_channel_timer_type::timer_wptr_t& /*output_handle*/,
                                      std::chrono::system_clock::duration /*timeout*/) {
  // TODO(owent):
}

void mq_channel_manager::add_channel(rpc::context& /*ctx*/, const mq_channel_ptr_type& /*mq_channel*/) {
  // TODO(owent):
}

void mq_channel_manager::remove_channel(const std::string& /*mq_channel_id*/, const mq_channel* /*except*/) {
  // TODO(owent):
}

void mq_channel_manager::set_more_transfer() noexcept { more_transfer_now_ = true; }

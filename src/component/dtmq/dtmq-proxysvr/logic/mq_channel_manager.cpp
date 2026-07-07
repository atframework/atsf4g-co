// Copyright 2026 atframework
// @brief Created by owent

#include "logic/mq_channel_manager.h"

#include <atframe/modules/service_discovery_module.h>

#include <config/extern_service_types.h>

#include <logic/logic_server_setup.h>

#include <memory>
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

int mq_channel_manager::init() {
  time_t now = util::time::time_utility::get_now();
  timers_.init(now);

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
  dtmq_proxysvr_distribute_etcd_revision_ = service_discovery_mod->get_last_etcd_event_discovery_header().revision;

  service_discovery_mod->add_on_node_discovery_event([](atfw::atapp::etcd_discovery_action_t /*action*/,
                                                        const atfw::atapp::etcd_discovery_node::ptr_t& discovery) {
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
    if (discovery_version > mq_channel_manager::me()->dtmq_proxysvr_distribute_etcd_revision_) {
      mq_channel_manager::me()->dtmq_proxysvr_distribute_etcd_revision_ = discovery_version;
      FWLOGINFO("dtmq_proxysvr distribute changed, dtmq_proxysvr_distribute_etcd_revision_  {}", discovery_version);
    }

    // 设置冷静窗口，应该要小于HPA模块的replicate_period配置
    if (mq_channel_manager::me()->resolve_channel_distribution_timepoint_ ==
        std::chrono::system_clock::from_time_t(0)) {
      mq_channel_manager::me()->resolve_channel_distribution_timepoint_ =
          util::time::time_utility::sys_now() + protobuf_to_chrono_duration<std::chrono::system_clock::duration>(
                                                    inner_dtmq_proxysvr_cfg.channel_transfer_stabilization_window());
    }

    mq_channel_manager::me()->tick();
  });

  return 0;
}

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

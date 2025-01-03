// Copyright 2024 atframework
// Created by owent

#include "logic/hpa/logic_hpa_puller.h"

#include <memory/object_allocator.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.protocol.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "logic/hpa/pull/prometheus/logic_hpa_puller_prometheus.h"

SERVER_FRAME_API logic_hpa_puller::logic_hpa_puller(logic_hpa_policy& owner) : owner_(&owner) {}

SERVER_FRAME_API logic_hpa_puller::~logic_hpa_puller() {}

SERVER_FRAME_API atfw::util::memory::strong_rc_ptr<logic_hpa_puller> logic_hpa_puller_factory::make_new_instance(
    logic_hpa_policy& policy, std::shared_ptr<rpc::telemetry::group_type> telemetry_group,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  auto metrics_configure = rpc::telemetry::global_service::get_metrics_configure(telemetry_group);
  if (metrics_configure.has_exporters() && metrics_configure.exporters().has_prometheus_http_api()) {
    return atfw::util::memory::static_pointer_cast<logic_hpa_puller>(
        atfw::memory::stl::make_strong_rc<logic_hpa_puller_prometheus>(policy, telemetry_group, hpa_cfg, policy_cfg));
  }

  return nullptr;
}

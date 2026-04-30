// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <atframe/etcdcli/etcd_discovery.h>
#include <atframe/modules/etcd_module.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/empty.pb.h>
#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/controller_service.pb.h>
#include <protocol/pbdesc/server_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_common_types.h>
#include "memory/rc_ptr.h"

#ifndef ORBIT_CONTROLLER_SERVICE_API
#  define ORBIT_CONTROLLER_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

namespace rpc {
class context;
}  // namespace rpc

class orbit_controller_manager : public util::design_pattern::singleton<orbit_controller_manager> {
 public:
  orbit_controller_manager();

  int init();
  void stop();
  void tick();

  // ---- 来自 Agent ----
  // Client 已启动（Agent 上报）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_notify_client_started(
      rpc::context& ctx, const orbit::ATCNotifyClientStartedReq& request);

  // Client 已退出（Agent 上报）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_notify_client_exit(
      rpc::context& ctx, const orbit::ATCNotifyClientExitReq& request);

  // Client 发送消息至 Server（Agent 转发）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_forward_to_server(
      rpc::context& ctx, const orbit::ATCForwardToServerReq& request);

  // ---- 来自 Server ----
  // Server 请求启动 Client
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_launch_client(
      rpc::context& ctx, const orbit::STCLaunchClientReq& request, orbit::CTSLaunchClientRsp& response);

  // Server 发送消息至 Client（下行转发）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_send_to_client(
      rpc::context& ctx, const orbit::STCSendToClientNotify& request);

  // Server 心跳
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_server_heartbeat(
      rpc::context& ctx, const orbit::STCServerHeartbeatNotify& request);

 private:
  orbit::DAgentIdentity select_agent_for_launch(double expected_cpu, double expected_memory_mb,
                                                const google::protobuf::RepeatedPtrField<std::string>& tags) noexcept;

  void on_server_node_event(atfw::atapp::etcd_module::node_action_t action_type,
                            const atfw::atapp::etcd_discovery_node::ptr_t& node);
  void on_agent_node_event(atfw::atapp::etcd_module::node_action_t action_type,
                           const atfw::atapp::etcd_discovery_node::ptr_t& node);

 private:
  bool stopped_ = false;
  std::string region_;
};

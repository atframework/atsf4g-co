// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <atframe/atapp.h>
#include <atframe/etcdcli/etcd_discovery.h>
#include <atframe/modules/service_discovery_module.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframe/atapp_conf.pb.h>
#include <google/protobuf/empty.pb.h>
#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/controller_service.pb.h>
#include <protocol/pbdesc/server_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/rc_ptr.h>
#include <rpc/rpc_common_types.h>

#include <cstdint>
#include <string>
#include <unordered_map>

#ifndef ORBIT_CONTROLLER_SERVICE_API
#  define ORBIT_CONTROLLER_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

namespace rpc {
class context;
}  // namespace rpc

struct orbit_controller_agent_info {
  orbit::DAgentEtcdLoadRecord load_record;
  double preallocated_cpu = 0.0;
  double preallocated_memory_mb = 0.0;
  uint32_t preallocated_client_count = 0;
};

class orbit_controller_manager : public util::design_pattern::singleton<orbit_controller_manager> {
 public:
  orbit_controller_manager();

  int init(atfw::atapp::app* app);
  int stop();
  void tick();

  // ---- 来自 Agent ----
  // Client 已启动（Agent 上报）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_notify_client_started(
      rpc::context& ctx, const orbit::ATCNotifyClientStartedReq& request, orbit::CTANotifyClientStartedRsp& response);

  // Client 已退出（Agent 上报）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_notify_client_exit(
      rpc::context& ctx, const orbit::ATCNotifyClientExitReq& request, orbit::CTANotifyClientExitRsp& response);

  // Agent 心跳（Agent 上报）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_agent_heartbeat(
      rpc::context& ctx, const orbit::ATCAgentHeartbeatReq& request);

  // Client 发送消息至 Server（Agent 转发）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_forward_to_server(
      rpc::context& ctx, const orbit::ATCForwardToServerReq& request, orbit::CTAForwardToServerRsp& response);

  // ---- 来自 Server ----
  // Server 请求启动 Client
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_launch_client(
      rpc::context& ctx, const orbit::STCLaunchClientReq& request, orbit::CTSLaunchClientRsp& response);

  // Server 发送消息至 Client（下行转发）
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_send_to_client(
      rpc::context& ctx, const orbit::STCSendToClientReq& request, orbit::CTSSendToClientRsp& response);

  // Server 心跳
  EXPLICIT_NODISCARD_ATTR ORBIT_CONTROLLER_SERVICE_API rpc::result_code_type handle_server_heartbeat(
      rpc::context& ctx, const orbit::STCServerHeartbeatNotify& request);

 private:
  orbit::DAgentIdentity select_agent_for_launch(double expected_cpu, double expected_memory_mb,
                                                const google::protobuf::RepeatedPtrField<std::string>& tags) noexcept;
  void on_agent_load_event(atfw::atapp::service_discovery_module::node_action_t action_type,
                           const orbit::DAgentEtcdLoadRecord& record);
  void update_agent_load(const orbit::DAgentEtcdLoadRecord& record);

 private:
  bool stopped_ = false;
  atfw::atapp::app* app_ = nullptr;
  std::string region_;

  // Agent节点信息: agent_server_id → orbit_controller_agent_info
  std::unordered_map<uint64_t, orbit_controller_agent_info> agents_;

  atapp::etcd_module etcd_mod_;
  atapp::service_discovery_module::service_discovery_cluster_context service_discovery_context_;
  atapp::etcd_watcher::ptr_t watcher_ = nullptr;
};

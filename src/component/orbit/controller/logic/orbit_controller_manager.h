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

namespace rpc {
class context;
}  // namespace rpc

struct orbit_controller_agent_info {
  bool seed_mode = false;
  atfw::orbit::DAgentEtcdLoadRecord load_record;
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
  EXPLICIT_NODISCARD_ATTR static rpc::result_code_type handle_notify_client_started(
      rpc::context& ctx, const atfw::orbit::ATCNotifyClientStartedReq& request,
      atfw::orbit::CTANotifyClientStartedRsp& response);

  // Client 已退出（Agent 上报）
  EXPLICIT_NODISCARD_ATTR static rpc::result_code_type handle_notify_client_exit(
      rpc::context& ctx, const atfw::orbit::ATCNotifyClientExitReq& request,
      atfw::orbit::CTANotifyClientExitRsp& response);

  // Agent 心跳（Agent 上报）
  EXPLICIT_NODISCARD_ATTR static rpc::result_code_type handle_agent_heartbeat(
      rpc::context& ctx, const atfw::orbit::ATCAgentHeartbeatReq& request);

  // Client 发送消息至 Server（Agent 转发）
  EXPLICIT_NODISCARD_ATTR static rpc::result_code_type handle_forward_to_server(
      rpc::context& ctx, const atfw::orbit::ATCForwardToServerReq& request,
      atfw::orbit::CTAForwardToServerRsp& response);

  // ---- 来自 Server ----
  // Server 请求启动 Client
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_launch_client(rpc::context& ctx,
                                                                     const atfw::orbit::STCLaunchClientReq& request,
                                                                     atfw::orbit::CTSLaunchClientRsp& response);

  // Server 发送消息至 Client（下行转发）
  EXPLICIT_NODISCARD_ATTR static rpc::result_code_type handle_send_to_client(
      rpc::context& ctx, const atfw::orbit::STCSendToClientReq& request, atfw::orbit::CTSSendToClientRsp& response);

  // Server 心跳
  EXPLICIT_NODISCARD_ATTR static rpc::result_code_type handle_server_heartbeat(
      rpc::context& ctx, const atfw::orbit::STCServerHeartbeatNotify& request);

 private:
  atfw::orbit::DAgentIdentity select_agent_for_launch(const atfw::orbit::DAgentClientStartArgsResource& resource,
                                                      const std::string& match_tag) noexcept;
  void on_agent_load_event(atfw::atapp::service_discovery_module::node_action_t action_type,
                           const atfw::orbit::DAgentEtcdLoadRecord& record);
  void update_agent_load(const atfw::orbit::DAgentEtcdLoadRecord& record);

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

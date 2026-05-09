// Copyright 2026 atframework

#include "logic/orbit_controller_manager.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/util/json_util.h>
#include <protocol/common/orbit.common.pb.h>
#include <protocol/config/orbit_controller_config.pb.h>
#include <protocol/pbdesc/controller_service.pb.h>
#include <protocol/pbdesc/server_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/rc_ptr.h>
#include <utility/random_engine.h>

#include <rpc/controllertoagentservice/controllertoagentservice.h>
#include <rpc/controllertoserverservice/controllertoserverservice.h>

orbit_controller_manager::orbit_controller_manager() = default;

int orbit_controller_manager::init(atfw::atapp::app* app) {
  app->set_metadata_label(
      "orbit.region", logic_config::me()->get_server_instance_config<orbit::config::orbit_controller_cfg>().region());

  auto etcd_mod = app->get_etcd_module();
  if (etcd_mod) {
    etcd_mod->add_on_node_discovery_event(on_node_event);
  }

  (*agent_policy_selector_.mutable_labels())["orbit.region"] = logic_config::me()->get_server_instance_config<orbit::config::orbit_controller_cfg>().region();

  return 0;
}

void orbit_controller_manager::stop() { stopped_ = true; }

void orbit_controller_manager::tick() {
  if (stopped_) {
    return;
  }
}

// ===================== private helpers =====================
orbit::DAgentIdentity orbit_controller_manager::select_agent_for_launch(
    double expected_cpu, double expected_memory_mb,
    ATFW_EXPLICIT_UNUSED_ATTR const google::protobuf::RepeatedPtrField<std::string>& tags) noexcept {
  // 收集候选 agent 及其权重
  struct candidate_t {
    uint64_t agent_server_id;
    double weight;
  };
  std::vector<candidate_t> candidates;
  double total_weight = 0.0;

  for (const auto& kv : agents_) {
    const auto& load = kv.second.load_snapshot;

    // 将预分配量计入已用资源
    double effective_cpu_used = load.cpu_used() + kv.second.preallocated_cpu;
    double effective_memory_used = load.memory_used_mb() + kv.second.preallocated_memory_mb;

    // 检查 CPU 余量
    if (load.cpu_capacity() > 0.0 && effective_cpu_used + expected_cpu > load.cpu_capacity()) {
      continue;
    }
    // 检查内存余量
    if (load.memory_capacity_mb() > 0.0 && effective_memory_used + expected_memory_mb > load.memory_capacity_mb()) {
      continue;
    }

    // 按 CPU 和内存综合利用率打分（越低越好）
    double cpu_ratio = (load.cpu_capacity() > 0.0) ? effective_cpu_used / load.cpu_capacity() : 0.0;
    double mem_ratio = (load.memory_capacity_mb() > 0.0) ? effective_memory_used / load.memory_capacity_mb() : 0.0;
    double score = (cpu_ratio + mem_ratio) * 0.5;

    // 权重 = 1 / (score + epsilon)，利用率越低权重越高
    double weight = 1.0 / (score + 0.01);
    candidates.push_back({kv.first, weight});
    total_weight += weight;
  }

  orbit::DAgentIdentity result;
  if (candidates.empty()) {
    return result;
  }

  // 加权随机选择
  double dice = atfw::component::random_engine::fast_random_between(0.0, total_weight);
  double accumulated = 0.0;
  uint64_t selected_id = candidates.back().agent_server_id;
  for (const auto& c : candidates) {
    accumulated += c.weight;
    if (dice <= accumulated) {
      selected_id = c.agent_server_id;
      break;
    }
  }

  result.set_agent_server_id(selected_id);

  // 更新预分配数据
  auto it = agents_.find(selected_id);
  if (it != agents_.end()) {
    it->second.preallocated_cpu += expected_cpu;
    it->second.preallocated_memory_mb += expected_memory_mb;
    ++it->second.preallocated_client_count;
  }

  return result;
}

void orbit_controller_manager::on_node_event(atfw::atapp::etcd_module::node_action_t action_type,
                                             const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  if (!node) {
    return;
  }

  if (node->get_discovery_info().type_id() !=
      static_cast<uint64_t>(atfw::component::logic_service_type::kOrbitAgentSvr)) {
    return;
  }

  orbit_controller_manager::me()->on_agent_node_event(action_type, node);
}

void orbit_controller_manager::on_agent_node_event(atfw::atapp::etcd_module::node_action_t action_type,
                                                   const atfw::atapp::etcd_discovery_node::ptr_t& node) {
  switch (action_type) {
    case atfw::atapp::etcd_module::node_action_t::kPut: {
      const uint64_t agent_server_id = node->get_discovery_info().id();
      auto& info = agents_[agent_server_id];

      // 从 metadata labels 中解析负载快照
      const auto& labels = node->get_discovery_info().metadata().labels();
      auto it = labels.find("orbit.agent.load");
      if (it != labels.end() && !it->second.empty()) {
        google::protobuf::util::JsonParseOptions parse_options;
        auto status = google::protobuf::util::JsonStringToMessage(it->second, &info.load_snapshot, parse_options);
        if (!status.ok()) {
          FWLOGWARNING("orbit controller failed to parse DAgentLoadSnapshot for agent {:#x}: {}", agent_server_id,
                       it->second);
        }
      }

      // 负载快照已更新，清除预分配数据（agent 上报的负载已包含之前预分配的 client）
      info.preallocated_cpu = 0.0;
      info.preallocated_memory_mb = 0.0;
      info.preallocated_client_count = 0;

      FWLOGINFO(
          "orbit controller agent {:#x} registered/updated: cpu={:.2f}/{:.2f}, mem={:.2f}/{:.2f} MB, "
          "clients={}, inflight={}",
          agent_server_id, info.load_snapshot.cpu_used(), info.load_snapshot.cpu_capacity(),
          info.load_snapshot.memory_used_mb(), info.load_snapshot.memory_capacity_mb(),
          info.load_snapshot.client_count(), info.load_snapshot.inflight_count());
      break;
    }
    case atfw::atapp::etcd_module::node_action_t::kDelete: {
      const uint64_t agent_server_id = node->get_discovery_info().id();
      agents_.erase(agent_server_id);
      FWLOGINFO("orbit controller agent {:#x} removed from registry", agent_server_id);
      break;
    }
    default:
      break;
  }
}

// ===================== Agent 侧 handlers =====================
rpc::result_code_type orbit_controller_manager::handle_notify_client_started(
    rpc::context& ctx, const orbit::ATCNotifyClientStartedReq& request) {
  const auto& identity = request.client_identity();
  const uint64_t agent_server_id = identity.agent_identity().agent_server_id();
  const std::string& client_id_str = identity.client_id().client_id();

  if (client_id_str.empty()) {
    FWLOGERROR("orbit controller notify_client_started rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  FWLOGINFO("orbit controller client {} started, addr={}", client_id_str, request.client_addr());

  // 通过请求中携带的 server_identity 路由到目标 Server
  const uint64_t target_server_unique_id = request.server_identity().unique_id();
  if (0 == target_server_unique_id) {
    FWLOGWARNING(
        "orbit controller notify_client_started for {}: no server_identity in request, skip CTSClientStartNotify",
        client_id_str);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto notify = rpc::make_shared_message<orbit::CTSClientStartNotify>(ctx);
  *notify->mutable_client_identity() = identity;
  notify->set_client_addr(request.client_addr());
  notify->set_data(request.custom_data());

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::controllertoserverservice::client_start_notify(ctx, agent_server_id, *notify));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTSClientStartNotify failed for {} to server {:#x}, res: {}", client_id_str,
               agent_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_notify_client_exit(
    rpc::context& ctx, const orbit::ATCNotifyClientExitReq& request) {
  const auto& identity = request.client_identity();
  const std::string& client_id_str = identity.client_id().client_id();

  FWLOGINFO("orbit controller client {} exited: reason={}, exit_code={}", client_id_str,
            static_cast<int>(request.exit_reason()), request.exit_code());

  // 通过请求中携带的 server_identity 路由到目标 Server
  const uint64_t target_server_unique_id = request.server_identity().unique_id();
  if (0 == target_server_unique_id) {
    FWLOGWARNING("orbit controller forward_to_server for {}: no server_identity in request, dropped", client_id_str);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto server_node_id = request.server_identity().server_node_id();
  if (server_node_id == 0) {
    FWLOGWARNING("orbit controller forward_to_server for {}: server session {} not found, dropped", client_id_str,
                 target_server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto notify = rpc::make_shared_message<orbit::CTSClientEndNotify>(ctx);
  *notify->mutable_client_identity() = identity;
  notify->set_exit_reason(request.exit_reason());
  notify->set_exit_data(request.custom_data());
  notify->set_exit_code(request.exit_code());

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::controllertoserverservice::client_end_notify(ctx, server_node_id, *notify));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTSClientEndNotify failed for {} to server {:#x}, res: {}", client_id_str,
               server_node_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_forward_to_server(rpc::context& ctx,
                                                                         const orbit::ATCForwardToServerReq& request) {
  const auto& identity = request.client_message().client_identity();
  const std::string& client_id_str = identity.client_id().client_id();

  // 通过请求中携带的 server_identity 路由到目标 Server
  const uint64_t target_server_unique_id = request.server_identity().unique_id();
  if (0 == target_server_unique_id) {
    FWLOGWARNING("orbit controller forward_to_server for {}: no server_identity in request, dropped", client_id_str);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto server_node_id = request.server_identity().server_node_id();
  if (server_node_id == 0) {
    FWLOGWARNING("orbit controller forward_to_server for {}: server session {} not found, dropped", client_id_str,
                 target_server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto notify = rpc::make_shared_message<orbit::CTSForwardToServerNotify>(ctx);
  *notify->mutable_client_message() = request.client_message();

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::controllertoserverservice::forward_to_server(ctx, server_node_id, *notify));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller forward_to_server failed for {} to server {:#x}, res: {}", client_id_str,
               server_node_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

// ===================== Server 侧 handlers =====================
rpc::result_code_type orbit_controller_manager::handle_launch_client(rpc::context& ctx,
                                                                     const orbit::STCLaunchClientReq& request,
                                                                     ATFW_EXPLICIT_UNUSED_ATTR orbit::CTSLaunchClientRsp& response) {
  const uint64_t server_unique_id = request.server_identity().unique_id();
  if (0 == server_unique_id) {
    FWLOGERROR("orbit controller launch_client rejected: server unique_id is 0");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  const std::string& client_id_str = request.args().client_start_args().client_id().client_id();
  if (client_id_str.empty()) {
    FWLOGERROR("orbit controller launch_client rejected: client_id is empty");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto session = request.server_identity().server_node_id();
  if (!session) {
    FWLOGWARNING("orbit controller launch_client: server unique_id={} not connected", server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  const double expected_cpu = request.args().expected_cpu();
  const double expected_memory_mb = request.args().expected_memory_mb();
  auto agent = select_agent_for_launch(expected_cpu, expected_memory_mb, request.match_tags());
  if (agent.agent_server_id() == 0) {
    FWLOGWARNING("orbit controller launch_client: no available agent for cpu={}, mem={}", expected_cpu,
                 expected_memory_mb);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_CONTROLLER_NO_AVAILABLE_AGENT);
  }

  auto start_req = rpc::make_shared_message<orbit::CTAStartClientReq>(ctx);
  auto start_rsp = rpc::make_shared_message<orbit::ATCStartClientRsp>(ctx);
  *start_req->mutable_args() = request.args();
  start_req->mutable_server_identity()->set_unique_id(server_unique_id);

  FWLOGINFO("orbit controller dispatching launch_client to agent {:#x}: client_id={}, cpu={}, mem={}",
            agent.agent_server_id(), client_id_str, expected_cpu, expected_memory_mb);

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::controllertoagentservice::start_client(ctx, agent.agent_server_id(), *start_req, *start_rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTAStartClientReq failed for {} to agent {:#x}, res: {}", client_id_str,
               agent.agent_server_id(), rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_send_to_client(rpc::context& ctx,
                                                                      const orbit::STCSendToClientNotify& request) {
  const auto& identity = request.client_identity();
  const std::string& client_id_str = identity.client_id().client_id();
  const uint64_t agent_server_id = identity.agent_identity().agent_server_id();

  auto forward_req = rpc::make_shared_message<orbit::CTAForwardToClientReq>(ctx);
  auto forward_rsp = rpc::make_shared_message<orbit::ATCForwardToClientRsp>(ctx);
  *forward_req->mutable_client_id() = identity.client_id();
  forward_req->set_payload(request.payload());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::controllertoagentservice::forward_to_client(ctx, agent_server_id, *forward_req, *forward_rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTAForwardToClientReq failed for {} to agent {:#x}, res: {}", client_id_str,
               agent_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_server_heartbeat(
    rpc::context& ctx, const orbit::STCServerHeartbeatNotify& request) {
  const uint64_t agent_server_id = request.agent_identity().agent_server_id();
  auto forward_req = rpc::make_shared_message<orbit::CTAServerHeartbeatReq>(ctx);
  *forward_req->mutable_server_identity() = request.server_identity();

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::controllertoagentservice::server_heartbeat(ctx, agent_server_id, *forward_req));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTAServerHeartbeatReq failed to agent {:#x}, res: {}", agent_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

// Copyright 2026 atframework

#include "logic/orbit_controller_manager.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/rpc_utils.h>
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

#include <rpc/controllertoagentservice/controllertoagentservice.atfw.gen.h>
#include <rpc/controllertoserverservice/controllertoserverservice.atfw.gen.h>

namespace {
constexpr const char* kEtcdByIdDir = "by_id";
constexpr const char* kEtcdByNameDir = "by_name";
constexpr const char* kEtcdTopologyDir = "topology";
constexpr const char* kEtcdOrbitLoadDir = "orbit_load";

static bool unpack_agent_load_record(atfw::orbit::DAgentEtcdLoadRecord& out, const std::string& /*path*/,
                                     const std::string& json, bool reset_data) {
  if (reset_data) {
    out.Clear();
  }

  if (!json.empty()) {
    ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;

    if (!ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::util::JsonStringToMessage(json, &out, options).ok()) {
      return false;
    }
  }

  return 0 != out.server_id();
}

struct orbit_load_watcher_state_t {
  using node_action_t = atfw::atapp::service_discovery_module::node_action_t;

  std::unordered_set<uint64_t> known_agent_ids;
  std::function<void(node_action_t, const atfw::orbit::DAgentEtcdLoadRecord&)> on_event;
};

struct orbit_load_watcher_callback_list_wrapper_t {
  using node_action_t = atfw::atapp::service_discovery_module::node_action_t;

  std::shared_ptr<orbit_load_watcher_state_t> state;

  explicit orbit_load_watcher_callback_list_wrapper_t(
      std::function<void(node_action_t, const atfw::orbit::DAgentEtcdLoadRecord&)> callback)
      : state(std::make_shared<orbit_load_watcher_state_t>()) {
    state->on_event = std::move(callback);
  }

  void operator()(const ::atframework::atapp::etcd_response_header& /*header*/,
                  const ::atframework::atapp::etcd_watcher::response_t& body) const {
    if (!state || !state->on_event) {
      return;
    }

    std::unordered_set<uint64_t> stale_agent_ids;
    if (body.snapshot) {
      stale_agent_ids = state->known_agent_ids;
    }

    for (size_t i = 0; i < body.events.size(); ++i) {
      const ::atframework::atapp::etcd_watcher::event_t& evt_data = body.events[i];

      atfw::orbit::DAgentEtcdLoadRecord record;
      if (!unpack_agent_load_record(record, evt_data.kv.key.empty() ? evt_data.prev_kv.key : evt_data.kv.key,
                                    evt_data.kv.value.empty() ? evt_data.prev_kv.value : evt_data.kv.value, true)) {
        continue;
      }

      if (body.snapshot) {
        stale_agent_ids.erase(record.server_id());
      }

      node_action_t action_type = node_action_t::kPut;
      if (evt_data.evt_type == ::atframework::atapp::etcd_watch_event::kDelete) {
        action_type = node_action_t::kDelete;
        state->known_agent_ids.erase(record.server_id());
      } else {
        state->known_agent_ids.insert(record.server_id());
      }

      state->on_event(action_type, record);
    }

    if (!body.snapshot) {
      return;
    }

    for (uint64_t stale_agent_id : stale_agent_ids) {
      atfw::orbit::DAgentEtcdLoadRecord deleted_record;
      deleted_record.set_server_id(stale_agent_id);
      state->known_agent_ids.erase(stale_agent_id);
      state->on_event(node_action_t::kDelete, deleted_record);
    }
  }
};

}  // namespace

orbit_controller_manager::orbit_controller_manager() = default;

int orbit_controller_manager::init(atfw::atapp::app* app) {
  app_ = app;
  region_ = logic_config::me()->get_server_instance_config<orbit::config::orbit_controller_cfg>().region();
  app->set_metadata_label("orbit.region", region_);

  auto service_discovery_module = app->get_service_discovery_module();
  if (!service_discovery_module) {
    return -1;
  }

  // 拿出配置 并更换Path
  std::string path =
      logic_config::me()->get_server_instance_config<orbit::config::orbit_controller_cfg>().agent_discovery_path();
  auto service_discovery_etcd_config = app->get_origin_configure().etcd();
  service_discovery_etcd_config.set_path(path);

  // Load数据通道
  {
    if (etcd_mod_.init(*app_, service_discovery_etcd_config, nullptr) != 0) {
      FWLOGERROR("orbit agent failed to initialize etcd module");
      return -3;
    }

    std::string watch_path = LOG_WRAPPER_FWAPI_FORMAT("{}{}", etcd_mod_.get_configure_path(), kEtcdOrbitLoadDir);

    auto discovery_watcher_load = atapp::etcd_watcher::create(etcd_mod_.get_etcd_cluster(), watch_path, "+1");
    if (!discovery_watcher_load) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_mod_.get_etcd_cluster(), "create etcd_watcher load failed.");
      return EN_ATBUS_ERR_MALLOC;
    }

    discovery_watcher_load->set_conf_from_protobuf(etcd_mod_.get_configure().watcher());
    if (!etcd_mod_.get_etcd_cluster().add_watcher(discovery_watcher_load)) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_mod_.get_etcd_cluster(), "add etcd_watcher load failed.");
      return EN_ATBUS_ERR_MALLOC;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_mod_.get_etcd_cluster(), "create etcd_watcher for load index {} success",
                                         watch_path);
    watcher_ = discovery_watcher_load;
    discovery_watcher_load->set_evt_handle(orbit_load_watcher_callback_list_wrapper_t(
        [this](atfw::atapp::service_discovery_module::node_action_t action_type,
               const atfw::orbit::DAgentEtcdLoadRecord& record) { on_agent_load_event(action_type, record); }));
  }

  return 0;
}

int orbit_controller_manager::stop() {
  bool not_stop_yet = false;
  not_stop_yet |= service_discovery_context_.stop() != 0;
  not_stop_yet |= etcd_mod_.stop() != 0;
  stopped_ = true;
  if (watcher_) {
    etcd_mod_.get_etcd_cluster().remove_watcher(watcher_);
    watcher_ = nullptr;
  }
  return not_stop_yet ? -1 : 0;
}

void orbit_controller_manager::tick() {
  etcd_mod_.tick();
  service_discovery_context_.tick();
  if (stopped_) {
    return;
  }
}

// ===================== private helpers =====================

atfw::orbit::DAgentIdentity orbit_controller_manager::select_agent_for_launch(
    const atfw::orbit::DAgentClientStartArgsResource& resource, const std::string& match_tag) noexcept {
  // 收集候选 agent 及其权重
  struct candidate_t {
    uint64_t agent_server_id;
    double weight;
  };
  std::vector<candidate_t> candidates;
  double total_weight = 0.0;

  for (const auto& kv : agents_) {
    const auto& load = kv.second.load_record;
    // 检查是否上线
    if (!load.agent_online()) {
      continue;
    }

    // 检查Tags标签
    if (!match_tag.empty() && !load.tag().empty()) {
      if (load.tag() != match_tag) {
        continue;
      }
    }

    // 将预分配量计入已用资源
    double effective_cpu_used = load.agent().cpu_used() + kv.second.preallocated_cpu;
    double effective_memory_used = load.agent().memory_used_mb() + kv.second.preallocated_memory_mb;

    if (kv.second.seed_mode) {
      effective_cpu_used += resource.seed_cpu();
      effective_memory_used += resource.seed_memory_mb();
    } else {
      effective_cpu_used += resource.normal_cpu();
      effective_memory_used += resource.normal_memory_mb();
    }
    // 检查 CPU 余量
    if (load.cpu_capacity() > 0.0 && effective_cpu_used > load.cpu_capacity()) {
      continue;
    }
    // 检查内存余量
    if (load.memory_capacity_mb() > 0.0 && effective_memory_used > load.memory_capacity_mb()) {
      continue;
    }
    // 检查并发
    if (load.max_batch_startup_count() > 0 &&
        kv.second.preallocated_client_count + load.agent().starting_client_count() >=
            static_cast<uint32_t>(load.max_batch_startup_count())) {
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

  atfw::orbit::DAgentIdentity result;
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
    it->second.preallocated_cpu += it->second.seed_mode ? resource.seed_cpu() : resource.normal_cpu();
    it->second.preallocated_memory_mb += it->second.seed_mode ? resource.seed_memory_mb() : resource.normal_memory_mb();
    ++it->second.preallocated_client_count;
  }

  return result;
}

void orbit_controller_manager::on_agent_load_event(atfw::atapp::service_discovery_module::node_action_t action_type,
                                                   const atfw::orbit::DAgentEtcdLoadRecord& record) {
  switch (action_type) {
    case atfw::atapp::service_discovery_module::node_action_t::kPut: {
      update_agent_load(record);
      break;
    }
    case atfw::atapp::service_discovery_module::node_action_t::kDelete: {
      const uint64_t agent_server_id = record.server_id();
      agents_.erase(agent_server_id);
      FWLOGINFO("orbit controller agent {:#x} removed from registry", agent_server_id);
      break;
    }
    default:
      break;
  }
}

void orbit_controller_manager::update_agent_load(const atfw::orbit::DAgentEtcdLoadRecord& record) {
  const uint64_t agent_server_id = record.server_id();

  if (record.region() != region_) {
    // 不同region的agent负载不处理
    agents_.erase(agent_server_id);
    FWLOGINFO("orbit controller agent {:#x} removed from registry diff region: {}", agent_server_id, record.region());
    return;
  }

  auto& info = agents_[agent_server_id];

  info.seed_mode = record.seed_mode();
  info.load_record = record;
  // 负载快照已更新，清除预分配数据（agent 上报的负载已包含之前预分配的 client）
  info.preallocated_cpu = 0.0;
  info.preallocated_memory_mb = 0.0;
  info.preallocated_client_count = 0;

  FWLOGINFO(
      "orbit controller agent {:#x}:{}:tag:{} seed:{} registered/updated: cpu={:.2f}/{:.2f}, mem={:.2f}/{:.2f} MB, "
      "starting={}, running={}",
      agent_server_id, info.load_record.region(), info.load_record.tag(), info.seed_mode,
      info.load_record.agent().cpu_used(), info.load_record.cpu_capacity(), info.load_record.agent().memory_used_mb(),
      info.load_record.memory_capacity_mb(), info.load_record.agent().starting_client_count(),
      info.load_record.agent().running_client_count());
}

// ===================== Agent 侧 handlers =====================
rpc::result_code_type orbit_controller_manager::handle_notify_client_started(
    rpc::context& ctx, const atfw::orbit::ATCNotifyClientStartedReq& request, atfw::orbit::CTANotifyClientStartedRsp& response) {
  const auto& identity = request.client_identity();
  const std::string& client_id_str = identity.client_id().client_id();

  if (client_id_str.empty()) {
    FWLOGERROR("orbit controller notify_client_started rejected: missing client_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  FWLOGINFO("orbit controller client {} started, addr={}", client_id_str, request.client_addr());

  // 通过请求中携带的 server_identity 路由到目标 Server
  if (0 == request.server_identity().unique_id() || 0 == request.server_identity().server_node_id()) {
    FWLOGWARNING("orbit controller notify_client_started for {}: no server_identity in request, skip CTSClientStartReq",
                 client_id_str);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto notify = rpc::make_shared_message<atfw::orbit::CTSClientStartReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::STCClientStartRsp>(ctx);
  *notify->mutable_client_identity() = identity;
  notify->set_client_addr(request.client_addr());
  notify->set_data(request.custom_data());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(rpc::controllertoserverservice::client_start_notify(
      ctx, request.server_identity().server_node_id(), *notify, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTSClientStartReq failed for {} to server {:#x}, res: {}", client_id_str,
               request.server_identity().server_node_id(), rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  response.set_error_code(rsp->error_code());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_notify_client_exit(rpc::context& ctx,
                                                                          const atfw::orbit::ATCNotifyClientExitReq& request,
                                                                          atfw::orbit::CTANotifyClientExitRsp& response) {
  const auto& identity = request.client_identity();
  const std::string& client_id_str = identity.client_id().client_id();

  FWLOGINFO("orbit controller client {} exited: reason={}, exit_code={}", client_id_str,
            static_cast<int>(request.exit_reason()), request.exit_code());

  // 通过请求中携带的 server_identity 路由到目标 Server
  const uint64_t target_server_unique_id = request.server_identity().unique_id();
  if (0 == target_server_unique_id) {
    FWLOGWARNING("orbit controller forward_to_server for {}: no server_identity in request, dropped", client_id_str);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto server_node_id = request.server_identity().server_node_id();
  if (server_node_id == 0) {
    FWLOGWARNING("orbit controller forward_to_server for {}: server session {} not found, dropped", client_id_str,
                 target_server_unique_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto notify = rpc::make_shared_message<atfw::orbit::CTSClientEndReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::STCClientEndRsp>(ctx);
  *notify->mutable_client_identity() = identity;
  notify->set_exit_reason(request.exit_reason());
  notify->set_exit_data(request.custom_data());
  notify->set_exit_code(request.exit_code());

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::controllertoserverservice::client_end_notify(ctx, server_node_id, *notify, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTSClientEndReq failed for {} to server {:#x}, res: {}", client_id_str, server_node_id,
               rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  response.set_error_code(rsp->error_code());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_agent_heartbeat(rpc::context& ctx,
                                                                       const atfw::orbit::ATCAgentHeartbeatReq& request) {
  // 通过请求中携带的 server_identity 路由到目标 Server
  const uint64_t target_server_unique_id = request.server_identity().unique_id();
  if (0 == target_server_unique_id) {
    FWLOGWARNING("orbit controller agent_heartbeat no server_identity in request, dropped");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto server_node_id = request.server_identity().server_node_id();
  if (server_node_id == 0) {
    FWLOGWARNING("orbit controller agent_heartbeat: server session {} not found, dropped", target_server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto notify = rpc::make_shared_message<atfw::orbit::CTSClientAgentHeartbeatNotify>(ctx);
  *notify->mutable_agent_identity() = request.agent_identity();
  *notify->mutable_client_ids() = request.client_ids();

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::controllertoserverservice::client_agent_heartbeat(ctx, server_node_id, *notify));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller agent_heartbeat failed to server {:#x}, res: {}", server_node_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_forward_to_server(rpc::context& ctx,
                                                                         const atfw::orbit::ATCForwardToServerReq& request,
                                                                         atfw::orbit::CTAForwardToServerRsp& response) {
  const auto& identity = request.client_message().client_identity();
  const std::string& client_id_str = identity.client_id().client_id();

  // 通过请求中携带的 server_identity 路由到目标 Server
  const uint64_t target_server_unique_id = request.server_identity().unique_id();
  if (0 == target_server_unique_id) {
    FWLOGWARNING("orbit controller forward_to_server for {}: no server_identity in request, dropped", client_id_str);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto server_node_id = request.server_identity().server_node_id();
  if (server_node_id == 0) {
    FWLOGWARNING("orbit controller forward_to_server for {}: server session {} not found, dropped", client_id_str,
                 target_server_unique_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto notify = rpc::make_shared_message<atfw::orbit::CTSForwardToServerReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::STCForwardToServerRsp>(ctx);
  *notify->mutable_client_message() = request.client_message();

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::controllertoserverservice::forward_to_server(ctx, server_node_id, *notify, *rsp));
  response.set_error_code(rsp->error_code());
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller forward_to_server failed for {} to server {:#x}, res: {}", client_id_str,
               server_node_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

// ===================== Server 侧 handlers =====================
rpc::result_code_type orbit_controller_manager::handle_launch_client(
    rpc::context& ctx, const atfw::orbit::STCLaunchClientReq& request,
    ATFW_EXPLICIT_UNUSED_ATTR atfw::orbit::CTSLaunchClientRsp& response) {
  const uint64_t server_unique_id = request.server_identity().unique_id();
  if (0 == server_unique_id) {
    FWLOGERROR("orbit controller launch_client rejected: server unique_id is 0");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  const std::string& client_id_str = request.args().client_start_args().client_id().client_id();
  if (client_id_str.empty()) {
    FWLOGERROR("orbit controller launch_client rejected: client_id is empty");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  uint64_t server_node_id = request.server_identity().server_node_id();
  if (server_node_id == 0) {
    FWLOGWARNING("orbit controller launch_client: server node_id={} not connected", server_node_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (request.args().resource().normal_cpu() <= std::numeric_limits<float>::epsilon() ||
      request.args().resource().normal_memory_mb() <= std::numeric_limits<float>::epsilon() ||
      request.args().resource().seed_cpu() <= std::numeric_limits<float>::epsilon() ||
      request.args().resource().seed_memory_mb() <= std::numeric_limits<float>::epsilon()) {
    FWLOGERROR("orbit controller launch_client rejected: invalid resource requirements for client_id={}",
               client_id_str);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int32_t retry_count = 3;
  while (retry_count > 0) {
    auto agent = select_agent_for_launch(request.args().resource(), request.args().match_tag());
    if (agent.agent_server_id() == 0) {
      FWLOGWARNING("orbit controller launch_client: no available agent");
      RPC_AWAIT_IGNORE_RESULT(rpc::wait(ctx, std::chrono::milliseconds(1000)));
      --retry_count;
      continue;
    }

    auto start_req = rpc::make_shared_message<atfw::orbit::CTAStartClientReq>(ctx);
    auto start_rsp = rpc::make_shared_message<atfw::orbit::ATCStartClientRsp>(ctx);
    *start_req->mutable_args() = request.args();
    *start_req->mutable_server_identity() = request.server_identity();

    FWLOGINFO("orbit controller dispatching launch_client to agent {:#x}: client_id={}", agent.agent_server_id(),
              client_id_str);

    int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
        rpc::controllertoagentservice::start_client(ctx, agent.agent_server_id(), *start_req, *start_rsp));
    if (start_rsp->has_load_record()) {
      update_agent_load(start_rsp->load_record());
    }
    *response.mutable_client_identity() = start_rsp->client_identity();
    if (rpc_result < 0) {
      if (rpc_result == PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD) {
        FWLOGWARNING("orbit controller launch_client: agent {:#x} overloaded, retrying with another agent",
                     agent.agent_server_id());
      } else {
        FWLOGERROR("orbit controller CTAStartClientReq failed for {} to agent {:#x}, res: {}", client_id_str,
                   agent.agent_server_id(), rpc_result);
        response.set_error_code(rpc_result);
        RPC_RETURN_CODE(rpc_result);
      }
    } else {
      // 成功，返回结果
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    --retry_count;
  }
  FWLOGERROR("orbit controller launch_client: all candidate agents are overloaded for client_id={}", client_id_str);
  response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_send_to_client(rpc::context& ctx,
                                                                      const atfw::orbit::STCSendToClientReq& request,
                                                                      atfw::orbit::CTSSendToClientRsp& response) {
  const auto& identity = request.client_identity();
  const std::string& client_id_str = identity.client_id().client_id();
  const uint64_t agent_server_id = identity.agent_identity().agent_server_id();

  auto forward_req = rpc::make_shared_message<atfw::orbit::CTAForwardToClientReq>(ctx);
  auto forward_rsp = rpc::make_shared_message<atfw::orbit::ATCForwardToClientRsp>(ctx);
  *forward_req->mutable_server_identity() = request.server_identity();
  *forward_req->mutable_client_id() = identity.client_id();
  forward_req->set_payload(request.payload());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::controllertoagentservice::forward_to_client(ctx, agent_server_id, *forward_req, *forward_rsp));
  response.set_error_code(forward_rsp->error_code());
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTAForwardToClientReq failed for {} to agent {:#x}, res: {}", client_id_str,
               agent_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_controller_manager::handle_server_heartbeat(
    rpc::context& ctx, const atfw::orbit::STCServerHeartbeatNotify& request) {
  auto forward_req = rpc::make_shared_message<atfw::orbit::CTAServerHeartbeatReq>(ctx);
  *forward_req->mutable_server_identity() = request.server_identity();

  for (const auto& agent_identity : request.agent_identity()) {
    const uint64_t agent_server_id = agent_identity.agent_server_id();
    int32_t rpc_result =
        RPC_AWAIT_CODE_RESULT(rpc::controllertoagentservice::server_heartbeat(ctx, agent_server_id, *forward_req));
    if (rpc_result < 0) {
      FWLOGERROR("orbit controller CTAServerHeartbeatReq failed to agent {:#x}, res: {}", agent_server_id, rpc_result);
      continue;
    }
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

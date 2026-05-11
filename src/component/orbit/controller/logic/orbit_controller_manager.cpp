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

namespace {
constexpr const char* kEtcdByIdDir = "by_id";
constexpr const char* kEtcdByNameDir = "by_name";
constexpr const char* kEtcdTopologyDir = "topology";
constexpr const char* kEtcdOrbitLoadDir = "orbit_load";

static bool unpack_agent_load_record(orbit::DAgentEtcdLoadRecord& out, const std::string& /*path*/,
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
  using node_action_t = atfw::atapp::etcd_module::node_action_t;

  std::unordered_set<uint64_t> known_agent_ids;
  std::function<void(node_action_t, const orbit::DAgentEtcdLoadRecord&)> on_event;
};

struct orbit_load_watcher_callback_list_wrapper_t {
  using node_action_t = atfw::atapp::etcd_module::node_action_t;

  std::shared_ptr<orbit_load_watcher_state_t> state;

  explicit orbit_load_watcher_callback_list_wrapper_t(
      std::function<void(node_action_t, const orbit::DAgentEtcdLoadRecord&)> callback)
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

      orbit::DAgentEtcdLoadRecord record;
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
      orbit::DAgentEtcdLoadRecord deleted_record;
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

  auto etcd_mod = app->get_etcd_module();
  if (!etcd_mod) {
    return -1;
  }

  std::string path = etcd_mod->generate_etcd_path(
      logic_config::me()->get_server_instance_config<orbit::config::orbit_controller_cfg>().agent_discovery_path());

  // 初始化另一个discovery通道
  if (init_discovery(etcd_mod, path) != 0) {
    return -2;
  }

  // Load数据通道
  {
    auto& etcd_ctx = etcd_mod->get_raw_etcd_ctx();
    std::string watch_path = LOG_WRAPPER_FWAPI_FORMAT("{}{}", path, kEtcdOrbitLoadDir);

    auto discovery_watcher_load = atapp::etcd_watcher::create(etcd_ctx, watch_path, "+1");
    if (!discovery_watcher_load) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "create etcd_watcher load failed.");
      return EN_ATBUS_ERR_MALLOC;
    }

    discovery_watcher_load->set_conf_from_protobuf(etcd_mod->get_configure().watcher());
    if (!etcd_ctx.add_watcher(discovery_watcher_load)) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "add etcd_watcher load failed.");
      return EN_ATBUS_ERR_MALLOC;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_watcher for load index {} success", watch_path);

    discovery_watcher_load->set_evt_handle(orbit_load_watcher_callback_list_wrapper_t(
        [this](atfw::atapp::etcd_module::node_action_t action_type, const orbit::DAgentEtcdLoadRecord& record) {
          on_agent_load_event(action_type, record);
        }));
  }

  return 0;
}

void orbit_controller_manager::stop() {
  stopped_ = true;

  auto& etcd_mod = app_->get_etcd_module();
  auto& etcd_ctx = etcd_mod->get_raw_etcd_ctx();
  for (const auto& watcher : watchers_) {
    etcd_ctx.remove_watcher(watcher);
  }
  for (const auto& keepalive_actor : keepalive_actors_) {
    etcd_mod->remove_keepalive_actor(keepalive_actor);
  }
  keepalive_actors_.clear();
  watchers_.clear();
}

void orbit_controller_manager::tick() {
  if (stopped_) {
    return;
  }
}

// ===================== private helpers =====================
int32_t orbit_controller_manager::init_discovery(std::shared_ptr<atfw::atapp::etcd_module> etcd_mod,
                                                 const std::string& path) {
  {
    auto& etcd_ctx = etcd_mod->get_raw_etcd_ctx();
    std::string watch_path = LOG_WRAPPER_FWAPI_FORMAT("{}{}", path, kEtcdByIdDir);

    auto discovery_watcher_by_id = atapp::etcd_watcher::create(etcd_ctx, watch_path, "+1");
    if (!discovery_watcher_by_id) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "create etcd_watcher by_id failed.");
      return EN_ATBUS_ERR_MALLOC;
    }

    discovery_watcher_by_id->set_conf_from_protobuf(etcd_mod->get_configure().watcher());
    if (!etcd_ctx.add_watcher(discovery_watcher_by_id)) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "add etcd_watcher by_id failed.");
      return EN_ATBUS_ERR_MALLOC;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_watcher for by_id index {} success", watch_path);

    discovery_watcher_by_id->set_evt_handle(etcd_mod->create_discovery_watcher_callback_list_wrapper());
    watchers_.push_back(discovery_watcher_by_id);
    std::string keepalive_path = LOG_WRAPPER_FWAPI_FORMAT("{}/{}-{}", watch_path, app_->get_app_name(), app_->get_id());
    std::string empty;
    auto keepalive_actor_ = etcd_mod->add_keepalive_actor(empty, keepalive_path);
    if (!keepalive_actor_) {
      FWLOGERROR("orbit agent failed to create etcd keepalive actor for path {}", keepalive_path);
      return -6;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_keepalive {} for by_id index {} success",
                                         reinterpret_cast<const void*>(keepalive_actor_.get()), keepalive_path);
    keepalive_actors_.push_back(keepalive_actor_);
  }
  {
    auto& etcd_ctx = etcd_mod->get_raw_etcd_ctx();
    std::string watch_path = LOG_WRAPPER_FWAPI_FORMAT("{}{}", path, kEtcdByNameDir);

    auto discovery_watcher_by_name = atapp::etcd_watcher::create(etcd_ctx, watch_path, "+1");
    if (!discovery_watcher_by_name) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "create etcd_watcher by_name failed.");
      return EN_ATBUS_ERR_MALLOC;
    }

    discovery_watcher_by_name->set_conf_from_protobuf(etcd_mod->get_configure().watcher());
    if (!etcd_ctx.add_watcher(discovery_watcher_by_name)) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "add etcd_watcher by_name failed.");
      return EN_ATBUS_ERR_MALLOC;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_watcher for by_name index {} success", watch_path);

    discovery_watcher_by_name->set_evt_handle(etcd_mod->create_discovery_watcher_callback_list_wrapper());
    watchers_.push_back(discovery_watcher_by_name);
    std::string keepalive_path = LOG_WRAPPER_FWAPI_FORMAT("{}/{}-{}", watch_path, app_->get_app_name(), app_->get_id());
    std::string empty;
    auto keepalive_actor_ = etcd_mod->add_keepalive_actor(empty, keepalive_path);
    if (!keepalive_actor_) {
      FWLOGERROR("orbit agent failed to create etcd keepalive actor for path {}", keepalive_path);
      return -6;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_keepalive {} for by_name index {} success",
                                         reinterpret_cast<const void*>(keepalive_actor_.get()), keepalive_path);
    keepalive_actors_.push_back(keepalive_actor_);
  }
  {
    auto& etcd_ctx = etcd_mod->get_raw_etcd_ctx();
    std::string watch_path = LOG_WRAPPER_FWAPI_FORMAT("{}{}", path, kEtcdTopologyDir);

    auto topology_watcher = atapp::etcd_watcher::create(etcd_ctx, watch_path, "+1");
    if (!topology_watcher) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "create etcd_watcher topology failed.");
      return EN_ATBUS_ERR_MALLOC;
    }

    topology_watcher->set_conf_from_protobuf(etcd_mod->get_configure().watcher());
    if (!etcd_ctx.add_watcher(topology_watcher)) {
      LIBATAPP_MACRO_ETCD_CLUSTER_LOG_ERROR(etcd_ctx, "add etcd_watcher topology failed.");
      return EN_ATBUS_ERR_MALLOC;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_watcher for topology index {} success", watch_path);

    topology_watcher->set_evt_handle(etcd_mod->create_topology_watcher_callback_list_wrapper());
    watchers_.push_back(topology_watcher);
    std::string keepalive_path = LOG_WRAPPER_FWAPI_FORMAT("{}/{}-{}", watch_path, app_->get_app_name(), app_->get_id());
    std::string empty;
    auto keepalive_actor_ = etcd_mod->add_keepalive_actor(empty, keepalive_path);
    if (!keepalive_actor_) {
      FWLOGERROR("orbit agent failed to create etcd keepalive actor for path {}", keepalive_path);
      return -6;
    }
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_keepalive {} for topology index {} success",
                                         reinterpret_cast<const void*>(keepalive_actor_.get()), keepalive_path);
    keepalive_actors_.push_back(keepalive_actor_);
  }

  const std::list<atapp::etcd_keepalive::ptr_t>* keepalive_actors[] = {&keepalive_actors_};
  if (!etcd_mod->check_keepalive_actor_start_success(app_, gsl::make_span(keepalive_actors))) {
    FWLOGERROR("orbit agent etcd keepalive actor start failed");
    return -7;
  }
  return 0;
}

orbit::DAgentIdentity orbit_controller_manager::select_agent_for_launch(
    double expected_cpu, double expected_memory_mb,
    const google::protobuf::RepeatedPtrField<std::string>& tags) noexcept {
  // 收集候选 agent 及其权重
  struct candidate_t {
    uint64_t agent_server_id;
    double weight;
  };
  std::vector<candidate_t> candidates;
  double total_weight = 0.0;

  for (const auto& kv : agents_) {
    // 检查Tags标签
    if (!tags.empty()) {
      bool not_found = false;
      for (const auto& tag : tags) {
        if (std::find(kv.second.load_record.tags().begin(), kv.second.load_record.tags().end(), tag) ==
            kv.second.load_record.tags().end()) {
          not_found = true;
          break;
        }
      }
      if (not_found) {
        continue;
      }
    }

    const auto& load = kv.second.load_record.agent();

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

void orbit_controller_manager::on_agent_load_event(atfw::atapp::etcd_module::node_action_t action_type,
                                                   const orbit::DAgentEtcdLoadRecord& record) {
  switch (action_type) {
    case atfw::atapp::etcd_module::node_action_t::kPut: {
      update_agent_load(record);
      break;
    }
    case atfw::atapp::etcd_module::node_action_t::kDelete: {
      const uint64_t agent_server_id = record.server_id();
      agents_.erase(agent_server_id);
      FWLOGINFO("orbit controller agent {:#x} removed from registry", agent_server_id);
      break;
    }
    default:
      break;
  }
}

void orbit_controller_manager::update_agent_load(const orbit::DAgentEtcdLoadRecord& record) {
  const uint64_t agent_server_id = record.server_id();

  if (record.region() != region_) {
    // 不同region的agent负载不处理
    agents_.erase(agent_server_id);
    FWLOGINFO("orbit controller agent {:#x} removed from registry diff region: {}", agent_server_id, record.region());
    return;
  }

  auto& info = agents_[agent_server_id];

  info.load_record = record;
  // 负载快照已更新，清除预分配数据（agent 上报的负载已包含之前预分配的 client）
  info.preallocated_cpu = 0.0;
  info.preallocated_memory_mb = 0.0;
  info.preallocated_client_count = 0;

  FWLOGINFO(
      "orbit controller agent {:#x}:{}:tags_size:{} registered/updated: cpu={:.2f}/{:.2f}, mem={:.2f}/{:.2f} MB, "
      "clients={}, inflight={}",
      agent_server_id, info.load_record.region(), info.load_record.tags_size(), info.load_record.agent().cpu_used(),
      info.load_record.agent().cpu_capacity(), info.load_record.agent().memory_used_mb(),
      info.load_record.agent().memory_capacity_mb(), info.load_record.agent().client_count(),
      info.load_record.agent().inflight_count());
}

// ===================== Agent 侧 handlers =====================
rpc::result_code_type orbit_controller_manager::handle_notify_client_started(
    rpc::context& ctx, const orbit::ATCNotifyClientStartedReq& request) {
  const auto& identity = request.client_identity();
  const std::string& client_id_str = identity.client_id().client_id();

  if (client_id_str.empty()) {
    FWLOGERROR("orbit controller notify_client_started rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  FWLOGINFO("orbit controller client {} started, addr={}", client_id_str, request.client_addr());

  // 通过请求中携带的 server_identity 路由到目标 Server
  if (0 == request.server_identity().unique_id() || 0 == request.server_identity().server_node_id()) {
    FWLOGWARNING(
        "orbit controller notify_client_started for {}: no server_identity in request, skip CTSClientStartNotify",
        client_id_str);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto notify = rpc::make_shared_message<orbit::CTSClientStartNotify>(ctx);
  *notify->mutable_client_identity() = identity;
  notify->set_client_addr(request.client_addr());
  notify->set_data(request.custom_data());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::controllertoserverservice::client_start_notify(ctx, request.server_identity().server_node_id(), *notify));
  if (rpc_result < 0) {
    FWLOGERROR("orbit controller CTSClientStartNotify failed for {} to server {:#x}, res: {}", client_id_str,
               request.server_identity().server_node_id(), rpc_result);
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

rpc::result_code_type orbit_controller_manager::handle_agent_heartbeat(rpc::context& ctx,
                                                                       const orbit::ATCAgentHeartbeatReq& request) {
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

  auto notify = rpc::make_shared_message<orbit::CTSClientAgentHeartbeatNotify>(ctx);
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
rpc::result_code_type orbit_controller_manager::handle_launch_client(
    rpc::context& ctx, const orbit::STCLaunchClientReq& request,
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

  uint64_t server_node_id = request.server_identity().server_node_id();
  if (server_node_id == 0) {
    FWLOGWARNING("orbit controller launch_client: server unique_id={} not connected", server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  const double expected_cpu = request.args().expected_cpu();
  const double expected_memory_mb = request.args().expected_memory_mb();
  int32_t retry_count = 3;
  while (retry_count > 0) {
    auto agent = select_agent_for_launch(expected_cpu, expected_memory_mb, request.match_tags());
    if (agent.agent_server_id() == 0) {
      FWLOGWARNING("orbit controller launch_client: no available agent for cpu={}, mem={}", expected_cpu,
                   expected_memory_mb);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_CONTROLLER_NO_AVAILABLE_AGENT);
    }

    auto start_req = rpc::make_shared_message<orbit::CTAStartClientReq>(ctx);
    auto start_rsp = rpc::make_shared_message<orbit::ATCStartClientRsp>(ctx);
    *start_req->mutable_args() = request.args();
    *start_req->mutable_server_identity() = request.server_identity();

    FWLOGINFO("orbit controller dispatching launch_client to agent {:#x}: client_id={}, cpu={}, mem={}",
              agent.agent_server_id(), client_id_str, expected_cpu, expected_memory_mb);

    int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
        rpc::controllertoagentservice::start_client(ctx, agent.agent_server_id(), *start_req, *start_rsp));
    if (start_rsp->has_load_record()) {
      update_agent_load(start_rsp->load_record());
    }
    if (rpc_result < 0) {
      if (rpc_result == PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD) {
        FWLOGWARNING("orbit controller launch_client: agent {:#x} overloaded, retrying with another agent",
                     agent.agent_server_id());
      } else {
        FWLOGERROR("orbit controller CTAStartClientReq failed for {} to agent {:#x}, res: {}", client_id_str,
                   agent.agent_server_id(), rpc_result);
        RPC_RETURN_CODE(rpc_result);
      }
    } else {
      // 成功，返回结果
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    --retry_count;
  }
  FWLOGERROR("orbit controller launch_client: all candidate agents are overloaded for client_id={}", client_id_str);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);
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
  auto forward_req = rpc::make_shared_message<orbit::CTAServerHeartbeatReq>(ctx);
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

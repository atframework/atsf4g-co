// Copyright 2026 atframework

#include "logic/orbit_agent_manager.h"

#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <uv.h>

#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <logic/logic_server_setup.h>
#include <rpc/agenttoclientservice/agenttoclientservice.h>
#include <rpc/agenttocontrollerservice/agenttocontrollerservice.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/util/json_util.h>
#include <protocol/config/orbit_agent_config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace {
static bool split_command_line(const std::string& input, std::vector<std::string>& output) {
  output.clear();

  std::string current;
  char quoted = '\0';
  for (size_t i = 0; i < input.size(); ++i) {
    const char current_char = input[i];
    if ('\0' != quoted) {
      if (current_char == quoted) {
        quoted = '\0';
        continue;
      }

      if ('\\' == current_char && i + 1 < input.size() && input[i + 1] == quoted) {
        current.push_back(input[i + 1]);
        ++i;
        continue;
      }

      current.push_back(current_char);
      continue;
    }

    if ('\'' == current_char || '"' == current_char) {
      quoted = current_char;
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(current_char)) != 0) {
      if (!current.empty()) {
        output.emplace_back(std::move(current));
        current.clear();
      }
      continue;
    }

    current.push_back(current_char);
  }

  if ('\0' != quoted) {
    return false;
  }

  if (!current.empty()) {
    output.emplace_back(std::move(current));
  }

  return !output.empty();
}

static void delete_uv_process_handle(uv_handle_t* handle) { delete reinterpret_cast<uv_process_t*>(handle); }

struct orbit_agent_process_exit_data {
  std::string client_id;
};

static void on_uv_process_exit(uv_process_t* handle, int64_t exit_status, int term_signal) {
  auto* data = static_cast<orbit_agent_process_exit_data*>(handle->data);
  orbit_agent_manager::me()->on_client_process_exit(data->client_id, exit_status, term_signal);
  delete data;
  handle->data = nullptr;
  uv_close(reinterpret_cast<uv_handle_t*>(handle), delete_uv_process_handle);
}
}  // namespace

uint64_t orbit_agent_client_record::get_controller_server_id() {
  auto common_mod = logic_server_last_common_module();
  auto discovery = common_mod->get_discovery_index_by_type(
      static_cast<uint64_t>(atframework::component::logic_service_type::kOrbitControllerSvr));
  auto node = discovery->get_node_by_consistent_hash(client_id);
  if (node == nullptr) {
    return 0;
  }
  return node->get_discovery_info().id();
}

orbit_agent_manager::orbit_agent_manager() = default;

int orbit_agent_manager::init(atfw::atapp::app* app) {
  owner_app_ = app;
  uv_disable_stdio_inheritance();

  app->set_metadata_label("orbit.region",
                          logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().region());

  std::string origin_configured_client_command_line_ =
      logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().configured_client_command_line();
  tags_ = logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().tags();

  cpu_capacity_ = logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().cpu_capacity();
  memory_capacity_mb_ =
      logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().memory_capacity_mb();

  if (!origin_configured_client_command_line_.empty()) {
    FWLOGINFO("orbit agent launch command configured: {}", origin_configured_client_command_line_);
  } else {
    FWLOGERROR("orbit agent launch command is empty, set ORBIT_AGENT_CLIENT_COMMAND_LINE before start_client");
    return -1;
  }

  if (!split_command_line(origin_configured_client_command_line_, configured_client_command_line_)) {
    FWLOGERROR("split_command_line failed for {}", origin_configured_client_command_line_);
    return -2;
  }

  if (configured_client_command_line_.empty()) {
    FWLOGERROR("orbit agent launch command is empty after split, invalid configured_client_command_line: {}",
               origin_configured_client_command_line_);
    return -3;
  }

  for (const auto& arg : configured_client_command_line_) {
    FWLOGINFO("orbit agent launch command argument: {}", arg);
  }

  if (cpu_capacity_ <= 0.0) {
    cpu_capacity_ = 1.0;
    FWLOGWARNING("orbit agent cpu_capacity not set, defaulting to {}", cpu_capacity_);
  }

  if (memory_capacity_mb_ <= 0.0) {
    memory_capacity_mb_ = 512.0;
    FWLOGWARNING("orbit agent memory_capacity_mb not set, defaulting to {}", memory_capacity_mb_);
  }

  const uint64_t local_server_id = logic_config::me()->get_local_server_id();
  if (local_server_id == 0) {
    FWLOGERROR("orbit agent failed to get local_server_id from logic_config");
    return -4;
  }
  agent_identity_.set_agent_server_id(local_server_id);

  return 0;
}

void orbit_agent_manager::stop() { stoped_ = true; }

void orbit_agent_manager::tick() {
  if (stoped_) {
    return;
  }

  time_t now = util::time::time_utility::get_sys_now();

  // 检查 Client 超时（无论是否已连接 Controller）
  check_client_timeouts(now);

  // 定期将负载快照写入 etcd metadata（每5秒更新一次）
  constexpr time_t kLoadUpdateIntervalSec = 5;
  if (now - last_load_etcd_update_timepoint_ >= kLoadUpdateIntervalSec) {
    last_load_etcd_update_timepoint_ = now;
    update_etcd_load_snapshot();
  }
}

orbit::DAgentLoadSnapshot orbit_agent_manager::build_agent_load_snapshot() const noexcept {
  orbit::DAgentLoadSnapshot snapshot;
  snapshot.set_cpu_capacity(cpu_capacity_);
  snapshot.set_memory_capacity_mb(memory_capacity_mb_);

  double cpu_used = 0.0;
  double memory_used_mb = 0.0;
  uint32_t client_count = 0;
  uint32_t inflight_count = 0;

  for (const auto& kv : clients_) {
    auto record = kv.second;
    if (orbit::EN_SLAVE_STATE_STARTING == record->state || orbit::EN_SLAVE_STATE_SEED == record->state) {
      ++inflight_count;
      // Account for reserved resources during startup
      cpu_used += record->expected_cpu;
      memory_used_mb += record->expected_memory_mb;
    } else if (orbit::EN_SLAVE_STATE_RUNNING == record->state || orbit::EN_SLAVE_STATE_EXITING == record->state) {
      ++client_count;
      cpu_used += record->load_snapshot.cpu_used();
      memory_used_mb += record->load_snapshot.memory_used_mb();
    }
  }

  snapshot.set_cpu_used(cpu_used);
  snapshot.set_memory_used_mb(memory_used_mb);
  snapshot.set_client_count(client_count);
  snapshot.set_inflight_count(inflight_count);
  return snapshot;
}

rpc::result_code_type orbit_agent_manager::handle_start_client(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                               const orbit::CTAStartClientReq& request,
                                                               ATFW_EXPLICIT_UNUSED_ATTR orbit::ATCStartClientRsp& response) {
  server_heartbeat(request.server_identity());

  // 检查负载状态
  {
    const double expected_cpu = request.args().expected_cpu();
    const double expected_memory_mb = request.args().expected_memory_mb();
    const auto load = build_agent_load_snapshot();
    if (cpu_capacity_ > 0.0 && load.cpu_used() + expected_cpu > cpu_capacity_) {
      FWLOGWARNING(
          "orbit agent start_client rejected (cpu overload): cpu_used={:.2f} + expected={:.2f} > capacity={:.2f}",
          load.cpu_used(), expected_cpu, cpu_capacity_);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);
    }
    if (memory_capacity_mb_ > 0.0 && load.memory_used_mb() + expected_memory_mb > memory_capacity_mb_) {
      FWLOGWARNING(
          "orbit agent start_client rejected (mem overload): mem_used={:.2f} + expected={:.2f} > capacity={:.2f}",
          load.memory_used_mb(), expected_memory_mb, memory_capacity_mb_);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);
    }
  }

  // 启动Client
  orbit_agent_client_record_ptr client_record = nullptr;
  int prepare_result = prepare_start_client_record(request, client_record);
  if (prepare_result < 0) {
    RPC_RETURN_CODE(prepare_result);
  }

  int spawn_result = spawn_client_process(client_record);
  if (spawn_result < 0) {
    RPC_RETURN_CODE(spawn_result);
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_forward_to_client(rpc::context& ctx,
                                                                    const orbit::CTAForwardToClientReq& request,
                                                                    ATFW_EXPLICIT_UNUSED_ATTR orbit::ATCForwardToClientRsp& response) {
  server_heartbeat(request.server_identity());

  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent forward_to_client rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record || 0 == client_record->client_server_id) {
    FWLOGWARNING("orbit agent forward_to_client ignored for {}: client_server_id={:#x}, state={}", client_id,
                 nullptr != client_record ? client_record->client_server_id : 0,
                 nullptr != client_record ? static_cast<int>(client_record->state)
                                          : static_cast<int>(orbit::EN_SLAVE_STATE_UNSPECIFIED));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto notify_request = rpc::make_shared_message<orbit::ATDForwardToClientNotify>(ctx);
  notify_request->set_payload(request.payload());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttoclientservice::forward_to_client(ctx, client_record->client_server_id, *notify_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent forward_to_client failed for {} to client node {:#x}, res: {}", client_record->client_id,
               client_record->client_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_server_heartbeat(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                                   const orbit::CTAServerHeartbeatReq& request) {
  server_heartbeat(request.server_identity());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

void orbit_agent_manager::server_heartbeat(const orbit::DServerIdentity& server_identity) {
  server_unique_id_to_identity_[server_identity.unique_id()] = server_identity;
}

orbit::DServerIdentity* orbit_agent_manager::find_server_identity(uint64_t server_unique_id) {
  // TODO 超时流程 与 消息缓存
  auto iter = server_unique_id_to_identity_.find(server_unique_id);
  if (iter == server_unique_id_to_identity_.end()) {
    return nullptr;
  }
  return &iter->second;
}

rpc::result_code_type orbit_agent_manager::handle_client_start(rpc::context& ctx, uint64_t client_server_id,
                                                               const orbit::DTAClientStartReq& request) {
  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent client_start rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent client_start rejected: client_id {} not found in records", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  if (client_record->state != orbit::EN_SLAVE_STATE_STARTING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected STARTING", client_id,
                 static_cast<int>(client_record->state));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
  }

  client_record->state = orbit::EN_SLAVE_STATE_RUNNING;
  client_record->client_addr = request.client_addr();
  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();
  client_record->client_server_id = client_server_id;

  auto identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientStartedReq>(ctx);
  fill_client_identity(*notify_request->mutable_client_identity(), client_record);
  notify_request->set_client_addr(client_record->client_addr);
  notify_request->set_custom_data(request.custom_data());
  *notify_request->mutable_server_identity() = *identity;

  auto controller_server_id = client_record->get_controller_server_id();

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::notify_client_started(ctx, controller_server_id, *notify_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent notify_client_started failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_client_heartbeat(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                                   const orbit::DTAClientHeartbeatNotify& request) {
  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent client_heartbeat rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent client_heartbeat rejected: client_id {} not found in records", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  client_record->load_snapshot = request.snapshot();
  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_send_to_server(rpc::context& ctx,
                                                                 const orbit::DTASendToServerNotify& request) {
  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent send_to_server rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent send_to_server rejected: client_id {} not found in records", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  if (client_record->state != orbit::EN_SLAVE_STATE_RUNNING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected RUNNING", client_id,
                 static_cast<int>(client_record->state));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
  }

  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();

  auto identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto forward_request = rpc::make_shared_message<orbit::ATCForwardToServerReq>(ctx);
  orbit::DClientMessage* client_message = forward_request->mutable_client_message();
  fill_client_identity(*client_message->mutable_client_identity(), client_record);
  client_message->set_payload(request.payload());
  *forward_request->mutable_server_identity() = *identity;

  auto controller_server_id = client_record->get_controller_server_id();

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::forward_to_server(ctx, controller_server_id, *forward_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent forward_to_server failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_client_exit(rpc::context& ctx,
                                                              const orbit::DTAClientExitReq& request) {
  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent client_exit rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent handle_client_exit rejected: client_id {} not found in records", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  if (client_record->state != orbit::EN_SLAVE_STATE_RUNNING) {
    FWLOGERROR("orbit agent client_start ignored for {}: invalid state {}, expected RUNNING", client_id,
               static_cast<int>(client_record->state));
  }
  client_record->state = orbit::EN_SLAVE_STATE_EXITING;

  clients_.erase(client_id);

  auto identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientExitReq>(ctx);
  fill_client_identity(*notify_request->mutable_client_identity(), client_record);
  notify_request->set_exit_reason(request.exit_reason());
  notify_request->set_custom_data(request.custom_data());
  notify_request->set_exit_code(request.exit_code());
  *notify_request->mutable_server_identity() = *identity;

  auto controller_server_id = client_record->get_controller_server_id();

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::notify_client_exit(ctx, controller_server_id, *notify_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent notify_client_exit failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

orbit_agent_client_record_ptr orbit_agent_manager::find_client(const std::string& client_id) noexcept {
  auto iter = clients_.find(client_id);
  if (clients_.end() == iter) {
    return nullptr;
  }
  return iter->second;
}

const orbit_agent_client_record_ptr orbit_agent_manager::find_client(const std::string& client_id) const noexcept {
  auto iter = clients_.find(client_id);
  if (clients_.end() == iter) {
    return nullptr;
  }
  return iter->second;
}

int orbit_agent_manager::prepare_start_client_record(const orbit::CTAStartClientReq& request,
                                                     orbit_agent_client_record_ptr& output) {
  const std::string& client_id = request.args().client_start_args().client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent start_client rejected: missing client_id");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  auto client = find_client(client_id);
  if (client != nullptr) {
    FWLOGWARNING("orbit agent start_client rejected for {}: already active pid={}, state={}", client->client_id,
                 client->process_id, static_cast<int>(client->state));
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_ID_ALREADY_EXISTS;
  }

  auto record = atfw::util::memory::make_strong_rc<orbit_agent_client_record>();
  clients_[client_id] = record;
  record->client_id = client_id;
  record->custom_args = request.args().client_start_args().custom_args();
  record->expected_cpu = request.args().expected_cpu();
  record->expected_memory_mb = request.args().expected_memory_mb();
  record->startup_timeout_sec = request.args().startup_timeout_sec();
  record->heartbeat_timeout_sec = request.args().heartbeat_timeout_sec();
  record->server_unique_id = request.server_identity().unique_id();

  record->state = orbit::EN_SLAVE_STATE_STARTING;
  record->client_addr.clear();

  output = record;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

void orbit_agent_manager::fill_client_identity(orbit::DClientIdentity& output,
                                               orbit_agent_client_record_ptr client) const {
  *output.mutable_agent_identity() = agent_identity_;
  output.mutable_client_id()->set_client_id(client->client_id);
}

void orbit_agent_manager::build_client_launch_arguments(orbit_agent_client_record_ptr record,
                                                        std::vector<std::string>& output) const {
  output.reserve(configured_client_command_line_.size() + static_cast<size_t>(record->custom_args.size()));
  for (const std::string& arg : configured_client_command_line_) {
    output.emplace_back(arg);
  }
  for (const std::string& custom_arg : record->custom_args) {
    output.emplace_back(custom_arg);
  }
}

int orbit_agent_manager::spawn_client_process(orbit_agent_client_record_ptr record) {
  std::vector<std::string> launch_arguments;
  build_client_launch_arguments(record, launch_arguments);

  std::vector<char*> launch_argv;
  launch_argv.reserve(launch_arguments.size() + 1);
  for (std::string& launch_argument : launch_arguments) {
    launch_argv.emplace_back(const_cast<char*>(launch_argument.c_str()));
  }
  launch_argv.emplace_back(nullptr);

  auto* process_handle = new uv_process_t();
  std::memset(process_handle, 0, sizeof(*process_handle));

  auto* exit_data = new orbit_agent_process_exit_data();
  exit_data->client_id = record->client_id;
  process_handle->data = exit_data;

  uv_process_options_t options;
  std::memset(&options, 0, sizeof(options));
  options.file = launch_argv[0];
  options.args = launch_argv.data();
  options.exit_cb = on_uv_process_exit;

  int uv_result = uv_spawn(uv_default_loop(), process_handle, &options);
  if (uv_result < 0) {
    delete exit_data;
    process_handle->data = nullptr;
    delete process_handle;
    FWLOGERROR("orbit agent start_client failed for {}: {}", record->client_id, uv_strerror(uv_result));
    return uv_result;
  }

  record->process_id = static_cast<int64_t>(uv_process_get_pid(process_handle));
  if (record->process_id <= 0) {
    record->process_id = static_cast<int64_t>(process_handle->pid);
  }

  record->process_handle = process_handle;
  record->start_timepoint = static_cast<time_t>(util::time::time_utility::get_sys_now());

  std::string command_line_str;
  for (const auto& arg : launch_arguments) {
    if (!command_line_str.empty()) {
      command_line_str += " ";
    }
    command_line_str += arg;
  }

  FWLOGINFO("orbit agent started client {} with pid {} by command {}", record->client_id, record->process_id,
            command_line_str);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

void orbit_agent_manager::on_client_process_exit(const std::string& client_id, int64_t exit_status, int term_signal) {
  auto record = find_client(client_id);
  if (!record) {
    // 已由 handle_client_exit 或 check_client_timeouts 正常移除
    return;
  }

  // Client 未通过 STAClientExitReq 告知退出，视为异常退出
  FWLOGWARNING("orbit agent client {} exited unexpectedly: exit_status={}, term_signal={}, state={}", client_id,
               exit_status, term_signal, static_cast<int>(record->state));

  record->process_handle = nullptr;  // 句柄正在被 libuv 回调关闭
  record->state = orbit::EN_SLAVE_STATE_EXITING;
  clients_.erase(client_id);

  auto controller_server_id = record->get_controller_server_id();
  if (0 == controller_server_id) {
    FWLOGWARNING("orbit agent process crash for {} but no controller connected, skip notify", client_id);
    return;
  }

  auto server_identity_ptr = find_server_identity(record->server_unique_id);
  if (server_identity_ptr == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, record->server_unique_id);
    return;
  }

  orbit::DClientIdentity identity;
  fill_client_identity(identity, record);
  const orbit::EnClientExitReason reason = orbit::EN_SLAVE_EXIT_REASON_CRASH;
  const int32_t exit_code = static_cast<int32_t>(exit_status);

  auto invoke_result =
      rpc::async_invoke("orbit_agent_manager", "notify_crash_exit",
                        [controller_server_id, identity = std::move(identity), server_identity = *server_identity_ptr,
                         reason, exit_code](rpc::context& ctx) mutable -> rpc::result_code_type {
                          auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientExitReq>(ctx);
                          *notify_request->mutable_client_identity() = std::move(identity);
                          *notify_request->mutable_server_identity() = std::move(server_identity);
                          notify_request->set_exit_reason(reason);
                          notify_request->set_exit_code(exit_code);
                          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::agenttocontrollerservice::notify_client_exit(
                              ctx, controller_server_id, *notify_request)));
                        });
  if (!invoke_result.is_success()) {
    FWLOGERROR("orbit agent failed to spawn notify_crash_exit task for {}, res: {}({})", client_id,
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

void orbit_agent_manager::check_client_timeouts(time_t now) {
  struct expire_entry {
    std::string client_id;
    orbit::EnClientExitReason reason;
  };
  std::vector<expire_entry> expired;

  for (const auto& kv : clients_) {
    const auto& record = kv.second;
    if (!record) {
      continue;
    }
    if (orbit::EN_SLAVE_STATE_STARTING == record->state || orbit::EN_SLAVE_STATE_SEED == record->state) {
      if (record->startup_timeout_sec > 0 && record->start_timepoint > 0 &&
          now >= static_cast<time_t>(record->start_timepoint) + static_cast<time_t>(record->startup_timeout_sec)) {
        expired.push_back({kv.first, orbit::EN_SLAVE_EXIT_REASON_STARTUP_TIMEOUT});
      }
    } else if (orbit::EN_SLAVE_STATE_RUNNING == record->state) {
      if (record->heartbeat_timeout_sec > 0 && record->last_heartbeat_timepoint > 0 &&
          now >= static_cast<time_t>(record->last_heartbeat_timepoint) +
                     static_cast<time_t>(record->heartbeat_timeout_sec)) {
        expired.push_back({kv.first, orbit::EN_SLAVE_EXIT_REASON_HEARTBEAT_TIMEOUT});
      }
    }
  }

  for (auto& entry : expired) {
    auto record = find_client(entry.client_id);
    if (!record) {
      continue;
    }

    record->state = orbit::EN_SLAVE_STATE_EXITING;
    clients_.erase(entry.client_id);

    auto controller_server_id = record->get_controller_server_id();
    if (0 == controller_server_id) {
      FWLOGWARNING("orbit agent process timeout for {} but no controller connected, skip notify", entry.client_id);
      continue;
    }

    auto server_identity_ptr = find_server_identity(record->server_unique_id);
    if (server_identity_ptr == nullptr) {
      FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
                 entry.client_id, record->server_unique_id);
      return;
    }

    orbit::DClientIdentity identity;
    fill_client_identity(identity, record);
    const orbit::EnClientExitReason reason = entry.reason;
    const int32_t exit_code = PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_TIMEOUT;

    auto invoke_result =
        rpc::async_invoke("orbit_agent_manager", "notify_timeout_exit",
                          [controller_server_id, identity = std::move(identity), server_identity = *server_identity_ptr,
                           reason, exit_code](rpc::context& ctx) mutable -> rpc::result_code_type {
                            auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientExitReq>(ctx);
                            *notify_request->mutable_client_identity() = std::move(identity);
                            *notify_request->mutable_server_identity() = std::move(server_identity);
                            notify_request->set_exit_reason(reason);
                            notify_request->set_exit_code(exit_code);
                            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::agenttocontrollerservice::notify_client_exit(
                                ctx, controller_server_id, *notify_request)));
                          });
    if (!invoke_result.is_success()) {
      FWLOGERROR("orbit agent failed to spawn notify_timeout_exit task for {}, res: {}({})", entry.client_id,
                 *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    }
  }
}

void orbit_agent_manager::update_etcd_load_snapshot() {
  auto load = build_agent_load_snapshot();

  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = false;
  options.always_print_enums_as_ints = true;
  options.preserve_proto_field_names = true;
  options.unquote_int64_if_possible = true;
  std::string json;
  if (!google::protobuf::util::MessageToJsonString(load, &json, options).ok()) {
    FWLOGERROR("orbit agent failed to serialize DAgentLoadSnapshot to JSON");
    return;
  }

  owner_app_->set_metadata_label("orbit.agent.load", json);
}
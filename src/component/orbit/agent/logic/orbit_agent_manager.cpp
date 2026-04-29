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

orbit_agent_manager::orbit_agent_manager() = default;

int orbit_agent_manager::init() {
  uv_disable_stdio_inheritance();

  std::string origin_configured_client_command_line_ =
      logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().configured_client_command_line();
  region_ = logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().region();
  cpu_capacity_ = logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().cpu_capacity();
  memory_capacity_mb_ =
      logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().memory_capacity_mb();
  register_attempt_interval_ = logic_config::me()
                                   ->get_server_instance_config<orbit::config::orbit_agent_cfg>()
                                   .register_attempt_interval()
                                   .seconds();
  heartbeat_interval_ =
      logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>().heartbeat_interval().seconds();

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

  if (register_attempt_interval_ <= 0) {
    register_attempt_interval_ = 5;
    FWLOGWARNING("orbit agent register_attempt_interval not set, defaulting to {}", register_attempt_interval_);
  }

  if (heartbeat_interval_ <= 0) {
    heartbeat_interval_ = 3;
    FWLOGWARNING("orbit agent heartbeat_interval not set, defaulting to {}", heartbeat_interval_);
  }

  const uint64_t local_server_id = logic_config::me()->get_local_server_id();
  if (local_server_id == 0) {
    FWLOGERROR("orbit agent failed to get local_server_id from logic_config");
    return -4;
  }
  agent_identity_.set_agent_id(local_server_id);

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

  // 未注册时，每 register_attempt_interval_ 秒尝试一次 register_agent
  if (0 == controller_server_id_) {
    if (!register_in_progress_ && now >= last_register_attempt_timepoint_ + static_cast<time_t>(register_attempt_interval_)) {
      last_register_attempt_timepoint_ = now;
      register_in_progress_ = true;
      auto invoke_result = rpc::async_invoke("orbit_agent_manager", "register_agent",
                                             [this](rpc::context& ctx) -> rpc::result_code_type {
                                               auto res = RPC_AWAIT_CODE_RESULT(connect_controller_server(ctx));
                                               register_in_progress_ = false;
                                               RPC_RETURN_CODE(res);
                                             });
      if (!invoke_result.is_success()) {
        register_in_progress_ = false;
        FWLOGERROR("orbit agent failed to spawn register_agent task, res: {}({})", *invoke_result.get_error(),
                   protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
      }
    }
    return;
  }

  // 已注册后，每 heartbeat_interval_ 秒发送一次 heartbeat_agent
  if (!heartbeat_in_progress_ && now >= last_heartbeat_to_controller_timepoint_ + static_cast<time_t>(heartbeat_interval_)) {
    last_heartbeat_to_controller_timepoint_ = now;
    heartbeat_in_progress_ = true;
    auto invoke_result =
        rpc::async_invoke("orbit_agent_manager", "heartbeat_agent", [this](rpc::context& ctx) -> rpc::result_code_type {
          auto res = RPC_AWAIT_CODE_RESULT(do_heartbeat_agent(ctx));
          heartbeat_in_progress_ = false;
          RPC_RETURN_CODE(res);
        });
    if (!invoke_result.is_success()) {
      heartbeat_in_progress_ = false;
      FWLOGERROR("orbit agent failed to spawn heartbeat_agent task, res: {}({})", *invoke_result.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    }
  }
}

rpc::result_code_type orbit_agent_manager::connect_controller_server(rpc::context& ctx) {
  // TODO 发消息 通过Region选择Controller Server，目前先通过一致性hash选择
  logic_server_common_module* common_mod = logic_server_last_common_module();
  if (nullptr == common_mod) {
    FWLOGERROR("orbit agent register_agent failed: logic_server_common_module not available");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
  }

  const uint64_t zone_id = logic_config::me() != nullptr ? logic_config::me()->get_local_zone_id() : 0;
  atfw::atapp::etcd_discovery_set::ptr_t discovery =
      common_mod->get_discovery_index_by_type_zone("orbit-controller", zone_id);
  if (!discovery || discovery->empty()) {
    discovery = common_mod->get_discovery_index_by_type("orbit-controller");
  }

  if (!discovery || discovery->empty()) {
    FWLOGWARNING("orbit agent register_agent: no orbit-controller found in service discovery (zone_id={})", zone_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  const uint64_t local_server_id = logic_config::me() != nullptr ? logic_config::me()->get_local_server_id() : 0;
  atfw::atapp::etcd_discovery_node::ptr_t node =
      discovery->get_node_by_consistent_hash(std::to_string(local_server_id));
  if (!node) {
    FWLOGWARNING("orbit agent register_agent: consistent_hash selection returned null (zone_id={})", zone_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  const uint64_t target_id = node->get_discovery_info().id();

  auto req = rpc::make_shared_message<orbit::ATCRegisterAgentReq>(ctx);
  auto rsp = rpc::make_shared_message<orbit::CTARegisterAgentRsp>(ctx);
  req->set_cpu_capacity(cpu_capacity_);
  req->set_memory_capacity_mb(memory_capacity_mb_);
  *req->mutable_agent_identity() = agent_identity_;

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(rpc::agenttocontrollerservice::register_agent(ctx, target_id, *req, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent register_agent to controller {:#x} failed, res: {}", target_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  controller_server_id_ = target_id;
  last_heartbeat_to_controller_timepoint_ = 0;
  FWLOGINFO("orbit agent registered with controller {:#x} (zone_id={})", controller_server_id_, zone_id);

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::do_heartbeat_agent(rpc::context& ctx) {
  if (0 == controller_server_id_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
  }

  auto req = rpc::make_shared_message<orbit::ATCHeartbeatAgentReq>(ctx);
  auto rsp = rpc::make_shared_message<orbit::CTAHeartbeatAgentRsp>(ctx);
  *req->mutable_load() = build_agent_load_snapshot();

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::agenttocontrollerservice::heartbeat_agent(ctx, controller_server_id_, *req, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent heartbeat_agent to controller {:#x} failed, res: {} -- will re-register",
               controller_server_id_, rpc_result);
    controller_server_id_ = 0;
    last_register_attempt_timepoint_ = 0;
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
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
                                                               orbit::ATCStartClientRsp& response) {
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
                                                                    orbit::ATCForwardToClientRsp& response) {
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

  auto notify_request = rpc::make_shared_message<orbit::ATSForwardToClientNotify>(ctx);
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

rpc::result_code_type orbit_agent_manager::handle_client_start(rpc::context& ctx, uint64_t client_server_id,
                                                               const orbit::STAClientStartReq& request) {
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

  auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientStartedReq>(ctx);
  fill_client_identity(*notify_request->mutable_client_identity(), client_record);
  notify_request->set_client_addr(client_record->client_addr);
  notify_request->set_custom_data(request.custom_data());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::notify_client_started(ctx, controller_server_id_, *notify_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent notify_client_started failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id_, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_client_heartbeat(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                                   const orbit::STAClientHeartbeatNotify& request) {
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
                                                                 const orbit::STASendToServerNotify& request) {
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

  auto forward_request = rpc::make_shared_message<orbit::ATCForwardToServerReq>(ctx);
  orbit::DClientMessage* client_message = forward_request->mutable_client_message();
  fill_client_identity(*client_message->mutable_client_identity(), client_record);
  client_message->set_payload(request.payload());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::forward_to_server(ctx, controller_server_id_, *forward_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent forward_to_server failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id_, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_client_exit(rpc::context& ctx,
                                                              const orbit::STAClientExitReq& request) {
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

  auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientExitReq>(ctx);
  fill_client_identity(*notify_request->mutable_client_identity(), client_record);
  notify_request->set_exit_reason(request.exit_reason());
  notify_request->set_custom_data(request.custom_data());
  notify_request->set_exit_code(request.exit_code());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::notify_client_exit(ctx, controller_server_id_, *notify_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent notify_client_exit failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id_, rpc_result);
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
  output.reserve(configured_client_command_line_.size() + record->custom_args.size());
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
  record->start_timepoint = static_cast<uint64_t>(util::time::time_utility::get_sys_now());

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

  if (0 == controller_server_id_) {
    FWLOGWARNING("orbit agent process crash for {} but no controller connected, skip notify", client_id);
    return;
  }

  orbit::DClientIdentity identity;
  fill_client_identity(identity, record);
  const orbit::EnClientExitReason reason = orbit::EN_SLAVE_EXIT_REASON_CRASH;
  const int32_t exit_code = static_cast<int32_t>(exit_status);
  const uint64_t controller_id = controller_server_id_;

  auto invoke_result =
      rpc::async_invoke("orbit_agent_manager", "notify_crash_exit",
                        [controller_id, identity = std::move(identity), reason,
                         exit_code](rpc::context& ctx) mutable -> rpc::result_code_type {
                          auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientExitReq>(ctx);
                          *notify_request->mutable_client_identity() = std::move(identity);
                          notify_request->set_exit_reason(reason);
                          notify_request->set_exit_code(exit_code);
                          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                              rpc::agenttocontrollerservice::notify_client_exit(ctx, controller_id, *notify_request)));
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
        expired.push_back({kv.first, orbit::EN_SLAVE_EXIT_REASON_HEARTBEAT_TIMEOUT});
      }
    } else if (orbit::EN_SLAVE_STATE_RUNNING == record->state) {
      if (record->heartbeat_timeout_sec > 0 && record->last_heartbeat_timepoint > 0 &&
          now >= static_cast<time_t>(record->last_heartbeat_timepoint) + static_cast<time_t>(record->heartbeat_timeout_sec)) {
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

    if (0 == controller_server_id_) {
      FWLOGWARNING("orbit agent process timeout for {} but no controller connected, skip notify", entry.client_id);
      continue;
    }

    orbit::DClientIdentity identity;
    fill_client_identity(identity, record);
    const orbit::EnClientExitReason reason = entry.reason;
    const int32_t exit_code = PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_TIMEOUT;
    const uint64_t controller_id = controller_server_id_;

    auto invoke_result =
        rpc::async_invoke("orbit_agent_manager", "notify_timeout_exit",
                          [controller_id, identity = std::move(identity), reason,
                           exit_code](rpc::context& ctx) mutable -> rpc::result_code_type {
                            auto notify_request = rpc::make_shared_message<orbit::ATCNotifyClientExitReq>(ctx);
                            *notify_request->mutable_client_identity() = std::move(identity);
                            notify_request->set_exit_reason(reason);
                            notify_request->set_exit_code(exit_code);
                            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::agenttocontrollerservice::notify_client_exit(
                                ctx, controller_id, *notify_request)));
                          });
    if (!invoke_result.is_success()) {
      FWLOGERROR("orbit agent failed to spawn notify_timeout_exit task for {}, res: {}({})", entry.client_id,
                 *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
    }
  }
}
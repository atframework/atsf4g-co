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
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace {
constexpr time_t kDefaultServerIdentityTimeoutSec = 30;
constexpr time_t kDefaultServerIdentityCheckIntervalSec = 5;

constexpr const char* kOrbitArgsConfigEnvPrefix = "--config_env";

static atapp::etcd_keepalive::checker_fn_t make_orbit_load_checker(uint64_t expected_server_id) {
  return [expected_server_id](const std::string& checked) -> bool {
    if (checked.empty()) {
      return true;
    }

    orbit::DAgentEtcdLoadRecord current_record;
    if (!ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::util::JsonStringToMessage(checked, &current_record).ok()) {
      return false;
    }

    return current_record.server_id() == expected_server_id;
  };
}

static void append_config_env_line(std::vector<std::string>& output, const char* key, const std::string& value) {
  output.emplace_back(kOrbitArgsConfigEnvPrefix);
  output.push_back(LOG_WRAPPER_FWAPI_FORMAT("{}={}", key, value));
}

static void append_config_env_line(std::vector<std::string>& output, const char* key, int32_t value) {
  append_config_env_line(output, key, std::to_string(value));
}

static void append_config_env_line(std::vector<std::string>& output, const char* key, uint64_t value) {
  append_config_env_line(output, key, std::to_string(static_cast<unsigned long long>(value)));
}

template <class Rep, class Period>
static std::string make_duration_config_env_value(std::chrono::duration<Rep, Period> input) {
  auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(input);
  if (0 == microseconds.count() % std::chrono::seconds{1}.count()) {
    return std::to_string(microseconds.count() / std::chrono::seconds{1}.count()) + "s";
  }

  if (0 == microseconds.count() % std::chrono::milliseconds{1}.count()) {
    return std::to_string(microseconds.count() / std::chrono::milliseconds{1}.count()) + "ms";
  }

  return std::to_string(microseconds.count()) + "us";
}

static void append_bus_config_env_arguments(const atbus::node::conf_t& bus_conf, std::vector<std::string>& output) {
  if (bus_conf.loop_times > 0) {
    append_config_env_line(output, "ATAPP_BUS_LOOP_TIMES", bus_conf.loop_times);
  }

  if (bus_conf.ttl > 0) {
    append_config_env_line(output, "ATAPP_BUS_TTL", bus_conf.ttl);
  }

  if (bus_conf.backlog > 0) {
    append_config_env_line(output, "ATAPP_BUS_BACKLOG", bus_conf.backlog);
  }

  if (bus_conf.first_idle_timeout.count() > 0) {
    append_config_env_line(output, "ATAPP_BUS_FIRST_IDLE_TIMEOUT",
                           make_duration_config_env_value(bus_conf.first_idle_timeout));
  }

  if (bus_conf.ping_interval.count() > 0) {
    append_config_env_line(output, "ATAPP_BUS_PING_INTERVAL", make_duration_config_env_value(bus_conf.ping_interval));
  }

  if (bus_conf.retry_interval.count() > 0) {
    append_config_env_line(output, "ATAPP_BUS_RETRY_INTERVAL", make_duration_config_env_value(bus_conf.retry_interval));
  }

  if (bus_conf.fault_tolerant > 0) {
    append_config_env_line(output, "ATAPP_BUS_FAULT_TOLERANT", static_cast<uint64_t>(bus_conf.fault_tolerant));
  }

  if (bus_conf.message_size > 0) {
    append_config_env_line(output, "ATAPP_BUS_MESSAGE_SIZE", static_cast<uint64_t>(bus_conf.message_size));
  }

  if (bus_conf.receive_buffer_size > 0) {
    append_config_env_line(output, "ATAPP_BUS_RECEIVE_BUFFER_SIZE",
                           static_cast<uint64_t>(bus_conf.receive_buffer_size));
  }

  if (bus_conf.send_buffer_size > 0) {
    append_config_env_line(output, "ATAPP_BUS_SEND_BUFFER_SIZE", static_cast<uint64_t>(bus_conf.send_buffer_size));
  }

  append_config_env_line(output, "ATAPP_BUS_SEND_BUFFER_NUMBER", static_cast<uint64_t>(bus_conf.send_buffer_number));

  size_t access_token_max_number = bus_conf.access_token_max_number;
  if (access_token_max_number < bus_conf.access_tokens.size()) {
    access_token_max_number = bus_conf.access_tokens.size();
  }

  if (access_token_max_number > 0) {
    append_config_env_line(output, "ATAPP_BUS_ACCESS_TOKEN_MAX_NUMBER", static_cast<uint64_t>(access_token_max_number));
  }

  for (size_t index = 0; index < bus_conf.access_tokens.size(); ++index) {
    std::string env_key = "ATAPP_BUS_ACCESS_TOKENS_" + std::to_string(static_cast<unsigned long long>(index));
    append_config_env_line(output, env_key.c_str(),
                           std::string{reinterpret_cast<const char*>(bus_conf.access_tokens[index].data()),
                                       bus_conf.access_tokens[index].size()});
  }
}

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

static int64_t get_total_process_cpu_time_us(const uv_rusage_t& usage) {
  int64_t total_us = static_cast<int64_t>(usage.ru_stime.tv_sec) + static_cast<int64_t>(usage.ru_utime.tv_sec);
  total_us *= 1000000;
  total_us += static_cast<int64_t>(usage.ru_stime.tv_usec) + static_cast<int64_t>(usage.ru_utime.tv_usec);
  return total_us;
}

static void on_uv_process_exit(uv_process_t* handle, int64_t exit_status, int term_signal) {
  auto* data = static_cast<orbit_agent_process_exit_data*>(handle->data);
  orbit_agent_manager::me()->on_client_process_exit(data->client_id, exit_status, term_signal);
  delete data;
  handle->data = nullptr;
  uv_close(reinterpret_cast<uv_handle_t*>(handle), delete_uv_process_handle);
}

static uint64_t make_initial_sequence_allocator() {
  return static_cast<uint64_t>(
             (util::time::time_utility::get_sys_now() - PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
             << 23) +
         static_cast<uint64_t>(util::time::time_utility::get_now_usec() << 3);
}
}  // namespace

uint64_t orbit_agent_client_record::get_controller_server_id() {
  return orbit_agent_manager::me()->select_controller_server_id(client_id);
}

orbit_agent_manager::orbit_agent_manager() = default;

int orbit_agent_manager::init(atfw::atapp::app* app) {
  owner_app_ = app;
  uv_disable_stdio_inheritance();

  const auto& config = logic_config::me()->get_server_instance_config<orbit::config::orbit_agent_cfg>();
  region_ = config.region();
  tags_ = config.tags();

  (*controller_policy_selector_.mutable_labels())["orbit.region"] = region_;

  std::string origin_configured_client_command_line_ = config.configured_client_command_line();
  cpu_capacity_ = config.cpu_capacity();
  memory_capacity_mb_ = config.memory_capacity_mb();
  server_identity_timeout_sec_ = static_cast<time_t>(config.server_identity_timeout_sec());
  server_identity_check_interval_sec_ = static_cast<time_t>(config.server_identity_check_interval_sec());

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

  // 启动参数 ./client.exe ... (预设启动参数) + (customed启动参数) + (agent需要的额外启动参数)
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

  if (server_identity_timeout_sec_ <= 0) {
    server_identity_timeout_sec_ = kDefaultServerIdentityTimeoutSec;
    FWLOGWARNING("orbit agent server_identity_timeout_sec not set, defaulting to {}", server_identity_timeout_sec_);
  }

  if (server_identity_check_interval_sec_ <= 0) {
    server_identity_check_interval_sec_ = kDefaultServerIdentityCheckIntervalSec;
    FWLOGWARNING("orbit agent server_identity_check_interval_sec not set, defaulting to {}",
                 server_identity_check_interval_sec_);
  }

  if (server_identity_check_interval_sec_ > server_identity_timeout_sec_) {
    FWLOGWARNING(
        "orbit agent server_identity_check_interval_sec={} is larger than server_identity_timeout_sec={}, clamp to {}",
        server_identity_check_interval_sec_, server_identity_timeout_sec_, server_identity_timeout_sec_);
    server_identity_check_interval_sec_ = server_identity_timeout_sec_;
  }

  const uint64_t local_server_id = logic_config::me()->get_local_server_id();
  if (local_server_id == 0) {
    FWLOGERROR("orbit agent failed to get local_server_id from logic_config");
    return -4;
  }
  agent_identity_.set_agent_server_id(local_server_id);
  sequence_allocator_ = make_initial_sequence_allocator();

  agent_endpoint_.clear();
  const auto& bus_config = owner_app_->get_origin_configure().bus();
  for (int i = 0; i < bus_config.listen_size(); ++i) {
    if (!bus_config.listen(i).empty()) {
      agent_endpoint_ = bus_config.listen(i);
      break;
    }
  }
  if (agent_endpoint_.empty()) {
    FWLOGERROR("orbit agent failed to resolve listen address from atapp bus.listen");
    return -8;
  }
  FWLOGINFO("orbit agent launch client endpoint: {}", agent_endpoint_);

  // 初始化Record
  load_record_.set_region(region_);
  for (const auto& tag : tags_) {
    load_record_.add_tags(tag);
  }
  load_record_.set_server_id(local_server_id);
  load_record_.mutable_agent()->set_cpu_capacity(cpu_capacity_);
  load_record_.mutable_agent()->set_memory_capacity_mb(memory_capacity_mb_);

  update_etcd_load_snapshot();
  need_update_load_json_ = true;
  load_record_to_json();

  // 初始化负载同步通道
  {
    auto etcd_mod = owner_app_->get_etcd_module();
    if (!etcd_mod) {
      FWLOGERROR("orbit agent failed to get etcd module from app");
      return -5;
    }
    auto& etcd_ctx = etcd_mod->get_raw_etcd_ctx();

    std::string keepalive_path = LOG_WRAPPER_FWAPI_FORMAT("{}{}/{}-{}", etcd_mod->get_configure_path(), "orbit_load",
                                                          owner_app_->get_app_name(), owner_app_->get_id());

    keepalive_actor_ = etcd_mod->add_keepalive_actor(load_json_, keepalive_path);
    if (!keepalive_actor_) {
      FWLOGERROR("orbit agent failed to create etcd keepalive actor for path {}", keepalive_path);
      return -6;
    }
    keepalive_actor_->set_checker(make_orbit_load_checker(local_server_id));
    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_ctx, "create etcd_keepalive {} for orbit_load index {} success",
                                         reinterpret_cast<const void*>(keepalive_actor_.get()), keepalive_path);
    std::list<atapp::etcd_keepalive::ptr_t> keepalive_list{keepalive_actor_};
    const std::list<atapp::etcd_keepalive::ptr_t>* keepalive_actors[] = {&keepalive_list};

    if (!etcd_mod->check_keepalive_actor_start_success(app, gsl::make_span(keepalive_actors))) {
      FWLOGERROR("orbit agent etcd keepalive actor start failed for path {}", keepalive_path);
      return -7;
    }
  }

  return 0;
}

void orbit_agent_manager::stop() {
  stoped_ = true;
  auto etcd_mod = owner_app_->get_etcd_module();
  if (etcd_mod) {
    etcd_mod->remove_keepalive_actor(keepalive_actor_);
  }
  keepalive_actor_.reset();
}

void orbit_agent_manager::tick() {
  if (stoped_) {
    return;
  }

  time_t now = util::time::time_utility::get_sys_now();

  if (now - last_server_identity_timeout_check_timepoint_ >= server_identity_check_interval_sec_) {
    last_server_identity_timeout_check_timepoint_ = now;
    check_server_identity_timeouts(now);
  }

  // 检查 Client 超时（无论是否已连接 Controller）
  check_client_timeouts(now);

  // 定期将负载快照写入 etcd（每5秒更新一次）
  constexpr time_t kAutoLoadUpdateIntervalSec = 5;
  if (now - last_auto_load_etcd_update_timepoint_ >= kAutoLoadUpdateIntervalSec) {
    last_auto_load_etcd_update_timepoint_ = now;
    update_etcd_load_snapshot();
    // 需要更新JSON字符串以同步到etcd
    need_update_load_json_ = true;
  }

  load_record_to_json();
  try_sync_load_to_etcd();
}

uint64_t orbit_agent_manager::select_controller_server_id(const std::string& client_id) const {
  auto common_mod = logic_server_last_common_module();
  if (nullptr == common_mod) {
    return 0;
  }

  auto discovery = common_mod->get_discovery_index_by_type(
      static_cast<uint64_t>(atframework::component::logic_service_type::kOrbitControllerSvr));
  if (!discovery) {
    return 0;
  }

  auto selected = discovery->get_node_by_consistent_hash(client_id, &controller_policy_selector_);
  if (!selected) {
    return 0;
  }

  return selected->get_discovery_info().id();
}

rpc::result_code_type orbit_agent_manager::handle_start_client(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                               const orbit::CTAStartClientReq& request,
                                                               orbit::ATCStartClientRsp& response) {
  update_etcd_load_snapshot();
  *response.mutable_load_record() = load_record_;
  server_heartbeat(request.server_identity());

  // 检查负载状态
  {
    const double expected_cpu = request.args().expected_cpu();
    const double expected_memory_mb = request.args().expected_memory_mb();
    update_etcd_load_snapshot();
    if (cpu_capacity_ > 0.0 && load_record_.agent().cpu_used() + expected_cpu > cpu_capacity_) {
      FWLOGWARNING(
          "orbit agent start_client rejected (cpu overload): cpu_used={:.2f} + expected={:.2f} > capacity={:.2f}",
          load_record_.agent().cpu_used(), expected_cpu, cpu_capacity_);
      need_update_load_json_ = true;                                        // 负载记录有变更需要更新JSON以同步到etcd
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);  // 尝试另一个Agent
    }
    if (memory_capacity_mb_ > 0.0 && load_record_.agent().memory_used_mb() + expected_memory_mb > memory_capacity_mb_) {
      FWLOGWARNING(
          "orbit agent start_client rejected (mem overload): mem_used={:.2f} + expected={:.2f} > capacity={:.2f}",
          load_record_.agent().memory_used_mb(), expected_memory_mb, memory_capacity_mb_);
      need_update_load_json_ = true;                                        // 负载记录有变更需要更新JSON以同步到etcd
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);  // 尝试另一个Agent
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

rpc::result_code_type orbit_agent_manager::handle_forward_to_client(
    rpc::context& ctx, const orbit::CTAForwardToClientReq& request,
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
                                          : static_cast<int>(orbit::EN_CLIENT_STATE_UNSPECIFIED));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
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

rpc::result_code_type orbit_agent_manager::handle_server_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                                                   const orbit::CTAServerHeartbeatReq& request) {
  server_heartbeat(request.server_identity());
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(agent_heartbeat(ctx, controller_server_id, request.server_identity())));
}

void orbit_agent_manager::server_heartbeat(const orbit::DServerIdentity& server_identity) {
  const uint64_t server_unique_id = server_identity.unique_id();
  const time_t now = util::time::time_utility::get_sys_now();
  const time_t expire_timepoint = now + server_identity_timeout_sec_;

  auto& info = server_unique_id_to_identity_[server_unique_id];
  info.identity = server_identity;
  info.expire_timepoint = expire_timepoint;
  server_identity_timeout_queue_.push_back({server_unique_id, expire_timepoint});
}

rpc::result_code_type orbit_agent_manager::agent_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                                           const orbit::DServerIdentity& server_identity) {
  // 找到这个server_identity对应的client_record 发送心跳
  auto iter = server_unique_id_to_client_ids_.find(server_identity.unique_id());
  if (iter == server_unique_id_to_client_ids_.end() || iter->second.empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);  // 没有相关Client，不需要发送心跳
  }

  auto heartbeat_request = rpc::make_shared_message<orbit::ATCAgentHeartbeatReq>(ctx);
  *heartbeat_request->mutable_agent_identity() = agent_identity_;

  for (const auto& client_id : iter->second) {
    heartbeat_request->add_client_ids()->set_client_id(client_id);
  }

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::agent_heartbeat(ctx, controller_server_id, *heartbeat_request));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent agent_heartbeat failed server:{} to controller {:#x}, res: {}", server_identity.unique_id(),
               controller_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
}

orbit::DServerIdentity* orbit_agent_manager::find_server_identity(uint64_t server_unique_id) {
  // TODO 消息缓存
  auto iter = server_unique_id_to_identity_.find(server_unique_id);
  if (iter == server_unique_id_to_identity_.end()) {
    return nullptr;
  }
  return &iter->second.identity;
}

void orbit_agent_manager::check_server_identity_timeouts(time_t now) {
  while (!server_identity_timeout_queue_.empty()) {
    const server_identity_timeout_entry_t& front = server_identity_timeout_queue_.front();
    if (front.expire_timepoint > now) {
      break;
    }

    const uint64_t server_unique_id = front.server_unique_id;
    const time_t expire_timepoint = front.expire_timepoint;
    server_identity_timeout_queue_.pop_front();

    auto identity_iter = server_unique_id_to_identity_.find(server_unique_id);
    if (identity_iter == server_unique_id_to_identity_.end()) {
      continue;
    }

    if (identity_iter->second.expire_timepoint != expire_timepoint) {
      continue;
    }

    FWLOGWARNING("orbit agent server identity heartbeat timeout: unique_id={:#x}, server_node_id={:#x}",
                 server_unique_id, identity_iter->second.identity.server_node_id());
    server_unique_id_to_identity_.erase(identity_iter);
  }
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
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
  }

  if (client_record->state != orbit::EN_CLIENT_STATE_STARTING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected STARTING", client_id,
                 static_cast<int>(client_record->state));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
  }

  client_record->state = orbit::EN_CLIENT_STATE_RUNNING;
  client_record->client_addr = request.client_addr();
  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();
  client_record->client_server_id = client_server_id;

  auto identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
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
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
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
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
  }

  if (client_record->state != orbit::EN_CLIENT_STATE_RUNNING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected RUNNING", client_id,
                 static_cast<int>(client_record->state));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
  }

  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();

  auto identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
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
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
  }

  if (client_record->state != orbit::EN_CLIENT_STATE_RUNNING) {
    FWLOGERROR("orbit agent client_start ignored for {}: invalid state {}, expected RUNNING", client_id,
               static_cast<int>(client_record->state));
  }
  client_record->state = orbit::EN_CLIENT_STATE_EXITING;

  delete_client(client_record);

  auto identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
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

void orbit_agent_manager::fill_normal_client_start_command(const orbit_agent_client_record& record, uint64_t app_id,
                                                           std::vector<std::string>& output) const {
  output.emplace_back("-id");
  output.emplace_back(std::to_string(static_cast<unsigned long long>(app_id)));
  output.emplace_back("--orbit-client-id");
  output.emplace_back(record.client_id);
  output.emplace_back("--orbit-agent-endpoint");
  output.emplace_back(agent_endpoint_);

  if (nullptr != owner_app_ && nullptr != owner_app_->get_bus_node()) {
    append_bus_config_env_arguments(owner_app_->get_bus_node()->get_conf(), output);
  }
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

  record->state = orbit::EN_CLIENT_STATE_STARTING;
  record->client_addr.clear();

  server_unique_id_to_client_ids_[record->server_unique_id].insert(record->client_id);

  output = record;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

void orbit_agent_manager::fill_client_identity(orbit::DClientIdentity& output,
                                               orbit_agent_client_record_ptr client) const {
  *output.mutable_agent_identity() = agent_identity_;
  output.mutable_client_id()->set_client_id(client->client_id);
}

void orbit_agent_manager::build_client_launch_arguments(orbit_agent_client_record_ptr record,
                                                        std::vector<std::string>& output) {
  uint64_t app_id = ++sequence_allocator_;
  output.clear();
  output.reserve(configured_client_command_line_.size() + static_cast<size_t>(record->custom_args.size()) + 6);

  for (const std::string& arg : configured_client_command_line_) {
    output.emplace_back(arg);
  }
  for (const std::string& custom_arg : record->custom_args) {
    output.emplace_back(custom_arg);
  }

  fill_normal_client_start_command(*record, app_id, output);
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
  for (size_t index = 0; index < launch_arguments.size(); ++index) {
    const auto& arg = launch_arguments[index];
    if (!command_line_str.empty()) {
      command_line_str += " ";
    }

    if (arg == kOrbitArgsConfigEnvPrefix && index + 1 < launch_arguments.size()) {
      command_line_str += std::string{kOrbitArgsConfigEnvPrefix} + " <redacted>";
      ++index;
      continue;
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
  record->state = orbit::EN_CLIENT_STATE_EXITING;
  delete_client(record);

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
  const orbit::EnClientExitReason reason = orbit::EN_CLIENT_EXIT_REASON_CRASH;
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
    if (orbit::EN_CLIENT_STATE_STARTING == record->state || orbit::EN_CLIENT_STATE_SEED == record->state) {
      if (record->startup_timeout_sec > 0 && record->start_timepoint > 0 &&
          now >= static_cast<time_t>(record->start_timepoint) + static_cast<time_t>(record->startup_timeout_sec)) {
        expired.push_back({kv.first, orbit::EN_CLIENT_EXIT_REASON_STARTUP_TIMEOUT});
      }
    } else if (orbit::EN_CLIENT_STATE_RUNNING == record->state) {
      if (record->heartbeat_timeout_sec > 0 && record->last_heartbeat_timepoint > 0 &&
          now >= static_cast<time_t>(record->last_heartbeat_timepoint) +
                     static_cast<time_t>(record->heartbeat_timeout_sec)) {
        expired.push_back({kv.first, orbit::EN_CLIENT_EXIT_REASON_HEARTBEAT_TIMEOUT});
      }
    }
  }

  for (auto& entry : expired) {
    auto record = find_client(entry.client_id);
    if (!record) {
      continue;
    }

    record->state = orbit::EN_CLIENT_STATE_EXITING;
    delete_client(record);

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
  double cpu_used = 0.0;
  double memory_used_mb = 0.0;
  uint32_t client_count = 0;
  uint32_t inflight_count = 0;

  double self_cpu_used = last_self_cpu_used_;
  double self_memory_used_mb = 0.0;

  do {
    uv_rusage_t current_usage;
    if (0 != uv_getrusage(&current_usage)) {
      break;
    }

    auto now = std::chrono::steady_clock::now();
    if (has_self_usage_sample_) {
      int64_t elapsed_us =
          std::chrono::duration_cast<std::chrono::microseconds>(now - last_self_usage_sample_timepoint_).count();
      if (elapsed_us > 0) {
        int64_t cpu_offset_us =
            get_total_process_cpu_time_us(current_usage) - get_total_process_cpu_time_us(last_self_rusage_);
        if (cpu_offset_us < 0) {
          cpu_offset_us = 0;
        }

        self_cpu_used = static_cast<double>(cpu_offset_us) / static_cast<double>(elapsed_us);
        last_self_cpu_used_ = self_cpu_used;
      }
    } else {
      has_self_usage_sample_ = true;
      last_self_cpu_used_ = 0.0;
      self_cpu_used = 0.0;
    }

    last_self_rusage_ = current_usage;
    last_self_usage_sample_timepoint_ = now;
  } while (false);

  size_t self_memory_rss = 0;
  if (0 == uv_resident_set_memory(&self_memory_rss)) {
    self_memory_used_mb = static_cast<double>(self_memory_rss) / (1024.0 * 1024.0);
  }

  cpu_used += self_cpu_used;
  memory_used_mb += self_memory_used_mb;

  for (const auto& kv : clients_) {
    auto record = kv.second;
    if (orbit::EN_CLIENT_STATE_STARTING == record->state || orbit::EN_CLIENT_STATE_SEED == record->state) {
      ++inflight_count;
      // Account for reserved resources during startup
      cpu_used += record->expected_cpu;
      memory_used_mb += record->expected_memory_mb;
    } else if (orbit::EN_CLIENT_STATE_RUNNING == record->state || orbit::EN_CLIENT_STATE_EXITING == record->state) {
      ++client_count;
      cpu_used += record->load_snapshot.cpu_used();
      memory_used_mb += record->load_snapshot.memory_used_mb();
    }
  }

  if (load_record_.agent().cpu_used() != cpu_used || load_record_.agent().memory_used_mb() != memory_used_mb ||
      load_record_.agent().client_count() != client_count || load_record_.agent().inflight_count() != inflight_count) {
    load_record_.mutable_agent()->set_cpu_used(cpu_used);
    load_record_.mutable_agent()->set_memory_used_mb(memory_used_mb);
    load_record_.mutable_agent()->set_client_count(client_count);
    load_record_.mutable_agent()->set_inflight_count(inflight_count);
    dirty_load_record_ = true;
  }
}

void orbit_agent_manager::load_record_to_json() {
  if (!need_update_load_json_) {
    return;
  }
  need_update_load_json_ = false;

  if (!dirty_load_record_ && !load_json_.empty()) {
    return;
  }
  dirty_load_record_ = false;
  load_json_.clear();

  ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::util::JsonPrintOptions options;
  options.add_whitespace = false;
  options.always_print_enums_as_ints = true;
  options.preserve_proto_field_names = true;
  options.unquote_int64_if_possible = true;
  if (!ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::util::MessageToJsonString(load_record_, &load_json_, options).ok()) {
    FWLOGERROR("orbit controller pack DAgentEtcdLoadRecord to json failed");
  } else {
    dirty_load_json_ = true;
  }
}

void orbit_agent_manager::try_sync_load_to_etcd() {
  if (!dirty_load_json_) {
    return;
  }
  dirty_load_json_ = false;
  keepalive_actor_->set_value(load_json_);
}

void orbit_agent_manager::delete_client(orbit_agent_client_record_ptr client_record) {
  clients_.erase(client_record->client_id);
  auto& client_ids = server_unique_id_to_client_ids_[client_record->server_unique_id];
  client_ids.erase(client_record->client_id);
  if (client_ids.empty()) {
    server_unique_id_to_client_ids_.erase(client_record->server_unique_id);
  }
}
// Copyright 2026 atframework

#include "logic/orbit_agent_manager.h"

#include <uv.h>

#include <atframe/modules/worker_pool_module.h>

#include <memory/object_allocator.h>

#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <logic/logic_server_setup.h>
#include <rpc/agenttoclientservice/agenttoclientservice.atfw.gen.h>
#include <rpc/agenttocontrollerservice/agenttocontrollerservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

#include <logic/hpa/logic_hpa_controller.h>
#include <logic/hpa/logic_hpa_observer.h>
#include <logic/hpa/logic_hpa_policy.h>

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

#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr time_t kDefaultServerIdentityTimeoutSec = 30;
constexpr time_t kDefaultServerIdentityCheckIntervalSec = 5;
constexpr time_t kDefaultClientForceCleanupDelaySec = 5;

constexpr const char* kOrbitArgsConfigEnvPrefix = "--config_env";
constexpr const char* kOrbitEnabledArg = "--enable_orbit";

static atapp::etcd_keepalive::checker_fn_t make_orbit_load_checker(uint64_t expected_server_id) {
  return [expected_server_id](const std::string& checked) -> bool {
    if (checked.empty()) {
      return true;
    }

    atfw::orbit::DAgentEtcdLoadRecord current_record;
    if (!ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::util::JsonStringToMessage(checked, &current_record).ok()) {
      return false;
    }

    return current_record.server_id() == expected_server_id;
  };
}

static std::string render_string_template(const std::string& input,
                                          const std::unordered_map<std::string, std::string>& render_values) {
  // 将 input 中的 ${field} 占位符替换为 render_values[field] 的实际值；
  // 字段缺失或占位符未闭合时保留原样。
  std::string output;
  output.reserve(input.size());
  size_t pos = 0;
  while (pos < input.size()) {
    const size_t begin = input.find("${", pos);
    if (std::string::npos == begin) {
      output.append(input, pos, std::string::npos);
      break;
    }

    output.append(input, pos, begin - pos);
    const size_t end = input.find('}', begin + 2);
    if (std::string::npos == end) {
      output.append(input, begin, input.size() - begin);
      break;
    }

    const std::string field = input.substr(begin + 2, end - begin - 2);
    const auto iter = render_values.find(field);
    if (render_values.end() != iter) {
      output.append(iter->second);
    } else {
      output.append(input, begin, end - begin + 1);
    }
    pos = end + 1;
  }
  return output;
}

static void append_config_env_line(std::vector<std::string>& output, const char* key, const std::string& value) {
  output.emplace_back(kOrbitArgsConfigEnvPrefix);
  output.push_back(LOG_WRAPPER_FWAPI_FORMAT("{}={}", key, value));
}

static void append_config_env_line(std::vector<std::string>& output, const char* key, int32_t value) {
  append_config_env_line(output, key, std::to_string(value));
}

static void append_config_env_line(std::vector<std::string>& output, const char* key, uint64_t value) {
  append_config_env_line(output, key, std::to_string(value));
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
    std::string env_key = "ATAPP_BUS_ACCESS_TOKENS_" + std::to_string(static_cast<uint64_t>(index));
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

struct orbit_agent_process_exit_data {
  std::string client_id;
};

static int64_t get_total_process_cpu_time_us(const uv_rusage_t& usage) {
  int64_t total_us = static_cast<int64_t>(usage.ru_stime.tv_sec) + static_cast<int64_t>(usage.ru_utime.tv_sec);
  total_us *= 1000000;
  total_us += static_cast<int64_t>(usage.ru_stime.tv_usec) + static_cast<int64_t>(usage.ru_utime.tv_usec);
  return total_us;
}

static void on_uv_process_exit_callback(uv_process_t* handle, int64_t exit_status, int term_signal) {
  orbit_agent_manager::me()->on_uv_process_exit(handle, exit_status, term_signal);
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
  tag_ = config.tag();

  (*controller_policy_selector_.mutable_labels())["orbit.region"] = region_;

  std::string origin_configured_client_command_line_ = config.configured_client_command_line();
  std::string origin_seed_client_command_line_ = config.seed_client_command_line();
  cpu_capacity_ = config.cpu_capacity();
  memory_capacity_mb_ = config.memory_capacity_mb();
  server_identity_timeout_sec_ = static_cast<time_t>(config.server_identity_timeout_sec());
  server_identity_check_interval_sec_ = static_cast<time_t>(config.server_identity_check_interval_sec());
  max_batch_startup_count_ = config.max_batch_startup_count();
#if defined(__linux__) || defined(__unix__)
  seed_mode_enabled_ = config.enable_seed_mode();
#else
  seed_mode_enabled_ = false;
#endif
  seed_startup_timeout_sec_ = config.seed_startup_timeout_sec();
  seed_heartbeat_timeout_sec_ = config.seed_heartbeat_timeout_sec();

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

  configured_client_command_line_.emplace_back(kOrbitEnabledArg);

  // 启动参数 ./client.exe ... (预设启动参数) + (customed启动参数) + (agent需要的额外启动参数)
  for (const auto& arg : configured_client_command_line_) {
    FWLOGINFO("orbit agent launch command argument: {}", arg);
  }

  if (seed_mode_enabled_) {
    if (!split_command_line(origin_seed_client_command_line_, seed_client_command_line_)) {
      FWLOGERROR("split_command_line failed for {}", origin_seed_client_command_line_);
      return -4;
    }

    if (seed_client_command_line_.empty()) {
      FWLOGERROR("orbit agent launch command is empty after split, invalid seed_client_command_line: {}",
                 origin_seed_client_command_line_);
      return -5;
    }

    seed_client_command_line_.emplace_back(kOrbitEnabledArg);
    seed_client_command_line_.emplace_back("--seed_mode");

    // 启动参数 ./client.exe ... (预设启动参数) + (customed启动参数) + (agent需要的额外启动参数)
    for (const auto& arg : seed_client_command_line_) {
      FWLOGINFO("orbit agent launch seed client command argument: {}", arg);
    }
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
    return -6;
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
    return -7;
  }
  FWLOGINFO("orbit agent launch client endpoint: {}", agent_endpoint_);

  if (seed_mode_enabled_) {
    agent_online_ = false;
    FWLOGINFO("orbit agent seed mode enabled, begin startup seed client");
    int startup_res = startup_seed_client();
    if (startup_res != 0) {
      FWLOGERROR("orbit agent failed to startup seed client, res: {}", startup_res);
      return -8;
    }
  } else {
    agent_online_ = true;
  }

  // 初始化Record
  load_record_.set_region(region_);
  load_record_.set_tag(tag_);
  load_record_.set_server_id(local_server_id);
  load_record_.set_cpu_capacity(cpu_capacity_);
  load_record_.set_memory_capacity_mb(memory_capacity_mb_);
  load_record_.set_max_batch_startup_count(max_batch_startup_count_);
  load_record_.set_seed_mode(seed_mode_enabled_);
  load_record_.set_agent_online(agent_online_);

  update_etcd_load_snapshot();
  need_update_load_json_ = true;
  load_record_to_json();

  // 初始化负载同步通道
  {
    if (etcd_mod_.init(*owner_app_, owner_app_->get_origin_configure().etcd(), nullptr) != 0) {
      FWLOGERROR("orbit agent failed to initialize etcd module");
      return -9;
    }

    std::string keepalive_path = LOG_WRAPPER_FWAPI_FORMAT("{}{}/{}-{}", etcd_mod_.get_configure_path(), "orbit_load",
                                                          owner_app_->get_app_name(), owner_app_->get_id());

    keepalive_actor_ = atapp::etcd_keepalive::create(etcd_mod_.get_etcd_cluster(), keepalive_path);
    if (!keepalive_actor_) {
      FWLOGERROR("orbit agent failed to create etcd keepalive actor for path {}", keepalive_path);
      return -10;
    }

    keepalive_actor_->set_checker(make_orbit_load_checker(local_server_id));
    keepalive_actor_->set_value(load_json_);

    if (!etcd_mod_.get_etcd_cluster().add_keepalive(keepalive_actor_)) {
      keepalive_actor_.reset();
    }

    LIBATAPP_MACRO_ETCD_CLUSTER_LOG_INFO(etcd_mod_.get_etcd_cluster(),
                                         "create etcd_keepalive {} for orbit_load index {} success",
                                         reinterpret_cast<const void*>(keepalive_actor_.get()), keepalive_path);

    std::list<atapp::etcd_keepalive::ptr_t> keepalive_list{keepalive_actor_};
    const std::list<atapp::etcd_keepalive::ptr_t>* keepalive_actors[] = {&keepalive_list};
    if (!etcd_mod_.check_keepalive_actor_start_success(gsl::make_span(keepalive_actors))) {
      FWLOGERROR("orbit agent etcd keepalive actor start failed for path {}", keepalive_path);
      return -11;
    }
  }

  {
    logic_server_common_module* common_mod = logic_server_last_common_module();
    if (nullptr != common_mod) {
      auto hpa_controller = common_mod->get_hpa_controller();
      if (!hpa_controller) {
        return -1;
      }
      hpa_controller->set_on_setup_custom_policy(
          "orbit_agent_load",
          [local_server_id](logic_hpa_controller&, const std::shared_ptr<logic_hpa_policy>& custom_policy) {
            custom_policy->add_observer_custom(
                logic_hpa_policy::custom_observer_register_type::kDouble,
                [local_server_id](logic_hpa_policy&, logic_hpa_observer& observer) {
                  std::pair<gsl::string_view, opentelemetry::common::AttributeValue> attributes[] = {
                      {"region", orbit_agent_manager::me()->region_},
                      {"tag", orbit_agent_manager::me()->tag_},
                      {"agent_id", local_server_id}};
                  observer.observe(orbit_agent_manager::get_load_value(), attributes);
                });
          });
    }
  }

  // 初始化Worker回调
  auto worker_pool = (nullptr != owner_app_) ? owner_app_->get_worker_pool_module() : nullptr;
  if (!worker_pool) {
    return -12;
  }
  worker_pool->add_event_callback_on_worker_exiting(orbit_agent_manager::worker_exit_callback);
  return 0;
}

int orbit_agent_manager::stop() {
  int ret = etcd_mod_.stop();
  stoped_ = true;
  if (keepalive_actor_) {
    etcd_mod_.get_etcd_cluster().remove_keepalive(keepalive_actor_);
    keepalive_actor_ = nullptr;
  }
  if (seed_client_record_) {
    // Stop时关闭种子进程 但是不关闭子进程
    stop_client_process(seed_client_record_, atfw::orbit::EN_CLIENT_EXIT_REASON_NORMAL, 0);
    ret = 1;
  }
  while (!clients_.empty()) {
    auto record = clients_.begin()->second;
    delete_client(record);
    async_notify_client_exit(record);
  }
  return ret;
}

void orbit_agent_manager::tick() {
  process_uv_actions();
  etcd_mod_.tick();
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
  check_client_force_kill(now);

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

void orbit_agent_manager::cleanup() {
  if (seed_client_record_) {
    kill_client_process(seed_client_record_, SIGKILL, atfw::orbit::EN_CLIENT_EXIT_REASON_NORMAL, 0);
  }
}

uint64_t orbit_agent_manager::select_controller_server_id(const std::string& client_id) const {
  auto* common_mod = logic_server_last_common_module();
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

rpc::result_code_type orbit_agent_manager::handle_start_client(rpc::context& ctx,
                                                               const atfw::orbit::CTAStartClientReq& request,
                                                               atfw::orbit::ATCStartClientRsp& response) {
  if (!agent_online_) {
    FWLOGERROR("orbit agent start_client rejected: agent not online yet");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_NOT_ONLINE);
  }

  update_etcd_load_snapshot();
  *response.mutable_load_record() = load_record_;
  server_heartbeat(request.server_identity());

  if (max_batch_startup_count_ > 0 && batch_startup_count_ >= max_batch_startup_count_) {
    FWLOGWARNING("orbit agent start_client rejected: batch_startup_count={} >= max_batch_startup_count={}",
                 batch_startup_count_, max_batch_startup_count_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);  // 尝试另一个Agent
  }

  // 检查负载状态
  {
    const double expected_cpu =
        seed_mode_enabled_ ? request.args().resource().seed_cpu() : request.args().resource().normal_cpu();
    const double expected_memory_mb =
        seed_mode_enabled_ ? request.args().resource().seed_memory_mb() : request.args().resource().normal_memory_mb();
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
    delete_client(client_record);
    RPC_RETURN_CODE(prepare_result);
  }
  fill_client_identity(*response.mutable_client_identity(), client_record);

  if (seed_mode_enabled_) {
    // 种子模式
    int32_t spawn_result = RPC_AWAIT_CODE_RESULT(spawn_seed_client_process(ctx, client_record));
    if (spawn_result < 0) {
      delete_client(client_record);
      RPC_RETURN_CODE(spawn_result);
    }
  } else {
    // 普通模式
    int spawn_result = spawn_client_process(client_record, configured_client_command_line_);
    if (spawn_result < 0) {
      delete_client(client_record);
      RPC_RETURN_CODE(spawn_result);
    }
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_forward_to_client(
    rpc::context& ctx, const atfw::orbit::CTAForwardToClientReq& request,
    ATFW_EXPLICIT_UNUSED_ATTR atfw::orbit::ATCForwardToClientRsp& response) {
  server_heartbeat(request.server_identity());

  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent forward_to_client rejected: missing client_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record || 0 == client_record->client_server_id ||
      (atfw::orbit::EN_CLIENT_STATE_RUNNING != client_record->state &&
       atfw::orbit::EN_CLIENT_STATE_EXITING != client_record->state)) {
    FWLOGWARNING("orbit agent forward_to_client ignored for {}: client_server_id={:#x}, state={}", client_id,
                 nullptr != client_record ? client_record->client_server_id : 0,
                 nullptr != client_record ? static_cast<int>(client_record->state)
                                          : static_cast<int>(atfw::orbit::EN_CLIENT_STATE_UNSPECIFIED));
    response.set_error_code(nullptr != client_record ? PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID
                                                     : PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto notify_request = rpc::make_shared_message<atfw::orbit::ATDForwardToClientReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::DTAForwardToClientRsp>(ctx);
  notify_request->set_payload(request.payload());

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttoclientservice::forward_to_client(ctx, client_record->client_server_id, *notify_request, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent forward_to_client failed for {} to client node {:#x}, res: {}", client_record->client_id,
               client_record->client_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  response.set_error_code(rsp->error_code());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_server_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                                                   const atfw::orbit::CTAServerHeartbeatReq& request) {
  server_heartbeat(request.server_identity());
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(agent_heartbeat(ctx, controller_server_id, request.server_identity())));
}

void orbit_agent_manager::server_heartbeat(const atfw::orbit::DServerIdentity& server_identity) {
  if (server_identity.unique_id() == 0 || server_identity.server_node_id() == 0) {
    FWLOGERROR("orbit agent received invalid server identity heartbeat");
    return;
  }
  const uint64_t server_unique_id = server_identity.unique_id();
  const time_t now = util::time::time_utility::get_sys_now();
  const time_t expire_timepoint = now + server_identity_timeout_sec_;

  auto& info = server_unique_id_to_identity_[server_unique_id];
  info.identity = server_identity;
  info.expire_timepoint = expire_timepoint;
  server_identity_timeout_queue_.push_back({server_unique_id, expire_timepoint});
}

rpc::result_code_type orbit_agent_manager::agent_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                                           const atfw::orbit::DServerIdentity& server_identity) {
  // 找到这个server_identity对应的client_record 发送心跳
  auto iter = server_unique_id_to_client_ids_.find(server_identity.unique_id());
  if (iter == server_unique_id_to_client_ids_.end() || iter->second.empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);  // 没有相关Client，不需要发送心跳
  }

  auto heartbeat_request = rpc::make_shared_message<atfw::orbit::ATCAgentHeartbeatReq>(ctx);
  *heartbeat_request->mutable_server_identity() = server_identity;
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
  RPC_RETURN_CODE(0);
}

atfw::orbit::DServerIdentity* orbit_agent_manager::find_server_identity(uint64_t server_unique_id) {
  // TODO(yousongyang): 消息缓存
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
                                                               const atfw::orbit::DTAClientStartReq& request,
                                                               atfw::orbit::ATDClientStartRsp& response) {
  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent client_start rejected: missing client_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent client_start rejected: client_id {} not found in records", client_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->state != atfw::orbit::EN_CLIENT_STATE_STARTING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected STARTING", client_id,
                 static_cast<int>(client_record->state));
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  client_record->client_addr = request.client_addr();
  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();
  client_record->client_server_id = client_server_id;

  set_client_state(client_record, atfw::orbit::EN_CLIENT_STATE_RUNNING);
  if (client_record->seed_process) {
    // 服务启动
    agent_online_ = true;
    update_etcd_load_snapshot();
  } else {
    auto* identity = find_server_identity(client_record->server_unique_id);
    if (identity == nullptr) {
      FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
                 client_id, client_record->server_unique_id);
      response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    auto notify_request = rpc::make_shared_message<atfw::orbit::ATCNotifyClientStartedReq>(ctx);
    auto rsp = rpc::make_shared_message<atfw::orbit::CTANotifyClientStartedRsp>(ctx);
    fill_client_identity(*notify_request->mutable_client_identity(), client_record);
    notify_request->set_client_addr(client_record->client_addr);
    notify_request->set_custom_data(request.custom_data());
    *notify_request->mutable_server_identity() = *identity;

    auto controller_server_id = client_record->get_controller_server_id();

    int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
        rpc::agenttocontrollerservice::notify_client_started(ctx, controller_server_id, *notify_request, *rsp));
    if (rpc_result < 0) {
      FWLOGERROR("orbit agent notify_client_started failed for {} to controller {:#x}, res: {}",
                 client_record->client_id, controller_server_id, rpc_result);
      RPC_RETURN_CODE(rpc_result);
    }
    response.set_error_code(rsp->error_code());
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_client_heartbeat(
    ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx, const atfw::orbit::DTAClientHeartbeatNotify& request) {
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
                                                                 const atfw::orbit::DTASendToServerReq& request,
                                                                 atfw::orbit::ATDSendToServerRsp& response) {
  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent send_to_server rejected: missing client_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent send_to_server rejected: client_id {} not found in records", client_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->state != atfw::orbit::EN_CLIENT_STATE_RUNNING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected RUNNING", client_id,
                 static_cast<int>(client_record->state));
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();

  auto* identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_id, client_record->server_unique_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto forward_request = rpc::make_shared_message<atfw::orbit::ATCForwardToServerReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::CTAForwardToServerRsp>(ctx);

  atfw::orbit::DClientMessage* client_message = forward_request->mutable_client_message();
  fill_client_identity(*client_message->mutable_client_identity(), client_record);
  client_message->set_payload(request.payload());
  *forward_request->mutable_server_identity() = *identity;

  auto controller_server_id = client_record->get_controller_server_id();

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::forward_to_server(ctx, controller_server_id, *forward_request, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent forward_to_server failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  response.set_error_code(rsp->error_code());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_client_exit(rpc::context& ctx,
                                                              const atfw::orbit::DTAClientExitReq& request,
                                                              atfw::orbit::ATDClientExitRsp& response) {
  const std::string& client_id = request.client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent client_exit rejected: missing client_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent handle_client_exit rejected: client_id {} not found in records", client_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->seed_process) {
    if (!stoped_) {
      // 种子进程意外退出
      FWLOGERROR("orbit agent seed client exited unexpectedly");
      agent_fatal_error();
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->state == atfw::orbit::EN_CLIENT_STATE_EXITED) {
    FWLOGWARNING("orbit agent client_exit ignored for {}: already exited", client_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    delete_client(client_record);  // 补充delete
    response.set_error_code(RPC_AWAIT_CODE_RESULT(notify_client_exit(ctx, client_record, request.custom_data())));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->process_handle == nullptr) {
    // 没有句柄 直接标记为退出
    delete_client(client_record);
  } else {
    if (client_record->state != atfw::orbit::EN_CLIENT_STATE_EXITING) {
      // 存在句柄 等待超时 或者 exit
      client_record->force_kill_timepoint =
          util::time::time_utility::get_sys_now() + kDefaultClientForceCleanupDelaySec;
      client_record->exit_reason = request.exit_reason();
      client_record->exit_code = request.exit_code();
      set_client_state(client_record, atfw::orbit::EN_CLIENT_STATE_EXITING);
    }
  }

  response.set_error_code(RPC_AWAIT_CODE_RESULT(notify_client_exit(ctx, client_record, request.custom_data())));
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int orbit_agent_manager::startup_seed_client() {
  std::string client_id = atfw::util::log::format("seed_client_{}_{}", logic_config::me()->get_local_server_id(),
                                                  util::time::time_utility::get_sys_now());
  auto record = atfw::component::memory::stl::make_strong_rc<orbit_agent_client_record>();
  clients_[client_id] = record;
  seed_client_record_ = record;
  record->seed_process = true;
  record->client_id = client_id;
  record->startup_timeout_sec = seed_startup_timeout_sec_;
  record->heartbeat_timeout_sec = seed_heartbeat_timeout_sec_;

  set_client_state(record, atfw::orbit::EN_CLIENT_STATE_STARTING);
  record->client_addr.clear();

  return spawn_client_process(record, seed_client_command_line_);
}

orbit_agent_client_record_ptr orbit_agent_manager::find_client(const std::string& client_id) noexcept {
  auto iter = clients_.find(client_id);
  if (clients_.end() == iter) {
    return nullptr;
  }
  return iter->second;
}

orbit_agent_client_record_ptr orbit_agent_manager::find_client(const std::string& client_id) const noexcept {
  auto iter = clients_.find(client_id);
  if (clients_.end() == iter) {
    return nullptr;
  }
  return iter->second;
}

void orbit_agent_manager::set_client_state(const orbit_agent_client_record_ptr& record,
                                           atfw::orbit::EnClientState state) {
  if (record->state == state) {
    return;
  }
  FWLOGDEBUG("orbit agent client {} state changed: {} -> {}", record->client_id, static_cast<int>(record->state),
             static_cast<int>(state));
  if (record->state == atfw::orbit::EN_CLIENT_STATE_STARTING) {
    batch_startup_count_--;
  }
  if (state == atfw::orbit::EN_CLIENT_STATE_STARTING) {
    batch_startup_count_++;
  }
  if (record->state == atfw::orbit::EN_CLIENT_STATE_STARTING && !record->seed_process) {
    if (state == atfw::orbit::EN_CLIENT_STATE_RUNNING) {
      repeated_startup_failures_ = 0;
    } else {
      ++repeated_startup_failures_;
      if (repeated_startup_failures_ >= 3) {
        FWLOGERROR("orbit agent repeated client startup failures reached {}, exiting process",
                   repeated_startup_failures_);
        agent_fatal_error();
      }
    }
  }
  record->state = state;
}

void orbit_agent_manager::fill_normal_client_start_command(const orbit_agent_client_record& record, uint64_t app_id,
                                                           std::vector<std::string>& output) const {
  output.emplace_back("-id");
  output.emplace_back(std::to_string(app_id));
  output.emplace_back("--orbit-client-id");
  output.emplace_back(record.client_id);
  output.emplace_back("--orbit-agent-endpoint");
  output.emplace_back(agent_endpoint_);

  if (nullptr != owner_app_ && nullptr != owner_app_->get_bus_node()) {
    append_bus_config_env_arguments(owner_app_->get_bus_node()->get_conf(), output);
  }
}

int orbit_agent_manager::prepare_start_client_record(const atfw::orbit::CTAStartClientReq& request,
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

  auto record = atfw::component::memory::stl::make_strong_rc<orbit_agent_client_record>();
  clients_[client_id] = record;
  record->client_id = client_id;
  record->custom_args = request.args().client_start_args().custom_args();
  record->expected_cpu =
      seed_mode_enabled_ ? request.args().resource().seed_cpu() : request.args().resource().normal_cpu();
  record->expected_memory_mb =
      seed_mode_enabled_ ? request.args().resource().seed_memory_mb() : request.args().resource().normal_memory_mb();
  record->startup_timeout_sec = request.args().startup_timeout_sec();
  record->heartbeat_timeout_sec = request.args().heartbeat_timeout_sec();
  record->server_unique_id = request.server_identity().unique_id();

  set_client_state(record, atfw::orbit::EN_CLIENT_STATE_STARTING);
  record->client_addr.clear();

  record->start_timepoint = util::time::time_utility::get_sys_now();
  server_unique_id_to_client_ids_[record->server_unique_id].insert(record->client_id);

  output = record;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

void orbit_agent_manager::fill_client_identity(atfw::orbit::DClientIdentity& output,
                                               const orbit_agent_client_record_ptr& client) const {
  *output.mutable_agent_identity() = agent_identity_;
  output.mutable_client_id()->set_client_id(client->client_id);
}

void orbit_agent_manager::build_client_launch_arguments(
    const orbit_agent_client_record_ptr& record, const std::unordered_map<std::string, std::string>& render_values,
    const std::vector<std::string>& command_line, std::vector<std::string>& output) {
  uint64_t app_id = ++sequence_allocator_;
  output.clear();
  output.reserve(command_line.size() + static_cast<size_t>(record->custom_args.size()) + 6);

  // 渲染启动参数中的 ${field} 占位符，用 render_values 中的实际值替换
  for (const std::string& arg : command_line) {
    output.emplace_back(render_string_template(arg, render_values));
  }
  for (const std::string& custom_arg : record->custom_args) {
    output.emplace_back(render_string_template(custom_arg, render_values));
  }

  fill_normal_client_start_command(*record, app_id, output);
}

void orbit_agent_manager::worker_exit_callback(const atfw::atapp::worker_context& worker_ctx) {
  uint64_t worker_unique_id = worker_ctx.worker_unique_id;
  uv_loop_t* loop_ = nullptr;
  tbb::concurrent_hash_map<uint64_t, uv_loop_t*>::accessor accessor;
  if (orbit_agent_manager::me()->uv_loop_queue_.find(accessor, worker_unique_id)) {
    loop_ = accessor->second;
    uv_stop(loop_);
    uv_loop_close(loop_);
    delete loop_;
    orbit_agent_manager::me()->uv_loop_queue_.erase(accessor);
  }
}

int32_t orbit_agent_manager::spawn_client_async(const std::string& client_id, std::vector<std::string>&& command_line) {
  auto worker_pool = (nullptr != owner_app_) ? owner_app_->get_worker_pool_module() : nullptr;
  if (!worker_pool) {
    FWLOGERROR("orbit agent spawn client {} failed: worker pool module is not available", client_id);
    return atfw::atapp::EN_ATAPP_ERR_WORKER_POOL_CLOSED;
  }

  auto spawn_func = [client_id_copy = client_id, launch_arguments = std::move(command_line),
                     worker_pool](const atfw::atapp::worker_context& worker_ctx) mutable {
    uint64_t worker_unique_id = worker_ctx.worker_unique_id;
    uv_loop_t* loop_ = nullptr;
    tbb::concurrent_hash_map<uint64_t, uv_loop_t*>::accessor accessor;
    if (!orbit_agent_manager::me()->uv_loop_queue_.find(accessor, worker_unique_id)) {
      // 创建流程
      loop_ = new uv_loop_t();
      uv_loop_init(loop_);
      orbit_agent_manager::me()->uv_loop_queue_.emplace(worker_unique_id, loop_);
      worker_pool->add_tick_callback([loop_](const atfw::atapp::worker_context&) { uv_run(loop_, UV_RUN_NOWAIT); },
                                     worker_ctx);
    } else {
      loop_ = accessor->second;
    }

    std::vector<char*> launch_argv;
    launch_argv.reserve(launch_arguments.size() + 1);
    for (std::string& launch_argument : launch_arguments) {
      launch_argv.emplace_back(const_cast<char*>(launch_argument.c_str()));
    }
    launch_argv.emplace_back(nullptr);

    std::string command_line_str;
    for (const auto& arg : launch_arguments) {
      if (!command_line_str.empty()) {
        command_line_str += " ";
      }
      command_line_str += arg;
    }

    auto* process_handle = new uv_process_t();
    std::memset(process_handle, 0, sizeof(*process_handle));

    auto* exit_data = new orbit_agent_process_exit_data();
    exit_data->client_id = client_id_copy;
    process_handle->data = exit_data;

    uv_process_options_t options;
    std::memset(&options, 0, sizeof(options));
    options.file = launch_argv[0];
    options.args = launch_argv.data();
    options.exit_cb = on_uv_process_exit_callback;

    FWLOGINFO("orbit agent spawning client {} by command {}", client_id_copy, command_line_str);
    int uv_result = uv_spawn(loop_, process_handle, &options);

    spawn_completion_t completion;
    completion.client_id = client_id_copy;  // record is not captured in the lambda anymore
    completion.process_handle = process_handle;
    completion.uv_result = uv_result;
    if (uv_result >= 0) {
      completion.process_id = static_cast<int64_t>(uv_process_get_pid(process_handle));
      if (completion.process_id <= 0) {
        completion.process_id = static_cast<int64_t>(process_handle->pid);
      }
      FWLOGINFO("orbit agent started client {} with pid {} by command {}", client_id_copy, completion.process_id,
                command_line_str);
    } else {
      FWLOGERROR("orbit agent start_client failed for {}: {} by command {}", client_id_copy, uv_strerror(uv_result),
                 command_line_str);
    }
    uv_action_t action;
    action.is_spawn_completion_ = true;
    action.spawn_completion_ = std::move(completion);
    orbit_agent_manager::me()->uv_actions_.push(std::move(action));
  };
  return worker_pool->spawn(spawn_func);
}

int orbit_agent_manager::spawn_client_process(const orbit_agent_client_record_ptr& record,
                                              const std::vector<std::string>& command_line) {
  std::vector<std::string> launch_arguments;

  // 渲染启动参数中占位符的取值来源，当前从 record 上取出 client_id
  std::unordered_map<std::string, std::string> render_values;
  render_values.emplace("client_id", record->client_id);
  std::tm tm_local = atfw::util::time::time_utility::get_local_tm(atfw::util::time::time_utility::get_sys_now());
  char buf[64] = {0};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_local);
  render_values.emplace("time", buf);

  build_client_launch_arguments(record, render_values, command_line, launch_arguments);

  int32_t spawn_result = spawn_client_async(record->client_id, std::move(launch_arguments));
  if (spawn_result < 0) {
    FWLOGERROR("orbit agent submit spawn client {} to worker pool failed, res: {}({})", record->client_id, spawn_result,
               protobuf_mini_dumper_get_error_msg(spawn_result));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
  }

  FWLOGINFO("orbit agent submitted spawn client {} to worker pool", record->client_id);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

void orbit_agent_manager::process_uv_actions() {
  uv_action_t action;
  if (!uv_actions_.try_pop(action)) {
    return;
  }

  if (action.is_spawn_completion_) {
    process_spawn_completion(action.spawn_completion_);
  } else {
    process_exit_action(action.process_exit_action_);
  }
}

void orbit_agent_manager::process_spawn_completion(const spawn_completion_t& completion) {
  auto record = find_client(completion.client_id);
  if (nullptr == record) {
    FWLOGERROR("orbit agent spawn completion ignored for {}: record not found", completion.client_id);
    delete reinterpret_cast<orbit_agent_process_exit_data*>(completion.process_handle->data);
    completion.process_handle->data = nullptr;
    uv_close(reinterpret_cast<uv_handle_t*>(completion.process_handle),
             [](uv_handle_t* handle) { delete reinterpret_cast<uv_process_t*>(handle); });
    return;
  }

  record->process_id = completion.process_id;
  record->process_handle = completion.process_handle;
  if (completion.uv_result < 0) {
    delete_client(record);
    async_notify_client_exit(record);
    return;
  }
  // 启动成功
  FWLOGINFO("orbit agent spawn completion for {}: pid={}", record->client_id, record->process_id);
}

void orbit_agent_manager::on_uv_process_exit(uv_process_t* process_handle, int64_t exit_status, int term_signal) {
  uv_action_t action;
  action.is_spawn_completion_ = false;
  action.process_exit_action_ = {.handle_ = process_handle, .exit_status_ = exit_status, .term_signal_ = term_signal};
  uv_actions_.push(std::move(action));
}

void orbit_agent_manager::process_exit_action(const process_exit_action_t& action) {
  auto* data = static_cast<orbit_agent_process_exit_data*>(action.handle_->data);
  auto record = find_client(data->client_id);
  if (!record) {
    // 已移除
    delete data;
    action.handle_->data = nullptr;
    uv_close(reinterpret_cast<uv_handle_t*>(action.handle_),
             [](uv_handle_t* handle) { delete reinterpret_cast<uv_process_t*>(handle); });
    return;
  }
  on_client_process_exit(record, action.exit_status_, action.term_signal_);
}

rpc::result_code_type orbit_agent_manager::spawn_seed_client_process(rpc::context& ctx,
                                                                     orbit_agent_client_record_ptr record) {
  // 种子模式 发送消息给种子进程
  if (nullptr == seed_client_record_) {
    FWLOGERROR("orbit agent spawn_seed_client_process failed: seed_client_record_ is null");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
  }

  uint64_t app_id = ++sequence_allocator_;
  auto req = rpc::make_shared_message<atfw::orbit::ATDForkSeedClientReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::DTAForkSeedClientRsp>(ctx);
  req->set_app_id(app_id);
  req->mutable_start_args()->mutable_client_id()->set_client_id(record->client_id);
  *req->mutable_start_args()->mutable_custom_args() = record->custom_args;

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttoclientservice::fork_seed_client(ctx, seed_client_record_->client_server_id, *req, *rsp));
  if (rpc_result == 0) {
    rpc_result = rsp->error_code();
  }
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent fork_seed_client failed for {} to client node {:#x}, res: {}",
               seed_client_record_->client_id, seed_client_record_->client_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  } else {
    record->process_id = rsp->pid();
  }
  RPC_RETURN_CODE(rsp->error_code());
}

int orbit_agent_manager::kill_client_process(const orbit_agent_client_record_ptr& client_record, int signal_number,
                                             atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code) {
  if (!client_record) {
    return UV_EINVAL;
  }

  int uv_result = UV_ESRCH;
  if (nullptr != client_record->process_handle) {
    uv_result = uv_process_kill(client_record->process_handle, signal_number);
  } else if (client_record->process_id > 0) {
    uv_result = uv_kill(static_cast<int>(client_record->process_id), signal_number);
  }
  client_record->force_kill_timepoint = 0;
  client_record->exit_reason = exit_reason;
  client_record->exit_code = exit_code;

  if (UV_ESRCH == uv_result) {
    FWLOGWARNING("orbit agent kill client {} skipped: process already exited, pid={}, signal={}",
                 client_record->client_id, client_record->process_id, signal_number);
    return uv_result;
  }

  if (uv_result < 0) {
    FWLOGERROR("orbit agent kill client {} failed: pid={}, signal={}, error={}", client_record->client_id,
               client_record->process_id, signal_number, uv_strerror(uv_result));
    return uv_result;
  }

  FWLOGWARNING("orbit agent sent signal {} to client {}, pid={}", signal_number, client_record->client_id,
               client_record->process_id);
  return uv_result;
}

void orbit_agent_manager::stop_client_process(const orbit_agent_client_record_ptr& client_record,
                                              atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code) {
  if (!client_record || client_record->force_kill_timepoint > 0) {
    return;
  }
  // 设置超时时间
  client_record->force_kill_timepoint = util::time::time_utility::get_sys_now() + kDefaultClientForceCleanupDelaySec;
  client_record->exit_reason = exit_reason;
  client_record->exit_code = exit_code;
  set_client_state(client_record, atfw::orbit::EN_CLIENT_STATE_EXITING);
  // 通知退出
  async_notify_client_exit(client_record);
  // 发送stop_client
  if (client_record->client_server_id == 0) {
    // 还没启动成功
    FWLOGWARNING("orbit agent stop_client_process failed for {}: client_server_id is 0", client_record->client_id);
    return;
  }
  auto invoke_result = rpc::async_invoke(
      logic_server_get_current_tick_context(), "async stop_client_process",
      [client_server_id = client_record->client_server_id,
       exit_reason](rpc::context& sub_ctx) mutable -> rpc::result_code_type {
        auto notify_request = rpc::make_shared_message<atfw::orbit::ATDStopClientReq>(sub_ctx);
        notify_request->set_reason(exit_reason);
        RPC_RETURN_CODE(
            RPC_AWAIT_CODE_RESULT(rpc::agenttoclientservice::stop_client(sub_ctx, client_server_id, *notify_request)));
      });
  if (!invoke_result.is_success()) {
    FWLOGERROR("orbit agent failed to spawn async stop_client_process task for {}, res: {}({})",
               client_record->client_id, *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

void orbit_agent_manager::on_client_process_exit(const orbit_agent_client_record_ptr& record, int64_t exit_status,
                                                 int term_signal) {
  if (record->state != atfw::orbit::EN_CLIENT_STATE_EXITING) {
    // Client 未通过 STAClientExitReq 告知退出，视为异常退出
    FWLOGWARNING("orbit agent client {} exited unexpectedly: exit_status={}, term_signal={}, state={}",
                 record->client_id, exit_status, term_signal, static_cast<int>(record->state));
  }

  record->exit_reason = atfw::orbit::EN_CLIENT_EXIT_REASON_CRASH;
  record->exit_code = static_cast<int32_t>(exit_status);
  // 统一删除 这边进程确定退出了
  delete_client(record);
  if (record->seed_process) {
    if (!stoped_) {
      // 种子进程意外退出
      FWLOGERROR("orbit agent seed client exited unexpectedly");
      agent_fatal_error();
    }
    return;
  }
  async_notify_client_exit(record);
}

void orbit_agent_manager::async_notify_client_exit(const orbit_agent_client_record_ptr& record) {
  if (!record) {
    return;
  }
  if (record->notify_client_exit) {
    return;
  }
  record->notify_client_exit = true;

  auto controller_server_id = record->get_controller_server_id();
  if (0 == controller_server_id) {
    FWLOGWARNING("orbit agent process crash for {} but no controller connected, skip notify", record->client_id);
    return;
  }

  auto* server_identity_ptr = find_server_identity(record->server_unique_id);
  if (server_identity_ptr == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               record->client_id, record->server_unique_id);
    return;
  }

  atfw::orbit::DClientIdentity identity;
  fill_client_identity(identity, record);

  auto invoke_result =
      rpc::async_invoke(logic_server_get_current_tick_context(), "orbit_agent_manager.async_notify_client_exit",
                        [controller_server_id, identity = std::move(identity), server_identity = *server_identity_ptr,
                         reason = record->exit_reason,
                         exit_code = record->exit_code](rpc::context& sub_ctx) mutable -> rpc::result_code_type {
                          auto notify_request = rpc::make_shared_message<atfw::orbit::ATCNotifyClientExitReq>(sub_ctx);
                          auto rsp = rpc::make_shared_message<atfw::orbit::CTANotifyClientExitRsp>(sub_ctx);
                          *notify_request->mutable_client_identity() = std::move(identity);
                          *notify_request->mutable_server_identity() = std::move(server_identity);
                          notify_request->set_exit_reason(reason);
                          notify_request->set_exit_code(exit_code);
                          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::agenttocontrollerservice::notify_client_exit(
                              sub_ctx, controller_server_id, *notify_request, *rsp)));
                        });
  if (!invoke_result.is_success()) {
    FWLOGERROR("orbit agent failed to spawn async_notify_client_exit task for {}, res: {}({})", record->client_id,
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type orbit_agent_manager::notify_client_exit(
    rpc::context& ctx, orbit_agent_client_record_ptr client_record, const std::string& custom_data) {
  if (!client_record) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (client_record->notify_client_exit) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  client_record->notify_client_exit = true;

  auto* identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_record->client_id, client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
  }

  auto notify_request = rpc::make_shared_message<atfw::orbit::ATCNotifyClientExitReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::CTANotifyClientExitRsp>(ctx);
  fill_client_identity(*notify_request->mutable_client_identity(), client_record);
  notify_request->set_exit_reason(client_record->exit_reason);
  notify_request->set_custom_data(custom_data);
  notify_request->set_exit_code(client_record->exit_code);
  *notify_request->mutable_server_identity() = *identity;

  auto controller_server_id = client_record->get_controller_server_id();

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::notify_client_exit(ctx, controller_server_id, *notify_request, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent notify_client_exit failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  RPC_RETURN_CODE(rsp->error_code());
}

void orbit_agent_manager::check_client_timeouts(time_t now) {
  struct expire_entry {
    std::string client_id;
    atfw::orbit::EnClientExitReason reason;
  };
  std::vector<expire_entry> expired;

  for (const auto& kv : clients_) {
    const auto& record = kv.second;
    if (!record) {
      continue;
    }
    if (atfw::orbit::EN_CLIENT_STATE_STARTING == record->state) {
      if (record->startup_timeout_sec > 0 && record->start_timepoint > 0 &&
          now >= record->start_timepoint + static_cast<time_t>(record->startup_timeout_sec)) {
        expired.emplace_back(kv.first, atfw::orbit::EN_CLIENT_EXIT_REASON_STARTUP_TIMEOUT);
      }
    } else if (atfw::orbit::EN_CLIENT_STATE_RUNNING == record->state) {
      if (record->heartbeat_timeout_sec > 0 && record->last_heartbeat_timepoint > 0 &&
          now >= record->last_heartbeat_timepoint + static_cast<time_t>(record->heartbeat_timeout_sec)) {
        expired.emplace_back(kv.first, atfw::orbit::EN_CLIENT_EXIT_REASON_HEARTBEAT_TIMEOUT);
      }
    }
  }

  for (auto& entry : expired) {
    auto record = find_client(entry.client_id);
    if (!record) {
      continue;
    }

    FWLOGWARNING(
        "orbit agent client {} timed out: reason={}, state={}", entry.client_id,
        entry.reason == atfw::orbit::EN_CLIENT_EXIT_REASON_STARTUP_TIMEOUT ? "startup_timeout" : "heartbeat_timeout",
        static_cast<int>(record->state));
    if (record->seed_process) {
      // 种子进程意外退出
      FWLOGERROR("orbit agent seed client exited unexpectedly");
      agent_fatal_error();
    }
    // 通知退出
    stop_client_process(record, entry.reason, 0);
  }
}

void orbit_agent_manager::check_client_force_kill(time_t now) {
  std::vector<std::string> expired;
  expired.reserve(clients_.size());

  for (const auto& kv : clients_) {
    const auto& record = kv.second;
    if (!record || record->force_kill_timepoint <= 0 || now < record->force_kill_timepoint) {
      continue;
    }

    expired.emplace_back(kv.first);
  }

  for (const auto& client_id : expired) {
    auto record = find_client(client_id);

    int kill_result = kill_client_process(record, SIGKILL, record->exit_reason, record->exit_code);
    if (kill_result < 0 && UV_ESRCH != kill_result) {
      FWLOGWARNING("orbit agent client {} cleanup kill returned {}, continue cleanup", client_id, kill_result);
    }
    // kill后直接删除
    delete_client(record);
    async_notify_client_exit(record);
  }
}

void orbit_agent_manager::update_etcd_load_snapshot() {
  double cpu_used = 0.0;
  double memory_used_mb = 0.0;
  uint32_t running_client_count = 0;
  uint32_t starting_client_count = 0;

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
    switch (record->state) {
      case atfw::orbit::EN_CLIENT_STATE_STARTING:
        ++starting_client_count;
        break;
      case atfw::orbit::EN_CLIENT_STATE_RUNNING:
      case atfw::orbit::EN_CLIENT_STATE_EXITING:
        ++running_client_count;
        break;
      default:
        break;
    }
    if (record->load_snapshot.cpu_used() > record->expected_cpu) {
      cpu_used += record->load_snapshot.cpu_used();
    } else {
      cpu_used += record->expected_cpu;
    }
    if (record->load_snapshot.memory_used_mb() > record->expected_memory_mb) {
      memory_used_mb += record->load_snapshot.memory_used_mb();
    } else {
      memory_used_mb += record->expected_memory_mb;
    }
  }

  FWLOGDEBUG(
      "orbit agent load snapshot: agent_cpu_used={}, agent_memory_used_mb={}, total_cpu_used={}, "
      "total_memory_used_mb={}, running_client_count={}, "
      "starting_client_count={}, agent_online={}",
      self_cpu_used, self_memory_used_mb, cpu_used, memory_used_mb, running_client_count, starting_client_count,
      agent_online_);

  double esp = 1e-9;

  if (std::abs(load_record_.agent().cpu_used() - cpu_used) > esp ||
      std::abs(load_record_.agent().memory_used_mb() - memory_used_mb) > esp ||
      load_record_.agent().running_client_count() != running_client_count ||
      load_record_.agent().starting_client_count() != starting_client_count ||
      load_record_.agent_online() != agent_online_) {
    load_record_.mutable_agent()->set_cpu_used(cpu_used);
    load_record_.mutable_agent()->set_memory_used_mb(memory_used_mb);
    load_record_.mutable_agent()->set_running_client_count(running_client_count);
    load_record_.mutable_agent()->set_starting_client_count(starting_client_count);
    load_record_.set_agent_online(agent_online_);
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
  if (keepalive_actor_ != nullptr) {
    keepalive_actor_->set_value(load_json_);
  }
}

void orbit_agent_manager::delete_client(const orbit_agent_client_record_ptr& client_record) {
  if (!client_record) {
    return;
  }
  clients_.erase(client_record->client_id);
  set_client_state(client_record, atfw::orbit::EN_CLIENT_STATE_EXITED);
  auto& client_ids = server_unique_id_to_client_ids_[client_record->server_unique_id];
  client_ids.erase(client_record->client_id);
  if (client_ids.empty()) {
    server_unique_id_to_client_ids_.erase(client_record->server_unique_id);
  }
  if (client_record == seed_client_record_) {
    seed_client_record_ = nullptr;
  }
  if (client_record->process_handle != nullptr) {
    delete reinterpret_cast<orbit_agent_process_exit_data*>(client_record->process_handle->data);
    client_record->process_handle->data = nullptr;
    uv_close(reinterpret_cast<uv_handle_t*>(client_record->process_handle),
             [](uv_handle_t* handle) { delete reinterpret_cast<uv_process_t*>(handle); });
    client_record->process_handle = nullptr;
  }
}

double orbit_agent_manager::get_load_value() {
  // TODO(yousongyang): 计算负载系数
  return 1.0f;
}

void orbit_agent_manager::agent_fatal_error() {
  FWLOGERROR("orbit agent fatal error, exiting process");
  agent_online_ = false;
  update_etcd_load_snapshot();
  // TODO(yousongyang): 后续退出流程
}

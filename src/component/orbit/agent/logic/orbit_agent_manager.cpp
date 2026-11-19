// Copyright 2026 atframework

#include "logic/orbit_agent_manager.h"

#include <atframe/modules/worker_pool_module.h>

#include <memory/object_allocator.h>

#include <config/excel/config_easy_api.h>
#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <logic/logic_server_setup.h>
#include <random/uuid_generator.h>
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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr time_t kAgentRunningSummaryLogIntervalSec = 60;
constexpr time_t kDefaultServerIdentityTimeoutSec = 30;
constexpr time_t kDefaultServerIdentityCheckIntervalSec = 5;
constexpr time_t kDefaultClientForceCleanupDelaySec = 5;

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

static bool split_command_line(const std::string& input, std::vector<std::string>& output) {
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

static int64_t get_total_process_cpu_time_us(const uv_rusage_t& usage) {
  int64_t total_us = static_cast<int64_t>(usage.ru_stime.tv_sec) + static_cast<int64_t>(usage.ru_utime.tv_sec);
  total_us *= 1000000;
  total_us += static_cast<int64_t>(usage.ru_stime.tv_usec) + static_cast<int64_t>(usage.ru_utime.tv_usec);
  return total_us;
}

static uint64_t make_initial_sequence_allocator() {
  return static_cast<uint64_t>((atfw::util::time::time_utility::get_sys_now() -
                                PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
                               << 23) +
         static_cast<uint64_t>(atfw::util::time::time_utility::get_now_usec() << 3) +
         static_cast<uint64_t>(logic_config::me()->get_local_server_id());
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

  // Agent 实例唯一标识，随启动参数下发给 Client，用于让 Client 识别 Agent 重启
  agent_instance_id_ = atfw::util::random::uuid_generator::generate_string_random();

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
  {
    if (!split_command_line(config.client_path(), client_command_line_)) {
      FWLOGERROR("split_command_line failed for {}", config.client_path());
      return -3;
    }
    if (client_command_line_.empty()) {
      FWLOGERROR("client_command_line_ is empty after splitting client_path");
      return -3;
    }
    client_path_ = client_command_line_.front();
    client_command_line_.erase(client_command_line_.begin());

    if (client_path_.empty()) {
      FWLOGERROR("orbit agent seed_client_path is empty");
      return -1;
    }

    if (!config.client_command_line().empty()) {
      FWLOGINFO("orbit agent launch command configured: {}", config.client_command_line());
      if (!split_command_line(config.client_command_line(), client_command_line_)) {
        FWLOGERROR("split_command_line failed for {}", config.client_command_line());
        return -2;
      }
    }

    client_command_line_.emplace_back(kOrbitEnabledArg);

    // 启动参数 ./client.exe ... (预设启动参数) + (customed启动参数)
    FWLOGINFO("orbit agent launch path: {}", client_path_);
    for (const auto& arg : client_command_line_) {
      FWLOGINFO("orbit agent launch command argument: {}", arg);
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

  agent_endpoint_ = config.local_agent_addr();

  client_ip_ = config.client_ip();
  log_name_ = config.log_name();
  repeated_startup_failures_fatal_error_ = config.repeated_startup_failures_fatal_error();
  pre_start_repeated_failures_limit_ = config.pre_start_repeated_failures_limit();
  remote_agent_endpoint_ = config.remote_agent_addr();
  enable_pre_start_ = config.enable_pre_start();

  if (agent_endpoint_.empty() || remote_agent_endpoint_.empty()) {
    FWLOGERROR("orbit agent failed to resolve agent_endpoint_ {} or remote_agent_endpoint_ {}", agent_endpoint_,
               remote_agent_endpoint_);
    return -7;
  }

  FWLOGINFO("orbit agent launch client endpoint: {}, remote endpoint: {}", agent_endpoint_, remote_agent_endpoint_);

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

  // 预启动需要读 Excel 配置，tag 没有任何匹配模板时不允许启动
  if (enable_pre_start_) {
    int pre_start_init_result = init_pre_start_templates();
    if (pre_start_init_result != 0) {
      FWLOGERROR("orbit agent failed to init pre start templates, res: {}", pre_start_init_result);
      return pre_start_init_result;
    }
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
    // 初始化负载限制值
    double load_limit = calculate_load_limit();
    if (load_limit <= std::numeric_limits<double>::epsilon()) {
      FWLOGERROR("Calculated load limit is too small");
      return -1;
    }
    load_limit_.store(load_limit);
  }

  {
    logic_server_common_module* common_mod = logic_server_last_common_module();
    if (nullptr != common_mod) {
      auto hpa_controller = common_mod->get_hpa_controller();
      if (!hpa_controller) {
        return -1;
      }
      hpa_controller->set_on_setup_custom_policy(
          "orbit_agent_load_current",
          [local_server_id](logic_hpa_controller&, const std::shared_ptr<logic_hpa_policy>& custom_policy) {
            custom_policy->add_observer_custom(
                logic_hpa_policy::custom_observer_register_type::kDouble,
                [local_server_id](logic_hpa_policy&, logic_hpa_observer& observer) {
                  std::pair<gsl::string_view, opentelemetry::common::AttributeValue> attributes[] = {
                      {"region", orbit_agent_manager::me()->region_},
                      {"tag", orbit_agent_manager::me()->tag_},
                      {"agent_id", local_server_id}};
                  observer.observe(orbit_agent_manager::me()->get_load_value(), attributes);
                });
          });
      hpa_controller->set_on_setup_custom_policy(
          "orbit_agent_load_limit",
          [local_server_id](logic_hpa_controller&, const std::shared_ptr<logic_hpa_policy>& custom_policy) {
            custom_policy->add_observer_custom(
                logic_hpa_policy::custom_observer_register_type::kDouble,
                [local_server_id](logic_hpa_policy&, logic_hpa_observer& observer) {
                  std::pair<gsl::string_view, opentelemetry::common::AttributeValue> attributes[] = {
                      {"region", orbit_agent_manager::me()->region_},
                      {"tag", orbit_agent_manager::me()->tag_},
                      {"agent_id", local_server_id}};
                  observer.observe(orbit_agent_manager::me()->get_load_limit_value(), attributes);
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
  if (keepalive_actor_) {
    etcd_mod_.get_etcd_cluster().remove_keepalive(keepalive_actor_);
    keepalive_actor_ = nullptr;
  }
  if (stoped_) {
    return ret;
  }
  stoped_ = true;

  for (auto iter = clients_.begin(); iter != clients_.end();) {
    auto record = iter->second;
    ++iter;
    if (!record) {
      continue;
    }
    if (record->start_client_sent) {
      // 正在运行的Client
      if (stop_running_client_) {
        FWLOGINFO("orbit agent stop running {}", record);
        stop_client_process(record, atfw::orbit::EN_CLIENT_EXIT_REASON_NORMAL, 0);
      } else {
        FWLOGINFO("orbit agent stop notify client exit {}", record);
        async_notify_client_exit(record);
        delete_client(record);
      }
      continue;
    }
    // 没有发送的统一stop
    FWLOGINFO("orbit agent stop not running {}", record);
    stop_client_process(record, atfw::orbit::EN_CLIENT_EXIT_REASON_NORMAL, 0);
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

  // 按表补齐预启动进程，并周期打印运行情况
  tick_pre_start(now);
  log_agent_running_summary(now);

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
  // 清理所有还在的进程 不需要清理的早已移除
  for (auto& kv : clients_) {
    auto record = kv.second;
    if (!record) {
      continue;
    }
    if (record->process_id <= 0) {
      continue;
    }

    int uv_result = uv_kill(static_cast<int>(record->process_id), SIGKILL);
    if (uv_result < 0 && UV_ESRCH != uv_result) {
      FWLOGWARNING("orbit agent cleanup kill pre start {} failed: pid={}, error={}", record, record->process_id,
                   uv_strerror(uv_result));
    } else {
      FWLOGINFO("orbit agent cleanup kill pre start {}, pid={}", record, record->process_id);
    }
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
  server_heartbeat(request.server_identity());

  const std::string& client_id = request.arg().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent start_client rejected: missing client_id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (find_client_by_client_id(client_id) != nullptr) {
    FWLOGERROR("orbit agent start_client rejected for {}: client already exists", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_ID_ALREADY_EXISTS);
  }

  client_template_t client_template;
  if (!load_client_template(request.arg().client_template_id(), client_template)) {
    FWLOGERROR("orbit agent start_client rejected: client_template_id {} not found",
               request.arg().client_template_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_CLIENT_TEMPLATE_NOT_FOUND);
  }

  if (max_batch_startup_count_ > 0 && batch_startup_count_ >= max_batch_startup_count_) {
    FWLOGWARNING("orbit agent start_client rejected: batch_startup_count={} >= max_batch_startup_count={}",
                 batch_startup_count_, max_batch_startup_count_);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);  // 尝试另一个Agent
  }

  // 检查负载状态
  {
    const double expected_cpu = client_template.expected_cpu;
    const double expected_memory_mb = client_template.expected_memory_mb;
    if (cpu_capacity_ > 0.0 && load_record_.agent().cpu_used() + expected_cpu > cpu_capacity_) {
      FWLOGWARNING(
          "orbit agent start_client rejected (cpu overload): cpu_used={:.2f} + expected={:.2f} > capacity={:.2f}",
          load_record_.agent().cpu_used(), expected_cpu, cpu_capacity_);
      need_update_load_json_ = true;  // 负载记录有变更需要更新JSON以同步到etcd
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);  // 尝试另一个Agent
    }
    if (memory_capacity_mb_ > 0.0 && load_record_.agent().memory_used_mb() + expected_memory_mb > memory_capacity_mb_) {
      FWLOGWARNING(
          "orbit agent start_client rejected (mem overload): mem_used={:.2f} + expected={:.2f} > capacity={:.2f}",
          load_record_.agent().memory_used_mb(), expected_memory_mb, memory_capacity_mb_);
      need_update_load_json_ = true;  // 负载记录有变更需要更新JSON以同步到etcd
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_OVERLOAD);  // 尝试另一个Agent
    }
  }

  bool remote_start = client_template.remote_start;

  // 远端启动的模板不参与预启动（init_pre_start_templates 已把它排除在预启动清单外），
  // 这里再挡一次，避免以后放开过滤时误认领。
  if (!remote_start) {
    // 先看同模板是否有空闲的预启动进程
    orbit_agent_client_record_ptr pre_start_record = find_idle_pre_start_client(client_template.client_template_id);
    if (pre_start_record != nullptr) {
      // 认领客户端
      claime_client(pre_start_record, request);
      fill_client_identity(*response.mutable_client_identity(), pre_start_record);
      // 异步 start_client
      schedule_start_claimed_client(pre_start_record);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
  }

  // 启动Client
  orbit_agent_client_record_ptr client_record = nullptr;
  int prepare_result = prepare_start_client_record(request, client_template, client_record);
  if (prepare_result < 0) {
    delete_client(client_record);
    RPC_RETURN_CODE(prepare_result);
  }
  fill_client_identity(*response.mutable_client_identity(), client_record);
  client_record->remote_start = remote_start;

  if (remote_start) {
    // 远程启动模式 不需要Agent相关的参数
    int32_t spawn_result = RPC_AWAIT_CODE_RESULT(remote_spawn_client_process(ctx, client_record, client_command_line_));
    if (spawn_result < 0) {
      delete_client(client_record);
      RPC_RETURN_CODE(spawn_result);
    }
  } else {
    // 普通模式：Seed 模式下从同模板种子进程 Fork，否则本机拉起
    int32_t spawn_result = RPC_AWAIT_CODE_RESULT(spawn_client_process(ctx, client_record));
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

  auto client_record = find_client_by_client_id(client_id);
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

int32_t orbit_agent_manager::handle_server_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                                     const atfw::orbit::CTAServerHeartbeatReq& request) {
  server_heartbeat(request.server_identity());
  return agent_heartbeat(ctx, controller_server_id, request.server_identity());
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

int32_t orbit_agent_manager::agent_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                             const atfw::orbit::DServerIdentity& server_identity) {
  // 找到这个server_identity对应的client_record 发送心跳
  auto iter = server_unique_id_to_client_ids_.find(server_identity.unique_id());
  if (iter == server_unique_id_to_client_ids_.end() || iter->second.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;  // 没有相关Client，不需要发送心跳
  }

  auto heartbeat_request = rpc::make_shared_message<atfw::orbit::ATCAgentHeartbeatReq>(ctx);
  *heartbeat_request->mutable_server_identity() = server_identity;
  *heartbeat_request->mutable_agent_identity() = agent_identity_;

  for (const auto& client_id : iter->second) {
    // 退出状态不发Heartbeat
    auto client_record = find_client_by_client_id(client_id);
    if (nullptr == client_record) {
      FWLOGERROR("orbit agent agent_heartbeat failed: client_id {} not found in records", client_id);
      continue;
    }
    if (client_record->state == atfw::orbit::EnClientState::EN_CLIENT_STATE_EXITING ||
        client_record->state == atfw::orbit::EnClientState::EN_CLIENT_STATE_EXITED) {
      continue;
    }
    heartbeat_request->add_client_ids()->set_client_id(client_id);
  }

  int32_t rpc_result =
      rpc::agenttocontrollerservice::agent_heartbeat(ctx, controller_server_id, *heartbeat_request).unwrap();
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent agent_heartbeat failed server:{} to controller {:#x}, res: {}", server_identity.unique_id(),
               controller_server_id, rpc_result);
  }
  return rpc_result;
}

const atfw::orbit::DServerIdentity* orbit_agent_manager::find_server_identity(uint64_t server_unique_id) const {
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
  const uint64_t client_instance_id = request.client_instance_id().instance_id();
  if (0 == client_instance_id) {
    FWLOGERROR("orbit agent client_start rejected: missing client_instance_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_instance_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent client_start rejected: client_instance_id {} not found in records", client_instance_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->state != atfw::orbit::EN_CLIENT_STATE_STARTING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected STARTING", client_record,
                 static_cast<int>(client_record->state));
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->remote_start) {
    // 远端使用远端回传的
    client_record->client_addr = atfw::util::string::format("{}:{}", request.client_ip(), request.client_port());
  } else {
    // 本地起的Client 用本地的replace_id拼接
    client_record->client_addr = atfw::util::string::format("{}:{}", client_ip_, request.client_port());
  }
  FWLOGINFO("orbit agent client_start succeeded for {}: client_addr={}", client_record, client_record->client_addr);
  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();
  client_record->client_server_id = client_server_id;

  set_client_state(client_record, atfw::orbit::EN_CLIENT_STATE_RUNNING);
  if (client_record->seed_process) {
    // 种子进程只上报 Ready：不下发 start_client、不通知 Controller。
    // 所有种子都出结果后 Agent 才转为可分配，失败的种子不阻塞上线
    client_record->seed_ready = true;
    resolve_seed_client_startup(client_record, true);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (!client_record->claimed) {
    // 此处不通知 Controller，等被认领后再由 Agent 通知
    FWLOGINFO("orbit agent pre start {} is ready, waiting for claim", client_record);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 自动发送StartClient
  if (!client_record->start_client_sent) {
    schedule_send_start_client(client_record);
  }

  {
    // 发送启动通知
    const auto* identity = find_server_identity(client_record->server_unique_id);
    if (identity == nullptr) {
      FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
                 client_record, client_record->server_unique_id);
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

rpc::result_code_type orbit_agent_manager::handle_client_heartbeat(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                                   const atfw::orbit::DTAClientHeartbeatReq& request,
                                                                   atfw::orbit::ATDClientHeartbeatRsp& response) {
  const uint64_t client_instance_id = request.client_instance_id().instance_id();
  if (0 == client_instance_id) {
    FWLOGERROR("orbit agent client_heartbeat rejected: missing client_instance_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_instance_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent client_heartbeat rejected: client_instance_id {} not found in records", client_instance_id);
    // 回带 Agent 实例标识，Client 据此判断自己已不属于当前 Agent
    response.set_agent_instance_id(agent_instance_id_);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  client_record->load_snapshot = request.snapshot();
  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();

  response.set_agent_instance_id(agent_instance_id_);
  response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_agent_manager::handle_send_to_server(rpc::context& ctx,
                                                                 const atfw::orbit::DTASendToServerReq& request,
                                                                 atfw::orbit::ATDSendToServerRsp& response) {
  const uint64_t client_instance_id = request.client_instance_id().instance_id();
  if (0 == client_instance_id) {
    FWLOGERROR("orbit agent send_to_server rejected: missing client_instance_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_instance_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent send_to_server rejected: client_instance_id {} not found in records", client_instance_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->state != atfw::orbit::EN_CLIENT_STATE_RUNNING) {
    FWLOGWARNING("orbit agent client_start ignored for {}: invalid state {}, expected RUNNING", client_record,
                 static_cast<int>(client_record->state));
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_STATE_INVALID);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  client_record->last_heartbeat_timepoint = util::time::time_utility::get_sys_now();

  const auto* identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               client_record, client_record->server_unique_id);
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
  const uint64_t client_instance_id = request.client_instance_id().instance_id();
  if (0 == client_instance_id) {
    FWLOGERROR("orbit agent client_exit rejected: missing client_instance_id");
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto client_record = find_client(client_instance_id);
  if (nullptr == client_record) {
    FWLOGERROR("orbit agent handle_client_exit rejected: client_instance_id {} not found in records",
               client_instance_id);
    response.set_error_code(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_NOT_FOUND);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->seed_process) {
    if (!stoped_) {
      // 种子进程意外退出
      FWLOGERROR("orbit agent seed client exited unexpectedly");
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->state == atfw::orbit::EN_CLIENT_STATE_EXITED) {
    FWLOGWARNING("orbit agent client_exit ignored for {}: already exited", client_record);
    delete_client(client_record);
    response.set_error_code(RPC_AWAIT_CODE_RESULT(notify_client_exit(ctx, client_record, request.custom_data())));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (client_record->process_handle_main_thread == nullptr) {
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

orbit_agent_client_record_ptr orbit_agent_manager::find_client(uint64_t client_instance_id) noexcept {
  auto iter = clients_.find(client_instance_id);
  if (clients_.end() == iter) {
    return nullptr;
  }
  return iter->second;
}

orbit_agent_client_record_ptr orbit_agent_manager::find_client(uint64_t client_instance_id) const noexcept {
  auto iter = clients_.find(client_instance_id);
  if (clients_.end() == iter) {
    return nullptr;
  }
  return iter->second;
}

orbit_agent_client_record_ptr orbit_agent_manager::find_client_by_client_id(const std::string& client_id) noexcept {
  if (client_id.empty()) {
    return nullptr;
  }

  auto index_iter = client_id_to_instance_id_.find(client_id);
  if (client_id_to_instance_id_.end() == index_iter) {
    return nullptr;
  }
  return find_client(index_iter->second);
}

orbit_agent_client_record_ptr orbit_agent_manager::find_client_by_client_id(
    const std::string& client_id) const noexcept {
  if (client_id.empty()) {
    return nullptr;
  }

  auto index_iter = client_id_to_instance_id_.find(client_id);
  if (client_id_to_instance_id_.end() == index_iter) {
    return nullptr;
  }
  return find_client(index_iter->second);
}

void orbit_agent_manager::set_client_state(const orbit_agent_client_record_ptr& record,
                                           atfw::orbit::EnClientState state) {
  if (record->state == state) {
    return;
  }
  FWLOGDEBUG("orbit agent {} state changed: {} -> {}", record, static_cast<int>(record->state),
             static_cast<int>(state));
  if (record->state == atfw::orbit::EN_CLIENT_STATE_STARTING) {
    batch_startup_count_--;
  }
  if (state == atfw::orbit::EN_CLIENT_STATE_STARTING) {
    batch_startup_count_++;
  }

  // 启动成功与否统一以 start_client_sent 判定：离开前还没有下发过 start_client，
  // 说明这次拉起从未真正启动成功，按启动失败计数（成功计数在置位时结算）
  if (!stoped_ && !record->start_client_sent &&
      (state == atfw::orbit::EN_CLIENT_STATE_EXITING || state == atfw::orbit::EN_CLIENT_STATE_EXITED)) {
    record_client_startup_failure(record);
  }

  record->state = state;
}

void orbit_agent_manager::fill_client_identity(atfw::orbit::DClientIdentity& output,
                                               const orbit_agent_client_record_ptr& client) const {
  *output.mutable_agent_identity() = agent_identity_;
  output.mutable_client_id()->set_client_id(client->client_id);
}

void orbit_agent_manager::async_notify_client_exit(const orbit_agent_client_record_ptr& record) {
  auto invoke_result = rpc::async_invoke(
      logic_server_get_current_tick_context(), "orbit_agent_manager.async_notify_client_exit",
      [record](rpc::context& sub_ctx) mutable -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(orbit_agent_manager::me()->notify_client_exit(sub_ctx, record, "")));
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
  if (!client_record->claimed) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (client_record->notify_client_exit) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  client_record->notify_client_exit = true;

  auto controller_server_id = client_record->get_controller_server_id();
  if (controller_server_id == 0) {
    FWLOGWARNING("orbit agent process exit {} but no controller connected, skip notify", client_record);
  }

  const auto* identity = find_server_identity(client_record->server_unique_id);
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

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::notify_client_exit(ctx, controller_server_id, *notify_request, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit agent notify_client_exit failed for {} to controller {:#x}, res: {}", client_record->client_id,
               controller_server_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  RPC_RETURN_CODE(rsp->error_code());
}

int orbit_agent_manager::init_pre_start_templates() {
  pre_start_template_ids_.clear();
  bool has_matched_template = false;
  const auto& all_templates = excel::get_ExcelOrbitClientTemplate_all_of_client_template_id();
  for (const auto& kv : all_templates) {
    if (kv.second == nullptr) {
      continue;
    }

    // 预启动只按 tag 匹配，不考虑 region
    if (tag_ != kv.second->match_tag()) {
      continue;
    }

    has_matched_template = true;
    // 远端启动的模板不由本机预启动
    if (kv.second->pre_start_count() > 0 && !kv.second->remote_start_client()) {
      pre_start_template_ids_.push_back(kv.second->client_template_id());
    }
  }

  if (!has_matched_template) {
    FWLOGERROR("orbit agent found no client template matching tag={}", tag_);
    return -13;
  }

  FWLOGINFO("orbit agent loaded pre start templates: count={}", pre_start_template_ids_.size());
  return 0;
}

bool orbit_agent_manager::load_client_template(int32_t client_template_id, client_template_t& output) const {
  auto row = excel::get_ExcelOrbitClientTemplate_by_client_template_id(client_template_id);
  if (row == nullptr) {
    return false;
  }

  output.client_template_id = client_template_id;
  if (seed_mode_enabled_) {
    output.expected_cpu = row->expected_seed_cpu();
    output.expected_memory_mb = row->expected_seed_memory_mb();
  } else {
    output.expected_cpu = row->expected_normal_cpu();
    output.expected_memory_mb = row->expected_normal_memory_mb();
  }
  output.startup_timeout_sec = row->startup_timeout_sec();
  output.heartbeat_timeout_sec = row->heartbeat_timeout_sec();
  output.pre_start_count = row->pre_start_count();
  output.remote_start = row->remote_start_client();
  output.seed_mode = row->seed_mode();
  output.match_tag = row->match_tag();
  output.launch_args.assign(row->launch_args().begin(), row->launch_args().end());
  return true;
}

orbit_agent_client_record_ptr orbit_agent_manager::find_idle_pre_start_client(int32_t client_template_id) noexcept {
  orbit_agent_client_record_ptr selected = nullptr;
  for (const auto& kv : clients_) {
    const auto& record = kv.second;
    if (!record || record->claimed || record->seed_process) {
      continue;
    }

    if (record->client_template_id != client_template_id || record->state != atfw::orbit::EN_CLIENT_STATE_RUNNING ||
        0 == record->client_server_id) {
      continue;
    }

    // 先安排拉起最早的进程
    if (selected == nullptr || record->start_timepoint < selected->start_timepoint) {
      selected = record;
    }
  }
  return selected;
}

void orbit_agent_manager::schedule_start_claimed_client(const orbit_agent_client_record_ptr& client_record) {
  if (!client_record) {
    return;
  }

  auto invoke_result = rpc::async_invoke(
      logic_server_get_current_tick_context(), "orbit_agent_manager.start_claimed_client",
      [client_record](rpc::context& sub_ctx) mutable -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(orbit_agent_manager::me()->start_claimed_client(sub_ctx, client_record)));
      });
  if (!invoke_result.is_success()) {
    FWLOGERROR("orbit agent failed to schedule start_claimed_client for {}, res: {}({})", client_record,
               *invoke_result.get_error(), protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

void orbit_agent_manager::claime_client(const orbit_agent_client_record_ptr& client_record,
                                        const atfw::orbit::CTAStartClientReq& request) {
  if (!client_record) {
    return;
  }

  // 填充真实client id
  client_record->claimed = true;
  client_record->client_id = request.arg().client_id();
  client_record->server_unique_id = request.server_identity().unique_id();

  client_id_to_instance_id_[client_record->client_id] = client_record->client_instance_id;
  server_unique_id_to_client_ids_[client_record->server_unique_id].insert(client_record->client_id);
}

void orbit_agent_manager::schedule_send_start_client(const orbit_agent_client_record_ptr& client_record) {
  if (!client_record) {
    return;
  }

  auto invoke_result = rpc::async_invoke(
      logic_server_get_current_tick_context(), "orbit_agent_manager.send_start_client",
      [client_record](rpc::context& sub_ctx) mutable -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(orbit_agent_manager::me()->send_start_client(sub_ctx, client_record)));
      });
  if (!invoke_result.is_success()) {
    FWLOGERROR("orbit agent failed to schedule send_start {}, res: {}({})", client_record, *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

void orbit_agent_manager::record_client_startup_failure(const orbit_agent_client_record_ptr& client_record) {
  if (!client_record) {
    return;
  }
  if (client_record->startup_failure_counted) {
    return;
  }
  client_record->startup_failure_counted = true;
  if (client_record->seed_process) {
    return;
  }

  if (client_record->pre_start && !client_record->claimed) {
    // 预启动是 Agent 主动拉起的，失败不能走 agent_fatal_error（否则会把整个 Agent 置为不可用），
    // 连续失败到上限只停掉该模板的预启动；上限为 0 时不限制
    uint32_t& failures = pre_start_repeated_failures_[client_record->client_template_id];
    ++failures;
    FWLOGWARNING("orbit agent pre start {} for client_template_id={} failed {} time(s)", client_record,
                 client_record->client_template_id, failures);
    if (pre_start_repeated_failures_limit_ > 0 && failures >= pre_start_repeated_failures_limit_) {
      disable_pre_start_template(client_record->client_template_id);
    }
    return;
  }

  ++repeated_startup_failures_;
  FWLOGWARNING("orbit agent {} exited before start_client sent, startup failed {} time(s) in a row", client_record,
               repeated_startup_failures_);
  if (repeated_startup_failures_fatal_error_ > 0 &&
      repeated_startup_failures_ > repeated_startup_failures_fatal_error_) {
    FWLOGERROR("orbit agent repeated client startup failures reached {}, exiting process", repeated_startup_failures_);
    agent_fatal_error();
  }
}

void orbit_agent_manager::record_client_startup_success(const orbit_agent_client_record_ptr& client_record) {
  if (!client_record) {
    return;
  }
  if (client_record->seed_process) {
    return;
  }
  repeated_startup_failures_ = 0;
  if (client_record->pre_start) {
    pre_start_repeated_failures_[client_record->client_template_id] = 0;
  }
}

rpc::result_code_type orbit_agent_manager::start_claimed_client(rpc::context& ctx,
                                                                orbit_agent_client_record_ptr client_record) {
  if (!client_record) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  int32_t send_result = RPC_AWAIT_CODE_RESULT(send_start_client(ctx, client_record));
  if (send_result < 0) {
    RPC_RETURN_CODE(send_result);
  }

  FWLOGINFO("orbit agent {} claimed", client_record);
  const auto* identity = find_server_identity(client_record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent notify_client_started failed for {}: server_unique_id {:#x} not found", client_record,
               client_record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
  }

  auto notify_request = rpc::make_shared_message<atfw::orbit::ATCNotifyClientStartedReq>(ctx);
  auto notify_rsp = rpc::make_shared_message<atfw::orbit::CTANotifyClientStartedRsp>(ctx);
  fill_client_identity(*notify_request->mutable_client_identity(), client_record);
  notify_request->set_client_addr(client_record->client_addr);
  *notify_request->mutable_server_identity() = *identity;

  auto controller_server_id = client_record->get_controller_server_id();
  int32_t notify_result = RPC_AWAIT_CODE_RESULT(
      rpc::agenttocontrollerservice::notify_client_started(ctx, controller_server_id, *notify_request, *notify_rsp));
  if (notify_result < 0) {
    FWLOGERROR("orbit agent notify_client_started failed for {} to controller {:#x}, res: {}", client_record,
               controller_server_id, notify_result);
    RPC_RETURN_CODE(notify_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

void orbit_agent_manager::disable_pre_start_template(int32_t client_template_id) {
  auto iter = std::find(pre_start_template_ids_.begin(), pre_start_template_ids_.end(), client_template_id);
  if (pre_start_template_ids_.end() == iter) {
    return;
  }

  pre_start_template_ids_.erase(iter);
  pre_start_repeated_failures_.erase(client_template_id);
  FWLOGERROR(
      "orbit agent disabled pre start for client_template_id={} after {} consecutive startup failures, "
      "remaining pre start template count={}",
      client_template_id, pre_start_repeated_failures_limit_, pre_start_template_ids_.size());
}

void orbit_agent_manager::tick_pre_start(time_t now) {
  if (!enable_pre_start_ || !agent_online_ || pre_start_template_ids_.empty()) {
    return;
  }

  // 按索引遍历：拉起失败时可能触发 disable_pre_start_template 删除元素，用迭代器会失效
  for (size_t template_index = 0; template_index < pre_start_template_ids_.size(); ++template_index) {
    int32_t client_template_id = pre_start_template_ids_[template_index];
    client_template_t client_template;
    if (!load_client_template(client_template_id, client_template) || 0 == client_template.pre_start_count) {
      continue;
    }

    uint32_t idle_count = 0;
    uint32_t starting_count = 0;
    for (const auto& kv : clients_) {
      const auto& record = kv.second;
      if (!record || record->seed_process || record->claimed || record->client_template_id != client_template_id) {
        continue;
      }

      if (atfw::orbit::EN_CLIENT_STATE_STARTING == record->state) {
        ++starting_count;
      } else if (atfw::orbit::EN_CLIENT_STATE_RUNNING == record->state) {
        ++idle_count;
      }
    }

    if (idle_count + starting_count >= client_template.pre_start_count) {
      continue;
    }

    // 优先级：实际负载 > max_batch > 预启动；容量不足时不补齐，也不重复打日志
    if (max_batch_startup_count_ > 0 && batch_startup_count_ >= max_batch_startup_count_) {
      return;
    }
    if (cpu_capacity_ > 0.0 && load_record_.agent().cpu_used() + client_template.expected_cpu > cpu_capacity_) {
      return;
    }
    if (memory_capacity_mb_ > 0.0 &&
        load_record_.agent().memory_used_mb() + client_template.expected_memory_mb > memory_capacity_mb_) {
      return;
    }

    auto record = atfw::component::memory::stl::make_strong_rc<orbit_agent_client_record>();
    record->client_instance_id = ++sequence_allocator_;
    record->client_id = atfw::util::log::format("prestart_{}_{}", record->client_instance_id,
                                                logic_config::me()->get_local_server_id());
    record->client_template_id = client_template.client_template_id;
    record->remote_start = false;
    record->pre_start = true;
    record->claimed = false;
    record->expected_cpu = client_template.expected_cpu;
    record->expected_memory_mb = client_template.expected_memory_mb;
    record->startup_timeout_sec = client_template.startup_timeout_sec > 0 ? client_template.startup_timeout_sec : 120;
    record->heartbeat_timeout_sec =
        client_template.heartbeat_timeout_sec > 0 ? client_template.heartbeat_timeout_sec : 60;
    record->custom_args.Clear();
    for (const std::string& launch_arg : client_template.launch_args) {
      *record->custom_args.Add() = launch_arg;
    }
    record->start_timepoint = now;
    set_client_state(record, atfw::orbit::EN_CLIENT_STATE_STARTING);
    record->client_addr.clear();
    clients_[record->client_instance_id] = record;

    // 异步拉起：Seed 模式下要走 Fork 的 RPC，不能在这里同步等待结果
    schedule_spawn_client_process(record);

    FWLOGINFO("orbit agent pre start {} scheduled for template {}, idle={}, starting={}", record,
              client_template.client_template_id, idle_count, starting_count);
  }
}

void orbit_agent_manager::log_agent_running_summary(time_t now) {
  if (now - last_summary_log_timepoint_ < kAgentRunningSummaryLogIntervalSec) {
    return;
  }
  last_summary_log_timepoint_ = now;

  if (!enable_pre_start_) {
    return;
  }

  std::string summary;
  for (int32_t client_template_id : pre_start_template_ids_) {
    uint32_t idle_count = 0;
    uint32_t starting_count = 0;
    uint32_t claimed_count = 0;
    for (const auto& kv : clients_) {
      const auto& record = kv.second;
      if (!record || record->seed_process || record->client_template_id != client_template_id) {
        continue;
      }

      if (record->start_client_sent) {
        ++claimed_count;
      } else if (atfw::orbit::EN_CLIENT_STATE_STARTING == record->state) {
        ++starting_count;
      } else if (atfw::orbit::EN_CLIENT_STATE_RUNNING == record->state) {
        ++idle_count;
      }
    }

    if (!summary.empty()) {
      summary += ", ";
    }
    summary += atfw::util::log::format("template {}: idle={}, starting={}, claimed={}", client_template_id, idle_count,
                                       starting_count, claimed_count);
  }

  FWLOGINFO("orbit agent running summary: clients={}, pre_start=[{}]", clients_.size(), summary);
}

void orbit_agent_manager::check_client_timeouts(time_t now) {
  struct expire_entry {
    uint64_t client_instance_id = 0;
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
    auto record = find_client(entry.client_instance_id);
    if (!record) {
      continue;
    }

    FWLOGWARNING(
        "orbit agent {} timed out: reason={}, state={}", record,
        entry.reason == atfw::orbit::EN_CLIENT_EXIT_REASON_STARTUP_TIMEOUT ? "startup_timeout" : "heartbeat_timeout",
        static_cast<int>(record->state));
    if (record->seed_process) {
      if (record->seed_ready) {
        // 已作为 Fork 源的种子进程失去响应，本 Agent 无法再拉起新 Client
        FWLOGERROR("orbit agent seed client {} is not responding", record);
        agent_fatal_error();
      } else {
        // 启动阶段的种子已确定失败，先结算结果，不必等进程真正退出再上线
        resolve_seed_client_startup(record, false);
      }
    }
    // 通知退出
    stop_client_process(record, entry.reason, 0);
  }
}

void orbit_agent_manager::check_client_force_kill(time_t now) {
  std::vector<uint64_t> expired;
  expired.reserve(clients_.size());

  for (const auto& kv : clients_) {
    const auto& record = kv.second;
    if (!record || record->force_kill_timepoint <= 0 || now < record->force_kill_timepoint) {
      continue;
    }

    expired.emplace_back(kv.first);
  }

  for (uint64_t client_instance_id : expired) {
    auto record = find_client(client_instance_id);

    int kill_result = kill_client_process(record, SIGKILL, record->exit_reason, record->exit_code);
    if (kill_result < 0 && UV_ESRCH != kill_result) {
      FWLOGWARNING("orbit agent client {} cleanup kill returned {}, continue cleanup", client_instance_id, kill_result);
    }
    // kill后直接删除
    async_notify_client_exit(record);
    delete_client(record);
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

  // 各模板的预启动进程数量：idle 为已 ready 未被认领，starting 为已拉起但未 ready
  std::map<int32_t, std::pair<uint32_t, uint32_t>> pre_start_counts;

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

    if (!record->seed_process && !record->claimed) {
      auto& counts = pre_start_counts[record->client_template_id];
      if (atfw::orbit::EN_CLIENT_STATE_STARTING == record->state) {
        ++counts.second;
      } else if (atfw::orbit::EN_CLIENT_STATE_RUNNING == record->state) {
        ++counts.first;
      }
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

  // 预启动明细按模板 id 升序排列，便于与上次上报的内容比较
  bool pre_start_changed = static_cast<int>(pre_start_counts.size()) != load_record_.agent().pre_start_templates_size();
  if (!pre_start_changed) {
    size_t index = 0;
    for (const auto& kv : pre_start_counts) {
      const auto& reported = load_record_.agent().pre_start_templates(static_cast<int>(index));
      if (reported.client_template_id() != kv.first || reported.idle_count() != kv.second.first ||
          reported.starting_count() != kv.second.second) {
        pre_start_changed = true;
        break;
      }
      ++index;
    }
  }

  if (std::abs(load_record_.agent().cpu_used() - cpu_used) > esp ||
      std::abs(load_record_.agent().memory_used_mb() - memory_used_mb) > esp ||
      load_record_.agent().running_client_count() != running_client_count ||
      load_record_.agent().starting_client_count() != starting_client_count ||
      load_record_.agent_online() != agent_online_ || pre_start_changed) {
    load_record_.mutable_agent()->set_cpu_used(cpu_used);
    load_record_.mutable_agent()->set_memory_used_mb(memory_used_mb);
    load_record_.mutable_agent()->set_running_client_count(running_client_count);
    load_record_.mutable_agent()->set_starting_client_count(starting_client_count);
    load_record_.mutable_agent()->clear_pre_start_templates();
    for (const auto& kv : pre_start_counts) {
      auto* pre_start_snapshot = load_record_.mutable_agent()->add_pre_start_templates();
      pre_start_snapshot->set_client_template_id(kv.first);
      pre_start_snapshot->set_idle_count(kv.second.first);
      pre_start_snapshot->set_starting_count(kv.second.second);
    }
    load_record_.set_agent_online(agent_online_);
    dirty_load_record_ = true;
  }

  double cpu_load = load_record_.agent().cpu_used() / cpu_capacity_;
  double memory_load = load_record_.agent().memory_used_mb() / memory_capacity_mb_;
  double load_value = std::max(cpu_load, memory_load);
  FWLOGDEBUG("CPU load: {}, Memory load: {}, Load value: {}", cpu_load, memory_load, load_value);
  load_value = std::min(load_value, 1.0);
  load_value = load_limit_.load() * load_value;
  FWLOGDEBUG("Load value after applying load limit: {}", load_value);
  load_usage_.store(load_value);
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
  clients_.erase(client_record->client_instance_id);
  if (client_record->seed_process) {
    // 种子进程退出：移出种子池，保证后续不会再被当作 Fork 源；
    // 尚未结算的启动结果按失败处理，避免 Agent 因为一个种子起不来而永远不上线
    auto seed_iter = seed_clients_.find(client_record->client_template_id);
    if (seed_clients_.end() != seed_iter && seed_iter->second == client_record) {
      seed_clients_.erase(seed_iter);
    }
    resolve_seed_client_startup(client_record, false);
  }
  if (!client_record->client_id.empty()) {
    auto index_iter = client_id_to_instance_id_.find(client_record->client_id);
    if (client_id_to_instance_id_.end() != index_iter && index_iter->second == client_record->client_instance_id) {
      client_id_to_instance_id_.erase(index_iter);
    }
  }
  set_client_state(client_record, atfw::orbit::EN_CLIENT_STATE_EXITED);
  auto& client_ids = server_unique_id_to_client_ids_[client_record->server_unique_id];
  client_ids.erase(client_record->client_id);
  if (client_ids.empty()) {
    server_unique_id_to_client_ids_.erase(client_record->server_unique_id);
  }
  client_record->process_handle_main_thread = nullptr;
  FWLOGINFO("Deleted client: {}", client_record->client_id.c_str());
}

// 计算负载系数
// 负载分为两个数据 一个上限与当前值
double orbit_agent_manager::get_load_value() {
  // 这边给出的是当前值
  // 具体含义为启动的Client数量 如果单个Client超过预设的CPU或内存,取实际值
  // 如果大量的Client超过预设的CPU或内存,可能会出现负载低于最大值1.0,但是已经无法启动Client了
  // Agent预设的CPU和内存容量应该小于Pod的容量，用于应对运行时的Client负载波动,可以是可用数据的80%左右
  return load_usage_.load();
}

double orbit_agent_manager::get_load_limit_value() {
  // 这边给出的是上限值
  return load_limit_.load();
}

double orbit_agent_manager::calculate_load_limit() {
  // 对于混合部署的情况 各个Pod的上限可能不同 但是这个上限值在运行时不变
  // 由CPU与内存的预设上限决定 CPU与内存的给出的值相同
  // 1.计算Agent匹配的Tag Client
  std::vector<std::pair<
      excel::excel_config_type_traits::shared_ptr<const PROJECT_NAMESPACE_ID::config::ExcelOrbitClientTemplate>,
      uint32_t>>
      match_template;
  const auto& all_templates = excel::get_ExcelOrbitClientTemplate_all_of_client_template_id();
  uint32_t total_load_percentage = 0;
  for (const auto& kv : all_templates) {
    if (kv.second == nullptr) {
      continue;
    }
    if (tag_ != kv.second->match_tag()) {
      continue;
    }
    match_template.push_back(std::make_pair(kv.second, kv.second->load_percentage()));
    total_load_percentage += kv.second->load_percentage();
  }
  // 2.根据占比与负载计算平均CPU与内存
  double load_cpu = 0;
  double load_memory = 0;
  for (const auto& client_template : match_template) {
    double load_percentage = 0.0f;
    if (total_load_percentage == 0) {
      // 全部都为0 百分比为平均
      load_percentage = 1.0f / static_cast<double>(match_template.size());
    } else {
      // 根据占比计算CPU与内存
      load_percentage = static_cast<double>(client_template.second) / total_load_percentage;
    }
    if (load_percentage <= std::numeric_limits<double>::epsilon()) {
      continue;
    }
    if (seed_mode_enabled_) {
      load_cpu += load_percentage * client_template.first->expected_seed_cpu();
      load_memory += load_percentage * client_template.first->expected_seed_memory_mb();
    } else {
      load_cpu += load_percentage * client_template.first->expected_normal_cpu();
      load_memory += load_percentage * client_template.first->expected_normal_memory_mb();
    }
  }
  // 3.平均负载
  FWLOGDEBUG("Average load CPU: {}, Average load Memory: {}", load_cpu, load_memory);
  // 4.计算CPU与内存的负载个数
  if (load_cpu <= std::numeric_limits<double>::epsilon() || load_memory <= std::numeric_limits<double>::epsilon()) {
    FWLOGERROR("Load CPU or Memory is too small, cannot calculate load count");
    return 0.0f;
  }
  double cpu_load_count = cpu_capacity_ / load_cpu;
  double memory_load_count = memory_capacity_mb_ / load_memory;
  FWLOGDEBUG("CPU load count: {}, Memory load count: {}", cpu_load_count, memory_load_count);
  return std::min(cpu_load_count, memory_load_count);
}

void orbit_agent_manager::agent_fatal_error() {
  FWLOGERROR("orbit agent fatal error, exiting process");
  agent_online_ = false;
  update_etcd_load_snapshot();
  // TODO(yousongyang): 后续退出流程
}

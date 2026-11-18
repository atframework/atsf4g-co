// Copyright 2026 atframework

#include "logic/orbit_agent_manager.h"

#include <rpc/agenttoclientservice/agenttoclientservice.atfw.gen.h>
#include <rpc/agenttocontrollerservice/agenttocontrollerservice.atfw.gen.h>

#include <logic/logic_server_setup.h>
#include <rpc/rpc_shared_message.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr time_t kDefaultClientForceCleanupDelaySec = 5;

constexpr const char* kOrbitArgsConfigEnvPrefix = "--config_env";
constexpr const char* kOrbitStartTimeout = "--start_timeout";
constexpr const char* kOrbitClientInstanceIdArg = "--orbit-client-instance-id";
constexpr const char* kOrbitAgentInstanceIdArg = "--orbit-agent-instance-id";
constexpr const char* kAtappMetadataScopeEnvKey = "ATAPP_METADATA_SCOPE";
constexpr const char* kOrbitSeedMode = "--seed_mode";
constexpr const char* kStartClientCustomDataLogName = "log_name";

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

static void append_config_env_line(std::vector<std::string>& output, const char* key, uint64_t value) {
  append_config_env_line(output, key, std::to_string(value));
}

static void append_bus_config_env_arguments(const atbus::node::conf_t& bus_conf, std::vector<std::string>& output) {
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

// 将 atapp metadata 中需要继承给子进程的配置打入环境变量（与 atapp_conf 的 env 映射一致）
static void append_atapp_metadata_env_arguments(const atapp::protocol::atapp_metadata& metadata,
                                                std::vector<std::string>& output) {
  if (!metadata.scope().empty()) {
    append_config_env_line(output, kAtappMetadataScopeEnvKey, metadata.scope());
  }
}

struct orbit_agent_process_exit_data {
  uint64_t client_instance_id = 0;
  uint64_t worker_unique_id = 0;
};

static void on_uv_process_exit_callback(uv_process_t* handle, int64_t exit_status, int term_signal) {
  orbit_agent_manager::me()->on_uv_process_exit(handle, exit_status, term_signal);
}

}  // namespace

int orbit_agent_manager::startup_seed_client() {
  // 遍历表 找到需要SeedMode的客户端
  // TODO(yousongyang)
  // std::string client_id = atfw::util::log::format("seed_client_{}_{}", logic_config::me()->get_local_server_id(),
  //                                                 util::time::time_utility::get_sys_now());
  // auto record = atfw::component::memory::stl::make_strong_rc<orbit_agent_client_record>();
  // record->client_instance_id = ++sequence_allocator_;
  // record->client_id = client_id;
  // clients_[record->client_instance_id] = record;
  // record->seed_process = true;
  // // record->startup_timeout_sec = seed_startup_timeout_sec_;
  // // record->heartbeat_timeout_sec = seed_heartbeat_timeout_sec_;

  // set_client_state(record, atfw::orbit::EN_CLIENT_STATE_STARTING);
  // record->client_addr.clear();

  // return spawn_client_process(logic_server_get_current_tick_context(), record, true);
  return 0;
}

void orbit_agent_manager::fill_normal_client_start_command(const orbit_agent_client_record& record, uint64_t app_id,
                                                           std::vector<std::string>& output, bool remote_start) const {
  output.emplace_back("-id");
  output.emplace_back(std::to_string(app_id));
  output.emplace_back("--orbit-client-id");
  output.emplace_back(record.client_id);
  output.emplace_back("--orbit-agent-endpoint");
  if (remote_start) {
    output.emplace_back(remote_agent_endpoint_);
  } else {
    output.emplace_back(agent_endpoint_);
  }

  if (nullptr != owner_app_ && nullptr != owner_app_->get_bus_node()) {
    append_bus_config_env_arguments(owner_app_->get_bus_node()->get_conf(), output);
  }
  // 非远端启动时，把本进程的 atapp metadata.scope 一并传给子进程，保持作用域一致；
  // 远端启动的进程不在本机拉起，不使用本机的 scope
  if (!remote_start && nullptr != owner_app_) {
    append_atapp_metadata_env_arguments(owner_app_->get_metadata(), output);
  }
}

int orbit_agent_manager::prepare_start_client_record(const atfw::orbit::CTAStartClientReq& request,
                                                     const client_template_t& client_template,
                                                     orbit_agent_client_record_ptr& output) {
  const std::string& client_id = request.arg().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit agent start_client rejected: missing client_id");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (find_client_by_client_id(client_id) != nullptr) {
    FWLOGWARNING("orbit agent start_client rejected for {}: already active", client_id);
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_CLIENT_ID_ALREADY_EXISTS;
  }

  auto record = atfw::component::memory::stl::make_strong_rc<orbit_agent_client_record>();
  record->client_instance_id = ++sequence_allocator_;
  record->client_id = client_id;
  record->client_template_id = client_template.client_template_id;
  clients_[record->client_instance_id] = record;
  client_id_to_instance_id_[client_id] = record->client_instance_id;

  record->custom_args.Clear();
  for (const std::string& launch_arg : client_template.launch_args) {
    *record->custom_args.Add() = launch_arg;
  }

  record->claimed = true;
  record->pre_start = false;
  record->expected_cpu = client_template.expected_cpu;
  record->expected_memory_mb = client_template.expected_memory_mb;
  record->startup_timeout_sec = client_template.startup_timeout_sec;
  record->heartbeat_timeout_sec = client_template.heartbeat_timeout_sec;
  if (record->startup_timeout_sec <= 0) {
    record->startup_timeout_sec = 120;  // 默认120秒保底
  }
  if (record->heartbeat_timeout_sec <= 0) {
    record->heartbeat_timeout_sec = 60;  // 默认60秒保底
  }
  record->server_unique_id = request.server_identity().unique_id();

  set_client_state(record, atfw::orbit::EN_CLIENT_STATE_STARTING);
  record->client_addr.clear();

  record->start_timepoint = util::time::time_utility::get_sys_now();
  server_unique_id_to_client_ids_[record->server_unique_id].insert(record->client_id);

  output = record;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

void orbit_agent_manager::build_client_launch_arguments(
    const orbit_agent_client_record_ptr& record, const std::unordered_map<std::string, std::string>& render_values,
    const std::string& client_path, const std::vector<std::string>& command_line, std::vector<std::string>& output,
    bool remote_start) {
  uint64_t app_id = ++sequence_allocator_;
  output.clear();

  if (!client_path.empty()) {
    output.emplace_back(client_path);
  }
  // 渲染启动参数中的 ${field} 占位符，用 render_values 中的实际值替换
  for (const std::string& arg : command_line) {
    output.emplace_back(render_string_template(arg, render_values));
  }
  for (const std::string& custom_arg : record->custom_args) {
    output.emplace_back(render_string_template(custom_arg, render_values));
  }
  if (record->startup_timeout_sec > 0) {
    output.emplace_back(kOrbitStartTimeout);
    output.emplace_back(std::to_string(record->startup_timeout_sec));
  }
  // 通信标识与 Agent 实例标识：Client 用它们与 Agent 通信，并识别 Agent 是否已重启
  output.emplace_back(kOrbitClientInstanceIdArg);
  output.emplace_back(std::to_string(record->client_instance_id));
  output.emplace_back(kOrbitAgentInstanceIdArg);
  output.emplace_back(agent_instance_id_);
  fill_normal_client_start_command(*record, app_id, output, remote_start);
}

void orbit_agent_manager::delete_uv_process_handle(uv_process_t* process_handle) {
  if (process_handle) {
    auto* data = static_cast<orbit_agent_process_exit_data*>(process_handle->data);
    delete data;
    process_handle->data = nullptr;
    uv_close(reinterpret_cast<uv_handle_t*>(process_handle),
             [](uv_handle_t* handle) { delete reinterpret_cast<uv_process_t*>(handle); });
  }
}

void orbit_agent_manager::worker_exit_callback(const atfw::atapp::worker_context& worker_ctx) {
  uint64_t worker_unique_id = worker_ctx.worker_unique_id;
  tbb::concurrent_hash_map<uint64_t, std::shared_ptr<uv_loop_data>>::accessor accessor;
  if (!orbit_agent_manager::me()->uv_loop_queue_.find(accessor, worker_unique_id)) {
    return;
  }

  uv_loop_t* loop_ = accessor->second->loop_;
  const size_t closing_handle_count = accessor->second->process_handles_.size();
  // 关闭所有还未退出的子进程。uv_close 只是登记关闭，进程句柄的内存在 close 回调里才释放
  for (uv_process_t* process_handle : accessor->second->process_handles_) {
    delete_uv_process_handle(process_handle);
  }
  accessor->second->process_handles_.clear();

  // 不能调用 uv_stop：它会把 stop_flag 置位，导致后面的 uv_run 一次迭代都不执行，
  // close 回调永远不跑。所有 handle 都已 uv_close，这个 loop 也没有其它 handle 或 request，
  // 跑一轮 UV_RUN_DEFAULT 执行完 close 回调后会立刻返回，不会阻塞等待子进程退出
  uv_run(loop_, UV_RUN_DEFAULT);

  int close_result = uv_loop_close(loop_);
  if (close_result < 0) {
    // 还有 handle 没释放，此时 delete loop_ 会让残留 handle 指向已释放的内存，
    // worker 即将退出，记录后放弃释放这一份资源
    FWLOGERROR(
        "orbit agent worker {} exit: uv_loop_close failed with {}, {} process handle(s) not released, keep loop "
        "allocated",
        worker_unique_id, uv_strerror(close_result), closing_handle_count);
  } else {
    delete loop_;
  }
  orbit_agent_manager::me()->uv_loop_queue_.erase(accessor);
}

void orbit_agent_manager::worker_tick_callback(const atfw::atapp::worker_context& worker_ctx) {
  // 处理need_kill进程
  uv_loop_t* loop_ = nullptr;
  {
    tbb::concurrent_hash_map<uint64_t, std::shared_ptr<uv_loop_data>>::accessor tick_accessor;
    if (orbit_agent_manager::me()->uv_loop_queue_.find(tick_accessor, worker_ctx.worker_unique_id)) {
      loop_ = tick_accessor->second->loop_;
      for (uv_process_t* process_handle : tick_accessor->second->process_handles_) {
        tbb::concurrent_hash_map<uv_process_t*, int>::accessor kill_process_accessor;
        if (orbit_agent_manager::me()->need_kill_process_.find(kill_process_accessor, process_handle)) {
          // 需要kill的进程
          FWLOGINFO("orbit agent killing process pid={} for client", process_handle->pid);
          uv_process_kill(process_handle, kill_process_accessor->second);
          orbit_agent_manager::me()->need_kill_process_.erase(kill_process_accessor);
        }
      }
    }
  }
  if (loop_) {
    uv_run(loop_, UV_RUN_NOWAIT);
  }
}

int32_t orbit_agent_manager::spawn_client_async(uint64_t client_instance_id, std::vector<std::string>&& command_line,
                                                bool detached) {
  auto worker_pool = (nullptr != owner_app_) ? owner_app_->get_worker_pool_module() : nullptr;
  if (!worker_pool) {
    FWLOGERROR("orbit agent spawn client {} failed: worker pool module is not available", client_instance_id);
    return atfw::atapp::EN_ATAPP_ERR_WORKER_POOL_CLOSED;
  }

  auto spawn_func = [client_instance_id, launch_arguments = std::move(command_line), worker_pool,
                     detached](const atfw::atapp::worker_context& worker_ctx) mutable {
    uint64_t worker_unique_id = worker_ctx.worker_unique_id;
    tbb::concurrent_hash_map<uint64_t, std::shared_ptr<uv_loop_data>>::accessor accessor;
    std::shared_ptr<uv_loop_data> loop_data_ptr = nullptr;
    if (!orbit_agent_manager::me()->uv_loop_queue_.find(accessor, worker_unique_id)) {
      // 创建流程
      uv_loop_t* loop_ = new uv_loop_t();
      uv_loop_init(loop_);
      loop_data_ptr = std::make_shared<uv_loop_data>();
      loop_data_ptr->loop_ = loop_;
      orbit_agent_manager::me()->uv_loop_queue_.emplace(worker_unique_id, loop_data_ptr);
      worker_pool->add_tick_callback(worker_tick_callback, worker_ctx);
    } else {
      loop_data_ptr = accessor->second;
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
    exit_data->client_instance_id = client_instance_id;
    exit_data->worker_unique_id = worker_unique_id;
    process_handle->data = exit_data;

    uv_process_options_t options;
    std::memset(&options, 0, sizeof(options));
    options.file = launch_argv[0];
    options.args = launch_argv.data();
    options.exit_cb = on_uv_process_exit_callback;
    // 独立进程不继承 Agent 的 stdin/stdout/stderr：Agent 退出后这些管道会关闭，
    // 子进程继续往控制台写日志会收到 SIGPIPE，Linux 下默认直接终止进程（无任何日志）
    uv_stdio_container_t child_stdio[3];
    std::memset(child_stdio, 0, sizeof(child_stdio));
    if (detached) {
      options.flags = UV_PROCESS_DETACHED;
      child_stdio[0].flags = UV_IGNORE;
      child_stdio[1].flags = UV_IGNORE;
      child_stdio[2].flags = UV_IGNORE;
      options.stdio = child_stdio;
      options.stdio_count = 3;
    }

    FWLOGINFO("orbit agent spawning client {} by command {}", client_instance_id, command_line_str);
    int uv_result = uv_spawn(loop_data_ptr->loop_, process_handle, &options);

    spawn_completion_t completion;
    completion.client_instance_id = client_instance_id;
    completion.process_handle = process_handle;
    completion.uv_result = uv_result;
    if (uv_result >= 0) {
      completion.process_id = static_cast<int64_t>(uv_process_get_pid(process_handle));
      if (completion.process_id <= 0) {
        completion.process_id = static_cast<int64_t>(process_handle->pid);
      }
      // 塞入handle列表
      loop_data_ptr->process_handles_.insert(process_handle);
      FWLOGINFO("orbit agent started client {} with pid {} by command {}", client_instance_id, completion.process_id,
                command_line_str);
    } else {
      // 处理失败handle
      delete_uv_process_handle(process_handle);
      completion.process_handle = nullptr;
      FWLOGERROR("orbit agent start_client failed for {}: {} by command {}", client_instance_id, uv_strerror(uv_result),
                 command_line_str);
    }
    uv_action_t action;
    action.is_spawn_completion_ = true;
    action.spawn_completion_ = std::move(completion);
    orbit_agent_manager::me()->uv_actions_.push(std::move(action));
  };
  return worker_pool->spawn(spawn_func);
}

void orbit_agent_manager::fill_render_values(const orbit_agent_client_record_ptr& record,
                                             std::unordered_map<std::string, std::string>& render_values) const {
  render_values.emplace("client_id", record->client_id);
  render_values.emplace("instance_id", atfw::util::string::format("{}", record->client_instance_id));
  std::tm tm_local = atfw::util::time::time_utility::get_local_tm(atfw::util::time::time_utility::get_sys_now());
  char buf[64] = {0};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_local);
  render_values.emplace("time", buf);
  std::string log_name = render_string_template(log_name_, render_values);
  render_values.emplace(kStartClientCustomDataLogName, log_name);
}

rpc::result_code_type orbit_agent_manager::send_start_client(rpc::context& ctx,
                                                             orbit_agent_client_record_ptr client_record) {
  if (!client_record) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // 下发真实 client_id；Client 收到后才回调 on_start_client。
  // 普通启动与预启动认领都走这里，Client 无需区分是否预启动
  auto req = rpc::make_shared_message<atfw::orbit::ATDStartClientReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::DTAStartClientRsp>(ctx);
  req->mutable_client_id()->set_client_id(client_record->client_id);
  std::unordered_map<std::string, std::string> render_values;
  fill_render_values(client_record, render_values);
  if (client_record->pre_start) {
    (*req->mutable_custom_data())[kStartClientCustomDataLogName] = render_values[kStartClientCustomDataLogName];
  }

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::agenttoclientservice::start_client(ctx, client_record->client_server_id, *req, *rsp));
  if (rpc_result == 0) {
    rpc_result = rsp->error_code();
  }
  if (rpc_result != 0) {
    FWLOGERROR("orbit agent start_client failed for {} to client node {:#x}, res: {}", client_record,
               client_record->client_server_id, rpc_result);
    // 不重试、不退回预启动池，按启动失败结束该进程并通知 Controller
    stop_client_process(client_record, atfw::orbit::EN_CLIENT_EXIT_STARTUP_FAILED, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  client_record->start_client_sent = true;
  record_client_startup_success(client_record);
  FWLOGINFO("orbit agent start_client sent for {}", client_record);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int orbit_agent_manager::spawn_client_process(rpc::context&, const orbit_agent_client_record_ptr& record,
                                              bool seed_client) {
  std::vector<std::string> launch_arguments;

  std::unordered_map<std::string, std::string> render_values;
  fill_render_values(record, render_values);

  build_client_launch_arguments(record, render_values, client_path_, client_command_line_, launch_arguments, false);
  bool detached = !seed_client && !stop_running_client_;
  int32_t spawn_result = spawn_client_async(record->client_instance_id, std::move(launch_arguments), detached);
  if (spawn_result < 0) {
    FWLOGERROR("orbit agent submit spawn {} to worker pool failed, res: {}({})", record, spawn_result,
               protobuf_mini_dumper_get_error_msg(spawn_result));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
  }

  FWLOGINFO("orbit agent submitted spawn {} to worker pool", record);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;

  // TODO(yousongyang)
  // // 种子模式 发送消息给种子进程
  // if (nullptr == seed_client_record_) {
  //   FWLOGERROR("orbit agent spawn_seed_client_process failed: seed_client_record_ is null");
  //   RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
  // }

  // uint64_t app_id = ++sequence_allocator_;
  // auto req = rpc::make_shared_message<atfw::orbit::ATDForkSeedClientReq>(ctx);
  // auto rsp = rpc::make_shared_message<atfw::orbit::DTAForkSeedClientRsp>(ctx);
  // req->set_app_id(app_id);
  // req->mutable_start_args()->mutable_client_id()->set_client_id(record->client_id);
  // *req->mutable_start_args()->mutable_custom_args() = record->custom_args;

  // int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
  //     rpc::agenttoclientservice::fork_seed_client(ctx, seed_client_record_->client_server_id, *req, *rsp));
  // if (rpc_result == 0) {
  //   rpc_result = rsp->error_code();
  // }
  // if (rpc_result < 0) {
  //   FWLOGERROR("orbit agent fork_seed_client failed for {} to client node {:#x}, res: {}",
  //              seed_client_record_->client_id, seed_client_record_->client_server_id, rpc_result);
  //   RPC_RETURN_CODE(rpc_result);
  // } else {
  //   record->process_id = rsp->pid();
  // }
  // RPC_RETURN_CODE(rsp->error_code());
}

rpc::result_code_type orbit_agent_manager::remote_spawn_client_process(rpc::context& ctx,
                                                                       const orbit_agent_client_record_ptr& record,
                                                                       const std::vector<std::string>& command_line) {
  std::vector<std::string> launch_arguments;

  // 渲染启动参数中占位符的取值来源，当前从 record 上取出 client_id
  std::unordered_map<std::string, std::string> render_values;
  fill_render_values(record, render_values);
  build_client_launch_arguments(record, render_values, "", command_line, launch_arguments, true);

  auto req = rpc::make_shared_message<atfw::orbit::ATCRemoteStartClientReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::CTARemoteStartClientRsp>(ctx);
  const auto* identity = find_server_identity(record->server_unique_id);
  if (identity == nullptr) {
    FWLOGERROR("orbit agent client_start failed for {}: server_unique_id {:#x} not found in server identities",
               record->client_id, record->server_unique_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_AGENT_SERVER_NOT_FOUND);
  }
  *req->mutable_server_identity() = *identity;
  fill_client_identity(*req->mutable_client_identity(), record);
  for (const auto& arg : launch_arguments) {
    req->mutable_arg()->add_command_lines(arg);
  }

  auto controller_server_id = record->get_controller_server_id();

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::agenttocontrollerservice::remote_start_client(ctx, controller_server_id, *req, *rsp));
  if (rpc_result == 0) {
    rpc_result = rsp->error_code();
  }
  if (rpc_result != 0) {
    FWLOGERROR("orbit agent remote spawn client {} failed, rpc_result: {}", record->client_id, rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
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
  auto record = find_client(completion.client_instance_id);
  if (nullptr == record) {
    FWLOGERROR("orbit agent spawn completion ignored for {}: record not found", completion.client_instance_id);
    return;
  }

  record->process_id = completion.process_id;
  record->process_handle_main_thread = completion.process_handle;
  if (completion.uv_result < 0) {
    async_notify_client_exit(record);
    delete_client(record);
    return;
  }
  // 启动成功
  FWLOGINFO("orbit agent spawn completion for {}: pid={}", record, record->process_id);
}

void orbit_agent_manager::on_uv_process_exit(uv_process_t* process_handle, int64_t exit_status, int term_signal) {
  uv_action_t action;
  action.is_spawn_completion_ = false;
  auto* data = static_cast<orbit_agent_process_exit_data*>(process_handle->data);
  action.process_exit_action_ = {
      .client_instance_id = data->client_instance_id, .exit_status_ = exit_status, .term_signal_ = term_signal};
  uint64_t worker_unique_id = data->worker_unique_id;
  // close handle
  delete_uv_process_handle(process_handle);
  {
    // 删除handle缓存
    tbb::concurrent_hash_map<uint64_t, std::shared_ptr<uv_loop_data>>::accessor accessor;
    if (orbit_agent_manager::me()->uv_loop_queue_.find(accessor, worker_unique_id)) {
      accessor->second->process_handles_.erase(process_handle);
    }
  }
  uv_actions_.push(std::move(action));
}

void orbit_agent_manager::process_exit_action(const process_exit_action_t& action) {
  auto record = find_client(action.client_instance_id);
  if (!record) {
    // 已移除
    return;
  }
  on_client_process_exit(record, action.exit_status_, action.term_signal_);
}

int orbit_agent_manager::kill_client_process(const orbit_agent_client_record_ptr& client_record, int signal_number,
                                             atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code) {
  if (!client_record) {
    return UV_EINVAL;
  }

  int uv_result = 0;
  if (nullptr != client_record->process_handle_main_thread) {
    // 到worker上执行流程
    need_kill_process_.insert({client_record->process_handle_main_thread, signal_number});
    client_record->process_handle_main_thread = nullptr;
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

  auto notify_request =
      rpc::make_shared_message<atfw::orbit::ATDStopClientReq>(logic_server_get_current_tick_context());
  notify_request->set_reason(exit_reason);
  int32_t result = rpc::agenttoclientservice::stop_client(logic_server_get_current_tick_context(),
                                                          client_record->client_server_id, *notify_request)
                       .unwrap();
  if (result < 0) {
    FWLOGERROR("orbit agent stop_client_process failed for {}: client_server_id {:#x}, res: {}",
               client_record->client_id, client_record->client_server_id, result);
  }
}

void orbit_agent_manager::on_client_process_exit(const orbit_agent_client_record_ptr& record, int64_t exit_status,
                                                 int term_signal) {
  if (record->state != atfw::orbit::EN_CLIENT_STATE_EXITING) {
    // Client 未通过 STAClientExitReq 告知退出，视为异常退出
    FWLOGWARNING("orbit agent client {} exited unexpectedly: exit_status={}, term_signal={}, state={}",
                 record->client_id, exit_status, term_signal, static_cast<int>(record->state));
  }

  if (record->exit_reason == atfw::orbit::EN_CLIENT_EXIT_REASON_UNSPECIFIED) {
    record->exit_reason = atfw::orbit::EN_CLIENT_EXIT_REASON_CRASH;
  }
  if (record->exit_code == 0) {
    record->exit_code = static_cast<int32_t>(exit_status);
  }
  // 统一删除 这边进程确定退出了
  async_notify_client_exit(record);
  delete_client(record);
  if (record->seed_process) {
    if (!stoped_) {
      // 种子进程意外退出
      FWLOGERROR("orbit agent seed client exited unexpectedly");
      agent_fatal_error();
    }
    return;
  }
}

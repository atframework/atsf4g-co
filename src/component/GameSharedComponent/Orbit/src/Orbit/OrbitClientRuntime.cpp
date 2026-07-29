// Copyright 2026 atframework

#include <Orbit/OrbitClientRuntime.h>
#include <Orbit/OrbitRPCDispatcher.h>

#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/text_format.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#if defined(__linux__) || defined(__unix__)
#  include <unistd.h>
#endif
#include <string_view>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

#define ORBIT_LOG(level, message) log(level, __FILE__, __LINE__, message)

namespace orbit_client_sdk {
namespace {

constexpr const char *kLogCategory = "OrbitClientRuntime";
constexpr const char *kAtappProgramName = "orbit-client-runtime";

constexpr const char *kOrbitArgsAppId = "-id";
constexpr const char *kOrbitArgsClientIdArgument = "--orbit-client-id";
constexpr const char *kOrbitArgsAgentEndpointArgument = "--orbit-agent-endpoint";
constexpr const char *kOrbitArgsConfigEnvArgument = "--config_env";
constexpr const char *kOrbitArgsSeedMode = "--seed_mode";
constexpr const char *kOrbitEnable = "--enable_orbit";

int64_t get_total_process_cpu_time_us(const uv_rusage_t &usage) {
  int64_t total_us = static_cast<int64_t>(usage.ru_stime.tv_sec) + static_cast<int64_t>(usage.ru_utime.tv_sec);
  total_us *= 1000000;
  total_us += static_cast<int64_t>(usage.ru_stime.tv_usec) + static_cast<int64_t>(usage.ru_utime.tv_usec);
  return total_us;
}

void fill_client_id(orbit::DClientId &client_id, const std::string &value) { client_id.set_client_id(value); }

void emit_log(const OrbitClientCallbacks &callbacks,  OrbitClientLogLevel level, const char *file_name, int line_number,
              const std::string &message) {
  if (!callbacks.on_log) {
    return;
  }

  OrbitClientLogRecord record;
  record.file_name = file_name;
  record.line_number = line_number;
  record.level = level;
  record.category = kLogCategory;
  record.message = message;
  callbacks.on_log(record);
}

bool try_consume_argument_value(int argc, char *argv[], int &index, const char *option_name, std::string &output) {
  if (nullptr == argv || index < 0 || index >= argc || nullptr == argv[index]) {
    return false;
  }

  if (0 == std::strcmp(argv[index], option_name)) {
    output.clear();
    if (index + 1 < argc && nullptr != argv[index + 1]) {
      output.assign(argv[++index]);
    }
    return true;
  }
  return false;
}

bool try_consume_argument_mode(int argc, char *argv[], int &index, const char *option_name) {
  if (nullptr == argv || index < 0 || index >= argc || nullptr == argv[index]) {
    return false;
  }

  if (0 == std::strcmp(argv[index], option_name)) {
    return true;
  }
  return false;
}

bool try_parse_uint64_argument(const std::string &input, uint64_t &output) {
  if (input.empty()) {
    return false;
  }

  errno = 0;
  char *end_ptr = nullptr;
  unsigned long long parsed_value = std::strtoull(input.c_str(), &end_ptr, 0);
  if (0 != errno || nullptr == end_ptr || '\0' != *end_ptr) {
    return false;
  }

  output = static_cast<uint64_t>(parsed_value);
  return true;
}

int set_process_environment_variable(const std::string &key, const std::string &value) {
#if defined(_WIN32)
  return _putenv_s(key.c_str(), value.c_str());
#else
  return setenv(key.c_str(), value.c_str(), 1);
#endif
}

std::string_view trim_ascii_whitespace(std::string_view input) {
  while (!input.empty()) {
    char current = input.front();
    if (current != ' ' && current != '\t' && current != '\r' && current != '\n') {
      break;
    }

    input.remove_prefix(1);
  }

  while (!input.empty()) {
    char current = input.back();
    if (current != ' ' && current != '\t' && current != '\r' && current != '\n') {
      break;
    }

    input.remove_suffix(1);
  }

  return input;
}

int apply_config_env_overrides(const OrbitClientOptions &options) {
  if (options.config_env.empty()) {
    return 0;
  }

  for (const std::string &env_line : options.config_env) {
    std::string_view trimmed_line = trim_ascii_whitespace(env_line);
    size_t equal_pos = env_line.find('=');
    if (equal_pos == std::string_view::npos || 0 == equal_pos) {
      return -1;
    }

    std::string_view key = trim_ascii_whitespace(trimmed_line.substr(0, equal_pos));
    if (key.empty()) {
      return -1;
    }

    std::string_view value = trimmed_line.substr(equal_pos + 1);
    if (0 != set_process_environment_variable(std::string{key}, std::string{value})) {
      return -1;
    }
  }

  return 0;
}

uint64_t make_initial_sequence_allocator() {
  return static_cast<uint64_t>((::util::time::time_utility::get_sys_now() - 1577836800) << 23) +
         static_cast<uint64_t>(::util::time::time_utility::get_now_usec() << 3);
}

}  // namespace

#if defined(ORBIT_CLIENT_SDK_DLL) && ORBIT_CLIENT_SDK_DLL
#  if defined(ORBIT_CLIENT_SDK_NATIVE) && ORBIT_CLIENT_SDK_NATIVE
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(OrbitClientRuntime);
#  else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(OrbitClientRuntime);
#  endif
#else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(OrbitClientRuntime);
#endif

ORBIT_CLIENT_SDK_API OrbitClientRuntime::OrbitClientRuntime()
    : app_(),
      callbacks_(),
      options_(),
      state_(OrbitClientRuntimeState::kIdle),
      configured_(false),
      app_callbacks_installed_(false),
      agent_bus_id_(0),
      sequence_allocator_(0),
      last_heartbeat_timepoint_(0) {}

ORBIT_CLIENT_SDK_API OrbitClientRuntime::~OrbitClientRuntime() { restore_app_callbacks(); }

ORBIT_CLIENT_SDK_API int OrbitClientRuntime::init(int argc, char *argv[], const OrbitClientCallbacks &callbacks) {
  OrbitClientOptions options;
  uint64_t app_id = 0;

  int extract_result = extract_launch_options(argc, argv, app_id, options);
  if (extract_result >= 0) {
    if (!enabled()) {
      ORBIT_LOG(OrbitClientLogLevel::kWarning, "Orbit Not Enabled");
      return 0;
    }
    OrbitRPCDispatcher::me()->init();
    return init(app_id, options, callbacks);
  } else {
    emit_log(callbacks, OrbitClientLogLevel::kError, __FILE__, __LINE__,
             LOG_WRAPPER_FWAPI_FORMAT("init rejected: invalid extract_launch_options result {}", extract_result));
  }
  return extract_result;
}

ORBIT_CLIENT_SDK_API bool OrbitClientRuntime::enabled() const { return enabled_; }

int OrbitClientRuntime::extract_launch_options(int argc, char *argv[], uint64_t &app_id, OrbitClientOptions &options) {
  if (argc <= 0 || nullptr == argv) {
    return -1;
  }

  app_id = 0;
  bool has_app_id = false;
  bool has_agent_endpoint = false;
  bool has_client_id = false;
  bool app_id_value_invalid = false;

  for (int index = 1; index < argc; ++index) {
    if (try_consume_argument_mode(argc, argv, index, kOrbitArgsSeedMode)) {
#if defined(__linux__) || defined(__unix__)
      options.seed_mode = true;
#else
      options.seed_mode = false;
#endif
      continue;
    }

    if (try_consume_argument_mode(argc, argv, index, kOrbitEnable)) {
      enabled_ = true;
    }

    std::string parsed_value;
    if (try_consume_argument_value(argc, argv, index, kOrbitArgsAppId, parsed_value)) {
      if (!try_parse_uint64_argument(parsed_value, app_id)) {
        app_id_value_invalid = true;
      } else {
        has_app_id = true;
      }
      continue;
    }

    if (try_consume_argument_value(argc, argv, index, kOrbitArgsAgentEndpointArgument, parsed_value)) {
      options.agent_endpoint = parsed_value;
      has_agent_endpoint = !options.agent_endpoint.empty();
      continue;
    }

    if (try_consume_argument_value(argc, argv, index, kOrbitArgsClientIdArgument, parsed_value)) {
      options.client_id = parsed_value;
      has_client_id = !options.client_id.empty();
      continue;
    }

    if (try_consume_argument_value(argc, argv, index, kOrbitArgsConfigEnvArgument, parsed_value)) {
      if (!parsed_value.empty()) {
        options.config_env.push_back(parsed_value);
      }
      continue;
    }

    if (argv[index] != nullptr) {
      // 非预留都写入custom_launch_arguments
      options.custom_launch_arguments.push_back(std::string(argv[index]));
    }
  }

  if (app_id_value_invalid) {
    return -2;
  }

  if (!has_app_id || 0 == app_id) {
    return -3;
  }

  if (!has_agent_endpoint) {
    return -4;
  }

  if (!has_client_id) {
    return -5;
  }

  return 0;
}

ORBIT_CLIENT_SDK_API int OrbitClientRuntime::init(uint64_t app_id, const OrbitClientOptions &options,
                                                  const OrbitClientCallbacks &callbacks) {
  callbacks_ = callbacks;
  if (state_ == OrbitClientRuntimeState::kConnecting || state_ == OrbitClientRuntimeState::kRunning ||
      state_ == OrbitClientRuntimeState::kStopping) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "init rejected: runtime is busy");
    return -1;
  }

  if (options.client_id.empty()) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "init rejected: client_id is empty");
    return -2;
  }

  if (options.agent_endpoint.empty()) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "init rejected: agent_endpoint is empty");
    return -3;
  }

  if (options.heartbeat_interval_second <= 0) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "init rejected: heartbeat_interval must be positive");
    return -4;
  }

  restore_app_callbacks();

  options_ = options;
  configured_ = false;
  agent_bus_id_ = 0;
  sequence_allocator_ = make_initial_sequence_allocator();
  set_state(OrbitClientRuntimeState::kIdle);

  if (0 != apply_config_env_overrides(options_)) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "init rejected: failed to inject config env overrides");
    return -8;
  }

  uint64_t resolved_app_id = app_id;
  if (0 == resolved_app_id) {
    resolved_app_id = allocate_sequence();
  } else if (sequence_allocator_ < resolved_app_id) {
    sequence_allocator_ = resolved_app_id;
  }

  std::vector<std::string> launch_arguments;
  build_client_launch_arguments(resolved_app_id, launch_arguments);

  std::vector<const char *> launch_argv;
  launch_argv.reserve(launch_arguments.size());
  for (const std::string &launch_argument : launch_arguments) {
    launch_argv.emplace_back(launch_argument.c_str());
  }

  app_ = std::make_unique<::atframework::atapp::app>();
  int app_init_result =
      app_->init(uv_default_loop(), static_cast<int>(launch_argv.size()), launch_argv.data(), nullptr);
  if (0 != app_init_result) {
    std::ostringstream stream;
    stream << "init rejected: atapp init failed for app_id=" << resolved_app_id << ", code=" << app_init_result;
    ORBIT_LOG(OrbitClientLogLevel::kError, stream.str());
    return -5;
  }

  if (!app_->get_bus_node()) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "init rejected: bus node is unavailable");
    return -6;
  }

  install_app_callbacks();
  configured_ = true;
  ORBIT_LOG(OrbitClientLogLevel::kInfo, std::string{"runtime begin connecting, app_id="} +
                                            std::to_string(static_cast<unsigned long long>(resolved_app_id)));

  if (!connect()) {
    restore_app_callbacks();
    configured_ = false;
    return -7;
  }
  ORBIT_LOG(OrbitClientLogLevel::kInfo, "runtime initialized");

  return 0;
}

void OrbitClientRuntime::build_client_launch_arguments(uint64_t app_id, std::vector<std::string> &output) const {
  output.clear();
  output.reserve(4);
  output.emplace_back(kAtappProgramName);
  output.emplace_back("-id");
  output.emplace_back(std::to_string(static_cast<unsigned long long>(app_id)));
  output.emplace_back("start");
}

bool OrbitClientRuntime::connect() {
  if (!configured_) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "connect rejected: runtime is not configured");
    return false;
  }

  if (nullptr == app_ || !app_->get_bus_node()) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "connect rejected: bus node is unavailable");
    return false;
  }

  if (state_ != OrbitClientRuntimeState::kIdle && state_ != OrbitClientRuntimeState::kStopped) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "connect rejected: runtime state does not allow reconnect");
    return false;
  }

  set_state(OrbitClientRuntimeState::kConnecting);

  int connect_result = app_->get_bus_node()->connect(options_.agent_endpoint);
  if (0 != connect_result) {
    set_state(OrbitClientRuntimeState::kIdle);
    ORBIT_LOG(OrbitClientLogLevel::kError,
              std::string{"connect rejected: get_bus_node()->connect failed, code="} + std::to_string(connect_result));
    return false;
  }

  // 等待连接完成
  time_t begin_connect =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  while (state_ == OrbitClientRuntimeState::kConnecting) {
    app_->run_once(0, std::chrono::seconds{0});
    time_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - begin_connect > 5) {
      set_state(OrbitClientRuntimeState::kIdle);
      ORBIT_LOG(OrbitClientLogLevel::kError, "connect rejected: timeout while waiting for connection");
      return false;
    }
  }

  ORBIT_LOG(OrbitClientLogLevel::kInfo, "agent connect requested");
  return true;
}

ORBIT_CLIENT_SDK_API bool OrbitClientRuntime::is_seed_process() const { return options_.seed_mode; }

ORBIT_CLIENT_SDK_API int32_t OrbitClientRuntime::notify_seed_process_ready() {
  if (!is_seed_process()) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "notify_seed_process_ready rejected: not a seed process");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  if (state_ != OrbitClientRuntimeState::kConnected) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "notify_seed_process_ready rejected: runtime is not connected");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  orbit::DTAClientStartReq request;
  fill_client_id(*request.mutable_client_id(), options_.client_id);
  OrbitClientRequestOptions request_options;
  request_options.reliable = true;
  request_options.retry_times = 3;
  int32_t send_result = rpc_send_client_start(request, nullptr, request_options);
  if (send_result < 0) {
    ORBIT_LOG(OrbitClientLogLevel::kError,
              std::string{"failed to send client_start request, code="} + std::to_string(send_result));
    return send_result;
  }
  last_heartbeat_timepoint_ = ::util::time::time_utility::get_sys_now();
  set_state(OrbitClientRuntimeState::kRunning);
  ORBIT_LOG(OrbitClientLogLevel::kInfo, "seed_start sent");

  while (true) {
    if (state_ == OrbitClientRuntimeState::kStopping) {
      break;
    }

    tick();
    if (callbacks_.on_seed_waiting_tick) {
      callbacks_.on_seed_waiting_tick();
    }
    int32_t res = process_fork_request();
    if (res < 0) {
      ORBIT_LOG(OrbitClientLogLevel::kError, std::string{"process_fork_request failed, code="} + std::to_string(res));
      return res;
    }
    if (!is_seed_process()) {
      // 变成了Child进程 退出循环
      break;
    }
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::on_received_fork_request(const orbit::ATDForkSeedClientReq &request) {
  if (!is_seed_process()) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "on_received_fork_request rejected: not a seed process");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }
  pending_fork_requests_.push_back(request);
  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::process_fork_request() {
  if (!is_seed_process()) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "process_fork_request rejected: not a seed process");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  if (pending_fork_requests_.empty()) {
    return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }

  orbit::ATDForkSeedClientReq request = pending_fork_requests_.front();
  pending_fork_requests_.pop_front();

#if defined(__linux__) || defined(__unix__)
  // Fork
  // TODO atapp 需要在fork前和后处理 现在先略过了
  pid_t child_pid = fork();
  if (child_pid < 0) {
    ORBIT_LOG(OrbitClientLogLevel::kError, std::string{"process_fork_request rejected: fork failed, errno="} +
                                               std::to_string(static_cast<int>(errno)));
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  if (child_pid != 0) {
    // Seed返回
    return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }
#else
  ORBIT_LOG(OrbitClientLogLevel::kError, "process_fork_request rejected: not linux or unix platform, cannot fork");
  return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
#endif

  // Child进程 需要重新初始化自己
  state_ = OrbitClientRuntimeState::kIdle;
  options_.seed_mode = false;
  pending_fork_requests_.clear();
  options_.client_id = request.start_args().client_id().client_id();
  options_.custom_launch_arguments =
      std::vector<std::string>(request.start_args().custom_args().begin(), request.start_args().custom_args().end());
  return init(request.app_id(), options_,
              callbacks_);  // TODO Init内部应该还有问题 但是传入参数是正确的
}

ORBIT_CLIENT_SDK_API const std::vector<std::string> &OrbitClientRuntime::get_custom_launch_arguments() const {
  return options_.custom_launch_arguments;
}

ORBIT_CLIENT_SDK_API const std::string &OrbitClientRuntime::find_custom_launch_argument(const std::string &key) const {
  auto it = std::find(options_.custom_launch_arguments.begin(), options_.custom_launch_arguments.end(), key);

  if (it != options_.custom_launch_arguments.end() && std::next(it) != options_.custom_launch_arguments.end()) {
    return *std::next(it);
  }

  static const std::string empty_string;
  return empty_string;
}

ORBIT_CLIENT_SDK_API int32_t OrbitClientRuntime::notify_process_ready(const std::string &client_addr,
                                                                      const std::string &custom_data) {
  if (state_ != OrbitClientRuntimeState::kConnected) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "notify_process_ready rejected: runtime is not connected");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  if (client_addr.empty()) {
    ORBIT_LOG(OrbitClientLogLevel::kError, "notify_process_ready rejected: client_addr is empty");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  orbit::DTAClientStartReq request;
  fill_client_id(*request.mutable_client_id(), options_.client_id);
  request.set_client_addr(client_addr);
  request.set_custom_data(custom_data);
  OrbitClientRequestOptions request_options;
  request_options.reliable = true;
  request_options.retry_times = 3;

  int32_t send_result = rpc_send_client_start(request, nullptr, request_options);
  if (send_result < 0) {
    ORBIT_LOG(OrbitClientLogLevel::kError,
              std::string{"failed to send client_start request, code="} + std::to_string(send_result));
    return send_result;
  }

  last_heartbeat_timepoint_ = ::util::time::time_utility::get_sys_now();
  set_state(OrbitClientRuntimeState::kRunning);

  ORBIT_LOG(OrbitClientLogLevel::kInfo, "client_start sent");
  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

ORBIT_CLIENT_SDK_API void OrbitClientRuntime::tick() {
  if (app_->is_inited() && !app_->check_flag(::atframework::atapp::app::flag_t::kInTick)) {
    app_->run_once(0, std::chrono::seconds{0});
  }

  if (state_ == OrbitClientRuntimeState::kRunning) {
    execute_pending_request_timeouts();

    OrbitRPCDispatcher::me()->tick();

    do {
      time_t now = ::util::time::time_utility::get_sys_now();
      if (now - last_heartbeat_timepoint_ < options_.heartbeat_interval_second) {
        break;
      }

      ORBIT_LOG(OrbitClientLogLevel::kInfo, "heartbeat begin");
      send_heartbeat(make_default_load_snapshot());
    } while (false);
  }

  if (state_ == OrbitClientRuntimeState::kStopping && app_->is_closed()) {
    ORBIT_LOG(OrbitClientLogLevel::kInfo, "stopping finalized");
    set_state(OrbitClientRuntimeState::kStopped);
    if (callbacks_.on_request_stop) {
      callbacks_.on_request_stop();
    }
  }
}

int32_t OrbitClientRuntime::send_heartbeat(const OrbitClientLoadSnapshot &snapshot) {
  if (state_ != OrbitClientRuntimeState::kRunning) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "send_heartbeat rejected: runtime is not running");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  orbit::DTAClientHeartbeatNotify request;
  fill_client_id(*request.mutable_client_id(), options_.client_id);
  request.mutable_snapshot()->set_cpu_used(snapshot.cpu_used);
  request.mutable_snapshot()->set_memory_used_mb(snapshot.memory_used_mb);

  int32_t send_result = rpc_send_client_heartbeat(request);
  if (send_result < 0) {
    ORBIT_LOG(OrbitClientLogLevel::kError,
              std::string{"failed to send client_heartbeat request, code="} + std::to_string(send_result));
    return send_result;
  }

  last_heartbeat_timepoint_ = ::util::time::time_utility::get_sys_now();
  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

ORBIT_CLIENT_SDK_API int32_t OrbitClientRuntime::send_to_server(
    const std::string &payload, OrbitClientRpcCallback<orbit::ATDSendToServerRsp> callback,
    const OrbitClientRequestOptions &request_options) {
  if (state_ != OrbitClientRuntimeState::kRunning) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning, "send_to_server rejected: runtime is not running");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  orbit::DTASendToServerReq request;
  fill_client_id(*request.mutable_client_id(), options_.client_id);
  request.set_payload(payload);

  int32_t send_result = rpc_send_send_to_server(request, std::move(callback), request_options);
  if (send_result < 0) {
    ORBIT_LOG(OrbitClientLogLevel::kError,
              std::string{"failed to send send_to_server request, code="} + std::to_string(send_result));
    return send_result;
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

ORBIT_CLIENT_SDK_API int32_t OrbitClientRuntime::request_end(orbit::EnClientExitReason reason, int32_t exit_code,
                                                             const std::string &custom_data) {
  OrbitClientRuntimeState previous_state = state_;
  set_state(OrbitClientRuntimeState::kStopping);
  OrbitClientRequestOptions request_options;
  request_options.reliable = true;
  request_options.retry_times = 3;

  int32_t send_result = orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  if (previous_state == OrbitClientRuntimeState::kConnected || previous_state == OrbitClientRuntimeState::kRunning) {
    orbit::DTAClientExitReq request;
    fill_client_id(*request.mutable_client_id(), options_.client_id);
    request.set_exit_reason(reason);
    request.set_custom_data(custom_data);
    request.set_exit_code(exit_code);
    auto wrapped_callback = [this](int32_t, const orbit::ATDClientExitRsp &) mutable { finalize_shutdown(); };
    send_result = rpc_send_client_exit(request, std::move(wrapped_callback), request_options);
    if (send_result < 0) {
      ORBIT_LOG(OrbitClientLogLevel::kError,
                std::string{"failed to send client_exit request, code="} + std::to_string(send_result));
    }
  }

  if (send_result < 0 ||
      (previous_state != OrbitClientRuntimeState::kConnected && previous_state != OrbitClientRuntimeState::kRunning)) {
    finalize_shutdown();
  }

  return send_result;
}

void OrbitClientRuntime::finalize_shutdown() {
  ORBIT_LOG(OrbitClientLogLevel::kInfo, "finalize shutdown");
  app_->stop();
  restore_app_callbacks();
  pending_client_request_map_.clear();
  pending_client_request_timeout_map_.clear();
  agent_bus_id_ = 0;
  configured_ = false;
}

void OrbitClientRuntime::install_app_callbacks() {
  if (app_callbacks_installed_ || nullptr == app_) {
    return;
  }

  app_->set_evt_on_forward_request(
      [this](::atframework::atapp::app &app, const ::atframework::atapp::app::message_sender_t &source,
             const ::atframework::atapp::app::message_t &msg) { return on_atapp_forward_request(app, source, msg); });
  app_->set_evt_on_forward_response(
      [this](::atframework::atapp::app &app, const ::atframework::atapp::app::message_sender_t &source,
             const ::atframework::atapp::app::message_t &msg,
             int32_t error_code) { return on_atapp_forward_response(app, source, msg, error_code); });
  app_->set_evt_on_app_connected([this](::atframework::atapp::app &app, ::atbus::endpoint &ep, int status) {
    return on_atapp_connected(app, ep, status);
  });
  app_->set_evt_on_app_disconnected([this](::atframework::atapp::app &app, ::atbus::endpoint &ep, int status) {
    return on_atapp_disconnected(app, ep, status);
  });

  app_callbacks_installed_ = true;
}

void OrbitClientRuntime::restore_app_callbacks() {
  if (!app_callbacks_installed_) {
    return;
  }

  if (app_) {
    app_->set_evt_on_forward_request({});
    app_->set_evt_on_forward_response({});
    app_->set_evt_on_app_connected({});
    app_->set_evt_on_app_disconnected({});
  }

  app_callbacks_installed_ = false;
}

int OrbitClientRuntime::on_atapp_forward_request(::atframework::atapp::app &app,
                                                 const ::atframework::atapp::app::message_sender_t &source,
                                                 const ::atframework::atapp::app::message_t &msg) {
  if (should_handle_atapp_message(app, source, msg)) {
    std::string payload;
    payload.assign(reinterpret_cast<const char *>(msg.data.data()), msg.data.size());
    on_received_message(payload);
    return 0;
  }

  return 0;
}

int OrbitClientRuntime::on_atapp_forward_response(::atframework::atapp::app &app,
                                                  const ::atframework::atapp::app::message_sender_t &source,
                                                  const ::atframework::atapp::app::message_t &msg, int32_t error_code) {
  if (&app == app_.get() && source.id == agent_bus_id_ && error_code < 0) {
    ORBIT_LOG(OrbitClientLogLevel::kWarning,
              std::string{"send message to agent failed, code="} + std::to_string(error_code));

    atframework::SSMsg failed_message;
    if (failed_message.ParseFromArray(msg.data.data(), static_cast<int>(msg.data.size())) &&
        failed_message.has_head() && failed_message.head().has_rpc_request() &&
        0 != failed_message.head().source_task_id()) {
      uint64_t task_id = failed_message.head().source_task_id();
      auto pending_iter = pending_client_request_map_.find(task_id);
      if (pending_iter != pending_client_request_map_.end()) {
        if (!retry_pending_request(task_id, pending_iter->second, error_code, "transport send failed")) {
          complete_pending_request(task_id, error_code, nullptr);
        }
      }
    }
  }

  return 0;
}

int OrbitClientRuntime::on_atapp_connected(::atframework::atapp::app &app, ::atbus::endpoint &ep, int status) {
  ORBIT_LOG(OrbitClientLogLevel::kInfo, "atapp connected");
  if (&app == app_.get() && 0 == status && OrbitClientRuntimeState::kConnecting == state_ && 0 == agent_bus_id_) {
    agent_bus_id_ = ep.get_id();
    set_state(OrbitClientRuntimeState::kConnected);
    ORBIT_LOG(OrbitClientLogLevel::kInfo, std::string{"agent connected, endpoint id="} +
                                              std::to_string(static_cast<unsigned long long>(agent_bus_id_)));
  }

  return 0;
}

int OrbitClientRuntime::on_atapp_disconnected(::atframework::atapp::app &app, ::atbus::endpoint &ep, int status) {
  ORBIT_LOG(OrbitClientLogLevel::kInfo, "atapp disconnected");
  if (&app == app_.get() && 0 != agent_bus_id_ && ep.get_id() == agent_bus_id_) {
    agent_bus_id_ = 0;
    if (state_ != OrbitClientRuntimeState::kStopping && state_ != OrbitClientRuntimeState::kStopped) {
      set_state(OrbitClientRuntimeState::kIdle);
    }
    ORBIT_LOG(OrbitClientLogLevel::kWarning, std::string{"agent disconnected, status="} + std::to_string(status));
  }

  return 0;
}

bool OrbitClientRuntime::should_handle_atapp_message(const ::atframework::atapp::app &app,
                                                     const ::atframework::atapp::app::message_sender_t &source,
                                                     const ::atframework::atapp::app::message_t &msg) const {
  (void)msg;

  return &app == app_.get() && 0 != agent_bus_id_ && source.id == agent_bus_id_;
}

uint64_t OrbitClientRuntime::allocate_sequence() { return ++sequence_allocator_; }

void OrbitClientRuntime::set_state(OrbitClientRuntimeState next_state) { state_ = next_state; }

ORBIT_CLIENT_SDK_API void OrbitClientRuntime::log(OrbitClientLogLevel level, const char *file_name, int line_number,
                                                  const std::string &message) const {
  if (!callbacks_.on_log) {
    return;
  }

  OrbitClientLogRecord record;
  record.file_name = file_name;
  record.line_number = line_number;
  record.level = level;
  record.category = kLogCategory;
  record.message = message;
  callbacks_.on_log(record);
}

ORBIT_CLIENT_SDK_API std::string OrbitClientRuntime::protobuf_mini_dumper_get_readable(
    const ::google::protobuf::Message &msg) {
  std::string debug_string;
  // 16K is in bin of tcache in jemalloc, and MEDIUM_PAGE in mimalloc
  debug_string.reserve(16 * 1024);

  ::google::protobuf::TextFormat::Printer printer;
  printer.SetUseUtf8StringEscaping(true);
  // printer.SetExpandAny(true);
  printer.SetUseShortRepeatedPrimitives(true);
  printer.SetSingleLineMode(false);
  printer.SetTruncateStringFieldLongerThan(4096);
  printer.SetPrintMessageFieldsInIndexOrder(false);

  if (!printer.PrintToString(msg, &debug_string)) {
    // Handle the error, e.g., log it or append an error message
    debug_string += "... PrintToString failed";
  }

  // Old implementation will use COW and the new compiler will use NRVO here.
  return debug_string;
}

OrbitClientLoadSnapshot OrbitClientRuntime::make_default_load_snapshot() {
  double cpu_used = 0.0;
  double memory_used_mb = 0.0;

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

        cpu_used = static_cast<double>(cpu_offset_us) / static_cast<double>(elapsed_us);
        last_self_cpu_used_ = cpu_used;
      }
    } else {
      has_self_usage_sample_ = true;
      last_self_cpu_used_ = 0.0;
      cpu_used = 0.0;
    }

    last_self_rusage_ = current_usage;
    last_self_usage_sample_timepoint_ = now;
  } while (false);

  size_t self_memory_rss = 0;
  if (0 == uv_resident_set_memory(&self_memory_rss)) {
    memory_used_mb = static_cast<double>(self_memory_rss) / (1024.0 * 1024.0);
  }

  return OrbitClientLoadSnapshot{cpu_used, memory_used_mb};
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END

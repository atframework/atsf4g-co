// Copyright 2026 atframework

#include <Orbit/OrbitClientRuntime.h>

#include <time/time_utility.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <sstream>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {
namespace {

constexpr const char* kLogCategory = "OrbitClientRuntime";
constexpr const char* kAtappProgramName = "orbit-client-runtime";

constexpr const char* kOrbitArgsAppId = "-id";
constexpr const char* kOrbitArgsClientIdArgument = "--orbit-client-id";
constexpr const char* kOrbitArgsAgentEndpointArgument = "--orbit-agent-endpoint";

void fill_client_id(orbit::DClientId& client_id, const std::string& value) { client_id.set_client_id(value); }

OrbitClientLoadSnapshot make_default_load_snapshot() { return OrbitClientLoadSnapshot{}; }

void emit_log(const OrbitClientCallbacks& callbacks, OrbitClientLogLevel level, const std::string& message) {
  if (!callbacks.on_log) {
    return;
  }

  OrbitClientLogRecord record;
  record.level = level;
  record.category = kLogCategory;
  record.message = message;
  callbacks.on_log(record);
}

bool try_consume_argument_value(int argc, char* argv[], int& index, const char* option_name, std::string& output) {
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

  size_t option_name_len = std::strlen(option_name);
  if (0 != std::strncmp(argv[index], option_name, option_name_len)) {
    return false;
  }

  if ('=' != argv[index][option_name_len]) {
    return false;
  }

  output.assign(argv[index] + option_name_len + 1);
  return true;
}

bool try_parse_uint64_argument(const std::string& input, uint64_t& output) {
  if (input.empty()) {
    return false;
  }

  errno = 0;
  char* end_ptr = nullptr;
  unsigned long long parsed_value = std::strtoull(input.c_str(), &end_ptr, 0);
  if (0 != errno || nullptr == end_ptr || '\0' != *end_ptr) {
    return false;
  }

  output = static_cast<uint64_t>(parsed_value);
  return true;
}

uint64_t make_initial_sequence_allocator() {
  return static_cast<uint64_t>(
             (::util::time::time_utility::get_sys_now() - PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
             << 23) +
         static_cast<uint64_t>(::util::time::time_utility::get_now_usec() << 3);
}

}  // namespace

OrbitClientRuntime::OrbitClientRuntime()
    : app_(),
      callbacks_(),
      options_(),
      state_(OrbitClientRuntimeState::kIdle),
      configured_(false),
      app_callbacks_installed_(false),
      agent_bus_id_(0),
      sequence_allocator_(0),
      last_heartbeat_timepoint_() {}

OrbitClientRuntime::~OrbitClientRuntime() { restore_app_callbacks(); }

int OrbitClientRuntime::init(int argc, char* argv[], const OrbitClientCallbacks& callbacks) {
  OrbitClientOptions options;
  uint64_t app_id = 0;

  int extract_result = extract_launch_options(argc, argv, app_id, options);
  if (extract_result >= 0) {
    return init(app_id, options, callbacks);
  }

  switch (extract_result) {
    case -20:
      emit_log(callbacks, OrbitClientLogLevel::kError, "init rejected: invalid argc/argv input");
      break;
    case -21:
      emit_log(callbacks, OrbitClientLogLevel::kError, "init rejected: missing -id launch argument");
      break;
    case -22:
      emit_log(callbacks, OrbitClientLogLevel::kError, "init rejected: invalid -id launch argument value");
      break;
    case -23:
      emit_log(callbacks, OrbitClientLogLevel::kError, "init rejected: missing --orbit-agent-endpoint launch argument");
      break;
    case -24:
      emit_log(callbacks, OrbitClientLogLevel::kError, "init rejected: missing --orbit-client-id launch argument");
      break;
    default:
      emit_log(callbacks, OrbitClientLogLevel::kError, "init rejected: failed to extract orbit launch arguments");
      break;
  }

  return extract_result;
}

int OrbitClientRuntime::extract_launch_options(int argc, char* argv[], uint64_t& app_id,
                                               OrbitClientOptions& options) const {
  if (argc <= 0 || nullptr == argv) {
    return -20;
  }

  app_id = 0;
  options = OrbitClientOptions{};

  bool has_app_id = false;
  bool has_agent_endpoint = false;
  bool has_client_id = false;
  bool app_id_value_invalid = false;

  for (int index = 1; index < argc; ++index) {
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
  }

  if (app_id_value_invalid) {
    return -22;
  }

  if (!has_app_id || 0 == app_id) {
    return -21;
  }

  if (!has_agent_endpoint) {
    return -23;
  }

  if (!has_client_id) {
    return -24;
  }

  return 0;
}

int OrbitClientRuntime::init(uint64_t app_id, const OrbitClientOptions& options,
                             const OrbitClientCallbacks& callbacks) {
  if (state_ == OrbitClientRuntimeState::kConnecting || state_ == OrbitClientRuntimeState::kRunning ||
      state_ == OrbitClientRuntimeState::kStopping) {
    log(OrbitClientLogLevel::kWarning, "init rejected: runtime is busy");
    return -1;
  }

  if (options.client_id.empty()) {
    log(OrbitClientLogLevel::kError, "init rejected: client_id is empty");
    return -2;
  }

  if (options.agent_endpoint.empty()) {
    log(OrbitClientLogLevel::kError, "init rejected: agent_endpoint is empty");
    return -3;
  }

  if (options.heartbeat_interval.count() <= 0) {
    log(OrbitClientLogLevel::kError, "init rejected: heartbeat_interval must be positive");
    return -4;
  }

  restore_app_callbacks();
  app_.reset();

  callbacks_ = callbacks;
  options_ = options;
  configured_ = false;
  agent_bus_id_ = 0;
  sequence_allocator_ = make_initial_sequence_allocator();
  last_heartbeat_timepoint_ = clock_type::time_point{};
  set_state(OrbitClientRuntimeState::kIdle);

  uint64_t resolved_app_id = app_id;
  if (0 == resolved_app_id) {
    resolved_app_id = allocate_sequence();
  } else if (sequence_allocator_ < resolved_app_id) {
    sequence_allocator_ = resolved_app_id;
  }

  std::vector<std::string> launch_arguments;
  build_client_launch_arguments(resolved_app_id, launch_arguments);

  std::vector<const char*> launch_argv;
  launch_argv.reserve(launch_arguments.size());
  for (const std::string& launch_argument : launch_arguments) {
    launch_argv.emplace_back(launch_argument.c_str());
  }

  app_ = std::make_unique<::atframework::atapp::app>();
  int app_init_result =
      app_->init(uv_default_loop(), static_cast<int>(launch_argv.size()), launch_argv.data(), nullptr);
  if (0 != app_init_result) {
    std::ostringstream stream;
    stream << "init rejected: atapp init failed for app_id=" << resolved_app_id << ", code=" << app_init_result;
    log(OrbitClientLogLevel::kError, stream.str());
    app_.reset();
    return -5;
  }

  if (!app_->get_bus_node()) {
    log(OrbitClientLogLevel::kError, "init rejected: bus node is unavailable");
    app_.reset();
    return -6;
  }

  install_app_callbacks();
  configured_ = true;
  log(OrbitClientLogLevel::kInfo, std::string{"runtime begin connecting, app_id="} +
                                      std::to_string(static_cast<unsigned long long>(resolved_app_id)));

  if (!connect()) {
    restore_app_callbacks();
    app_.reset();
    configured_ = false;
    return -7;
  }
  log(OrbitClientLogLevel::kInfo, "runtime initialized");

  return 0;
}

void OrbitClientRuntime::build_client_launch_arguments(uint64_t app_id, std::vector<std::string>& output) const {
  output.clear();
  output.reserve(4);
  output.emplace_back(kAtappProgramName);
  output.emplace_back("-id");
  output.emplace_back(std::to_string(static_cast<unsigned long long>(app_id)));
  output.emplace_back("start");
}

bool OrbitClientRuntime::connect() {
  if (!configured_) {
    log(OrbitClientLogLevel::kError, "connect rejected: runtime is not configured");
    return false;
  }

  if (nullptr == app_ || !app_->get_bus_node()) {
    log(OrbitClientLogLevel::kError, "connect rejected: bus node is unavailable");
    return false;
  }

  if (state_ != OrbitClientRuntimeState::kIdle && state_ != OrbitClientRuntimeState::kStopped) {
    log(OrbitClientLogLevel::kWarning, "connect rejected: runtime state does not allow reconnect");
    return false;
  }

  set_state(OrbitClientRuntimeState::kConnecting);

  int connect_result = app_->get_bus_node()->connect(options_.agent_endpoint);
  if (0 != connect_result) {
    set_state(OrbitClientRuntimeState::kIdle);
    log(OrbitClientLogLevel::kError,
        std::string{"connect rejected: get_bus_node()->connect failed, code="} + std::to_string(connect_result));
    return false;
  }

  // 等待连接完成
  time_t begin_connect =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  while (state_ == OrbitClientRuntimeState::kConnecting) {
    app_->tick();
    time_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - begin_connect > 5) {
      set_state(OrbitClientRuntimeState::kIdle);
      log(OrbitClientLogLevel::kError, "connect rejected: timeout while waiting for connection");
      return false;
    }
  }

  log(OrbitClientLogLevel::kInfo, "agent connect requested");
  return true;
}

bool OrbitClientRuntime::notify_process_ready(const std::string& custom_data) {
  if (state_ != OrbitClientRuntimeState::kConnected) {
    log(OrbitClientLogLevel::kWarning, "notify_process_ready rejected: runtime is not connected");
    return false;
  }

  if (options_.client_addr.empty()) {
    log(OrbitClientLogLevel::kError, "notify_process_ready rejected: client_addr is empty");
    return false;
  }

  orbit::DTAClientStartReq request;
  fill_client_id(*request.mutable_client_id(), options_.client_id);
  request.set_client_addr(options_.client_addr);
  request.set_custom_data(custom_data);

  if (!rpc_send_client_start(request)) {
    log(OrbitClientLogLevel::kError, "failed to send client_start request");
    return false;
  }

  last_heartbeat_timepoint_ = clock_type::now();
  set_state(OrbitClientRuntimeState::kRunning);

  log(OrbitClientLogLevel::kInfo, "client_start sent");
  return true;
}

void OrbitClientRuntime::tick() {
  if (nullptr != app_ && app_->is_inited() && !app_->check_flag(::atframework::atapp::app::flag_t::kInTick)) {
    app_->tick();
  }

  if (state_ != OrbitClientRuntimeState::kRunning) {
    return;
  }

  do {
    if (!options_.auto_send_heartbeat_on_tick) {
      break;
    }
    clock_type::time_point now = clock_type::now();

    if (last_heartbeat_timepoint_ != clock_type::time_point{} &&
        now - last_heartbeat_timepoint_ < options_.heartbeat_interval) {
      break;
    }

    send_heartbeat(make_default_load_snapshot());
  } while (false);
}

bool OrbitClientRuntime::send_heartbeat(const OrbitClientLoadSnapshot& snapshot) {
  if (state_ != OrbitClientRuntimeState::kRunning) {
    log(OrbitClientLogLevel::kWarning, "send_heartbeat rejected: runtime is not running");
    return false;
  }

  orbit::DTAClientHeartbeatNotify request;
  fill_client_id(*request.mutable_client_id(), options_.client_id);
  request.mutable_snapshot()->set_cpu_used(snapshot.cpu_used);
  request.mutable_snapshot()->set_memory_used_mb(snapshot.memory_used_mb);

  if (!rpc_send_client_heartbeat(request)) {
    log(OrbitClientLogLevel::kError, "failed to send client_heartbeat request");
    return false;
  }

  last_heartbeat_timepoint_ = clock_type::now();
  return true;
}

bool OrbitClientRuntime::send_to_server(const std::string& payload) {
  if (state_ != OrbitClientRuntimeState::kRunning) {
    log(OrbitClientLogLevel::kWarning, "send_to_server rejected: runtime is not running");
    return false;
  }

  orbit::DTASendToServerNotify request;
  fill_client_id(*request.mutable_client_id(), options_.client_id);
  request.set_payload(payload);

  if (!rpc_send_send_to_server(request)) {
    log(OrbitClientLogLevel::kError, "failed to send send_to_server request");
    return false;
  }

  return true;
}

bool OrbitClientRuntime::request_end(orbit::EnClientExitReason reason, int32_t exit_code,
                                     const std::string& custom_data) {
  OrbitClientRuntimeState previous_state = state_;
  set_state(OrbitClientRuntimeState::kStopping);

  bool send_result = true;
  if (previous_state == OrbitClientRuntimeState::kConnected || previous_state == OrbitClientRuntimeState::kRunning) {
    orbit::DTAClientExitReq request;
    fill_client_id(*request.mutable_client_id(), options_.client_id);
    request.set_exit_reason(reason);
    request.set_custom_data(custom_data);
    request.set_exit_code(exit_code);
    send_result = rpc_send_client_exit(request);
    if (!send_result) {
      log(OrbitClientLogLevel::kError, "failed to send client_exit request");
    }
  }

  if (nullptr != app_ && app_->get_bus_node() && 0 != agent_bus_id_) {
    int disconnect_result = app_->get_bus_node()->disconnect(agent_bus_id_);
    if (0 != disconnect_result) {
      log(OrbitClientLogLevel::kWarning,
          std::string{"disconnect agent failed, code="} + std::to_string(disconnect_result));
    }
  }

  restore_app_callbacks();
  app_.reset();
  agent_bus_id_ = 0;
  configured_ = false;
  last_heartbeat_timepoint_ = clock_type::time_point{};
  set_state(OrbitClientRuntimeState::kStopped);
  return send_result;
}

void OrbitClientRuntime::install_app_callbacks() {
  if (app_callbacks_installed_ || nullptr == app_) {
    return;
  }

  app_->set_evt_on_forward_request(
      [this](::atframework::atapp::app& app, const ::atframework::atapp::app::message_sender_t& source,
             const ::atframework::atapp::app::message_t& msg) { return on_atapp_forward_request(app, source, msg); });
  app_->set_evt_on_forward_response(
      [this](::atframework::atapp::app& app, const ::atframework::atapp::app::message_sender_t& source,
             const ::atframework::atapp::app::message_t& msg,
             int32_t error_code) { return on_atapp_forward_response(app, source, msg, error_code); });
  app_->set_evt_on_app_connected([this](::atframework::atapp::app& app, ::atbus::endpoint& ep, int status) {
    return on_atapp_connected(app, ep, status);
  });
  app_->set_evt_on_app_disconnected([this](::atframework::atapp::app& app, ::atbus::endpoint& ep, int status) {
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

int OrbitClientRuntime::on_atapp_forward_request(::atframework::atapp::app& app,
                                                 const ::atframework::atapp::app::message_sender_t& source,
                                                 const ::atframework::atapp::app::message_t& msg) {
  if (should_handle_atapp_message(app, source, msg)) {
    std::string payload;
    payload.assign(reinterpret_cast<const char*>(msg.data.data()), msg.data.size());
    on_received_message(payload);
    return 0;
  }

  return 0;
}

int OrbitClientRuntime::on_atapp_forward_response(::atframework::atapp::app& app,
                                                  const ::atframework::atapp::app::message_sender_t& source,
                                                  const ::atframework::atapp::app::message_t& msg, int32_t error_code) {
  (void)msg;

  if (&app == app_.get() && source.id == agent_bus_id_ && error_code < 0) {
    log(OrbitClientLogLevel::kWarning, std::string{"send message to agent failed, code="} + std::to_string(error_code));
  }

  return 0;
}

int OrbitClientRuntime::on_atapp_connected(::atframework::atapp::app& app, ::atbus::endpoint& ep, int status) {
  if (&app == app_.get() && 0 == status && OrbitClientRuntimeState::kConnecting == state_ && 0 == agent_bus_id_) {
    agent_bus_id_ = ep.get_id();
    set_state(OrbitClientRuntimeState::kConnected);
    log(OrbitClientLogLevel::kInfo,
        std::string{"agent connected, endpoint id="} + std::to_string(static_cast<unsigned long long>(agent_bus_id_)));
  }

  return 0;
}

int OrbitClientRuntime::on_atapp_disconnected(::atframework::atapp::app& app, ::atbus::endpoint& ep, int status) {
  if (&app == app_.get() && 0 != agent_bus_id_ && ep.get_id() == agent_bus_id_) {
    agent_bus_id_ = 0;
    if (state_ != OrbitClientRuntimeState::kStopping && state_ != OrbitClientRuntimeState::kStopped) {
      set_state(OrbitClientRuntimeState::kIdle);
    }
    log(OrbitClientLogLevel::kWarning, std::string{"agent disconnected, status="} + std::to_string(status));
  }

  return 0;
}

bool OrbitClientRuntime::should_handle_atapp_message(const ::atframework::atapp::app& app,
                                                     const ::atframework::atapp::app::message_sender_t& source,
                                                     const ::atframework::atapp::app::message_t& msg) const {
  (void)msg;

  return &app == app_.get() && 0 != agent_bus_id_ && source.id == agent_bus_id_;
}

uint64_t OrbitClientRuntime::allocate_sequence() { return ++sequence_allocator_; }

void OrbitClientRuntime::set_state(OrbitClientRuntimeState next_state) { state_ = next_state; }

void OrbitClientRuntime::log(OrbitClientLogLevel level, const std::string& message) const {
  if (!callbacks_.on_log) {
    return;
  }

  OrbitClientLogRecord record;
  record.level = level;
  record.category = kLogCategory;
  record.message = message;
  callbacks_.on_log(record);
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END
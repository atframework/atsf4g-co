// Copyright 2026 atframework

#include <Orbit/OrbitClientRuntime.h>
#include <Orbit/OrbitRPCHandle.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.orbit.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on
namespace {

constexpr const char* kOrbitArgsAppId = "-id";
constexpr const char* kOrbitArgsClientId = "--orbit-client-id";
constexpr const char* kOrbitArgsAgentEndpoint = "--orbit-agent-endpoint";
constexpr const char* kOrbitArgsConfigEnv = "--config_env";

constexpr auto kTickInterval = std::chrono::milliseconds{100};
constexpr auto kReadyDelay = std::chrono::seconds{10};
constexpr auto kPingDelay = std::chrono::seconds{1};
constexpr auto kExitDelay = std::chrono::seconds{60};

int get_process_id() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

std::filesystem::path get_log_file_path(int argc, char* argv[]) {
  std::error_code error_code;
  if (argc > 0 && nullptr != argv && nullptr != argv[0]) {
    std::filesystem::path executable_path = std::filesystem::absolute(std::filesystem::path{argv[0]}, error_code);
    if (!error_code) {
      return executable_path.parent_path() / ("log_" + std::to_string(get_process_id()) + ".log");
    }
  }

  error_code.clear();
  std::filesystem::path current_path = std::filesystem::current_path(error_code);
  if (error_code) {
    current_path = ".";
  }

  return current_path / ("log_" + std::to_string(get_process_id()) + ".log");
}

const char* to_string(ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientLogLevel level) {
  using log_level_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientLogLevel;
  switch (level) {
    case log_level_t::kDebug:
      return "DEBUG";
    case log_level_t::kInfo:
      return "INFO";
    case log_level_t::kWarning:
      return "WARN";
    case log_level_t::kError:
      return "ERROR";
    default:
      break;
  }

  return "UNKNOWN";
}
}  // namespace

ORBIT_RPC_HANDLE(echo, hello.OrbitServerRpcService, hello::OrbitServerEchoReq, hello::OrbitServerEchoRsp)
ORBIT_TASK_ACTION(echo, hello::OrbitClientEchoReq, hello::OrbitClientEchoRsp)

int task_action_echo::hook_run(const rpc_request_type& req_body, rpc_response_type& rsp_body) {
  rsp_body.set_text(req_body.text());
  return 0;
}

int main(int argc, char* argv[]) {
  using runtime_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientRuntime;
  using callbacks_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientCallbacks;
  using options_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientOptions;

  const std::filesystem::path log_file_path = get_log_file_path(argc, argv);
  std::ofstream log_file{log_file_path, std::ios::out | std::ios::trunc};
  std::mutex log_mutex;
  auto write_log_line = [&log_file, &log_mutex](const std::string& message) {
    std::lock_guard<std::mutex> lock{log_mutex};
    if (log_file.is_open()) {
      log_file << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "] " << message
               << std::endl;
      return;
    }
    std::cerr << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "] " << message
              << std::endl;
  };

  bool stop_requested = false;
  bool received_forward_payload = false;

  {
    ORBIT_REGISTER_ACTION_CODE(echo, hello::OrbitClientRpcService, hello.OrbitClientRpcService.echo);
  }

  if (log_file.is_open()) {
    write_log_line(std::string{"log file: "} + log_file_path.string());
  } else {
    std::cerr << "open log file failed: " << log_file_path.string() << std::endl;
  }

  callbacks_t callbacks;
  callbacks.on_log =
      [&write_log_line](const ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientLogRecord& record) {
        std::ostringstream stream;
        stream << '[' << to_string(record.level) << "] " << record.category << ": " << record.message;
        write_log_line(stream.str());
      };
  callbacks.on_request_stop = [&stop_requested]() { stop_requested = true; };
  callbacks.on_forward_to_client = [&received_forward_payload](const std::string&) { received_forward_payload = true; };

  int init_result = runtime_t::me()->init(argc, argv, "localhost:12345", callbacks);
  if (init_result != 0) {
    write_log_line(std::string{"orbit runtime init failed, code="} + std::to_string(init_result));
    return init_result;
  }

  bool ready_sent = false;
  bool ping_sent = false;
  auto begin_timepoint = std::chrono::steady_clock::now();
  auto ready_timepoint = begin_timepoint;

  while (!stop_requested) {
    runtime_t::me()->tick();

    const auto now = std::chrono::steady_clock::now();
    if (!ready_sent && now - begin_timepoint >= kReadyDelay) {
      int32_t ready_result = runtime_t::me()->notify_process_ready("orbit-auto-client ready");
      if (ready_result != 0) {
        runtime_t::me()->request_end(orbit::EN_CLIENT_EXIT_REASON_CRASH, ready_result,
                                     "orbit-auto-client ready failed");
        return ready_result;
      }
      ready_sent = true;
      ready_timepoint = now;
    }

    if (ready_sent && !ping_sent && now - ready_timepoint >= kPingDelay) {
      hello::OrbitServerEchoReq req;
      req.set_text("client echo message");
      int32_t echo_result = echo(req, nullptr, 1);
      if (echo_result < 0) {
        runtime_t::me()->request_end(orbit::EN_CLIENT_EXIT_REASON_CRASH, echo_result, "orbit-auto-client echo failed");
        return echo_result;
      }
      ping_sent = true;
    }

    if (ready_sent && now - ready_timepoint >= kExitDelay) {
      break;
    }

    std::this_thread::sleep_for(kTickInterval);
  }

  int32_t shutdown_result =
      runtime_t::me()->request_end(orbit::EN_CLIENT_EXIT_REASON_NORMAL, 0,
                                   received_forward_payload ? "orbit-auto-client done" : "orbit-auto-client timeout");
  if (shutdown_result < 0) {
    write_log_line(std::string{"orbit runtime shutdown failed, code="} + std::to_string(shutdown_result));
    return shutdown_result;
  }
  write_log_line("orbit runtime shutdown successfully");
  return 0;
}
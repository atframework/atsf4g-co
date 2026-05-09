// Copyright 2026 atframework

#include <Orbit/OrbitClientRuntime.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kOrbitArgsAppId = "-id";
constexpr const char* kOrbitArgsClientId = "--orbit-client-id";
constexpr const char* kOrbitArgsAgentEndpoint = "--orbit-agent-endpoint";
constexpr const char* kOrbitArgsConfigEnv = "--config_env";

constexpr auto kTickInterval = std::chrono::milliseconds{100};
constexpr auto kReadyDelay = std::chrono::milliseconds{500};
constexpr auto kPingDelay = std::chrono::seconds{1};
constexpr auto kExitDelay = std::chrono::seconds{8};

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

int main(int argc, char* argv[]) {
  using runtime_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientRuntime;
  using callbacks_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientCallbacks;
  using options_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientOptions;

  bool stop_requested = false;
  bool received_forward_payload = false;

  callbacks_t callbacks;
  callbacks.on_log = [](const ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientLogRecord& record) {
    std::cout << '[' << to_string(record.level) << "] " << record.category << ": " << record.message << std::endl;
  };
  callbacks.on_request_stop = [&stop_requested]() { stop_requested = true; };
  callbacks.on_forward_to_client = [&received_forward_payload](const std::string& payload) {
    received_forward_payload = true;
    std::cout << "forward_to_client: " << payload << std::endl;
  };

  int init_result = runtime_t::me()->init(argc, argv, "localhost:12345", callbacks);
  if (init_result < 0) {
    std::cerr << "orbit runtime init failed, code=" << init_result << std::endl;
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
      ready_sent = runtime_t::me()->notify_process_ready("orbit-auto-client ready");
      if (!ready_sent) {
        runtime_t::me()->request_end(orbit::EN_SLAVE_EXIT_REASON_CRASH, -1, "orbit-auto-client ready failed");
        return -3;
      }
      ready_timepoint = now;
    }

    if (ready_sent && !ping_sent && now - ready_timepoint >= kPingDelay) {
      runtime_t::me()->send_to_server("orbit-auto-client ping");
      ping_sent = true;
    }

    if (ready_sent && now - ready_timepoint >= kExitDelay) {
      break;
    }

    std::this_thread::sleep_for(kTickInterval);
  }

  runtime_t::me()->request_end(orbit::EN_SLAVE_EXIT_REASON_NORMAL, 0,
                               received_forward_payload ? "orbit-auto-client done" : "orbit-auto-client timeout");
  return 0;
}
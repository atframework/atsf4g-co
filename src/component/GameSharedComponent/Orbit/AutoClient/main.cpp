// Copyright 2026 atframework

#include <common/file_system.h>
#include <nostd/mutex.h>
#include <string/string_format.h>

#include <Orbit/OrbitEasyApi.h>

#include <chrono>
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

constexpr auto kTickInterval = std::chrono::milliseconds{500};
constexpr auto kReadyDelay = std::chrono::seconds{5};
constexpr auto kExitDelay = std::chrono::seconds{120};

enum client_state {
  kInit,
  kStartUp,
  kReady,
  kReceiveStart,
  kStopping,
};

int get_process_id() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

std::string get_log_file_dir(std::string bin_dir) {
  return bin_dir + "/../log/";
}

std::string get_log_file_name() {
  return"log_" + std::to_string(get_process_id()) + ".log";
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

ORBIT_RPC_HANDLE(echo, "atframework.shared.OrbitServerRpcService/echo", PROJECT_NAMESPACE_ID::OrbitServerEchoReq,
                 PROJECT_NAMESPACE_ID::OrbitServerEchoRsp)
ORBIT_TASK_ACTION(echo, PROJECT_NAMESPACE_ID::OrbitClientEchoReq, PROJECT_NAMESPACE_ID::OrbitClientEchoRsp)

int task_action_echo::hook_run(void*, const rpc_request_type& req_body, rpc_response_type& rsp_body) {
  rsp_body.set_text(req_body.text());
  return 0;
}

int main(int argc, char* argv[]) {
  using callbacks_t = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientCallbacks;

  std::filesystem::path exe_path(argv[0]);
  std::string dir = exe_path.parent_path().string();

  const std::string log_file_dir = get_log_file_dir(dir);
  std::filesystem::create_directories(log_file_dir);
  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  std::ofstream log_file{log_file_dir + get_log_file_name(), std::ios::out | std::ios::trunc};

  std::mutex log_mutex;
  std::mutex child_mutex;
  std::mutex* log_mutex_ptr = &log_mutex;
  auto write_log_line = [&log_file, log_mutex_ptr](const std::string& message) {
    atfw::util::nostd::scoped_lock<std::mutex> lock{*log_mutex_ptr};
    if (log_file.is_open()) {
      log_file << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "|"
               << std::to_string(get_process_id()) << "] " << message << '\n';
      log_file.flush();
      return;
    }
    std::cerr << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "|"
              << std::to_string(get_process_id()) << "] " << message << '\n';
  };

  { ORBIT_REGISTER_ACTION_CODE(echo, PROJECT_NAMESPACE_ID::OrbitClientRpcService); }

  if (log_file.is_open()) {
    write_log_line(atfw::util::string::format("log file: {}", log_file_dir + get_log_file_name()));
  } else {
    std::cerr << "open log file failed: " << log_file_dir + get_log_file_name() << '\n';
  }

  callbacks_t callbacks;
  bool stopping = false;
  bool stopped = false;
  bool start_client = false;
  bool send_echo = false;
  bool after_fork = false;
  int32_t child_tick_count = 5;
  client_state state = client_state::kInit;
  std::string echo_message = "Seed Echo";
  callbacks.on_log =
      [&write_log_line](const ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientLogRecord& record) {
        std::ostringstream stream;
        stream << '[' << to_string(record.level) << "] " << record.category << ": " << record.message;
        write_log_line(stream.str());
      };
  callbacks.on_request_stop = [&stopped] { stopped = true; };
  callbacks.on_start_client = [&start_client, &write_log_line](const std::string& client_id,
                                                               const google::protobuf::Map<std::string, std::string>&) {
    start_client = true;
    write_log_line(atfw::util::string::format("Start client received: {}", client_id));
  };
  callbacks.on_after_fork_once = [&](const google::protobuf::Map<std::string, std::string>&) {
    after_fork = true;
    send_echo = false;
    echo_message = "Child Echo";
    log_file = std::ofstream{log_file_dir + get_log_file_name(), std::ios::out | std::ios::trunc};
    log_mutex_ptr = &child_mutex;
    return 0;
  };
  callbacks.on_after_fork_tick = [&](const google::protobuf::Map<std::string, std::string>&) {
    child_tick_count--;
    if (child_tick_count == 0) {
      int32_t ready_result = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk_easy_api::notify_process_ready(
          12346, "orbit-auto-client child ready");
      if (ready_result != 0) {
        state = client_state::kStopping;
        write_log_line(atfw::util::string::format("kStopping, code={}", ready_result));
      }
    }
    return child_tick_count;
  };

  int init_result = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk_easy_api::init(
      argc, argv, dir + "/../cfg/orbit_config.yaml", true, callbacks);
  if (init_result != 0) {
    write_log_line(atfw::util::string::format("orbit runtime init failed, code={}", init_result));
    return init_result;
  }

  if (!ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk_easy_api::enabled()) {
    write_log_line("orbit runtime is not enabled, exiting");
    return 0;
  }

  auto state_timepoint = std::chrono::steady_clock::now();

  while (!stopped) {
    ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk_easy_api::tick();
    auto now = std::chrono::steady_clock::now();
    switch (state) {
      case client_state::kInit: {
        state = client_state::kStartUp;
        write_log_line("kStartUp");
        break;
      }

      case client_state::kStartUp: {
        // 等待Startup时间
        if (now > state_timepoint + kReadyDelay) {
          state = client_state::kReady;
          write_log_line("kReady");
          state_timepoint = now;
          int32_t ready_result = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk_easy_api::notify_process_ready(
              12345, "orbit-auto-client seed ready");
          if (ready_result != 0) {
            state = client_state::kStopping;
            write_log_line(atfw::util::string::format("kStopping, code={}", ready_result));
          }
        }
        break;
      }
      case client_state::kReady: {
        // 等待Start
        if (start_client) {
          state = client_state::kReceiveStart;
          write_log_line("kReceiveStart");
        }
        break;
      }
      case client_state::kReceiveStart: {
        if (!send_echo) {
          send_echo = true;
          write_log_line("send_echo");
          state_timepoint = now;
          PROJECT_NAMESPACE_ID::OrbitServerEchoReq req;
          req.set_text(echo_message);
          int32_t echo_result = echo(req, nullptr, 1);
          if (echo_result < 0) {
            ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk_easy_api::request_end(
                atfw::orbit::EN_CLIENT_EXIT_STARTUP_FAILED, echo_result, "orbit-auto-client echo failed");
            return echo_result;
          }
        }
        if (now > state_timepoint + kExitDelay) {
          write_log_line("timeout");
          state = client_state::kStopping;
        }
        break;
      }
      case client_state::kStopping: {
        if (!stopping) {
          write_log_line(atfw::util::string::format("kStopping"));
          stopping = true;
          ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk_easy_api::request_end(
              atfw::orbit::EN_CLIENT_EXIT_REASON_NORMAL, 0, {});
        }
        break;
      }
    }
    std::this_thread::sleep_for(kTickInterval);
  }
  return 0;
}

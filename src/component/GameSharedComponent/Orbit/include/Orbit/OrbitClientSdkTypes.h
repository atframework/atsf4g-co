// Copyright 2026 atframework

#pragma once

#include <Orbit/OrbitConfig.h>

#include <nostd/string_view.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/client_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {

enum class OrbitClientLogLevel : uint8_t { kDebug = 0, kInfo = 1, kWarning = 2, kError = 3 };

enum class OrbitClientRuntimeState : uint8_t {
  kIdle = 0,
  kConnecting = 1,
  kConnected = 2,
  kRunning = 3,
  kStopping = 4,
  kStopped = 5,
};

struct OrbitClientLoadSnapshot {
  double cpu_used = 0;
  double memory_used_mb = 0;
};

struct OrbitClientOptions {
  std::string client_id;
  // Agent 拉起进程时分配的通信标识，进程存活期内不变；Client 与 Agent 之间的报文都用它
  uint64_t client_instance_id = 0;
  std::string agent_endpoint;
  // Agent 实例唯一标识，随启动参数下发；心跳响应里的值不一致说明 Agent 已重启
  std::string agent_instance_id;
  // 未被 start_client 认领时，多久收不到心跳响应就自行退出；<=0 时取 3 个心跳周期
  time_t agent_alive_timeout_second = 0;
  time_t heartbeat_interval_second = 5;
  std::vector<std::string> config_env;
  std::vector<std::string> custom_launch_arguments;
  std::string config_path;
  bool seed_mode = false;
  // 所有请求会被转入IO线程 所有回调会在主动Tick时调用
  bool io_thread = false;
};

struct OrbitClientRequestOptions {
  bool reliable = false;
  int32_t retry_times = 0;
  time_t timeout_second = 4;
  // 不参与 request_end 的收尾等待（例如周期心跳，不应该阻塞退出）
  bool not_count_in_shutdown_wait = false;
};

struct OrbitClientLogRecord {
  OrbitClientLogLevel level = OrbitClientLogLevel::kInfo;
  const char* file_name = nullptr;
  int line_number = 0;
  ::atframework::util::nostd::string_view category;
  ::atframework::util::nostd::string_view message;
};

using OrbitClientLogCallback = std::function<void(const OrbitClientLogRecord& record)>;
using OrbitClientMessageCallback = std::function<void(const std::string& payload)>;
using OrbitClientStopCallback = std::function<void()>;
using OrbitClientSeedWaitingTickCallback = std::function<void()>;
// 收到 Agent 的 start_client（预启动进程被认领）；client_id 为空表示沿用拉起时下发的值
using OrbitClientStartCallback = std::function<void(const std::string& client_id)>;

template <class TResponse>
using OrbitClientRpcCallback = std::function<void(int32_t, const TResponse& response)>;

struct OrbitClientCallbacks {
  // 日志接口 io_thread True 时 on_log !!需要线程安全!!
  OrbitClientLogCallback on_log = nullptr;
  // 主动Stop调用接口 需要调用方准备退出自身
  OrbitClientStopCallback on_request_stop = nullptr;
  // 接收到转发协议
  OrbitClientMessageCallback on_forward_to_client = nullptr;
  // 接收到 start_client（预启动进程被认领）io_thread True 时会回到调用线程
  OrbitClientStartCallback on_start_client = nullptr;

  // Seed模式下 调用方需要在这个回调里准备好要Fork的数据 然后主动调用 notify_seed_process_ready
  // Seed: init -> 加载资源但不Begin -> notify_seed_process_ready(SDK开始等待Fork通知 此处阻塞)
  //    -> 循环on_seed_waiting_tick -> Fork通知 -> notify_seed_process_ready(Child返回 之后流程可视为 Seed继续阻塞)
  //    -> child notify_process_ready
  // Normal: init -> 等待地图加载完成 -> notify_process_ready(通知Agent进程已准备好)
  // 等待Fork时需要注册的Tick回调
  OrbitClientSeedWaitingTickCallback on_seed_waiting_tick = nullptr;
};

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END

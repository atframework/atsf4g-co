// Copyright 2026 atframework

#pragma once

#include <Orbit/OrbitConfig.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/client_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

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
  std::string agent_endpoint;
  time_t heartbeat_interval_second = 5;
  std::vector<std::string> config_env;
};

struct OrbitClientRequestOptions {
  bool reliable = false;
  int32_t retry_times = 0;
  time_t timeout_second = 4;
};

struct OrbitClientLogRecord {
  OrbitClientLogLevel level = OrbitClientLogLevel::kInfo;
  std::string category;
  std::string message;
};

using OrbitClientLogCallback = std::function<void(const OrbitClientLogRecord& record)>;
using OrbitClientMessageCallback = std::function<void(const std::string& payload)>;
using OrbitClientStopCallback = std::function<void()>;

template <class TResponse>
using OrbitClientRpcCallback = std::function<void(int32_t, const TResponse& response)>;

struct OrbitClientCallbacks {
  // 日志接口
  OrbitClientLogCallback on_log = nullptr;
  // 主动Stop调用接口 需要调用方准备退出自身
  OrbitClientStopCallback on_request_stop = nullptr;
  // 接收到转发协议
  OrbitClientMessageCallback on_forward_to_client = nullptr;
};

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END
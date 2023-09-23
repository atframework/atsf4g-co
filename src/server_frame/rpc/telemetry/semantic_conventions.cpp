// Copyright 2023 atframework
// Created by owent on 2023-09-23.
//

#include "rpc/telemetry/semantic_conventions.h"

namespace rpc {

namespace telemetry {

#if !((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
constexpr const char *semantic_conventions::kGroupNameDefault;
constexpr const char *semantic_conventions::kGroupNameHpa;
constexpr const char *semantic_conventions::kGroupNameDedicatedServer;

constexpr const char *semantic_conventions::kAtRpcResultCode;
constexpr const char *semantic_conventions::kAtRpcResponseCode;
constexpr const char *semantic_conventions::kAtRpcKind;
constexpr const char *semantic_conventions::kAtRpcSpanName;

constexpr const char *semantic_conventions::kRpcSystemValueAtRpcDistapcher;
constexpr const char *semantic_conventions::kRpcSystemValueAtRpcTask;
constexpr const char *semantic_conventions::kRpcServiceValueNoDispatcher;
#endif

}  // namespace telemetry
}  // namespace rpc

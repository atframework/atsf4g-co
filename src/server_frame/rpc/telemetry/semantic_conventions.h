// Copyright 2023 atframework
// Created by owent on 2023-09-14.
//

#pragma once

namespace rpc {

namespace telemetry {

struct semantic_conventions {
  static constexpr const char *kEventDomain = "event.domain";

  static constexpr const char *kGroupNameDefault = "";
  static constexpr const char *kGroupNameHpa = "hpa";
  static constexpr const char *kGroupNameDedicatedServer = "dedicated_server";
  static constexpr const char *kGroupNameCsActor = "cs_actor";
  static constexpr const char *kGroupNameNotification = "notification";

  static constexpr const char *kAtRpcResultCode = "rpc.atrpc.result_code";
  static constexpr const char *kAtRpcResponseCode = "rpc.atrpc.response_code";
  static constexpr const char *kAtRpcKind = "rpc.atrpc.kind";
  static constexpr const char *kAtRpcSpanName = "rpc.atrpc.span_name";
  static constexpr const char *kAtRpcStacktrace = "rpc.atrpc.stacktrace";

  static constexpr const char *kRpcSystemValueAtRpcDistapcher = "atrpc.dispatcher";
  static constexpr const char *kRpcSystemValueAtRpcTask = "atrpc.task";
  static constexpr const char *kRpcServiceValueNoDispatcher = "no_dispatcher";

  static constexpr const char *kRpcRouterObjectTypeID = "router_object.type_id";
  static constexpr const char *kRpcRouterObjectZoneID = "router_object.zone_id";
  static constexpr const char *kRpcRouterObjectInstanceID = "router_object.instance_id";
};

}  // namespace telemetry
}  // namespace rpc

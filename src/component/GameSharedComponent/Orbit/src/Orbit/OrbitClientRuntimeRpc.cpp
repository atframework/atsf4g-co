// Copyright 2026 atframework
#include <Orbit/OrbitClientRuntime.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message_lite.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/extension/atframework.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>
#include <sstream>
#include <unordered_map>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {
namespace {

constexpr const char* kMethodForwardToClient = "forward_to_client";
constexpr const char* kMethodForkSeedClient = "fork_seed_client";
constexpr const char* kMethodClientHeartbeat = "client_heartbeat";
constexpr const char* kMethodSendToServer = "send_to_server";
constexpr const char* kMethodClientStart = "client_start";
constexpr const char* kMethodClientExit = "client_exit";
constexpr int32_t kInServerMessageType = 11;

enum class orbit_receive_rpc_type_t : uint8_t {
  kInvalid = 0,
  kForwardToClient = 1,
  kForkSeedClient = 2,
};

int64_t get_now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void fill_protobuf_timestamp(google::protobuf::Timestamp& timestamp) {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto sec = std::chrono::duration_cast<std::chrono::seconds>(now);
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - sec);
  timestamp.set_seconds(sec.count());
  timestamp.set_nanos(static_cast<int32_t>(ns.count()));
}

const std::string& get_empty_string() {
  static const std::string empty_string;
  return empty_string;
}

const google::protobuf::MethodDescriptor* get_agent_to_client_method(const char* method_name) {
  return orbit::AgentToClientService::descriptor()->FindMethodByName(method_name);
}

const google::protobuf::MethodDescriptor* get_client_to_agent_method(const char* method_name) {
  return orbit::ClientToAgentService::descriptor()->FindMethodByName(method_name);
}

std::string get_rpc_full_name(const google::protobuf::MethodDescriptor& method) {
  std::string rpc_full_name;
  rpc_full_name.reserve(method.service()->full_name().size() + 1 + method.name().size());
  rpc_full_name.append(method.service()->full_name());
  rpc_full_name.push_back('/');
  rpc_full_name.append(method.name());
  return rpc_full_name;
}

const std::string& pick_rpc_name(const atframework::SSMsg& message) {
  if (!message.has_head()) {
    return get_empty_string();
  }

  switch (message.head().rpc_type_case()) {
    case atframework::SSMsgHead::kRpcRequest:
      return message.head().rpc_request().rpc_name();
    case atframework::SSMsgHead::kRpcResponse:
      return message.head().rpc_response().rpc_name();
    case atframework::SSMsgHead::kRpcStream:
      return message.head().rpc_stream().rpc_name();
    default:
      break;
  }

  return get_empty_string();
}

template <class TMessage>
bool unpack_body_message(const atframework::SSMsg& input, TMessage& output) {
  if (input.body_bin().empty()) {
    return output.ParseFromString(std::string{});
  }

  return output.ParseFromArray(input.body_bin().data(), static_cast<int>(input.body_bin().size()));
}

void register_receive_rpc(std::unordered_map<std::string, orbit_receive_rpc_type_t>& registry,
                          const google::protobuf::ServiceDescriptor* service_descriptor, const char* method_name,
                          orbit_receive_rpc_type_t rpc_type) {
  if (nullptr == service_descriptor) {
    return;
  }

  const google::protobuf::MethodDescriptor* method = service_descriptor->FindMethodByName(method_name);
  if (nullptr == method) {
    return;
  }

  registry.emplace(get_rpc_full_name(*method), rpc_type);
}

#define ORBIT_CLIENT_RUNTIME_REG_RECEIVE_RPC(REGISTRY, METHOD_NAME, RPC_TYPE)                \
  register_receive_rpc((REGISTRY), orbit::AgentToClientService::descriptor(), (METHOD_NAME), \
                       orbit_receive_rpc_type_t::RPC_TYPE)

const std::unordered_map<std::string, orbit_receive_rpc_type_t>& get_receive_rpc_registry() {
  static const std::unordered_map<std::string, orbit_receive_rpc_type_t> registry = [] {
    std::unordered_map<std::string, orbit_receive_rpc_type_t> result;

    ORBIT_CLIENT_RUNTIME_REG_RECEIVE_RPC(result, kMethodForwardToClient, kForwardToClient);
    ORBIT_CLIENT_RUNTIME_REG_RECEIVE_RPC(result, kMethodForkSeedClient, kForkSeedClient);

    return result;
  }();

  return registry;
}

#undef ORBIT_CLIENT_RUNTIME_REG_RECEIVE_RPC

}  // namespace

void OrbitClientRuntime::on_received_message(const std::string& message) {
  atframework::SSMsg unpacked_message;
  if (!unpack_message(unpacked_message, message)) {
    log(OrbitClientLogLevel::kError, "failed to parse transport message as SSMsg");
    return;
  }

  dispatch_received_message(unpacked_message);
}

bool OrbitClientRuntime::send_stream_message(const google::protobuf::MessageLite& body,
                                             const google::protobuf::MethodDescriptor& method) {
  std::string packed_message;
  if (!pack_stream_message(packed_message, method, body)) {
    return false;
  }

  if (nullptr == app_ || !app_->get_bus_node()) {
    log(OrbitClientLogLevel::kError, "send rejected: atapp bus node is unavailable");
    return false;
  }

  if (0 == agent_bus_id_ || !app_->get_bus_node()->is_endpoint_available(agent_bus_id_)) {
    log(OrbitClientLogLevel::kError, "send rejected: agent endpoint is unavailable");
    return false;
  }

  int send_result = app_->get_bus_node()->send_data(
      agent_bus_id_, kInServerMessageType,
      gsl::span<const unsigned char>(reinterpret_cast<const unsigned char*>(packed_message.data()),
                                     packed_message.size()));
  if (0 != send_result) {
    std::ostringstream stream;
    stream << "send_data failed for " << get_rpc_full_name(method) << ", code=" << send_result;
    log(OrbitClientLogLevel::kError, stream.str());
    return false;
  }

  return true;
}

bool OrbitClientRuntime::pack_stream_message(std::string& output, const google::protobuf::MethodDescriptor& method,
                                             const google::protobuf::MessageLite& body) {
  atframework::SSMsg message;
  atframework::SSMsgHead* head = message.mutable_head();
  if (nullptr == head) {
    log(OrbitClientLogLevel::kError, "failed to allocate SSMsg head");
    return false;
  }

  head->set_timestamp(get_now_seconds());
  head->set_sequence(allocate_sequence());
  head->set_node_name(options_.client_id);

  atframework::RpcStreamMeta* stream_meta = head->mutable_rpc_stream();
  if (nullptr == stream_meta) {
    log(OrbitClientLogLevel::kError, "failed to allocate rpc_stream meta");
    return false;
  }

  stream_meta->set_version("1");
  stream_meta->set_caller(options_.client_id);
  stream_meta->set_callee(method.service()->full_name());
  stream_meta->set_rpc_name(get_rpc_full_name(method));
  stream_meta->set_type_url(method.input_type()->full_name());
  fill_protobuf_timestamp(*stream_meta->mutable_caller_timestamp());

  if (!body.SerializeToString(message.mutable_body_bin())) {
    std::ostringstream stream;
    stream << "failed to serialize rpc body for " << get_rpc_full_name(method);
    log(OrbitClientLogLevel::kError, stream.str());
    return false;
  }

  if (!message.SerializeToString(&output)) {
    std::ostringstream stream;
    stream << "failed to serialize SSMsg for " << get_rpc_full_name(method);
    log(OrbitClientLogLevel::kError, stream.str());
    return false;
  }

  return true;
}

bool OrbitClientRuntime::unpack_message(atframework::SSMsg& output, const std::string& message) const {
  if (message.empty()) {
    return false;
  }

  return output.ParseFromArray(message.data(), static_cast<int>(message.size()));
}

bool OrbitClientRuntime::dispatch_received_message(const atframework::SSMsg& message) {
  const std::string& rpc_name = pick_rpc_name(message);
  if (rpc_name.empty()) {
    log(OrbitClientLogLevel::kWarning, "received SSMsg without rpc name");
    return false;
  }

  const auto& registry = get_receive_rpc_registry();
  auto dispatcher = registry.find(rpc_name);
  if (dispatcher == registry.end()) {
    log(OrbitClientLogLevel::kWarning, std::string{"unsupported rpc received: "} + rpc_name);
    return false;
  }

  switch (dispatcher->second) {
    case orbit_receive_rpc_type_t::kForwardToClient: {
      orbit::ATDForwardToClientNotify request;
      if (!unpack_body_message(message, request)) {
        log(OrbitClientLogLevel::kError, "failed to parse forward_to_client payload");
        return false;
      }

      return rpc_receive_forward_to_client(request);
    }

    case orbit_receive_rpc_type_t::kForkSeedClient: {
      orbit::ATDForkSeedClientNotify request;
      if (!unpack_body_message(message, request)) {
        log(OrbitClientLogLevel::kError, "failed to parse fork_seed_client payload");
        return false;
      }

      return rpc_receive_fork_seed_client(request);
    }

    default:
      break;
  }

  log(OrbitClientLogLevel::kWarning, std::string{"unsupported registered rpc type for: "} + rpc_name);
  return false;
}

bool OrbitClientRuntime::rpc_send_client_heartbeat(const orbit::DTAClientHeartbeatNotify& request) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodClientHeartbeat);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "client_heartbeat method descriptor not found");
    return false;
  }

  return send_stream_message(request, *method);
}

bool OrbitClientRuntime::rpc_send_send_to_server(const orbit::DTASendToServerNotify& request) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodSendToServer);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "send_to_server method descriptor not found");
    return false;
  }

  return send_stream_message(request, *method);
}

bool OrbitClientRuntime::rpc_send_client_start(const orbit::DTAClientStartReq& request) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodClientStart);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "client_start method descriptor not found");
    return false;
  }

  return send_stream_message(request, *method);
}

bool OrbitClientRuntime::rpc_send_client_exit(const orbit::DTAClientExitReq& request) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodClientExit);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "client_exit method descriptor not found");
    return false;
  }

  return send_stream_message(request, *method);
}

bool OrbitClientRuntime::rpc_receive_forward_to_client(const orbit::ATDForwardToClientNotify& request) {
  if (callbacks_.on_forward_to_client) {
    callbacks_.on_forward_to_client(request.payload());
  }

  return true;
}

bool OrbitClientRuntime::rpc_receive_fork_seed_client(const orbit::ATDForkSeedClientNotify& request) {
  (void)request;
  log(OrbitClientLogLevel::kInfo, "fork_seed_client ignored in current SDK stage");
  return true;
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END

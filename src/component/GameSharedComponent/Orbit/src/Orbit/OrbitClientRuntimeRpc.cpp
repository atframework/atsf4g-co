// Copyright 2026 atframework
#include <Orbit/OrbitClientRuntime.h>
#include <Orbit/OrbitRPCDispatcher.h>

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
constexpr time_t kDefaultRequestTimeoutSecond = 4;

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

time_t normalize_request_timeout_second(time_t timeout_second) {
  if (timeout_second > 0) {
    return timeout_second;
  }

  return kDefaultRequestTimeoutSecond;
}

int32_t normalize_request_retry_times(int32_t retry_times) {
  if (retry_times > 0) {
    return retry_times;
  }

  return 0;
}

template <class TResponse>
OrbitClientRuntime::client_request_raw_callback_t make_typed_request_callback(
    OrbitClientRuntime& runtime, const google::protobuf::MethodDescriptor& method,
    OrbitClientRpcCallback<TResponse> callback) {
  if (!callback) {
    return {};
  }

  std::string rpc_full_name = get_rpc_full_name(method);
  return [&runtime, rpc_full_name, callback = std::move(callback)](int32_t result,
                                                                   const atframework::SSMsg& message) mutable {
    TResponse response_body;
    if (result == orbit::EN_ORBIT_ERROR_CODE_SUCCESS) {
      if (!message.has_head() || !message.head().has_rpc_response()) {
        runtime.log(OrbitClientLogLevel::kError, std::string{"rpc response head missing for "} + rpc_full_name);
        result = orbit::EN_ORBIT_ERROR_CODE_MESSAGE_HEAD_NOT_FOUND;
      } else if (message.head().rpc_response().type_url() != TResponse::descriptor()->full_name()) {
        runtime.log(
            OrbitClientLogLevel::kError,
            LOG_WRAPPER_FWAPI_FORMAT("rpc response type mismatch for {}, expect {}, real {}", rpc_full_name,
                                     TResponse::descriptor()->full_name(), message.head().rpc_response().type_url()));
        result = orbit::EN_ORBIT_ERROR_CODE_PARSE_MESSAGE_FAILED;
      } else if (!unpack_body_message(message, response_body)) {
        runtime.log(OrbitClientLogLevel::kError,
                    LOG_WRAPPER_FWAPI_FORMAT("rpc response parse failed for {}, detail {}", rpc_full_name,
                                             response_body.InitializationErrorString()));
        result = orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
      }
    }

    callback(result, response_body);
  };
}

}  // namespace

void OrbitClientRuntime::on_received_message(const std::string& message) {
  atframework::SSMsg unpacked_message;
  int32_t unpack_result = unpack_message(unpacked_message, message);
  if (unpack_result < 0) {
    log(OrbitClientLogLevel::kError,
        std::string{"failed to parse transport message as SSMsg, code="} + std::to_string(unpack_result));
    return;
  }

  dispatch_received_message(unpacked_message);
}

int32_t OrbitClientRuntime::send_message(const std::string& packed_message,
                                         const google::protobuf::MethodDescriptor& method, bool reliable,
                                         uint64_t task_id) {
  if (nullptr == app_ || !app_->get_bus_node()) {
    log(OrbitClientLogLevel::kError, "send rejected: atapp bus node is unavailable");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  if (0 == agent_bus_id_ || !app_->get_bus_node()->is_endpoint_available(agent_bus_id_)) {
    log(OrbitClientLogLevel::kError, "send rejected: agent endpoint is unavailable");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  int send_result = app_->get_bus_node()->send_data(
      agent_bus_id_, kInServerMessageType,
      gsl::span<const unsigned char>(reinterpret_cast<const unsigned char*>(packed_message.data()),
                                     packed_message.size()));
  if (0 != send_result) {
    std::ostringstream stream;
    stream << "send_data failed for " << get_rpc_full_name(method) << ", code=" << send_result;
    if (reliable) {
      stream << ", reliable=true, task_id=" << task_id;
    }
    log(OrbitClientLogLevel::kError, stream.str());
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::send_stream_message(const google::protobuf::MessageLite& body,
                                                const google::protobuf::MethodDescriptor& method) {
  std::string packed_message;
  int32_t pack_result = pack_stream_message(packed_message, method, body);
  if (pack_result < 0) {
    return pack_result;
  }
  return send_message(packed_message, method);
}

int32_t OrbitClientRuntime::pack_stream_message(std::string& output, const google::protobuf::MethodDescriptor& method,
                                                const google::protobuf::MessageLite& body) {
  atframework::SSMsg message;
  atframework::SSMsgHead* head = message.mutable_head();
  if (nullptr == head) {
    log(OrbitClientLogLevel::kError, "failed to allocate SSMsg head");
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }

  head->set_timestamp(get_now_seconds());
  head->set_sequence(allocate_sequence());
  head->set_node_name(options_.client_id);

  atframework::RpcStreamMeta* stream_meta = head->mutable_rpc_stream();
  if (nullptr == stream_meta) {
    log(OrbitClientLogLevel::kError, "failed to allocate rpc_stream meta");
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
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
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  }

  if (!message.SerializeToString(&output)) {
    std::ostringstream stream;
    stream << "failed to serialize SSMsg for " << get_rpc_full_name(method);
    log(OrbitClientLogLevel::kError, stream.str());
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::send_request_message(const google::protobuf::MessageLite& body,
                                                 const google::protobuf::MethodDescriptor& method,
                                                 client_request_raw_callback_t callback,
                                                 const OrbitClientRequestOptions& request_options) {
  std::string packed_message;
  uint64_t task_id = 0;
  int32_t pack_result = pack_request_message(packed_message, task_id, method, body);
  if (pack_result < 0) {
    return pack_result;
  }

  bool reliable = request_options.reliable || request_options.retry_times > 0;
  bool track_response = reliable || callback != nullptr;
  if (track_response) {
    pending_client_request_t pending;
    pending.packed_message = packed_message;
    pending.method = &method;
    pending.callback = std::move(callback);
    pending.timeout_second = normalize_request_timeout_second(request_options.timeout_second);
    pending.retry_times_left = normalize_request_retry_times(request_options.retry_times);
    pending.reliable = reliable;
    reschedule_pending_request_timeout(task_id, pending);
    pending_client_request_map_.emplace(task_id, std::move(pending));
  }

  int32_t send_result = send_message(packed_message, method, reliable, task_id);
  if (send_result < 0 && track_response) {
    pending_client_request_map_.erase(task_id);
  }

  return send_result;
}

int32_t OrbitClientRuntime::pack_request_message(std::string& output, uint64_t& task_id,
                                                 const google::protobuf::MethodDescriptor& method,
                                                 const google::protobuf::MessageLite& body) {
  atframework::SSMsg message;
  atframework::SSMsgHead* head = message.mutable_head();
  if (nullptr == head) {
    log(OrbitClientLogLevel::kError, "failed to allocate SSMsg head");
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }

  head->set_timestamp(get_now_seconds());
  head->set_sequence(allocate_sequence());
  head->set_source_task_id(head->sequence());
  head->set_node_name(options_.client_id);
  task_id = head->source_task_id();

  atframework::RpcRequestMeta* request_meta = head->mutable_rpc_request();
  if (nullptr == request_meta) {
    log(OrbitClientLogLevel::kError, "failed to allocate rpc_request meta");
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }

  request_meta->set_version("1");
  request_meta->set_caller(options_.client_id);
  request_meta->set_callee(method.service()->full_name());
  request_meta->set_rpc_name(get_rpc_full_name(method));
  request_meta->set_type_url(method.input_type()->full_name());
  fill_protobuf_timestamp(*request_meta->mutable_caller_timestamp());

  if (!body.SerializeToString(message.mutable_body_bin())) {
    std::ostringstream stream;
    stream << "failed to serialize rpc body for " << get_rpc_full_name(method);
    log(OrbitClientLogLevel::kError, stream.str());
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  }

  if (!message.SerializeToString(&output)) {
    std::ostringstream stream;
    stream << "failed to serialize SSMsg for " << get_rpc_full_name(method);
    log(OrbitClientLogLevel::kError, stream.str());
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::send_response_message(const ::atframework::SSMsgHead& req_head,
                                                  const google::protobuf::MessageLite& body,
                                                  const google::protobuf::MethodDescriptor& method) {
  std::string packed_message;
  int32_t pack_result = pack_response_message(req_head, packed_message, method, body);
  if (pack_result < 0) {
    return pack_result;
  }

  return send_message(packed_message, method);
}

int32_t OrbitClientRuntime::pack_response_message(const ::atframework::SSMsgHead& req_head, std::string& output,
                                                  const google::protobuf::MethodDescriptor& method,
                                                  const google::protobuf::MessageLite& body) {
  atframework::SSMsg message;
  atframework::SSMsgHead* head = message.mutable_head();
  if (nullptr == head) {
    log(OrbitClientLogLevel::kError, "failed to allocate SSMsg head");
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }

  if (req_head.has_rpc_trace()) {
    *head->mutable_rpc_trace() = req_head.rpc_trace();
  }
  head->set_timestamp(get_now_seconds());
  head->set_sequence(req_head.sequence());
  head->set_node_name(options_.client_id);
  head->set_destination_task_id(req_head.source_task_id());

  atframework::RpcResponseMeta* response_meta = head->mutable_rpc_response();
  if (nullptr == response_meta) {
    log(OrbitClientLogLevel::kError, "failed to allocate rpc_response meta");
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }

  response_meta->set_version("1");
  response_meta->set_rpc_name(get_rpc_full_name(method));
  response_meta->set_type_url(method.output_type()->full_name());
  fill_protobuf_timestamp(*response_meta->mutable_caller_timestamp());

  if (!body.SerializeToString(message.mutable_body_bin())) {
    std::ostringstream stream;
    stream << "failed to serialize rpc body for " << get_rpc_full_name(method);
    log(OrbitClientLogLevel::kError, stream.str());
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  }

  if (!message.SerializeToString(&output)) {
    std::ostringstream stream;
    stream << "failed to serialize SSMsg for " << get_rpc_full_name(method);
    log(OrbitClientLogLevel::kError, stream.str());
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::unpack_message(atframework::SSMsg& output, const std::string& message) const {
  if (message.empty()) {
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  if (!output.ParseFromArray(message.data(), static_cast<int>(message.size()))) {
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::dispatch_received_message(const atframework::SSMsg& message) {
  if (message.has_head() && message.head().has_rpc_response()) {
    return dispatch_request_response(message);
  }

  const std::string& rpc_name = pick_rpc_name(message);
  if (rpc_name.empty()) {
    log(OrbitClientLogLevel::kWarning, "received SSMsg without rpc name");
    return orbit::EN_ORBIT_ERROR_CODE_RPC_NAME_ERROR;
  }

  const auto& registry = get_receive_rpc_registry();
  auto dispatcher = registry.find(rpc_name);
  if (dispatcher == registry.end()) {
    log(OrbitClientLogLevel::kWarning, std::string{"unsupported rpc received: "} + rpc_name);
    return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
  }

  switch (dispatcher->second) {
    case orbit_receive_rpc_type_t::kForwardToClient: {
      orbit::ATDForwardToClientReq request;
      if (!unpack_body_message(message, request)) {
        log(OrbitClientLogLevel::kError, "failed to parse forward_to_client payload");
        return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
      }

      return rpc_receive_forward_to_client(message.head(), request);
    }

    case orbit_receive_rpc_type_t::kForkSeedClient: {
      orbit::ATDForkSeedClientReq request;
      if (!unpack_body_message(message, request)) {
        log(OrbitClientLogLevel::kError, "failed to parse fork_seed_client payload");
        return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
      }

      return rpc_receive_fork_seed_client(message.head(), request);
    }

    default:
      break;
  }

  log(OrbitClientLogLevel::kWarning, std::string{"unsupported registered rpc type for: "} + rpc_name);
  return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
}

int32_t OrbitClientRuntime::dispatch_request_response(const atframework::SSMsg& message) {
  uint64_t task_id = message.head().destination_task_id();
  if (0 == task_id) {
    log(OrbitClientLogLevel::kWarning, "received rpc response without destination_task_id");
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }

  auto pending_iter = pending_client_request_map_.find(task_id);
  if (pending_iter == pending_client_request_map_.end()) {
    return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }

  int32_t error_code = message.head().error_code();
  // Head内的错误都认为RPC调用失败 非逻辑失败 需要重试
  if (error_code != orbit::EN_ORBIT_ERROR_CODE_SUCCESS &&
      retry_pending_request(task_id, pending_iter->second, error_code, "rpc response error")) {
    return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }

  complete_pending_request(task_id, error_code, &message);
  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

bool OrbitClientRuntime::retry_pending_request(uint64_t task_id, pending_client_request_t& pending, int32_t error_code,
                                               const char* reason) {
  if (!pending.reliable || pending.retry_times_left <= 0 || nullptr == pending.method) {
    return false;
  }

  --pending.retry_times_left;

  std::ostringstream stream;
  stream << "retry request " << get_rpc_full_name(*pending.method) << ", task_id=" << task_id << ", code=" << error_code
         << ", reason=" << reason << ", retries_left=" << pending.retry_times_left;
  log(OrbitClientLogLevel::kWarning, stream.str());

  reschedule_pending_request_timeout(task_id, pending);
  int32_t send_result = send_message(pending.packed_message, *pending.method, true, task_id);
  if (send_result < 0) {
    std::ostringstream resend_stream;
    resend_stream << "retry send_data failed for " << get_rpc_full_name(*pending.method) << ", task_id=" << task_id
                  << ", code=" << send_result;
    log(OrbitClientLogLevel::kWarning, resend_stream.str());
  }

  return true;
}

void OrbitClientRuntime::complete_pending_request(uint64_t task_id, int32_t error_code,
                                                  const atframework::SSMsg* message) {
  auto pending_iter = pending_client_request_map_.find(task_id);
  if (pending_iter == pending_client_request_map_.end()) {
    return;
  }

  client_request_raw_callback_t callback = std::move(pending_iter->second.callback);
  pending_client_request_map_.erase(pending_iter);

  if (!callback) {
    return;
  }

  atframework::SSMsg empty_message;
  callback(error_code, nullptr == message ? empty_message : *message);
}

void OrbitClientRuntime::reschedule_pending_request_timeout(uint64_t task_id, pending_client_request_t& pending) {
  pending.deadline = get_now_seconds() + normalize_request_timeout_second(pending.timeout_second);
  pending_client_request_timeout_map_.emplace(pending.deadline, task_id);
}

void OrbitClientRuntime::execute_pending_request_timeouts() {
  time_t current_time = get_now_seconds();
  for (auto iter = pending_client_request_timeout_map_.begin(); iter != pending_client_request_timeout_map_.end();) {
    if (iter->first >= current_time) {
      break;
    }

    uint64_t task_id = iter->second;
    time_t deadline = iter->first;
    iter = pending_client_request_timeout_map_.erase(iter);

    auto pending_iter = pending_client_request_map_.find(task_id);
    if (pending_iter == pending_client_request_map_.end() || pending_iter->second.deadline != deadline) {
      continue;
    }

    if (retry_pending_request(task_id, pending_iter->second, orbit::EN_ORBIT_ERROR_CODE_TIMEOUT, "request timeout")) {
      continue;
    }

    complete_pending_request(task_id, orbit::EN_ORBIT_ERROR_CODE_TIMEOUT, nullptr);
  }
}

int32_t OrbitClientRuntime::rpc_send_client_heartbeat(const orbit::DTAClientHeartbeatNotify& request) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodClientHeartbeat);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "client_heartbeat method descriptor not found");
    return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
  }

  return send_stream_message(request, *method);
}

int32_t OrbitClientRuntime::rpc_send_send_to_server(const orbit::DTASendToServerReq& request,
                                                    OrbitClientRpcCallback<orbit::ATDSendToServerRsp> callback,
                                                    const OrbitClientRequestOptions& request_options) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodSendToServer);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "send_to_server method descriptor not found");
    return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
  }

  return send_request_message(
      request, *method, make_typed_request_callback<orbit::ATDSendToServerRsp>(*this, *method, std::move(callback)),
      request_options);
}

int32_t OrbitClientRuntime::rpc_send_client_start(const orbit::DTAClientStartReq& request,
                                                  OrbitClientRpcCallback<orbit::ATDClientStartRsp> callback,
                                                  const OrbitClientRequestOptions& request_options) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodClientStart);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "client_start method descriptor not found");
    return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
  }

  return send_request_message(
      request, *method, make_typed_request_callback<orbit::ATDClientStartRsp>(*this, *method, std::move(callback)),
      request_options);
}

int32_t OrbitClientRuntime::rpc_send_client_exit(const orbit::DTAClientExitReq& request,
                                                 OrbitClientRpcCallback<orbit::ATDClientExitRsp> callback,
                                                 const OrbitClientRequestOptions& request_options) {
  const google::protobuf::MethodDescriptor* method = get_client_to_agent_method(kMethodClientExit);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "client_exit method descriptor not found");
    return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
  }

  return send_request_message(request, *method,
                              make_typed_request_callback<orbit::ATDClientExitRsp>(*this, *method, std::move(callback)),
                              request_options);
}

int32_t OrbitClientRuntime::rpc_receive_forward_to_client(const ::atframework::SSMsgHead& req_head,
                                                          const orbit::ATDForwardToClientReq& request) {
  // 立刻回包
  const google::protobuf::MethodDescriptor* method = get_agent_to_client_method(kMethodForwardToClient);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "forward_to_client method descriptor not found");
    return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
  }
  orbit::DTAForwardToClientRsp rsp;
  send_response_message(req_head, rsp, *method);

  ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitRPCDispatcher::me()->dispatch(request.payload());
  if (callbacks_.on_forward_to_client) {
    callbacks_.on_forward_to_client(request.payload());
  }

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

int32_t OrbitClientRuntime::rpc_receive_fork_seed_client(const ::atframework::SSMsgHead& req_head,
                                                         const orbit::ATDForkSeedClientReq& request) {
  const google::protobuf::MethodDescriptor* method = get_agent_to_client_method(kMethodForkSeedClient);
  if (nullptr == method) {
    log(OrbitClientLogLevel::kError, "fork_seed_client method descriptor not found");
    return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
  }

  int32_t ret = on_received_fork_request(request);
  orbit::DTAForkSeedClientRsp rsp;
  rsp.set_error_code(ret);
  send_response_message(req_head, rsp, *method);

  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END

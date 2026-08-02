#include "logic/orbit_msg_dispatcher.h"

#include "logic/orbit_server_manager.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <atgateway/protocol/libatgw_protocol_api.h>
#include <libatbus.h>
#include <libatbus_protocol.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <config/logic_config.h>

#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <rpc/rpc_context.h>
#include <utility/protobuf_mini_dumper.h>
#include <utility/tls_buffers.h>

#include <memory>
#include <string>
#include <utility>

namespace {
rpc::result_code_type on_receive_orbit_message(rpc::context &ctx, std::string client_id, const std::string &data) {
  // 解包
  RPC_RETURN_CODE(orbit_msg_dispatcher::me()->dispatch(ctx, client_id, data));
}
}  // namespace

#if defined(ORBIT_SERVER_SDK_DLL) && ORBIT_SERVER_SDK_DLL
#  if defined(ORBIT_SERVER_SDK_NATIVE) && ORBIT_SERVER_SDK_NATIVE
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(orbit_msg_dispatcher);
#  else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(orbit_msg_dispatcher);
#  endif
#else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(orbit_msg_dispatcher);
#endif

ORBIT_SERVER_SERVICE_API orbit_msg_dispatcher::orbit_msg_dispatcher() : sequence_allocator_(0) {}

ORBIT_SERVER_SERVICE_API orbit_msg_dispatcher::~orbit_msg_dispatcher() {}

ORBIT_SERVER_SERVICE_API int32_t orbit_msg_dispatcher::init() {
  sequence_allocator_ =
      static_cast<uint64_t>(
          (util::time::time_utility::get_sys_now() - PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
          << 23) +
      static_cast<uint64_t>(util::time::time_utility::get_now_usec() << 3);
  orbit_server_manager::me()->set_on_forward_to_server(on_receive_orbit_message);
  return 0;
}

ORBIT_SERVER_SERVICE_API const char *orbit_msg_dispatcher::name() const { return "orbit_msg_dispatcher"; }

ORBIT_SERVER_SERVICE_API uint64_t orbit_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
  orbit::OrbitRpcMessage *real_msg = get_protobuf_msg<orbit::OrbitRpcMessage>(raw_msg);
  if (nullptr == real_msg || !real_msg->has_head()) {
    return 0;
  }

  return real_msg->head().destination_task_id();
}

ORBIT_SERVER_SERVICE_API const std::string &orbit_msg_dispatcher::pick_rpc_name(
    const orbit::OrbitRpcMessage &orbit_msg) {
  if (!orbit_msg.has_head()) {
    return get_empty_string();
  }

  switch (orbit_msg.head().rpc_type_case()) {
    case orbit::OrbitRpcMessageHead::kRpcRequest:
      return orbit_msg.head().rpc_request().rpc_name();
    case orbit::OrbitRpcMessageHead::kRpcStream:
      return orbit_msg.head().rpc_stream().rpc_name();
    case orbit::OrbitRpcMessageHead::kRpcResponse:
      return orbit_msg.head().rpc_response().rpc_name();
    default:
      break;
  }

  return get_empty_string();
}

ORBIT_SERVER_SERVICE_API const std::string &orbit_msg_dispatcher::pick_rpc_name(msg_raw_t &raw_msg) {
  orbit::OrbitRpcMessage *real_msg = get_protobuf_msg<orbit::OrbitRpcMessage>(raw_msg);
  if (nullptr == real_msg) {
    return get_empty_string();
  }

  return pick_rpc_name(*real_msg);
}

ORBIT_SERVER_SERVICE_API int32_t orbit_msg_dispatcher::dispatch(rpc::context &ctx, const std::string &client_id,
                                                                const std::string &data) {
  if (client_id.empty()) {
    FWLOGERROR("receive a orbit message from unknown client_id");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  orbit::OrbitRpcMessage *orbit_msg = ctx.create<orbit::OrbitRpcMessage>();
  if (nullptr == orbit_msg) {
    FWLOGERROR("{} create message instance failed", name());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  dispatcher_raw_message callback_msg = dispatcher_make_default<dispatcher_raw_message>();
  int32_t ret = unpack_protobuf_msg(*orbit_msg, callback_msg, reinterpret_cast<const void *>(data.data()), data.size());
  if (ret != 0) {
    FWLOGERROR("{} unpack received message from [{}] failed, res: {}", name(), client_id, ret);
    return ret;
  }

  rpc::telemetry::tracer tracer;
  rpc::telemetry::trace_start_option trace_start_option;
  trace_start_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
  trace_start_option.is_remote = true;
  trace_start_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(orbit_msg_dispatcher::me());
  if (orbit_msg->head().has_rpc_trace()) {
    trace_start_option.parent_network_span = &orbit_msg->head().rpc_trace();
  } else {
    trace_start_option.parent_network_span = nullptr;
  }
  rpc::telemetry::trace_attribute_pair_type internal_rpc_trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "internal"},
      {opentelemetry::semconv::rpc::kRpcMethod, "orbit_msg_dispatcher/dispatch"}};
  trace_start_option.attributes = internal_rpc_trace_attributes;
  ctx.setup_tracer(tracer, "orbit_msg_dispatcher", std::move(trace_start_option));

  dispatcher_result_t res =
      on_receive_message(ctx, callback_msg, make_private_data(client_id), orbit_msg->head().sequence());
  ret = res.result_code;
  if (ret < 0) {
    FWLOGERROR("{} dispatch message from [{}] failed, res: {}", name(), client_id, ret);
  }

  return tracer.finish({ret, {}});
}

ORBIT_SERVER_SERVICE_API void orbit_msg_dispatcher::on_create_task_failed(dispatcher_start_data_type &start_data,
                                                                          int32_t error_code) {
  const std::string &rpc_name = pick_rpc_name(start_data.message);
  if (rpc_name.empty()) {
    return;
  }

  orbit::OrbitRpcMessage *real_msg = get_protobuf_msg<orbit::OrbitRpcMessage>(start_data.message);
  if (nullptr == real_msg || !real_msg->has_head() || !real_msg->head().has_rpc_request() ||
      0 == real_msg->head().source_task_id()) {
    return;
  }

  const std::string &source_client_id = get_source_client_id(start_data.private_data);
  if (source_client_id.empty()) {
    return;
  }

  rpc::context fallback_ctx{rpc::context::create_without_task()};
  rpc::context *response_ctx = &fallback_ctx;
  rpc::telemetry::tracer tracer;
  std::unique_ptr<rpc::context> child_context;
  if (nullptr != start_data.context) {
    child_context.reset(new rpc::context(*start_data.context));
    response_ctx = child_context.get();

    rpc::telemetry::trace_start_option trace_start_option;
    trace_start_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
    trace_start_option.is_remote = true;
    trace_start_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(orbit_msg_dispatcher::me());
    trace_start_option.parent_network_span = &real_msg->head().rpc_trace();
    response_ctx->setup_tracer(tracer, rpc_name, std::move(trace_start_option));
  }

  rpc::context::message_holder<orbit::OrbitRpcMessage> rsp{*response_ctx};
  orbit::OrbitRpcMessageHead *head = rsp->mutable_head();
  if (nullptr == head) {
    FWLOGERROR("malloc header failed when pack response of {} (source task id: {})", rpc_name,
               real_msg->head().source_task_id());
    return;
  }

  head->set_source_task_id(0);
  head->set_destination_task_id(real_msg->head().source_task_id());
  head->set_sequence(real_msg->head().sequence());
  head->set_error_code(error_code);
  head->set_timestamp(util::time::time_utility::get_now());

  auto rpc_response = head->mutable_rpc_response();
  if (nullptr != rpc_response) {
    rpc_response->set_version(logic_config::me()->get_atframework_settings().rpc_version());
    rpc_response->set_rpc_name(rpc_name);
    const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method = get_registered_method(rpc_name);
    if (nullptr != method) {
      rpc_response->set_type_url(method->output_type()->full_name());
    }
    protobuf_copy_message(*rpc_response->mutable_caller_timestamp(), real_msg->head().rpc_request().caller_timestamp());
  }

  int res = send_to_client_no_wait(*response_ctx, source_client_id, *rsp);
  if (res < 0) {
    FWLOGERROR("send create response failed of {} (source task id: {}) to [{}] failed, res: {}({})", rpc_name,
               real_msg->head().source_task_id(), source_client_id, res, protobuf_mini_dumper_get_error_msg(res));
  }
}

ORBIT_SERVER_SERVICE_API uint64_t orbit_msg_dispatcher::allocate_sequence() { return ++sequence_allocator_; }

ORBIT_SERVER_SERVICE_API int32_t orbit_msg_dispatcher::send_to_client_no_wait(rpc::context &ctx,
                                                                              const std::string &client_id,
                                                                              orbit::OrbitRpcMessage &orbit_msg) {
  atfw::atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp, maybe not initialized or already closed");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  if (client_id.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (0 == orbit_msg.head().sequence()) {
    orbit_msg.mutable_head()->set_sequence(allocate_sequence());
  }

  size_t msg_buf_len = orbit_msg.ByteSizeLong();
  auto tls_buffer =
      atfw::gateway::libatgw_protocol_api::get_tls_buffer(atfw::gateway::libatgw_protocol_api::tls_buffer_t::kCustom);
  if (msg_buf_len > tls_buffer.size()) {
    FWLOGERROR("send orbit msg to proc [{}] failed: require {}, only have {}", client_id, msg_buf_len,
               tls_buffer.size());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast<::google::protobuf::uint8 *>(tls_buffer.data());
  auto *next_buffer = orbit_msg.SerializeWithCachedSizesToArray(buf_start);
  if (next_buffer - reinterpret_cast<uint8_t *>(buf_start) != static_cast<ptrdiff_t>(msg_buf_len)) {
    FWLOGERROR("send orbit msg to proc [{}] failed: serialize size mismatch, expect {}, actual {}", client_id,
               msg_buf_len, next_buffer - reinterpret_cast<uint8_t *>(buf_start));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }
  FWLOGDEBUG("send orbit msg to proc [{}] {} bytes\n{}", client_id, msg_buf_len,
             protobuf_mini_dumper_get_readable(orbit_msg));

  int res = orbit_server_manager::me()->send_to_client_no_wait(ctx, client_id, buf_start, msg_buf_len);
  if (res < 0) {
    FWLOGERROR("send orbit msg to proc [{}] {} bytes failed, res: {}", client_id, msg_buf_len, res);
  } else {
    FWLOGDEBUG("send orbit msg to proc [{}] {} bytes success", client_id, msg_buf_len);
  }

  return res;
}

ORBIT_SERVER_SERVICE_API rpc::result_code_type orbit_msg_dispatcher::send_to_client(rpc::context &ctx,
                                                                                    const std::string &client_id,
                                                                                    orbit::OrbitRpcMessage &orbit_msg) {
  atfw::atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp, maybe not initialized or already closed");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
  }

  if (client_id.empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (0 == orbit_msg.head().sequence()) {
    orbit_msg.mutable_head()->set_sequence(allocate_sequence());
  }

  size_t msg_buf_len = orbit_msg.ByteSizeLong();
  auto tls_buffer =
      atfw::gateway::libatgw_protocol_api::get_tls_buffer(atfw::gateway::libatgw_protocol_api::tls_buffer_t::kCustom);
  if (msg_buf_len > tls_buffer.size()) {
    FWLOGERROR("send orbit msg to proc [{}] failed: require {}, only have {}", client_id, msg_buf_len,
               tls_buffer.size());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND);
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast<::google::protobuf::uint8 *>(tls_buffer.data());
  auto *next_buffer = orbit_msg.SerializeWithCachedSizesToArray(buf_start);
  if (next_buffer - reinterpret_cast<uint8_t *>(buf_start) != static_cast<ptrdiff_t>(msg_buf_len)) {
    FWLOGERROR("send orbit msg to proc [{}] failed: serialize size mismatch, expect {}, actual {}", client_id,
               msg_buf_len, next_buffer - reinterpret_cast<uint8_t *>(buf_start));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  FWLOGDEBUG("send orbit msg to proc [{}] {} bytes\n{}", client_id, msg_buf_len,
             protobuf_mini_dumper_get_readable(orbit_msg));

  int res = RPC_AWAIT_CODE_RESULT(orbit_server_manager::me()->send_to_client(ctx, client_id, buf_start, msg_buf_len));
  if (res < 0) {
    FWLOGERROR("send orbit msg to proc [{}] {} bytes failed, res: {}", client_id, msg_buf_len, res);
  } else {
    FWLOGDEBUG("send orbit msg to proc [{}] {} bytes success", client_id, msg_buf_len);
  }

  RPC_RETURN_CODE(res);
}

ORBIT_SERVER_SERVICE_API void *orbit_msg_dispatcher::make_private_data(const std::string &client_id) noexcept {
  return reinterpret_cast<void *>(const_cast<std::string *>(&client_id));
}

ORBIT_SERVER_SERVICE_API const std::string &orbit_msg_dispatcher::get_source_client_id(
    const void *private_data) noexcept {
  return *reinterpret_cast<const std::string *>(private_data);
}

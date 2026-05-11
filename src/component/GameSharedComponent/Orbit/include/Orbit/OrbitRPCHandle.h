#pragma once

#include <log/log_wrapper.h>
#include "OrbitClientRuntime.h"
#include "OrbitRPCReqBase.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {

namespace {
template <class TBodyType>
static inline int __pack_rpc_body(const TBodyType& input, std::string* output, const std::string& rpc_full_name,
                                  gsl::string_view type_full_name) {
  if (false == input.SerializeToString(output)) {
    OrbitClientRuntime::me()->log(
        OrbitClientLogLevel::kError,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} serialization rpc request message failed:\n{}", rpc_full_name,
                                 input.InitializationErrorString()));
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  } else {
    OrbitClientRuntime::me()->log(
        OrbitClientLogLevel::kDebug,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} serialization rpc request message success:\n{}", rpc_full_name,
                                 OrbitClientRuntime::protobuf_mini_dumper_get_readable(input)));
    return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }
}

template <class TBodyType>
static inline int __unpack_rpc_body(TBodyType&& output, const std::string& input, const std::string& rpc_full_name,
                                    gsl::string_view type_full_name) {
  if (false == output.ParseFromString(input)) {
    OrbitClientRuntime::me()->log(OrbitClientLogLevel::kError,
                                  LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} parse rpc request message failed:\n{}",
                                                           rpc_full_name, output.InitializationErrorString()));
    return orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  } else {
    OrbitClientRuntime::me()->log(
        OrbitClientLogLevel::kDebug,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} parse rpc request message success:\n{}", rpc_full_name,
                                 OrbitClientRuntime::protobuf_mini_dumper_get_readable(output)));
    return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }
}

static inline int __setup_rpc_request_header(orbit::OrbitRpcMessageHead& head, const std::string& rpc_full_name,
                                             gsl::string_view type_full_name) {
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }
  request_meta->set_callee("orbit.ClientRPCService");
  request_meta->set_rpc_name(rpc_full_name);
  request_meta->set_type_url(type_full_name);
  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

static inline int __setup_rpc_stream_header(orbit::OrbitRpcMessageHead& head, const std::string& rpc_full_name,
                                            gsl::string_view type_full_name) {
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }
  stream_meta->set_callee("orbit.ClientRPCService");
  stream_meta->set_rpc_name(rpc_full_name);
  stream_meta->set_type_url(type_full_name);
  return orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

static inline OrbitClientRequestOptions __make_rpc_request_options(int32_t retry_time) {
  OrbitClientRequestOptions options;
  if (retry_time > 0) {
    options.reliable = true;
    options.retry_times = retry_time;
  }
  return options;
}

static inline time_t __get_rpc_wait_timeout(const OrbitClientRequestOptions& request_options) {
  time_t timeout_second = request_options.timeout_second;
  if (timeout_second <= 0) {
    timeout_second = 4;
  }

  int32_t retry_times = request_options.retry_times;
  if (retry_times < 0) {
    retry_times = 0;
  }

  return timeout_second * (retry_times + 1);
}

}  // namespace

template <class req_type, class rsp_type>
int ATFW_UTIL_SYMBOL_VISIBLE orbit_rpc_handle(const std::string& rpc_name, const std::string& service_name,
                                              const req_type& req_body,
                                              std::function<void(int32_t, const rsp_type&)> callback,
                                              int32_t retry_time) {
  orbit::OrbitRpcMessage req_msg;
  std::string rpc_full_name = service_name + "." + rpc_name;
  OrbitClientRequestOptions request_options = __make_rpc_request_options(retry_time);
  int32_t res = __setup_rpc_request_header(*req_msg.mutable_head(), rpc_full_name, req_type::descriptor()->full_name());
  if (res < 0) {
    return res;
  }
  res = __pack_rpc_body(req_body, req_msg.mutable_body_bin(), rpc_full_name, req_type::descriptor()->full_name());
  if (res < 0) {
    return res;
  }
  uint64_t sequence = 0;
  res = OrbitRPCDispatcher::me()->send_req_to_proc(req_msg, sequence, request_options);
  if (res != 0) {
    return res;
  }
  return OrbitRPCDispatcher::me()->init_rpc_req_callback(
      sequence, __get_rpc_wait_timeout(request_options),
      [rpc_full_name, callback](const orbit::OrbitRpcMessage& rsp_msg) {
        int32_t res = 0;
        rsp_type rsp_body;
        if (rsp_msg.head().rpc_response().type_url() == rsp_type::descriptor()->full_name() &&
            !rsp_msg.body_bin().empty()) {
          res = __unpack_rpc_body(rsp_body, rsp_msg.body_bin(), rpc_full_name, rsp_type::descriptor()->full_name());
        }
        if (res == 0) {
          res = rsp_msg.head().error_code();
        }
        if (callback != nullptr) {
          callback(res, rsp_body);
        }
      });
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END

#define ORBIT_RPC_HANDLE(name, service, req_type, rsp_type)                                                        \
  int name(const req_type& req_body, std::function<void(int32_t, const rsp_type&)> callback, int32_t retry_time) { \
    return ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::orbit_rpc_handle(#name, #service, req_body, callback,  \
                                                                             retry_time);                          \
  }

#define ORBIT_REGISTER_ACTION_CODE(name, service, rpc_name)                                                           \
  int ret =                                                                                                           \
      ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitRPCDispatcher::me()->register_action<task_action_##name>( \
          service::descriptor(), #rpc_name);                                                                          \
  if (ret != 0) {                                                                                                     \
    ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientRuntime::me()->log(                                   \
        ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientLogLevel::kError,                                 \
        LOG_WRAPPER_FWAPI_FORMAT("register_orbit_rpc_action register action {} failed, ret: [{}]", #name, ret));      \
  }

#define ORBIT_TASK_ACTION(name, req_type, rsp_type)                                                                    \
  class task_action_##name                                                                                             \
      : public ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::task_action_orbit_rpc_base<req_type, rsp_type>,        \
        public std::enable_shared_from_this<task_action_##name> {                                                      \
    using base_type = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::task_action_orbit_rpc_base<req_type, rsp_type>; \
                                                                                                                       \
   public:                                                                                                             \
    explicit task_action_##name(orbit::OrbitRpcMessage&& ds_msg) : base_type(std::move(ds_msg)) {}                     \
    int operator()() {                                                                                                 \
      set_rsp_code(hook_run(get_request_body(), get_response_body()));                                                 \
      send_response();                                                                                                 \
      return 0;                                                                                                        \
    }                                                                                                                  \
    int hook_run(const rpc_request_type& req_body, rpc_response_type& rsp_body);                                       \
  };

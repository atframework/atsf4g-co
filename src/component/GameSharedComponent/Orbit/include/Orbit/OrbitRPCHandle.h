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
        OrbitClientLogLevel::kError, __FILE__, __LINE__,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} serialization rpc request message failed:\n{}", rpc_full_name,
                                 input.InitializationErrorString()));
    return atfw::orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  } else {
    OrbitClientRuntime::me()->log(
        OrbitClientLogLevel::kDebug, __FILE__, __LINE__,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} serialization rpc request message success:\n{}", rpc_full_name,
                                 OrbitClientRuntime::protobuf_mini_dumper_get_readable(input)));
    return atfw::orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }
}

template <class TBodyType>
static inline int __unpack_rpc_body(TBodyType&& output, const std::string& input, const std::string& rpc_full_name,
                                    gsl::string_view type_full_name) {
  if (false == output.ParseFromString(input)) {
    OrbitClientRuntime::me()->log(OrbitClientLogLevel::kError, __FILE__, __LINE__,
                                  LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} parse rpc request message failed:\n{}",
                                                           rpc_full_name, output.InitializationErrorString()));
    return atfw::orbit::EN_ORBIT_ERROR_CODE_SERIALIZETOSTRING;
  } else {
    OrbitClientRuntime::me()->log(
        OrbitClientLogLevel::kDebug, __FILE__, __LINE__,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} parse rpc request message success:\n{}", rpc_full_name,
                                 OrbitClientRuntime::protobuf_mini_dumper_get_readable(output)));
    return atfw::orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
  }
}

static inline int __setup_rpc_request_header(atfw::orbit::OrbitRpcMessageHead& head, const std::string& rpc_full_name,
                                             gsl::string_view type_full_name) {
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return atfw::orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }
  request_meta->set_callee("orbit.ClientRPCService");
  request_meta->set_rpc_name(rpc_full_name);
  request_meta->set_type_url(type_full_name);
  return atfw::orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

static inline int __setup_rpc_stream_header(atfw::orbit::OrbitRpcMessageHead& head, const std::string& rpc_full_name,
                                            gsl::string_view type_full_name) {
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return atfw::orbit::EN_ORBIT_ERROR_CODE_MALLOC;
  }
  stream_meta->set_callee("orbit.ClientRPCService");
  stream_meta->set_rpc_name(rpc_full_name);
  stream_meta->set_type_url(type_full_name);
  return atfw::orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
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

template <class orbit_rpc_req_type, class orbit_rpc_rsp_type>
int ATFW_UTIL_SYMBOL_VISIBLE orbit_rpc_handle_inner(const std::string& rpc_name, const std::string& service_name,
                                              const orbit_rpc_req_type& req_body,
                                              std::function<void(int32_t, const orbit_rpc_rsp_type&)> callback,
                                              int32_t retry_time) {
  atfw::orbit::OrbitRpcMessage req_msg;
  std::string rpc_full_name = service_name + "." + rpc_name;
  OrbitClientRequestOptions request_options = __make_rpc_request_options(retry_time);
  int32_t res =
      __setup_rpc_request_header(*req_msg.mutable_head(), rpc_full_name, orbit_rpc_req_type::descriptor()->full_name());
  if (res < 0) {
    return res;
  }
  res = __pack_rpc_body(req_body, req_msg.mutable_body_bin(), rpc_full_name,
                        orbit_rpc_req_type::descriptor()->full_name());
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
      [rpc_full_name, callback](const atfw::orbit::OrbitRpcMessage& rsp_msg) {
        int32_t res = 0;
        orbit_rpc_rsp_type rsp_body;
        if (rsp_msg.head().rpc_response().type_url() == orbit_rpc_rsp_type::descriptor()->full_name() &&
            !rsp_msg.body_bin().empty()) {
          res = __unpack_rpc_body(rsp_body, rsp_msg.body_bin(), rpc_full_name,
                                  orbit_rpc_rsp_type::descriptor()->full_name());
        }
        if (res == 0) {
          res = rsp_msg.head().error_code();
        }
        if (callback != nullptr) {
          callback(res, rsp_body);
        }
      });
}

template <class orbit_rpc_req_type, class orbit_rpc_rsp_type>
int ATFW_UTIL_SYMBOL_VISIBLE orbit_rpc_handle(const std::string& rpc_name, const std::string& service_name,
                                              const orbit_rpc_req_type& req_body,
                                              std::function<void(int32_t, const orbit_rpc_rsp_type&)> callback,
                                              int32_t retry_time) {
  if (!OrbitClientRuntime::me()->enabled_io_thread()) {
    return orbit_rpc_handle_inner<orbit_rpc_req_type, orbit_rpc_rsp_type>(rpc_name, service_name, req_body, callback, retry_time);
  }
  // 转入IO线程处理
  OrbitClientRuntime::me()->post_to_io_thread([=]() {
    orbit_rpc_handle_inner<orbit_rpc_req_type, orbit_rpc_rsp_type>(rpc_name, service_name, req_body, callback, retry_time);
  });
  return atfw::orbit::EN_ORBIT_ERROR_CODE_SUCCESS;
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END
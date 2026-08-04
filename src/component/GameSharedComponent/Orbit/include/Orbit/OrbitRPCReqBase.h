#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "OrbitClientRuntime.h"
#include "OrbitConfig.h"

#include <list>
#include <memory>
#include <string>
#include <utility>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {

class ORBIT_CLIENT_SDK_API task_action_orbit_req_base {
 public:
  using msg_type = orbit::OrbitRpcMessage;
  using msg_ref_type = msg_type&;
  using msg_cref_type = const msg_type&;

 public:
  explicit task_action_orbit_req_base(msg_type&& msg);
  virtual ~task_action_orbit_req_base();

  msg_ref_type add_rsp_msg();

  static int32_t init_msg(orbit::OrbitRpcMessage& msg, const orbit::OrbitRpcMessage& req_msg);

 protected:
  void send_response();
  void set_rsp_code(int32_t code) { rsp_code_ = code; }
  orbit::OrbitRpcMessage& get_request() { return req_message_; }
  const orbit::OrbitRpcMessage& get_request() const { return req_message_; }

 private:
  orbit::OrbitRpcMessage req_message_;
  orbit::OrbitRpcMessage response_messages_;
  bool rsp_init_;
  int32_t rsp_code_;
};

template <class TReqType, class TRspType>
class ATFW_UTIL_SYMBOL_VISIBLE task_action_orbit_rpc_base : public task_action_orbit_req_base {
 public:
  using base_type = task_action_orbit_req_base;
  using msg_type = base_type::msg_type;
  using msg_ref_type = msg_type&;
  using msg_cref_type = const msg_type&;
  using rpc_request_type = TReqType;
  using rpc_response_type = TRspType;

 protected:
  using base_type::get_request;

 public:
  explicit task_action_orbit_rpc_base(msg_type&& msg)
      : base_type(std::move(msg)), has_unpack_request_(false), has_pack_response_(false) {}

  ATFW_UTIL_FORCEINLINE rpc_request_type& get_request_body() {
    if (!has_unpack_request_) {
      unpack_request();
    }
    return request_body_;
  }

  ATFW_UTIL_FORCEINLINE rpc_response_type& get_response_body() { return response_body_; }

  ATFW_UTIL_FORCEINLINE static gsl::string_view get_request_type_url() {
    return rpc_request_type::descriptor()->full_name();
  }
  ATFW_UTIL_FORCEINLINE static gsl::string_view get_response_type_url() {
    return rpc_response_type::descriptor()->full_name();
  }

  virtual bool is_stream_rpc() const { return get_request().head().has_rpc_stream(); }

 protected:
  void send_response() {
    if (!has_pack_response_ && !is_stream_rpc()) {
      pack_response();
    }
    base_type::send_response();
  }

 private:
  void unpack_request() {
    has_unpack_request_ = true;
    // Check message type
    if (get_request().head().has_rpc_request()) {
      if (get_request_type_url() != get_request().head().rpc_request().type_url()) {
        OrbitClientRuntime::me()->log(
            OrbitClientLogLevel::kError, __FILE__, __LINE__,
            LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] expect message {}, real got {}", get_request_type_url(),
                                     get_request().head().rpc_request().type_url()));
      }
    } else if (get_request().head().has_rpc_stream()) {
      if (get_request_type_url() != get_request().head().rpc_stream().type_url()) {
        OrbitClientRuntime::me()->log(
            OrbitClientLogLevel::kError, __FILE__, __LINE__,
            LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] expect message {}, real got {}", get_request_type_url(),
                                     get_request().head().rpc_stream().type_url()));
      }
    }

    if (false == request_body_.ParseFromString(get_request().body_bin())) {
      OrbitClientRuntime::me()->log(
          OrbitClientLogLevel::kError, __FILE__, __LINE__,
          LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} ParseFromString Failed {}", get_request_type_url(),
                                   request_body_.InitializationErrorString()));
    } else {
      OrbitClientRuntime::me()->log(
          OrbitClientLogLevel::kInfo, __FILE__, __LINE__,
          LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} parse rpc request message success:\n{}", get_response_type_url(),
                                   OrbitClientRuntime::protobuf_mini_dumper_get_readable(request_body_)));
    }
  }

  void pack_response() {
    has_pack_response_ = true;

    orbit::OrbitRpcMessage& rsp = add_rsp_msg();
    orbit::OrbitRpcMessageHead* head = rsp.mutable_head();
    if (nullptr == head) {
      return;
    }

    if (get_request().head().has_rpc_request()) {
      head->clear_rpc_request();
      head->mutable_rpc_response()->set_rpc_name(get_request().head().rpc_request().rpc_name());
      head->mutable_rpc_response()->set_type_url(get_response_type_url());
    } else {
      head->clear_rpc_stream();
      head->mutable_rpc_stream()->set_rpc_name(get_request().head().rpc_stream().rpc_name());
      head->mutable_rpc_stream()->set_type_url(get_response_type_url());
      head->mutable_rpc_stream()->set_callee(get_request().head().rpc_stream().caller());
    }

    if (false == get_response_body().SerializeToString(rsp.mutable_body_bin())) {
      OrbitClientRuntime::me()->log(
          OrbitClientLogLevel::kError, __FILE__, __LINE__,
          LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} SerializeToString Failed {}", get_request_type_url(),
                                   get_response_body().InitializationErrorString()));
    } else {
      OrbitClientRuntime::me()->log(
          OrbitClientLogLevel::kInfo, __FILE__, __LINE__,
          LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] {} serialize rpc response message success:\n{}",
                                   get_response_type_url(),
                                   OrbitClientRuntime::protobuf_mini_dumper_get_readable(get_response_body())));
    }
  }

 private:
  bool has_unpack_request_;
  bool has_pack_response_;
  rpc_request_type request_body_;
  rpc_response_type response_body_;
};

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END
#pragma once

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <common/string_oprs.h>
#include <log/log_wrapper.h>

#include <config/logic_config.h>

#include <list>
#include <memory>
#include <string>
#include <utility>

#include <dispatcher/dispatcher_type_defines.h>
#include <dispatcher/task_action_base.h>
#include <rpc/rpc_common_types.h>
#include <utility/protobuf_mini_dumper.h>

class orbit_msg_dispatcher;

class ATFW_UTIL_SYMBOL_VISIBLE task_action_orbit_req_base : public task_action_req_base<orbit::OrbitRpcMessage> {
 public:
  using base_type = task_action_req_base<orbit::OrbitRpcMessage>;
  using message_type = base_type::message_type;
  using result_type = base_type::result_type;
  using msg_ref_type = message_type &;
  using msg_cref_type = const message_type &;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::operator();
  using base_type::set_response_code;
  using base_type::set_result;

 public:
  ORBIT_SERVER_SERVICE_API explicit task_action_orbit_req_base(dispatcher_start_data_type &&start_param);
  ORBIT_SERVER_SERVICE_API virtual ~task_action_orbit_req_base();

  ORBIT_SERVER_SERVICE_API result_type hook_run() override;

  ORBIT_SERVER_SERVICE_API msg_ref_type add_response_message();

  ORBIT_SERVER_SERVICE_API const std::string &get_request_client_id() const noexcept;

  ORBIT_SERVER_SERVICE_API void init_msg(msg_ref_type msg);
  ORBIT_SERVER_SERVICE_API void init_msg(msg_ref_type msg, msg_cref_type req_msg);

  ORBIT_SERVER_SERVICE_API std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  ORBIT_SERVER_SERVICE_API const char *get_type_name() const override;

  ORBIT_SERVER_SERVICE_API rpc::telemetry::trace_inherit_options get_inherit_option() const noexcept override;
  ORBIT_SERVER_SERVICE_API rpc::telemetry::trace_start_option get_trace_option() const noexcept override;

  ORBIT_SERVER_SERVICE_API virtual bool is_stream_rpc() const noexcept;

  virtual atfw::util::nostd::string_view get_request_type_url() const noexcept = 0;
  virtual atfw::util::nostd::string_view get_response_type_url() const noexcept = 0;

 protected:
  ORBIT_SERVER_SERVICE_API void send_response() override;

  ATFW_UTIL_FORCEINLINE bool has_response_message() const noexcept { return !response_messages_.empty(); }

 private:
  std::list<message_type *> response_messages_;
  std::string client_id_;
};

template <class TReqType, class TRspType>
class ATFW_UTIL_SYMBOL_VISIBLE task_action_orbit_rpc_base : public task_action_orbit_req_base {
 public:
  using base_type = task_action_orbit_req_base;
  using message_type = base_type::message_type;
  using result_type = base_type::result_type;
  using msg_ref_type = message_type &;
  using msg_cref_type = const message_type &;
  using rpc_request_type = TReqType;
  using rpc_response_type = TRspType;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::operator();
  using base_type::set_response_code;
  using base_type::set_result;

 public:
  explicit task_action_orbit_rpc_base(dispatcher_start_data_type &&start_param)
      : base_type(std::move(start_param)),
        has_unpack_request_(false),
        request_body_(nullptr),
        response_body_(nullptr) {}

  ATFW_UTIL_SYMBOL_VISIBLE rpc_request_type &get_request_body() {
    if (!has_unpack_request_) {
      unpack_request();
    }

    if (nullptr == request_body_) {
      static rpc_request_type empty_msg;
      empty_msg.Clear();
      return empty_msg;
    }

    return *request_body_;
  }

  ATFW_UTIL_SYMBOL_VISIBLE rpc_response_type &get_response_body() {
    if (nullptr == response_body_) {
      response_body_ = create_message_at_task_arena<rpc_response_type>();
    }

    if (nullptr == response_body_) {
      static rpc_response_type empty_msg;
      empty_msg.Clear();
      return empty_msg;
    }

    return *response_body_;
  }

  ATFW_UTIL_FORCEINLINE atfw::util::nostd::string_view get_request_type_url() const noexcept override {
    decltype(auto) full_name = rpc_request_type::descriptor()->full_name();
    return {atfw::util::nostd::data(full_name), atfw::util::nostd::size(full_name)};
  }

  ATFW_UTIL_FORCEINLINE atfw::util::nostd::string_view get_response_type_url() const noexcept override {
    decltype(auto) full_name = rpc_response_type::descriptor()->full_name();
    return {atfw::util::nostd::data(full_name), atfw::util::nostd::size(full_name)};
  }

 protected:
  ATFW_UTIL_SYMBOL_VISIBLE void send_response() override {
    if (!is_stream_rpc() && !has_response_message() && is_response_message_enabled()) {
      pack_response();
    }

    base_type::send_response();
  }

 private:
  bool unpack_request() {
    has_unpack_request_ = true;

    request_body_ = create_message_at_task_arena<rpc_request_type>();
    if (nullptr == request_body_) {
      return false;
    }

    if (get_request().head().has_rpc_request()) {
      decltype(auto) type_url = get_request().head().rpc_request().type_url();
      atfw::util::nostd::string_view type_url_view{atfw::util::nostd::data(type_url),
                                                   atfw::util::nostd::size(type_url)};
      if (get_request_type_url() != type_url_view) {
        FWLOGERROR("{}Expect message {}, real got {}", get_shared_context_log_prefix(), get_request_type_url(),
                   type_url_view);
        return false;
      }
    } else if (get_request().head().has_rpc_stream()) {
      decltype(auto) type_url = get_request().head().rpc_stream().type_url();
      atfw::util::nostd::string_view type_url_view{atfw::util::nostd::data(type_url),
                                                   atfw::util::nostd::size(type_url)};
      if (get_request_type_url() != type_url_view) {
        FWLOGERROR("{}Expect message {}, real got {}", get_shared_context_log_prefix(), get_request_type_url(),
                   type_url_view);
        return false;
      }
    }

    if (!get_request().body_bin().empty() && false == request_body_->ParseFromString(get_request().body_bin())) {
      FWLOGERROR("{}Try to parse message {} failed, message: {}", get_shared_context_log_prefix(),
                 get_request_type_url(), request_body_->InitializationErrorString());
      return false;
    }

    if (!get_request().body_bin().empty()) {
      FWLOGDEBUG("{}Parse orbit rpc request message {} success:\n{}", get_shared_context_log_prefix(),
                 get_request_type_url(), protobuf_mini_dumper_get_readable(*request_body_));
    }

    return true;
  }

  void pack_response() {
    orbit::OrbitRpcMessage &rsp = add_response_message();

    if (false == get_response_body().SerializeToString(rsp.mutable_body_bin())) {
      FWLOGERROR("{}Try to serialize message {} failed, success: {}", get_shared_context_log_prefix(),
                 get_response_type_url(), get_response_body().InitializationErrorString());
    } else {
      FWLOGDEBUG("{}Serialize orbit rpc response message {} success:\n{}", get_shared_context_log_prefix(),
                 get_response_type_url(), protobuf_mini_dumper_get_readable(get_response_body()));
    }
  }

 private:
  bool has_unpack_request_;
  rpc_request_type *request_body_;
  rpc_response_type *response_body_;
};